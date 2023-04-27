#pragma once
#include <hdr/hdr_histogram.h>
#include <mongoc.h>
#include "../social_network_commons.h"
#include "../social_network.pb.h"
#include "phmap.h"
#include "spinlock_mutex.h"
#include "../utils_mongodb.h"
#include <future>

std::string compose_post_addr;
std::string nginx_addr;
std::string post_storage_addr;
std::string data_file_path;

static mongoc_client_pool_t* mongodb_client_pool;
int mongodb_conns_num;
phmap::flat_hash_map<int64_t , std::set<int64_t>> user_followers_map;

phmap::flat_hash_map<int64_t , std::vector<int64_t>> user_home_timeline_map;


class ClientContext : public BasicContext
{
public:
    ClientContext(size_t cid, size_t sid, size_t rid) : client_id_(cid), server_sender_id_(sid), server_receiver_id_(rid)
    {
        forward_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);
        forward_all_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);
        backward_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);
    }
    ~ClientContext()
    {
        delete forward_mpmc_queue;
        delete forward_all_mpmc_queue;
        delete backward_mpmc_queue;
    }
    erpc::MsgBuffer req_forward_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer req_backward_msgbuf[kAppMaxBuffer];

    erpc::MsgBuffer resp_forward_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer resp_backward_msgbuf[kAppMaxBuffer];

    size_t client_id_;
    size_t server_sender_id_;
    size_t server_receiver_id_;

    int nginx_session_number{};
    int compose_post_session_number{};
    int post_storage_session_number{};

    MPMC_QUEUE *forward_mpmc_queue;
    MPMC_QUEUE *forward_all_mpmc_queue;
    MPMC_QUEUE *backward_mpmc_queue;
};

class ServerContext : public BasicContext
{
public:
    explicit ServerContext(size_t sid) : server_id_(sid)
    {
    }
    ~ServerContext()
    = default;
    size_t server_id_{};
    size_t stat_req_ping_tot{};
    size_t stat_req_home_timeline_write_req_tot{};
    size_t stat_req_home_timeline_read_req_tot{};
    size_t stat_req_post_storage_read_resp_tot{};
    size_t stat_req_err_tot{};

    spinlock_mutex init_mutex;
    bool is_pinged{false};
    bool mongodb_init_finished{false};

    void reset_stat()
    {
        stat_req_ping_tot = 0;
        stat_req_home_timeline_write_req_tot = 0;
        stat_req_home_timeline_read_req_tot = 0;
        stat_req_post_storage_read_resp_tot = 0;
        stat_req_err_tot = 0;
    }

    MPMC_QUEUE *forward_all_mpmc_queue{};

    erpc::MsgBuffer *req_forward_msgbuf_ptr{};
    erpc::MsgBuffer *req_backward_msgbuf_ptr{};
};

class AppContext
{
public:
    AppContext()
    {
        int ret = hdr_init(1, 1000 * 1000 * 10, 3,
                           &latency_hist_);
        rmem::rt_assert(ret == 0, "hdr_init failed");
        for (size_t i = 0; i < FLAGS_client_num; i++)
        {
#if defined(ERPC_PROGRAM)
            // notice that post_storage run rmem rpc, so rpc_id begin at kAppMaxRPC
            client_contexts_.push_back(new ClientContext(i, (i % FLAGS_server_num) + kAppMaxRPC, i % FLAGS_server_num));
#elif defined(RMEM_PROGRAM)
            client_contexts_.push_back(new ClientContext(i, (i % FLAGS_server_num) + kAppMaxRPC, (i % FLAGS_server_num) + kAppMaxRPC));
#endif
        }
        for (size_t i = 0; i < FLAGS_server_num; i++)
        {
            auto *ctx = new ServerContext(i);
            ctx->forward_all_mpmc_queue = client_contexts_[i]->forward_all_mpmc_queue;
            ctx->req_forward_msgbuf_ptr = client_contexts_[i]->req_forward_msgbuf;
            ctx->req_backward_msgbuf_ptr = client_contexts_[i]->req_backward_msgbuf;
            server_contexts_.push_back(ctx);
        }
    }
    ~AppContext()
    {
        hdr_close(latency_hist_);

        for (auto &ctx : client_contexts_)
        {
            delete ctx;
        }
        for (auto &ctx : server_contexts_)
        {
            delete ctx;
        }
    }

    [[maybe_unused]] [[nodiscard]] bool write_latency_and_reset(const std::string &filename) const
    {

        FILE *fp = fopen(filename.c_str(), "w");
        if (fp == nullptr)
        {
            return false;
        }
        hdr_percentiles_print(latency_hist_, fp, 5, 10, CLASSIC);
        fclose(fp);
        hdr_reset(latency_hist_);
        return true;
    }

    std::vector<ClientContext *> client_contexts_;
    std::vector<ServerContext *> server_contexts_;

    hdr_histogram *latency_hist_{};
};

void load_timeline_storage(const std::string& file_path)
{
    social_network::HomeTimelineStorage home_timeline_storage;
    std::ifstream input(file_path, std::ios::binary);
    rmem::rt_assert(input.is_open(),"file open failed");

    if (!home_timeline_storage.ParseFromIstream(&input)) {
        RMEM_ERROR("Failed to parse home_timeline_storage.");
        exit(-1);
    }

    for(const auto& pair : home_timeline_storage.users_to_posts()) {
        std::vector<int64_t >vec;
        for(const auto& value : pair.second.post_ids()) {
            vec.push_back(value);
        }
        user_home_timeline_map[pair.first] = std::move(vec);
    }
    RMEM_INFO("Timeline storage load finished! Total init %ld users", user_home_timeline_map.size());

}

void store_timeline_storage(const std::string& file_path)
{
    social_network::HomeTimelineStorage home_timeline_storage;
    for (const auto& pair : user_home_timeline_map) {
        social_network::VecPostID &vec =  home_timeline_storage.mutable_users_to_posts()->at(pair.first) ;
        for (const auto& value : pair.second) {
            vec.add_post_ids(value);
        }
    }

    std::ofstream output(file_path, std::ios::binary);
    rmem::rt_assert(output.is_open(),"file open failed");

    if (!home_timeline_storage.SerializeToOstream(&output)) {
        RMEM_ERROR("Failed to serial home_timeline_storage.");
        exit(-1);
    }

    RMEM_INFO("Timeline storage store finished! Total init %ld users", user_home_timeline_map.size());
}

// must be used after init_service_config

void init_specific_config(){
    auto value = config_json_all["compose_post"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    compose_post_addr = value;

    value = config_json_all["nginx"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    nginx_addr = value;

    value = config_json_all["post_storage"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    post_storage_addr = value;

    value = config_json_all["home_timeline"]["data_file_path"];
    rmem::rt_assert(!value.is_null(),"value is null");
    data_file_path = value;

    auto conns = config_json_all["social_network_mongodb"]["connections"];
    rmem::rt_assert(!conns.is_null(),"value is null");
    mongodb_conns_num = conns;
}

void read_home_time_line_post_details(void *buf_, erpc::Rpc<erpc::CTransport> *rpc_, MPMC_QUEUE *consumer_fwd, MPMC_QUEUE *consumer_back) {
    auto* req = static_cast<RPCMsgReq<HomeTimeLineReq> *>(buf_);

    bool is_fwd = true;

    if(req->req_control.stop_idx <= req->req_control.start_idx || req->req_control.start_idx < 0){
        is_fwd = false;
    }

    if(!user_home_timeline_map.contains(req->req_control.user_id)){
        is_fwd = false;
    }
    std::vector<int64_t> &post_ids = user_home_timeline_map[req->req_control.user_id];
    if(req->req_control.start_idx > static_cast<int>(post_ids.size())){
        is_fwd = false;
    }

    if(!is_fwd){
        erpc::MsgBuffer resp_buf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<HomeTimeLineReq>));
        new (resp_buf.buf_) RPCMsgReq<HomeTimeLineReq>(RPC_TYPE::RPC_HOME_TIMELINE_READ_RESP, req->req_common.req_number,
                                                                    {0, req->req_control.user_id, req->req_control.start_idx, req->req_control.stop_idx });
        consumer_back->push(resp_buf);
        return;
    }

    social_network::PostStorageReadReq post_storage_req;

    int now_index = 0;
    for(int64_t & post_id : post_ids){
        if(req->req_control.start_idx<=now_index && now_index<req->req_control.stop_idx){
            post_storage_req.add_post_ids(post_id);
        }
        now_index++;
        if(now_index==req->req_control.stop_idx){
            break;
        }
    }

    erpc::MsgBuffer fwd_req = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<CommonRPCReq>) + post_storage_req.ByteSizeLong());
    auto* fwd_req_msg = new (fwd_req.buf_) RPCMsgReq<CommonRPCReq>(RPC_TYPE::RPC_POST_STORAGE_READ_REQ, req->req_common.req_number);

    post_storage_req.SerializeToArray(fwd_req_msg+1, static_cast<int>(post_storage_req.ByteSizeLong()));

    consumer_fwd->push(fwd_req);
}

void write_home_timeline_and_return(void *buf_, erpc::Rpc<erpc::CTransport> *rpc_, MPMC_QUEUE *consumer_back) {
    auto* req = static_cast<RPCMsgReq<CommonRPCReq> *>(buf_);

    social_network::HomeTimelineWriteReq home_timeline_write_req;
    home_timeline_write_req.ParseFromArray(req+1, static_cast<int>(req->req_control.data_length));
    rmem::rt_assert(home_timeline_write_req.IsInitialized(),"req parsed error");

    std::set<int64_t> user_followers = user_followers_map[home_timeline_write_req.user_id()];

    for(auto m: home_timeline_write_req.user_mentions_id()) {
        user_followers.insert(m);
    }

    for(auto m: user_followers){
        if(!user_home_timeline_map.contains(m)){
            user_home_timeline_map[m] = std::vector<int64_t>();
        }
        user_home_timeline_map[m].push_back(home_timeline_write_req.post_id());
    }

    erpc::MsgBuffer resp_buf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<CommonRPCReq>));
    new (resp_buf.buf_) RPCMsgResp<CommonRPCReq>(RPC_TYPE::RPC_HOME_TIMELINE_WRITE_RESP, req->req_common.req_number, 0, {0});

    consumer_back->push(resp_buf);
}
