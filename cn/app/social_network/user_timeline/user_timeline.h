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


static mongoc_client_pool_t* mongodb_client_pool;
int mongodb_conns_num;
phmap::flat_hash_map<int64_t , std::set<int64_t>> user_timeline_map;


class ClientContext : public BasicContext
{
public:
    ClientContext(size_t cid, size_t sid, size_t rid) : client_id_(cid), server_sender_id_(sid), server_receiver_id_(rid)
    {
        forward_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);
        forward_all_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);
        backward_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);
        nginx_session_number = 0;
        compose_post_session_number = 0;
        post_storage_session_number = 0;
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

    int nginx_session_number;
    int compose_post_session_number;
    int post_storage_session_number;

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
    size_t stat_req_user_timeline_write_req_tot{};
    size_t stat_req_user_timeline_read_req_tot{};
    size_t stat_req_post_storage_read_resp_tot{};
    size_t stat_req_err_tot{};

    spinlock_mutex init_mutex;
    bool is_pinged{false};
    bool mongodb_init_finished{false};

    void reset_stat()
    {
        stat_req_ping_tot = 0;
        stat_req_user_timeline_write_req_tot = 0;
        stat_req_user_timeline_read_req_tot = 0;
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

    auto conns = config_json_all["user_timeline_mongodb"]["connections"];
    rmem::rt_assert(!conns.is_null(),"value is null");
    mongodb_conns_num = conns;
}

void read_post_details(void *buf_, erpc::Rpc<erpc::CTransport> *rpc_, MPMC_QUEUE *consumer_fwd, MPMC_QUEUE *consumer_back) {
    auto* req = static_cast<RPCMsgReq<UserTimeLineReq> *>(buf_);

    bool is_fwd = true;

    if(req->req_control.stop <= req->req_control.start || req->req_control.start < 0){
        is_fwd = false;
    }

    if(!user_timeline_map.contains(req->req_control.user_id)){
        is_fwd = false;
    }
    std::set<int64_t> &post_ids = user_timeline_map[req->req_control.user_id];
    if(req->req_control.start > static_cast<int>(post_ids.size())){
        is_fwd = false;
    }

    if(!is_fwd){
        erpc::MsgBuffer resp_buf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<UserTimeLineReq>));
        new (resp_buf.buf_) RPCMsgReq<UserTimeLineReq>(RPC_TYPE::RPC_USER_TIMELINE_READ_RESP, req->req_common.req_number,
                                                                    {0, req->req_control.user_id, req->req_control.start, req->req_control.stop });
        consumer_back->push(resp_buf);
        return;
    }

    social_network::PostStorageReadReq post_storage_req;
    post_storage_req.set_rpc_type(static_cast<uint32_t>(RPC_TYPE::RPC_USER_TIMELINE_READ_REQ));

    int now_index = 0;
    for(int64_t post_id : post_ids){
        if(req->req_control.start<=now_index && now_index<req->req_control.stop){
            post_storage_req.add_post_ids(post_id);
        }
        now_index++;
        if(now_index==req->req_control.stop){
            break;
        }
    }

    erpc::MsgBuffer fwd_req = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<CommonRPCReq>) + post_storage_req.ByteSizeLong());
    auto* fwd_req_msg = new (fwd_req.buf_) RPCMsgReq<CommonRPCReq>(RPC_TYPE::RPC_POST_STORAGE_READ_REQ, req->req_common.req_number);

    post_storage_req.SerializeToArray(fwd_req_msg+1, static_cast<int>(post_storage_req.ByteSizeLong()));

    consumer_fwd->push(fwd_req);
}

void write_post_ids_and_return(void *buf_, erpc::Rpc<erpc::CTransport> *rpc_, MPMC_QUEUE *consumer_back) {
    auto* req = static_cast<RPCMsgReq<UserTimeLineWriteReq> *>(buf_);

    erpc::MsgBuffer resp_buf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<UserTimeLineWriteReq>));

    auto* resp = new (resp_buf.buf_) RPCMsgReq<UserTimeLineWriteReq>(RPC_TYPE::RPC_USER_TIMELINE_WRITE_RESP, req->req_common.req_number,
                                                                    {req->req_control.post_id, req->req_control.user_id, req->req_control.timestamp });

    std::set<int64_t> &post_ids = user_timeline_map[req->req_control.user_id];
    if(post_ids.count(req->req_control.post_id)){
        resp->req_control.timestamp++;
        consumer_back->push(resp_buf);
        return;
    }

    UserTimeLineWriteReq user_timeline_write_req = req->req_control;

    std::future<void> mongo_update_future =
            std::async(std::launch::async, [=](UserTimeLineWriteReq r, const erpc::MsgBuffer resp_buffer,MPMC_QUEUE *c_back) {
                mongoc_client_t *mongodb_client = mongoc_client_pool_pop(mongodb_client_pool);
                auto collection = mongoc_client_get_collection(mongodb_client, "user-timeline", "user-timeline");

                bson_t *query = bson_new();

                BSON_APPEND_INT64(query, "user_id", r.user_id);
                bson_t *update =
                        BCON_NEW("$push", "{", "posts", "{", "$each", "[", "{", "post_id",
                                 BCON_INT64(r.post_id), "timestamp", BCON_INT64(r.timestamp), "}",
                                 "]", "$position", BCON_INT32(0), "}", "}");

                bson_error_t error;
                bson_t reply;

                bool updated = mongoc_collection_find_and_modify(collection, query, nullptr,
                                                                 update, nullptr, false, true,
                                                                 true, &reply, &error);

                if (!updated) {
                    // update the newly inserted document (upsert: false)
                    updated = mongoc_collection_find_and_modify(collection, query, nullptr,
                                                                update, nullptr, false, false,
                                                                true, &reply, &error);
                    if (!updated) {
                        RMEM_ERROR("mongodb update error! %ld %ld %ld ", r.user_id, r.post_id, r.timestamp);
                        exit(1);
                    }
                }
                bson_destroy(update);
                bson_destroy(&reply);
                bson_destroy(query);
                mongoc_collection_destroy(collection);
                mongoc_client_pool_push(mongodb_client_pool, mongodb_client);

                //闭包，烦死了
                c_back->push(resp_buffer);
            },user_timeline_write_req,resp_buf,consumer_back);
}