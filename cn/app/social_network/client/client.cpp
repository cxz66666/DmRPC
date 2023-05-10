#include <thread>
#include "numautil.h"
#include "spinlock_mutex.h"
#include "client.h"



void connect_sessions(ClientContext *c)
{

    // connect to backward server
    c->load_balance_session_num_ = c->rpc_->create_session(load_balance_addr, c->server_sender_id_);
    rmem::rt_assert(c->load_balance_session_num_ >= 0, "Failed to create session");

    c->post_storage_session_num_ = c->rpc_->create_session(post_storage_addr, c->server_sender_id_ + kAppMaxRPC);
    rmem::rt_assert(c->post_storage_session_num_ >= 0, "Failed to create session");

    while (c->num_sm_resps_ != 2)
    {
        c->rpc_->run_event_loop(kAppEvLoopMs);
        if (unlikely(ctrl_c_pressed == 1))
        {
            printf("Ctrl-C pressed. Exiting\n");
            return;
        }
    }
}


void ping_resp_handler(erpc::ReqHandle *req_handler, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_ping_tot++;

    auto *req_msgbuf = req_handler->get_req_msgbuf();
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<PingRPCReq>), "data size not match");

    auto *req = reinterpret_cast<RPCMsgReq<PingRPCReq> *>(req_msgbuf->buf_);

    new (req_handler->pre_resp_msgbuf_.buf_) RPCMsgResp<PingRPCResp>(req->req_common.type, req->req_common.req_number, 0, {0});
    ctx->rpc_->resize_msg_buffer(&req_handler->pre_resp_msgbuf_, sizeof(RPCMsgResp<PingRPCResp>));

    ctx->rpc_->enqueue_response(req_handler, &req_handler->pre_resp_msgbuf_);

    ctx->queue_store->PushResp(RESP_MSG{req->req_common.req_number, 0});

}

void compose_post_write_resp_handler(erpc::ReqHandle *req_handler, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_compose_post_tot++;
    auto *req_msgbuf = req_handler->get_req_msgbuf();

    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf->buf_);

    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<CommonRPCReq>) + req->req_control.data_length, "data size not match");
    hdr_record_value_atomic(latency_write_hist_,
                            static_cast<int64_t>(timers[ctx->server_id_][req->req_common.req_number % kAppMaxBuffer].toc() * 10));

    new (req_handler->pre_resp_msgbuf_.buf_) RPCMsgResp<CommonRPCResp>(req->req_common.type, req->req_common.req_number, 0, {0});
    ctx->rpc_->resize_msg_buffer(&req_handler->pre_resp_msgbuf_, sizeof(RPCMsgResp<CommonRPCResp>));

    ctx->queue_store->PushNextReq();
    __sync_synchronize();

    ctx->rpc_->enqueue_response(req_handler, &req_handler->pre_resp_msgbuf_);

}

void user_timeline_read_resp_handler(erpc::ReqHandle *req_handler, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_user_timeline_tot++;
    auto *req_msgbuf = req_handler->get_req_msgbuf();

    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf->buf_);
//    printf("user timeline read resp, type %u, number %u, data_length %ld\n", static_cast<uint32_t>(req->req_common.type), req->req_common.req_number, req->req_control.data_length);
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<CommonRPCReq>) + req->req_control.data_length, "data size not match");

    new (req_handler->pre_resp_msgbuf_.buf_) RPCMsgResp<CommonRPCResp>(req->req_common.type, req->req_common.req_number, 0, {0});
    ctx->rpc_->resize_msg_buffer(&req_handler->pre_resp_msgbuf_, sizeof(RPCMsgResp<CommonRPCResp>));


#if defined(ERPC_PROGRAM)
    hdr_record_value_atomic(latency_user_timeline_hist_,
                            static_cast<int64_t>(timers[ctx->server_id_][req->req_common.req_number % kAppMaxBuffer].toc() * 10));

    ctx->queue_store->PushNextReq();
    __sync_synchronize();
#elif defined(RMEM_PROGRAM)
    social_network::PostStorageReadRefResp post_storage_read_ref_resp;
    post_storage_read_ref_resp.ParseFromArray(req+1, req->req_control.data_length);
    ReaderHandler *reader_handler = new ReaderHandler();
    reader_handler->hist = latency_user_timeline_hist_;
    reader_handler->req_num = req->req_common.req_number;
    for(int i=0;i< post_storage_read_ref_resp.posts_ref_addr_size();i++)
    {
        reader_handler->addrs_size.push_back({post_storage_read_ref_resp.posts_ref_addr(i), post_storage_read_ref_resp.posts_ref_size(i)});
        reader_handler->rmem_bufs.push_back(malloc(post_storage_read_ref_resp.posts_ref_size(i)));
    }
    reader_queues[ctx->server_id_]->push(reader_handler);

#endif
    ctx->rpc_->enqueue_response(req_handler, &req_handler->pre_resp_msgbuf_);

}

void home_timeline_read_resp_handler(erpc::ReqHandle *req_handler, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_home_timeline_tot++;
    auto *req_msgbuf = req_handler->get_req_msgbuf();

    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf->buf_);
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<CommonRPCReq>) + req->req_control.data_length, "data size not match");

    new (req_handler->pre_resp_msgbuf_.buf_) RPCMsgResp<CommonRPCResp>(req->req_common.type, req->req_common.req_number, 0, {0});
    ctx->rpc_->resize_msg_buffer(&req_handler->pre_resp_msgbuf_, sizeof(RPCMsgResp<CommonRPCResp>));

#if defined(ERPC_PROGRAM)
    hdr_record_value_atomic(latency_home_timeline_hist_,
                            static_cast<int64_t>(timers[ctx->server_id_][req->req_common.req_number % kAppMaxBuffer].toc() * 10));

    ctx->queue_store->PushNextReq();
    __sync_synchronize();
#elif defined(RMEM_PROGRAM)
    social_network::PostStorageReadRefResp post_storage_read_ref_resp;
    post_storage_read_ref_resp.ParseFromArray(req+1, req->req_control.data_length);
    ReaderHandler *reader_handler = new ReaderHandler();
    reader_handler->hist = latency_home_timeline_hist_;
    reader_handler->req_num = req->req_common.req_number;
    for(int i=0;i< post_storage_read_ref_resp.posts_ref_addr_size();i++)
    {
        reader_handler->addrs_size.push_back({post_storage_read_ref_resp.posts_ref_addr(i), post_storage_read_ref_resp.posts_ref_size(i)});
        reader_handler->rmem_bufs.push_back(malloc(post_storage_read_ref_resp.posts_ref_size(i)));
    }
    reader_queues[ctx->server_id_]->push(reader_handler);

#endif

    ctx->rpc_->enqueue_response(req_handler, &req_handler->pre_resp_msgbuf_);

}

void callback_ping(void *_context, void *_tag)
{
    _unused(_tag);
    auto *ctx = static_cast<ClientContext *>(_context);

    auto *resp = reinterpret_cast<RPCMsgResp<PingRPCResp>*>(ctx->ping_resp_msgbuf.buf_);

    // 如果返回值不为0，则认为后续不会有响应，直接将请求号和错误码放入队列
    // 如果返回值为0，则认为后续将有响应，不care
    rmem::rt_assert(resp->resp_common.status == 0, "Ping callback failed");
}


void handler_ping(ClientContext *ctx, REQ_MSG req_msg)
{
    new (ctx->ping_msgbuf.buf_) RPCMsgReq<PingRPCReq>(RPC_TYPE::RPC_PING, req_msg.req_id, {0});
    ctx->rpc_->enqueue_request(ctx->load_balance_session_num_, static_cast<uint8_t>(RPC_TYPE::RPC_PING),
                               &ctx->ping_msgbuf, &ctx->ping_resp_msgbuf,
                               callback_ping, nullptr);
}

void callback_rmem_param(void *_context, void *_tag)
{
    _unused(_tag);
    auto *ctx = static_cast<ClientContext *>(_context);

    auto *resp = reinterpret_cast<RPCMsgResp<CommonRPCResp>*>(ctx->rmem_param_resp_msgbuf.buf_);

    // 如果返回值不为0，则认为后续不会有响应，直接将请求号和错误码放入队列
    // 如果返回值为0，则认为后续将有响应，不care
    rmem::rt_assert(resp->resp_common.status == 0, "rmem_param callback failed");

    if(rmems_[ctx->client_id_] != nullptr) {
        return;
    }

    if(resp->resp_control.data_length == 0) {
        RMEM_INFO("rmem not init yet, waiting for it");
    } else {
        social_network::RmemParam rmem_param;
        rmem_param.ParseFromArray(resp+1, resp->resp_control.data_length);
        rmem::rt_assert(rmem_param.has_addr(), "param doesn't have addr");
        rmem::rt_assert(rmems_[ctx->client_id_] == nullptr, "rmem already init!");
        auto *rmem = new rmem::Rmem(0);

        int session_id = rmem->connect_session(rmem_param.addr(), rmem_param.rmem_thread_id());
        rmem::rt_assert(session_id >= 0, "connect_session failed");
        printf("rmem param %ld %d %d\n", rmem_param.fork_rmem_addr(), rmem_param.rmem_thread_id(), rmem_param.rmem_session_id());
        rmem_base_addr[ctx->client_id_] = rmem->rmem_join(rmem_param.fork_rmem_addr(), rmem_param.rmem_thread_id(), rmem_param.rmem_session_id());
        RMEM_INFO("client thread %ld join success, based addr %ld\n", ctx->client_id_, rmem_base_addr[ctx->client_id_]);

        rmems_[ctx->client_id_] = rmem;
        __sync_synchronize();
        rmems_init_number++;
    }
}

void handler_rmem_param(ClientContext *ctx, REQ_MSG req_msg)
{
    new (ctx->rmem_param_msgbuf.buf_) RPCMsgReq<RmemParamReq>(RPC_TYPE::RPC_RMEM_PARAM, req_msg.req_id, {0});
    ctx->rpc_->enqueue_request(ctx->post_storage_session_num_, static_cast<uint8_t>(RPC_TYPE::RPC_RMEM_PARAM),
                               &ctx->rmem_param_msgbuf, &ctx->rmem_param_resp_msgbuf,
                               callback_rmem_param, nullptr);
}

void callback_common(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    auto *resp = reinterpret_cast<CommonResp *>(ctx->resp_msgbuf[req_id % kAppMaxBuffer].buf_);

    rmem::rt_assert(resp->status == 0, "resp status error");

    switch(resp->type){
        case RPC_TYPE::RPC_COMPOSE_POST_WRITE_REQ:
            ctx->compose_post_req_queue->push(ctx->req_msgbuf[req_id % kAppMaxBuffer]);
            break;
        case RPC_TYPE::RPC_USER_TIMELINE_READ_REQ:
            ctx->user_timeline_req_queue->push(ctx->req_msgbuf[req_id % kAppMaxBuffer]);
            break;
        case RPC_TYPE::RPC_HOME_TIMELINE_READ_REQ:
            ctx->home_timeline_req_queue->push(ctx->req_msgbuf[req_id % kAppMaxBuffer]);
            break;
        case RPC_TYPE::RPC_POST_STORAGE_WRITE_REQ:
            ctx->compose_post_req_queue->push(ctx->req_msgbuf[req_id % kAppMaxBuffer]);
            break;

        default:
            RMEM_ERROR("inlegal resp type");
            exit(1);
    }

}

void handler_compose_post_write_req(ClientContext *ctx, REQ_MSG req_msg)
{
    erpc::MsgBuffer &req_msgbuf = ctx->req_msgbuf[req_msg.req_id % kAppMaxBuffer];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_msgbuf[req_msg.req_id % kAppMaxBuffer];

    req_msgbuf = ctx->compose_post_req_queue->pop();
//    new (req_msgbuf.buf_) CommonReq{RPC_TYPE::RPC_COMPOSE_POST_WRITE_REQ, req_msg.req_id};

    new (req_msgbuf.buf_) CommonReq{RPC_TYPE::RPC_POST_STORAGE_WRITE_REQ, req_msg.req_id};

    timers[ctx->client_id_][req_msg.req_id % kAppMaxBuffer].tic();
    ctx->rpc_->enqueue_request(ctx->post_storage_session_num_, static_cast<uint8_t>(RPC_TYPE::RPC_POST_STORAGE_WRITE_REQ),
                               &req_msgbuf, &resp_msgbuf,
                               callback_common, reinterpret_cast<void *>(req_msg.req_id));
}

void handler_user_timeline_read_req(ClientContext *ctx, REQ_MSG req_msg)
{
    erpc::MsgBuffer &req_msgbuf = ctx->req_msgbuf[req_msg.req_id % kAppMaxBuffer];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_msgbuf[req_msg.req_id % kAppMaxBuffer];

    req_msgbuf = ctx->user_timeline_req_queue->pop();
    new (req_msgbuf.buf_) CommonReq{RPC_TYPE::RPC_USER_TIMELINE_READ_REQ, req_msg.req_id};

    timers[ctx->client_id_][req_msg.req_id % kAppMaxBuffer].tic();
    ctx->rpc_->enqueue_request(ctx->load_balance_session_num_, static_cast<uint8_t>(RPC_TYPE::RPC_USER_TIMELINE_READ_REQ),
                               &req_msgbuf, &resp_msgbuf,
                               callback_common, reinterpret_cast<void *>(req_msg.req_id));
}

void handler_home_timeline_read_req(ClientContext *ctx, REQ_MSG req_msg)
{
    erpc::MsgBuffer &req_msgbuf = ctx->req_msgbuf[req_msg.req_id % kAppMaxBuffer];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_msgbuf[req_msg.req_id % kAppMaxBuffer];

    req_msgbuf = ctx->home_timeline_req_queue->pop();
    new (req_msgbuf.buf_) CommonReq{RPC_TYPE::RPC_HOME_TIMELINE_READ_REQ, req_msg.req_id};

    timers[ctx->client_id_][req_msg.req_id % kAppMaxBuffer].tic();
    ctx->rpc_->enqueue_request(ctx->load_balance_session_num_, static_cast<uint8_t>(RPC_TYPE::RPC_HOME_TIMELINE_READ_REQ),
                               &req_msgbuf, &resp_msgbuf,
                               callback_common, reinterpret_cast<void *>(req_msg.req_id));

}


void client_thread_func(size_t thread_id, ClientContext *ctx, erpc::Nexus *nexus)
{
    ctx->client_id_ = thread_id;
    std::vector<size_t> port_vec = flags_get_numa_ports(0);
    uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

    uint8_t rpc_id = thread_id + FLAGS_server_num + kAppMaxRPC;

    erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(ctx),
                                    rpc_id,
                                    basic_sm_handler_client, phy_port);
    rpc.retry_connect_on_invalid_rpc_id_ = true;
    ctx->rpc_ = &rpc;

    for (size_t i=0; i< kAppMaxBuffer; i++)
    {
        ctx->resp_msgbuf[i] = rpc.alloc_msg_buffer_or_die(sizeof(RPCMsgResp<CommonRPCResp>));
    }
    generate_compose_post_req_msgbuf(ctx->rpc_, ctx->compose_post_req_queue);
    generate_user_timeline_req_msgbuf(ctx->rpc_, ctx->user_timeline_req_queue);
    generate_home_timeline_req_msgbuf(ctx->rpc_, ctx->home_timeline_req_queue);
    ctx->ping_msgbuf = rpc.alloc_msg_buffer_or_die(sizeof(RPCMsgReq<PingRPCReq>));
    ctx->ping_resp_msgbuf = rpc.alloc_msg_buffer_or_die(sizeof(RPCMsgResp<PingRPCResp>));
    ctx->rmem_param_msgbuf = rpc.alloc_msg_buffer_or_die(sizeof(RPCMsgReq<RmemParamReq>));
    ctx->rmem_param_resp_msgbuf = rpc.alloc_msg_buffer_or_die(KB(1));


    connect_sessions(ctx);

    using FUNC_HANDLER = std::function<void(ClientContext *, REQ_MSG)>;


    std::map<RPC_TYPE ,FUNC_HANDLER > handlers{
            {RPC_TYPE::RPC_PING, handler_ping},
            {RPC_TYPE::RPC_RMEM_PARAM, handler_rmem_param},
            {RPC_TYPE::RPC_COMPOSE_POST_WRITE_REQ, handler_compose_post_write_req},
            {RPC_TYPE::RPC_USER_TIMELINE_READ_REQ, handler_user_timeline_read_req},
            {RPC_TYPE::RPC_HOME_TIMELINE_READ_REQ, handler_home_timeline_read_req},
    };

    while (true)
    {
        unsigned size = ctx->queue_store->GetReqSize();
        __sync_synchronize();
        for (unsigned i = 0; i < size; i++)
        {
            REQ_MSG req_msg = ctx->queue_store->PopReq();

            handlers[req_msg.req_type](ctx, req_msg);
        }
        ctx->rpc_->run_event_loop_once();
        if (unlikely(ctrl_c_pressed))
        {
            break;
        }
    }
}

void server_thread_func(size_t thread_id, ServerContext *ctx, erpc::Nexus *nexus)
{
    ctx->server_id_ = thread_id;
    std::vector<size_t> port_vec = flags_get_numa_ports(0);
    uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

    uint8_t rpc_id = thread_id + kAppMaxRPC;

    erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(ctx),
                                    rpc_id,
                                    basic_sm_handler_server, phy_port);
    rpc.retry_connect_on_invalid_rpc_id_ = true;
    ctx->rpc_ = &rpc;

    while (true)
    {
        ctx->reset_stat();
        erpc::ChronoTimer start;
        start.reset();
        rpc.run_event_loop(kAppEvLoopMs);
        const double seconds = start.get_sec();
        printf("thread %zu: ping: %.2f, param: %.2f, compose: %.2f, user: %.2f, home: %.2f \n", thread_id,
               ctx->stat_req_ping_tot / seconds, ctx->stat_rmem_param_tot / seconds, ctx->stat_req_compose_post_tot / seconds,
               ctx->stat_req_user_timeline_tot / seconds, ctx->stat_req_home_timeline_tot / seconds);

        ctx->rpc_->reset_dpath_stats();
        // more handler
        if (ctrl_c_pressed == 1)
        {
            break;
        }
    }
}
#if defined(RMEM_PROGRAM)
void reader_thread_func(size_t thread_id, QueueStore* store) {
    std::queue<ReaderHandler*> queues;

    size_t reading_posts = 0;
    ReaderHandler *tmp = nullptr;
    const int max_batch = 9;
    int* batch_buffer = new int[max_batch];

    while(rmems_init_number != FLAGS_client_num) {
        usleep(1000);
    }
    while(true) {

        if(reading_posts<kAppMaxBuffer) {
            if(reader_queues[thread_id]->try_pop(tmp)){
                queues.push(tmp);
                for(size_t i=0;i<tmp->rmem_bufs.size();i++){
                    rmems_[thread_id]->rmem_read_async(tmp->rmem_bufs[i], tmp->addrs_size[i].first + rmem_base_addr[thread_id], tmp->addrs_size[i].second);
                }
                reading_posts += tmp->rmem_bufs.size();
            }
        }

        int fetch_num = rmems_[thread_id]->rmem_poll(batch_buffer, max_batch);

        reading_posts -= fetch_num;
        for(int i=0;i<fetch_num;i++){
        rmem::rt_assert(batch_buffer[i]==0, "poll post error!");
        }

        while(fetch_num){
            tmp = queues.front();
            size_t now_finish_num = tmp->finished_num;
            int parse_num = 0;
            for(int i=0;i<fetch_num && now_finish_num+i< tmp->rmem_bufs.size();i++){
                parse_num ++;
                tmp->finished_num++;
            }

            if(tmp->finished_num == tmp->rmem_bufs.size()) {
                hdr_record_value_atomic(tmp->hist,
                                        static_cast<int64_t>(timers[thread_id][tmp->req_num % kAppMaxBuffer].toc() * 10));

                store->PushNextReq();
                for(auto read_buf : tmp->rmem_bufs){
                    free(read_buf);
                }
                delete tmp;
                queues.pop();
            }
            fetch_num -= parse_num;
        }
        if(unlikely(ctrl_c_pressed == 1)) {
            break;
        }
    }
}
#endif

bool write_latency_and_reset(const std::string &filename, hdr_histogram *hdr)
{

    FILE *fp = fopen(filename.c_str(), "w");
    if (fp == nullptr)
    {
        return false;
    }
    hdr_percentiles_print(hdr, fp, 5, 10, CLASSIC);
    fclose(fp);
    hdr_reset(hdr);
    return true;
}

void leader_thread_func()
{
    erpc::Nexus nexus(FLAGS_server_addr, FLAGS_numa_server_node, 0);

    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_PING_RESP), ping_resp_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_COMPOSE_POST_WRITE_RESP), compose_post_write_resp_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_USER_TIMELINE_READ_RESP), user_timeline_read_resp_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_HOME_TIMELINE_READ_RESP), home_timeline_read_resp_handler);


    std::vector<std::thread> clients;
    std::vector<std::thread> servers;

    auto *context = new AppContext();

    clients.emplace_back(client_thread_func, 0, context->client_contexts_[0], &nexus);
    sleep(2);
    rmem::bind_to_core(clients[0], FLAGS_numa_client_node, get_bind_core(FLAGS_numa_client_node) + FLAGS_bind_core_offset);

    for (size_t i = 1; i < FLAGS_client_num; i++)
    {
        clients.emplace_back(client_thread_func, i, context->client_contexts_[i], &nexus);

        rmem::bind_to_core(clients[i], FLAGS_numa_client_node, get_bind_core(FLAGS_numa_client_node) + FLAGS_bind_core_offset);
    }

    for (size_t i = 0; i < FLAGS_server_num; i++)
    {
        servers.emplace_back(server_thread_func, i, context->server_contexts_[i], &nexus);

        rmem::bind_to_core(servers[i], FLAGS_numa_server_node, get_bind_core(FLAGS_numa_server_node) + FLAGS_bind_core_offset);
    }
#if defined(RMEM_PROGRAM)
    std::vector<std::thread> readers;
    for (size_t i = 0; i < FLAGS_server_num; i++)
    {
        readers.emplace_back(reader_thread_func, i, context->client_contexts_[i]->queue_store);

        rmem::bind_to_core(readers[i], FLAGS_numa_server_node, get_bind_core(FLAGS_numa_server_node) + FLAGS_bind_core_offset);
    }
#endif
    sleep(3);


    for (size_t i = 0; i < FLAGS_client_num; i++)
    {
        context->client_contexts_[i]->queue_store->PushPingReq();
    }

    for (size_t i = 0; i < FLAGS_server_num; i++)
    {
        // connect success
        RESP_MSG msg = context->server_contexts_[i]->queue_store->PopResp();
        // printf("server %zu: status %d, req_id %u\n", i, msg.status, msg.req_id);
        rmem::rt_assert(msg.status == 0 && msg.req_id == 0, "server connect failed");
    }

    RMEM_INFO("ping ready, begin to generate param");


    while(rmems_init_number != FLAGS_client_num) {
        for (size_t i = 0; i < FLAGS_client_num; i++)
        {
            context->client_contexts_[i]->queue_store->PushParamReq();
        }
        sleep(3);
    }

    RMEM_INFO("param ready, sleep 2 and begin to generate workload");
    sleep(2);
    for (size_t i = 0; i < FLAGS_client_num; i++)
    {
        size_t tmp = FLAGS_concurrency;
        while (tmp--)
        {
            context->client_contexts_[i]->queue_store->PushNextReq();
        }
    }

    if (FLAGS_timeout_second != UINT64_MAX)
    {
        sleep(FLAGS_timeout_second);
        ctrl_c_pressed = true;
    }

    for (size_t i = 0; i < FLAGS_client_num; i++)
    {
        clients[i].join();
    }
    for (size_t i = 0; i < FLAGS_server_num; i++)
    {
        servers[i].join();
    }
    write_latency_and_reset(FLAGS_latency_file+std::to_string(FLAGS_concurrency)+"-write", latency_write_hist_);
    write_latency_and_reset(FLAGS_latency_file+std::to_string(FLAGS_concurrency)+"-user", latency_user_timeline_hist_);
    write_latency_and_reset(FLAGS_latency_file+std::to_string(FLAGS_concurrency)+"-home", latency_home_timeline_hist_);

#if defined(RMEM_PROGRAM)
    for (size_t i=0 ;i< FLAGS_server_num; i++) {
        readers[i].join();
    }
#endif
}

int main(int argc, char **argv)
{
    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);
    // only config_file is required!!!
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    init_service_config(FLAGS_config_file,"client");
    init_specific_config();

    int ret = hdr_init(1, 1000 * 1000 * 10, 3,
                       &latency_write_hist_);
    rmem::rt_assert(ret == 0, "hdr_init failed");
    ret = hdr_init(1, 1000 * 1000 * 10, 3,
                   &latency_user_timeline_hist_);
    rmem::rt_assert(ret == 0, "hdr_init failed");
    ret = hdr_init(1, 1000 * 1000 * 10, 3,
                   &latency_home_timeline_hist_);
    rmem::rt_assert(ret == 0, "hdr_init failed");


    rmem::rmem_init(rmem_self_addr, FLAGS_numa_client_node);

    std::thread leader_thread(leader_thread_func);
    rmem::bind_to_core(leader_thread, 1, get_bind_core(1));
    leader_thread.join();


    hdr_close(latency_write_hist_);
    hdr_close(latency_user_timeline_hist_);
    hdr_close(latency_home_timeline_hist_);
}