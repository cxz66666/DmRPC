#pragma once
#include <hdr/hdr_histogram.h>
#include "../social_network_commons.h"
#include "../social_network.pb.h"
#include "phmap.h"
#include "spinlock_mutex.h"
#include "../utils_mongodb.h"
#include <regex>

std::string nginx_addr;
std::string unique_id_addr;
std::string url_shorten_addr;
std::string user_mention_addr;
std::string user_timeline_addr;
std::string user_service_addr;
std::string home_timeline_addr;
std::string post_storage_addr;


class ReqState
{
public:

    explicit ReqState(uint32_t r_id, const social_network::ComposePostData &data, MPMC_QUEUE *queue, erpc::Rpc<erpc::CTransport> *rpc){
        req_id = r_id;
        original_text = data.text();
        creator.set_user_id(data.user_id());
        creator.set_username(data.username());

        post.set_post_type(data.post_type());
        post.set_req_id(static_cast<int64_t>(r_id));
        rmem::rt_assert(data.media_ids_size()==data.media_types_size(), "media id size != media type size");

        for(int i=0; i<data.media_ids_size(); i++){
            social_network::Media media;
            media.set_media_id(data.media_ids(i));
            media.set_media_type(data.media_types(i));
            post.add_media()->CopyFrom(media);
        }

        consumer_mpmc_queue = queue;
        rpc_ = rpc;
        finished_number = 0;
    }

    void send_first_step() {
        // unique id
        auto req_msgbuf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<UniqueIDReq>));
        new(req_msgbuf.buf_) RPCMsgReq<UniqueIDReq>(RPC_TYPE::RPC_UNIQUE_ID, req_id, {0});
        consumer_mpmc_queue->push(req_msgbuf);

        // user
        size_t extra_length = creator.ByteSizeLong();
        req_msgbuf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<CommonRPCReq>)+ extra_length);
        auto req2 = new(req_msgbuf.buf_) RPCMsgReq<CommonRPCReq>(RPC_TYPE::RPC_COMPOSE_CREATOR_WITH_USER_ID, req_id, {extra_length});
        creator.SerializeToArray(req2+1, extra_length);
        consumer_mpmc_queue->push(req_msgbuf);

        std::regex e("@[a-zA-Z0-9-_]+");
        std::sregex_iterator it1(original_text.begin(), original_text.end(), e);
        std::sregex_iterator end;
        while (it1 != end) {
            user_mention_req.add_names((*it1).str().substr(1));
            it1++;
        }

        e = "(http://|https://)([a-zA-Z0-9_!~*'().&=+$%-]+)";
        std::sregex_iterator it2(original_text.begin(), original_text.end(), e);

        while (it2 != end) {
            url_shorten_req.add_urls((*it2).str());
            it2++;
        }

        extra_length = user_mention_req.ByteSizeLong();
        req_msgbuf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<CommonRPCReq>)+ extra_length);
        auto req3 = new(req_msgbuf.buf_) RPCMsgReq<CommonRPCReq>(RPC_TYPE::RPC_USER_MENTION, req_id, {extra_length});
        user_mention_req.SerializeToArray(req3+1, extra_length);
        consumer_mpmc_queue->push(req_msgbuf);

        extra_length = url_shorten_req.ByteSizeLong();
        req_msgbuf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<CommonRPCReq>)+ extra_length);
        auto req4 = new(req_msgbuf.buf_) RPCMsgReq<CommonRPCReq>(RPC_TYPE::RPC_URL_SHORTEN, req_id, {extra_length});
        url_shorten_req.SerializeToArray(req4+1, extra_length);
        consumer_mpmc_queue->push(req_msgbuf);

    }

    void send_second_step() {
//        printf("ready to second step %u\n", req_id);
        size_t extra_length = post.ByteSizeLong();
        auto req_msgbuf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<CommonRPCReq>)+ extra_length);
        auto req1 = new(req_msgbuf.buf_) RPCMsgReq<CommonRPCReq>(RPC_TYPE::RPC_POST_STORAGE_WRITE_REQ, req_id, {extra_length});
        post.SerializeToArray(req1+1, extra_length);
        consumer_mpmc_queue->push(req_msgbuf);


//        social_network::HomeTimelineWriteReq home_timeline_write_req;
//        home_timeline_write_req.set_user_id(creator.user_id());
//        home_timeline_write_req.set_post_id(post.post_id());
//        home_timeline_write_req.set_timestamp(post.timestamp());
//        for(auto &item: post.user_mentions()){
//            home_timeline_write_req.add_user_mentions_id(item.user_id());
//        }
//        extra_length = home_timeline_write_req.ByteSizeLong();
//        req_msgbuf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<CommonRPCReq>)+ extra_length);
//        auto req2 = new(req_msgbuf.buf_) RPCMsgReq<CommonRPCReq>(RPC_TYPE::RPC_HOME_TIMELINE_WRITE_REQ, req_id, {extra_length});
//        home_timeline_write_req.SerializeToArray(req2+1, extra_length);
//        consumer_mpmc_queue->push(req_msgbuf);
//
//        req_msgbuf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<UserTimeLineWriteReq>));
//        new(req_msgbuf.buf_) RPCMsgReq<UserTimeLineWriteReq>(RPC_TYPE::RPC_USER_TIMELINE_WRITE_REQ, req_id, {post.post_id(),creator.user_id(), post.timestamp()});
//        consumer_mpmc_queue->push(req_msgbuf);
    }
    erpc::MsgBuffer generate_resp_msg(){
        erpc::MsgBuffer resp_buf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<CommonRPCReq>));
        new (resp_buf.buf_) RPCMsgReq<CommonRPCReq>(RPC_TYPE::RPC_COMPOSE_POST_WRITE_RESP, req_id, {0});
        return resp_buf;
    }
    void  generate_next_step(){
        mutex.lock();
        int now_number = finished_number.fetch_add(1);
        printf("%d\n",now_number);
        if(now_number == 4) {
            send_second_step();
        }
        mutex.unlock();
    }

    uint32_t req_id;
    std::string original_text;
    social_network::Creator creator;
    social_network::UrlShortenReq url_shorten_req;
    social_network::UserMentionReq user_mention_req;


    MPMC_QUEUE *consumer_mpmc_queue;
    erpc::Rpc<erpc::CTransport> *rpc_;

    social_network::Post post;

    // is always true after construct

    std::atomic<int> finished_number;
    spinlock_mutex mutex;
};

class ReqStateStore
{
public:
    std::map<uint32_t, ReqState*> req_state_map;
};

class ClientContext : public BasicContext
{
public:
    ClientContext(size_t cid, size_t sid, size_t rid) : client_id_(cid), server_sender_id_(sid), server_receiver_id_(rid)
    {
        forward_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);
        forward_all_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);
        backward_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);

        state_store = new ReqStateStore();
    }
    ~ClientContext()
    {
        delete forward_mpmc_queue;
        delete forward_all_mpmc_queue;
        delete backward_mpmc_queue;

        delete state_store;
    }
    erpc::MsgBuffer req_backward_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer resp_backward_msgbuf[kAppMaxBuffer];

    erpc::MsgBuffer req_unique_id_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer resp_unique_id_msgbuf[kAppMaxBuffer];

    erpc::MsgBuffer req_url_shorten_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer resp_url_shorten_msgbuf[kAppMaxBuffer];

    erpc::MsgBuffer req_user_mention_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer resp_user_mention_msgbuf[kAppMaxBuffer];

    erpc::MsgBuffer req_user_timeline_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer resp_user_timeline_msgbuf[kAppMaxBuffer];

    erpc::MsgBuffer req_user_service_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer resp_user_service_msgbuf[kAppMaxBuffer];

    erpc::MsgBuffer req_home_timeline_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer resp_home_timeline_msgbuf[kAppMaxBuffer];

    erpc::MsgBuffer req_post_storage_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer resp_post_storage_msgbuf[kAppMaxBuffer];


    size_t client_id_;
    size_t server_sender_id_;
    size_t server_receiver_id_;

    int nginx_session_number;
    int unique_id_session_number;
    int url_shorten_session_number;
    int user_mention_session_number;
    int user_timeline_session_number;
    int user_service_session_number;
    int home_timeline_session_number;
    int post_storage_session_number;

    MPMC_QUEUE *forward_mpmc_queue;
    MPMC_QUEUE *forward_all_mpmc_queue;
    MPMC_QUEUE *backward_mpmc_queue;

    ReqStateStore* state_store;
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
    size_t stat_req_compose_post_write_req_tot{};
    size_t stat_req_err_tot{};

    void reset_stat()
    {
        stat_req_ping_tot = 0;
        stat_req_compose_post_write_req_tot = 0;
        stat_req_err_tot = 0;
    }

    MPMC_QUEUE *forward_all_mpmc_queue{};

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
            client_contexts_.push_back(new ClientContext(i, (i % FLAGS_server_num) + kAppMaxRPC , i % FLAGS_server_num));
#elif defined(RMEM_PROGRAM)
            client_contexts_.push_back(new ClientContext(i, (i % FLAGS_server_num) + kAppMaxRPC, (i % FLAGS_server_num) + kAppMaxRPC));
#endif
        }
        for (size_t i = 0; i < FLAGS_server_num; i++)
        {
            auto *ctx = new ServerContext(i);
            ctx->forward_all_mpmc_queue = client_contexts_[i]->forward_all_mpmc_queue;
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
    auto value = config_json_all["nginx"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    nginx_addr = value;

    value= config_json_all["unique_id"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    unique_id_addr = value;

    value = config_json_all["url_shorten"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    url_shorten_addr = value;

    value = config_json_all["user_mention"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    user_mention_addr = value;

    value = config_json_all["user_timeline"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    user_timeline_addr = value;

    value = config_json_all["user_service"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    user_service_addr = value;

    value = config_json_all["home_timeline"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    home_timeline_addr = value;

    value = config_json_all["post_storage"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    post_storage_addr = value;
}

void compose_post_write_and_create(void *buf_, ReqStateStore* store,  MPMC_QUEUE *forward_queue, erpc::Rpc<erpc::CTransport> *c_rpc){
    auto* req = static_cast<RPCMsgReq<CommonRPCReq> *>(buf_);
    social_network::ComposePostData data;
    data.ParseFromArray(req+1, req->req_control.data_length);

    rmem::rt_assert(!store->req_state_map.count(req->req_common.req_number), "req number already exist");

    ReqState *req_state = new ReqState(req->req_common.req_number, data, forward_queue, c_rpc);

    store->req_state_map[req->req_common.req_number] = req_state;

    req_state->send_first_step();

}