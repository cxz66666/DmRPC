#pragma once
#include <hdr/hdr_histogram.h>
#include "../social_network_commons.h"
#include "../social_network.pb.h"
#include <hs_clock.h>

#include "api.h"
#include "page.h"

std::string post_storage_addr;
std::string load_balance_addr;

std::string rmem_self_addr;

std::vector<rmem::Rmem *> rmems_;
std::vector<unsigned long> rmem_base_addr;
std::atomic<uint64_t> rmems_init_number;

int64_t generate_data_num;
int64_t user_num_;
int64_t home_num_;
int64_t write_num_;

hdr_histogram *latency_user_timeline_hist_;
hdr_histogram *latency_home_timeline_hist_;
hdr_histogram *latency_write_hist_;

std::vector<std::vector<rmem::Timer>> timers(kAppMaxRPC, std::vector<rmem::Timer>(kAppMaxBuffer));


struct REQ_MSG
{
    uint32_t req_id;
    RPC_TYPE req_type;
};

struct RESP_MSG
{
    uint32_t req_id;
    int status;
};
static_assert(sizeof(REQ_MSG) == 8, "REQ_MSG size is not 8");
static_assert(sizeof(RESP_MSG) == 8, "RESP_MSG size is not 8");

class QueueStore {
public:
    QueueStore(){
        spsc_queue = new atomic_queue::AtomicQueueB2<REQ_MSG, std::allocator<REQ_MSG>, true, false, false>(kAppMaxBuffer);
        resp_spsc_queue = new atomic_queue::AtomicQueueB2<RESP_MSG, std::allocator<RESP_MSG>, true, false, false>(kAppMaxBuffer);
        req_id_ = 1;
        rpc_type_arr.insert(rpc_type_arr.end(), user_num_, RPC_TYPE::RPC_USER_TIMELINE_READ_REQ);
        rpc_type_arr.insert(rpc_type_arr.end(), home_num_, RPC_TYPE::RPC_HOME_TIMELINE_READ_REQ);
        rpc_type_arr.insert(rpc_type_arr.end(), write_num_, RPC_TYPE::RPC_COMPOSE_POST_WRITE_REQ);
    }
    ~QueueStore(){
        delete spsc_queue;
        delete resp_spsc_queue;
    }
    void PushPingReq(){
        spsc_queue->push(REQ_MSG{0, RPC_TYPE::RPC_PING});
    }
    void PushParamReq(){
        spsc_queue->push(REQ_MSG{0, RPC_TYPE::RPC_RMEM_PARAM});
    }
    void PushNextReq(){
        uint32_t now_req_id;
        mutex.lock();
        now_req_id = req_id_++;
        mutex.unlock();
        //TODO
//        if(now_req_id % 3 == 0){
//            spsc_queue->push(REQ_MSG{now_req_id, RPC_TYPE::RPC_USER_TIMELINE_READ_REQ});
//        } else if(now_req_id % 3 == 1) {
//            spsc_queue->push(REQ_MSG{now_req_id, RPC_TYPE::RPC_USER_TIMELINE_READ_REQ});
//        } else {
//            spsc_queue->push(REQ_MSG{now_req_id, RPC_TYPE::RPC_USER_TIMELINE_READ_REQ});
//        }
        spsc_queue->push(REQ_MSG{now_req_id, rpc_type_arr[now_req_id % rpc_type_arr.size()]});
        __sync_synchronize();
    }

    void PushReq(REQ_MSG msg){
        spsc_queue->push(msg);
    }
    void PushResp(RESP_MSG msg){
        resp_spsc_queue->push(msg);
    }
    REQ_MSG PopReq(){
        return spsc_queue->pop();
    }
    unsigned  GetReqSize(){
        return spsc_queue->was_size();
    }

    RESP_MSG PopResp(){
        return resp_spsc_queue->pop();
    }

    unsigned GetRespSize(){
        return resp_spsc_queue->was_size();
    }

    spinlock_mutex mutex;
    uint32_t req_id_;
    atomic_queue::AtomicQueueB2<REQ_MSG, std::allocator<REQ_MSG>, true, false, false> *spsc_queue;
    atomic_queue::AtomicQueueB2<RESP_MSG, std::allocator<RESP_MSG>, true, false, false> *resp_spsc_queue{};
    std::vector<RPC_TYPE> rpc_type_arr;
};

class ReaderHandler
{
public:
    std::vector<void*> rmem_bufs;
    std::vector<std::pair<size_t,size_t>> addrs_size;
    size_t finished_num{};
};
using READER_QUEUE = atomic_queue::AtomicQueueB2<ReaderHandler*, std::allocator<ReaderHandler*>, true, false, false>;
std::vector<READER_QUEUE *> reader_queues;


class ClientContext : public BasicContext
{
public:
    ClientContext(size_t cid, size_t sid) : client_id_(cid), server_sender_id_(sid)
    {
        queue_store = new QueueStore();
        user_timeline_req_queue = new SPSC_QUEUE(generate_data_num);
        home_timeline_req_queue = new SPSC_QUEUE(generate_data_num);
        compose_post_req_queue = new SPSC_QUEUE(generate_data_num);

    }
    ~ClientContext()
    {
    }
    erpc::MsgBuffer req_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer resp_msgbuf[kAppMaxBuffer];

    erpc::MsgBuffer ping_msgbuf;
    erpc::MsgBuffer ping_resp_msgbuf;
    erpc::MsgBuffer rmem_param_msgbuf;
    erpc::MsgBuffer rmem_param_resp_msgbuf;


    size_t client_id_;
    size_t server_sender_id_;

    int load_balance_session_num_;
    int post_storage_session_num_;

    SPSC_QUEUE *user_timeline_req_queue;
    SPSC_QUEUE *home_timeline_req_queue;
    SPSC_QUEUE *compose_post_req_queue;

    QueueStore* queue_store;
};

class ServerContext : public BasicContext
{
public:
    explicit ServerContext(size_t sid) : server_id_(sid)
    {
    }
    ~ServerContext() = default;
    size_t server_id_{};
    size_t stat_req_ping_tot{};
    size_t stat_rmem_param_tot{};
    size_t stat_req_compose_post_tot{};
    size_t stat_req_user_timeline_tot{};
    size_t stat_req_home_timeline_tot{};
    size_t stat_req_err_tot{};

    void reset_stat()
    {
        stat_req_ping_tot = 0;
        stat_rmem_param_tot = 0;
        stat_req_compose_post_tot = 0;
        stat_req_user_timeline_tot = 0;
        stat_req_home_timeline_tot = 0;
        stat_req_err_tot = 0;
    }
    QueueStore* queue_store;
};

class AppContext
{
public:
    AppContext()
    {
        for (size_t i = 0; i < FLAGS_client_num; i++)
        {
            client_contexts_.push_back(new ClientContext(i, i % FLAGS_server_num));
        }
        for (size_t i = 0; i < FLAGS_server_num; i++)
        {
            auto *ctx = new ServerContext(i);
            ctx->queue_store = client_contexts_[i]->queue_store;
            server_contexts_.push_back(ctx);
        }

        for (size_t i = 0;i < FLAGS_client_num; i++){
            reader_queues.push_back(new READER_QUEUE(kAppMaxBuffer));
        }

        rmems_.resize(FLAGS_client_num);
        rmem_base_addr.resize(FLAGS_client_num);
    }
    ~AppContext()
    {
        for (auto &ctx : client_contexts_)
        {
            delete ctx;
        }
        for (auto &ctx : server_contexts_)
        {
            delete ctx;
        }
    }

    std::vector<ClientContext *> client_contexts_;
    std::vector<ServerContext *> server_contexts_;
};

// must be used after init_service_config

void init_specific_config(){
    auto value = config_json_all["post_storage"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    post_storage_addr = value;

    value = config_json_all["load_balance"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    load_balance_addr = value;

    value = config_json_all["client"]["rmem_self_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    rmem_self_addr = value;

    auto generate_num = config_json_all["client"]["generate_num"];
    rmem::rt_assert(!generate_num.is_null(),"generate_num is null");
    generate_data_num = generate_num;

    generate_num = config_json_all["client"]["user_num"];
    rmem::rt_assert(!generate_num.is_null(),"generate_num is null");
    user_num_ = generate_num;

    generate_num = config_json_all["client"]["home_num"];
    rmem::rt_assert(!generate_num.is_null(),"generate_num is null");
    home_num_ = generate_num;

    generate_num = config_json_all["client"]["write_num"];
    rmem::rt_assert(!generate_num.is_null(),"generate_num is null");
    write_num_ = generate_num;
}

std::string generate_random_string(int length) {
    // 使用当前时间作为随机数种子
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    // 定义字符集，这里使用数字和大写字母
    std::string charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    // 定义随机数生成器
    std::mt19937 generator(seed);
    // 定义字符分布
    std::uniform_int_distribution<int> distribution(0, charset.length() - 1);

    // 生成随机字符串
    std::string result;
    for (int i = 0; i < length; ++i) {
        result += charset[distribution(generator)];
    }
    return result;
}

//void generate_compose_post_req_msgbuf(erpc::Rpc<erpc::CTransport> *rpc_, SPSC_QUEUE* consumer_queue)
//{
//    for(int64_t i=0; i<generate_data_num ;i++){
//        social_network::ComposePostData compose_post_data;
//        compose_post_data.set_user_id(i);
//        compose_post_data.set_username("username_"+std::to_string(i));
//
//        std::string text = generate_random_string(2000);
//        for(size_t tmp = 0;tmp<5; tmp++){
//            text+="@username_" + std::to_string(rand()%generate_data_num);
//        }
//        text+=" ";
//
//        for(size_t tmp=0; tmp<5; tmp++){
//            text+="http://"+generate_random_string(64);
//        }
//        compose_post_data.set_text(text);
//
//        for(size_t tmp=0; tmp<20; tmp++){
//            compose_post_data.add_media_ids(rand());
//            compose_post_data.add_media_types("png");
//        }
//
//        compose_post_data.set_post_type(social_network::PostType::POST_TYPE_POST);
//
//        size_t size = compose_post_data.ByteSizeLong();
//        auto req_msgbuf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<CommonRPCReq>) + size);
//        auto req = new (req_msgbuf.buf_) RPCMsgReq<CommonRPCReq>(RPC_TYPE::RPC_COMPOSE_POST_WRITE_REQ, 0, {size});
//        compose_post_data.SerializeToArray(req+1, size);
//        consumer_queue->push(req_msgbuf);
//    }
//}

void generate_compose_post_req_msgbuf(erpc::Rpc<erpc::CTransport> *rpc_, SPSC_QUEUE* consumer_queue)
{
    for(int64_t i=0; i<generate_data_num ;i++){
        social_network::Post post;

        post.set_post_id(rand());

        post.mutable_creator()->set_user_id(i);
        post.mutable_creator()->set_username("username_"+std::to_string(i));

        post.set_req_id(i);
        std::string text = generate_random_string(2000);
        for(size_t tmp = 0;tmp<5; tmp++){
            text+="@username_" + std::to_string(rand()%generate_data_num);
        }
        text+=" ";

        for(size_t tmp=0; tmp<5; tmp++){
            text+="http://"+generate_random_string(64);
        }
        post.set_text(text);

        for(size_t tmp=0; tmp<10; tmp++){
            auto media = post.add_media();
            media->set_media_id(rand());
            media->set_media_type("png");
        }

        post.set_post_type(social_network::PostType::POST_TYPE_POST);

        size_t size = post.ByteSizeLong();
        auto req_msgbuf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<CommonRPCReq>) + size);
        auto req = new (req_msgbuf.buf_) RPCMsgReq<CommonRPCReq>(RPC_TYPE::RPC_COMPOSE_POST_WRITE_REQ, 0, {size});
        post.SerializeToArray(req+1, size);
        consumer_queue->push(req_msgbuf);
    }
}


void generate_user_timeline_req_msgbuf(erpc::Rpc<erpc::CTransport> *rpc_, SPSC_QUEUE* consumer_queue)
{
    for(int64_t i=0; i<generate_data_num ;i++){
        auto req_msgbuf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<UserTimeLineReq>));
        new (req_msgbuf.buf_) RPCMsgReq<UserTimeLineReq>(RPC_TYPE::RPC_USER_TIMELINE_READ_REQ, 0, {0, i, 0, 10});
        consumer_queue->push(req_msgbuf);
    }
}

void generate_home_timeline_req_msgbuf(erpc::Rpc<erpc::CTransport> *rpc_, SPSC_QUEUE* consumer_queue)
{
    for(int64_t i=0; i<generate_data_num ;i++){
        auto req_msgbuf = rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<HomeTimeLineReq>));
        new (req_msgbuf.buf_) RPCMsgReq<HomeTimeLineReq>(RPC_TYPE::RPC_HOME_TIMELINE_READ_REQ, 0, {0, i, 0, 10});
        consumer_queue->push(req_msgbuf);
    }
}




