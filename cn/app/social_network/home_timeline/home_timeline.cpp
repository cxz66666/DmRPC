#include <thread>
#include "numautil.h"
#include "home_timeline.h"


void connect_sessions(ClientContext *c)
{

    // connect to backward server
    c->compose_post_session_number = c->rpc_->create_session(compose_post_addr, c->server_receiver_id_);
    rmem::rt_assert(c->compose_post_session_number >= 0, "Failed to create session");

    c->nginx_session_number = c->rpc_->create_session(nginx_addr, c->server_receiver_id_);
    rmem::rt_assert(c->nginx_session_number >= 0, "Failed to create session");

    c->post_storage_session_number = c->rpc_->create_session(post_storage_addr, c->server_receiver_id_);
    rmem::rt_assert(c->post_storage_session_number >= 0, "Failed to create session");

    while (c->num_sm_resps_ != 3)
    {
        c->rpc_->run_event_loop(kAppEvLoopMs);
        if (unlikely(ctrl_c_pressed == 1))
        {
            printf("Ctrl-C pressed. Exiting\n");
            return;
        }
    }
}

void ping_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_ping_tot++;
    auto *req_msgbuf = req_handle->get_req_msgbuf();
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<PingRPCReq>), "data size not match");

    auto *req = reinterpret_cast<RPCMsgReq<PingRPCReq> *>(req_msgbuf->buf_);

    new (req_handle->pre_resp_msgbuf_.buf_) RPCMsgResp<PingRPCResp>(req->req_common.type, req->req_common.req_number, 0, {req->req_control.timestamp});
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(RPCMsgResp<PingRPCResp>));

    ctx->init_mutex.lock();
    if(ctx->mongodb_init_finished){
        ctx->forward_all_mpmc_queue->push(*req_msgbuf);
        req_handle->get_hacked_req_msgbuf()->set_no_dynamic();
    } else {
        ctx->is_pinged = true;
    }
    ctx->init_mutex.unlock();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void home_timeline_write_req_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_home_timeline_write_req_tot++;

    ctx->init_mutex.lock();
    rmem::rt_assert(ctx->mongodb_init_finished,"mongodb not init finished!!");
    ctx->init_mutex.unlock();

    auto *req_msgbuf = req_handle->get_req_msgbuf();
    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf->buf_);

    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<CommonRPCReq>) + req->req_control.data_length, "data size not match");

    new (req_handle->pre_resp_msgbuf_.buf_) RPCMsgResp<CommonRPCResp>(req->req_common.type, req->req_common.req_number, 0, {0});
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(RPCMsgResp<CommonRPCResp>));

//    if (likely(ctx->req_backward_msgbuf_ptr[req->req_common.req_number % kAppMaxBuffer].buf_ != nullptr))
//    {
//        ctx->rpc_->free_msg_buffer(ctx->req_backward_msgbuf_ptr[req->req_common.req_number % kAppMaxBuffer]);
//    }

    ctx->forward_all_mpmc_queue->push(*req_msgbuf);
    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);

}

void home_timeline_read_req_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_home_timeline_read_req_tot++;

    ctx->init_mutex.lock();
    rmem::rt_assert(ctx->mongodb_init_finished,"mongodb not init finished!!");
    ctx->init_mutex.unlock();

    auto *req_msgbuf = req_handle->get_req_msgbuf();
    auto *req = reinterpret_cast<RPCMsgReq<HomeTimeLineReq> *>(req_msgbuf->buf_);

    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<HomeTimeLineReq>), "data size not match");

    new (req_handle->pre_resp_msgbuf_.buf_) RPCMsgResp<CommonRPCResp>(req->req_common.type, req->req_common.req_number, 0, {0});
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(RPCMsgResp<CommonRPCResp>));

//    if (likely(ctx->req_forward_msgbuf_ptr[req->req_common.req_number % kAppMaxBuffer].buf_ != nullptr))
//    {
//        ctx->rpc_->free_msg_buffer(ctx->req_forward_msgbuf_ptr[req->req_common.req_number % kAppMaxBuffer]);
//    }

    ctx->forward_all_mpmc_queue->push(*req_msgbuf);
    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);

}

void post_storage_read_resp_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_post_storage_read_resp_tot++;

    ctx->init_mutex.lock();
    rmem::rt_assert(ctx->mongodb_init_finished,"mongodb not init finished!!");
    ctx->init_mutex.unlock();

    auto *req_msgbuf = req_handle->get_req_msgbuf();
    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf->buf_);

    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<CommonRPCReq>) + req->req_control.data_length, "data size not match");
    new (req_handle->pre_resp_msgbuf_.buf_) RPCMsgResp<CommonRPCResp>(req->req_common.type, req->req_common.req_number, 0, {0});
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(RPCMsgResp<CommonRPCResp>));

    if (likely(ctx->req_backward_msgbuf_ptr[req->req_common.req_number % kAppMaxBuffer].buf_ != nullptr))
    {
        ctx->rpc_->free_msg_buffer(ctx->req_backward_msgbuf_ptr[req->req_common.req_number % kAppMaxBuffer]);
    }

    ctx->forward_all_mpmc_queue->push(*req_msgbuf);
    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);

}

void callback_ping_resp(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    // erpc::MsgBuffer &req_msgbuf = ctx->req_backward_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[req_id];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<PingRPCResp>), "data size not match");

    // PingResp *resp = reinterpret_cast<PingResp *>(resp_msgbuf.buf_);

    // 如果返回值不为0，则认为后续不会有响应，直接将请求号和错误码放入队列
    // 如果返回值为0，则认为后续将有响应，不care
    // if (resp->resp.status != 0)
    // {
    // TODO
    // }

    // TODO check
    // ctx->rpc_->free_msg_buffer(req_msgbuf);
}

void handler_ping_resp(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{

    auto *req = reinterpret_cast<RPCMsgReq<PingRPCReq> *>(req_msgbuf.buf_);

    ctx->req_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer];

    ctx->rpc_->enqueue_request(ctx->nginx_session_number, static_cast<uint8_t>(RPC_TYPE::RPC_PING_RESP),
                               &ctx->req_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_ping_resp, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));
}

void callback_home_timeline_write_resp(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    erpc::MsgBuffer &req_msgbuf = ctx->req_backward_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[req_id];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<CommonRPCResp>), "data size not match");

    ctx->rpc_->free_msg_buffer(req_msgbuf);
}

void handler_home_timeline_write_resp(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf.buf_);
    ctx->req_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer];

    ctx->rpc_->enqueue_request(ctx->compose_post_session_number, static_cast<uint8_t>(RPC_TYPE::RPC_HOME_TIMELINE_WRITE_RESP),
                               &ctx->req_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_home_timeline_write_resp, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));
}

void callback_home_timeline_read_resp(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    // erpc::MsgBuffer &req_msgbuf = ctx->req_backward_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[req_id];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<CommonRPCResp>), "data size not match");

}

void handler_home_timeline_read_resp(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<RPCMsgReq<HomeTimeLineReq> *>(req_msgbuf.buf_);
    ctx->req_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer];

    ctx->rpc_->enqueue_request(ctx->nginx_session_number, static_cast<uint8_t>(RPC_TYPE::RPC_HOME_TIMELINE_READ_RESP),
                               &ctx->req_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_home_timeline_read_resp, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));

}

void callback_post_storage_read_req(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    erpc::MsgBuffer &req_msgbuf = ctx->req_backward_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_forward_msgbuf[req_id];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<CommonRPCResp>), "data size not match");

    ctx->rpc_->free_msg_buffer(req_msgbuf);
}

void handler_post_storage_read_req(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf.buf_);
    ctx->req_forward_msgbuf[req->req_common.req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_forward_msgbuf[req->req_common.req_number % kAppMaxBuffer];

    ctx->rpc_->enqueue_request(ctx->post_storage_session_number, static_cast<uint8_t>(RPC_TYPE::RPC_POST_STORAGE_READ_REQ),
                               &ctx->req_forward_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_post_storage_read_req, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));

}

void client_thread_func(size_t thread_id, ClientContext *ctx, erpc::Nexus *nexus)
{
    ctx->client_id_ = thread_id;
    std::vector<size_t> port_vec = flags_get_numa_ports(0);
    uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

    uint8_t rpc_id;
#if defined(ERPC_PROGRAM)
    rpc_id = thread_id + FLAGS_server_num;
#elif defined(RMEM_PROGRAM)
    rpc_id = thread_id + FLAGS_server_num + kAppMaxRPC;
#endif
    erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(ctx),
                                    rpc_id,
                                    basic_sm_handler_client, phy_port);
    rpc.retry_connect_on_invalid_rpc_id_ = true;
    ctx->rpc_ = &rpc;
    for (auto & i : ctx->resp_backward_msgbuf)
    {
        i = rpc.alloc_msg_buffer_or_die(sizeof(RPCMsgResp<CommonRPCResp>));
    }

    for(auto & i : ctx->resp_forward_msgbuf)
    {
        i = rpc.alloc_msg_buffer_or_die(sizeof(RPCMsgResp<CommonRPCResp>));
    }

    connect_sessions(ctx);

    using FUNC_HANDLER = std::function<void(ClientContext *, erpc::MsgBuffer)>;
    std::map<RPC_TYPE ,FUNC_HANDLER > handlers{
            {RPC_TYPE::RPC_PING_RESP, handler_ping_resp},
            {RPC_TYPE::RPC_HOME_TIMELINE_WRITE_RESP, handler_home_timeline_write_resp},
            {RPC_TYPE::RPC_HOME_TIMELINE_READ_RESP, handler_home_timeline_read_resp},
            {RPC_TYPE::RPC_POST_STORAGE_READ_REQ, handler_post_storage_read_req}
    };

    while (true)
    {
        unsigned size = ctx->forward_mpmc_queue->was_size();
        for (unsigned i = 0; i < size; i++)
        {
            erpc::MsgBuffer req_msg = ctx->forward_mpmc_queue->pop();
            auto *req = reinterpret_cast<CommonReq *>(req_msg.buf_);
            rmem::rt_assert(req->type == RPC_TYPE::RPC_POST_STORAGE_READ_REQ, "only RPC_POST_STORAGE_READ_REQ in forward queue");
            handlers[req->type](ctx, req_msg);
        }

        size = ctx->backward_mpmc_queue->was_size();
        for (unsigned i = 0; i < size; i++)
        {
            erpc::MsgBuffer req_msg = ctx->backward_mpmc_queue->pop();
            auto *req = reinterpret_cast<CommonReq *>(req_msg.buf_);

            rmem::rt_assert(req->type == RPC_TYPE::RPC_PING_RESP || req->type == RPC_TYPE::RPC_HOME_TIMELINE_WRITE_RESP ||
                            req->type == RPC_TYPE::RPC_HOME_TIMELINE_READ_RESP
                    , "only ping_resp and tc_resp in backward queue");
            handlers[req->type](ctx, req_msg);
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

    uint8_t rpc_id;
#if defined(ERPC_PROGRAM)
    rpc_id = thread_id;
#elif defined(RMEM_PROGRAM)
    rpc_id = thread_id + kAppMaxRPC;
#endif
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
        printf("thread %zu: ping_req : %.2f, home_timeline write: %.2f read: %.2f, post_storage read: %.2f \n", thread_id,
               ctx->stat_req_ping_tot / seconds, ctx->stat_req_home_timeline_write_req_tot / seconds,
               ctx->stat_req_home_timeline_read_req_tot/ seconds, ctx->stat_req_post_storage_read_resp_tot / seconds);

        ctx->rpc_->reset_dpath_stats();
        // more handler
        if (ctrl_c_pressed == 1)
        {
            break;
        }
    }
}
void worker_thread_func(size_t thread_id, MPMC_QUEUE *producer, MPMC_QUEUE *consumer_back, MPMC_QUEUE *consumer_fwd, erpc::Rpc<erpc::CTransport> *rpc_, erpc::Rpc<erpc::CTransport> *server_rpc_)
{
    _unused(thread_id);
    while (true)
    {
        unsigned size = producer->was_size();
        for (unsigned i = 0; i < size; i++)
        {
            erpc::MsgBuffer req_msg = producer->pop();

            auto *req = reinterpret_cast<CommonReq *>(req_msg.buf_);
            rmem::rt_assert(req->type == RPC_TYPE::RPC_PING || req->type == RPC_TYPE::RPC_HOME_TIMELINE_WRITE_REQ ||
                            req->type == RPC_TYPE::RPC_HOME_TIMELINE_READ_REQ || req->type == RPC_TYPE::RPC_POST_STORAGE_READ_RESP,
                            "req type error");

            if(req->type == RPC_TYPE::RPC_HOME_TIMELINE_READ_REQ){
                read_home_time_line_post_details(req_msg.buf_,rpc_,consumer_fwd,consumer_back);
                server_rpc_->free_msg_buffer(req_msg);
            } else if(req->type == RPC_TYPE::RPC_HOME_TIMELINE_WRITE_REQ){
                write_home_timeline_and_return(req_msg.buf_,rpc_,consumer_back);
                server_rpc_->free_msg_buffer(req_msg);
            } else if(req->type == RPC_TYPE::RPC_POST_STORAGE_READ_RESP){
                req->type = RPC_TYPE::RPC_HOME_TIMELINE_READ_RESP;
                consumer_back->push(req_msg);
            } else {
                req->type = RPC_TYPE::RPC_PING_RESP;
                consumer_back->push(req_msg);
            }
        }
        if (ctrl_c_pressed == 1)
        {
            break;
        }
    }
}

void mongodb_init(AppContext *ctx){
    mongodb_client_pool = init_mongodb_client_pool(config_json_all, "social_graph", mongodb_conns_num);
    mongoc_client_t *mongodb_client =  mongoc_client_pool_pop(mongodb_client_pool);

    auto collection = mongoc_client_get_collection(mongodb_client, "social-graph", "social-graph");

    rmem::rt_assert(collection, "Failed to get social_graph collection from DB social_graph");

    bson_t* query = bson_new();
    mongoc_cursor_t* cursor = mongoc_collection_find_with_opts(collection, query, nullptr, nullptr);

    const bson_t *doc;

    while(mongoc_cursor_next(cursor,&doc)) {
        bson_iter_t iter;
        bson_iter_t iter_0;
        bson_iter_t iter_1;
        bson_iter_t user_id_child;
        bson_iter_t timestamp_child;
        int index = 0;
        bson_iter_init(&iter_0, doc);
        bson_iter_init(&iter_1, doc);
        std::set<int64_t> user_ids;
        int64_t user_id;
        if (bson_iter_init_find(&iter, doc, "user_id")) {
            user_id = bson_iter_value(&iter)->value.v_int64;
        } else {
            RMEM_ERROR("cant't find user_id in mongodb");
            exit(1);
        }

        while (bson_iter_find_descendant(
                &iter_0,
                ("followers." + std::to_string(index) + ".user_id").c_str(),
                &user_id_child) &&
               BSON_ITER_HOLDS_INT64(&user_id_child) &&
               bson_iter_find_descendant(
                       &iter_1,
                       ("followers." + std::to_string(index) + ".timestamp").c_str(),
                       &timestamp_child) &&
               BSON_ITER_HOLDS_INT64(&timestamp_child)) {
            auto iter_user_id = bson_iter_int64(&user_id_child);
            //maybe unused
            [[maybe_unused]] auto iter_timestamp = bson_iter_int64(&timestamp_child);

            user_ids.insert(iter_user_id);
            bson_iter_init(&iter_0, doc);
            bson_iter_init(&iter_1, doc);
            index++;
        }

        user_followers_map[user_id] = std::move(user_ids);
        if(ctrl_c_pressed){
            return;
        }
    }

    bson_destroy(query);
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(collection);
    mongoc_client_pool_push(mongodb_client_pool, mongodb_client);

    for(auto item : ctx->server_contexts_){
        item->init_mutex.lock();
        if(item->is_pinged){
            auto _buf = item->rpc_->alloc_msg_buffer(sizeof(RPCMsgReq<PingRPCReq>));
            auto *req = reinterpret_cast<RPCMsgReq<PingRPCReq> *>(_buf.buf_);
            req->req_common.type = RPC_TYPE::RPC_PING;
            req->req_common.req_number = 0;
            req->req_control.timestamp = 0;

            item->forward_all_mpmc_queue->push(_buf);
        }


        item->mongodb_init_finished = true;
        item->init_mutex.unlock();
    }

    RMEM_INFO("mongodb init finished! Total init %ld users", user_followers_map.size());
}

void leader_thread_func()
{
    erpc::Nexus nexus(FLAGS_server_addr, FLAGS_numa_server_node, 0);

    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_PING), ping_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_HOME_TIMELINE_WRITE_REQ), home_timeline_write_req_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_HOME_TIMELINE_READ_REQ), home_timeline_read_req_handler);

    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_POST_STORAGE_READ_RESP), post_storage_read_resp_handler);

    std::vector<std::thread> clients(FLAGS_client_num);
    std::vector<std::thread> servers(FLAGS_server_num);
    std::vector<std::thread> workers(FLAGS_client_num);

    auto *context = new AppContext();

    std::thread mongodb_init_thread(mongodb_init, context);
    rmem::bind_to_core(mongodb_init_thread, 1, get_bind_core(1));


    clients[0] = std::thread(client_thread_func, 0, context->client_contexts_[0], &nexus);
    sleep(2);
    rmem::bind_to_core(clients[0], FLAGS_numa_client_node, get_bind_core(FLAGS_numa_client_node) + FLAGS_bind_core_offset);

    for (size_t i = 1; i < FLAGS_client_num; i++)
    {
        clients[i] = std::thread(client_thread_func, i, context->client_contexts_[i], &nexus);

        rmem::bind_to_core(clients[i], FLAGS_numa_client_node, get_bind_core(FLAGS_numa_client_node) + FLAGS_bind_core_offset);
    }

    for (size_t i = 0; i < FLAGS_server_num; i++)
    {
        servers[i] = std::thread(server_thread_func, i, context->server_contexts_[i], &nexus);

        rmem::bind_to_core(servers[i], FLAGS_numa_server_node, get_bind_core(FLAGS_numa_server_node) + FLAGS_bind_core_offset);
    }
    sleep(3);

    for (size_t i = 0; i < FLAGS_client_num; i++)
    {
        rmem::rt_assert(context->server_contexts_[i]->rpc_ != nullptr, "server rpc is null");
        workers[i] = std::thread(worker_thread_func, i, context->client_contexts_[i]->forward_all_mpmc_queue,
                                 context->client_contexts_[i]->backward_mpmc_queue, context->client_contexts_[i]->forward_mpmc_queue,
                                 context->client_contexts_[i]->rpc_, context->server_contexts_[i]->rpc_);
//        uint64_t worker_offset = FLAGS_worker_bind_core_offset == UINT64_MAX ? FLAGS_bind_core_offset : FLAGS_worker_bind_core_offset;
//        rmem::bind_to_core(workers[i], FLAGS_numa_worker_node, get_bind_core(FLAGS_numa_worker_node) + worker_offset);
        rmem::bind_to_core(workers[i], FLAGS_numa_client_node, get_bind_core(FLAGS_numa_client_node) + FLAGS_bind_core_offset);

    }

    sleep(2);
    if (FLAGS_timeout_second != UINT64_MAX)
    {
        sleep(FLAGS_timeout_second);
        ctrl_c_pressed = true;
    }

    mongodb_init_thread.join();
    for (size_t i = 0; i < FLAGS_client_num; i++)
    {
        clients[i].join();
    }
    for (size_t i = 0; i < FLAGS_server_num; i++)
    {
        servers[i].join();
    }
    for (size_t i = 0; i< FLAGS_client_num ; i++)
    {
        workers[i].join();
    }
}

int main(int argc, char **argv)
{

    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);
    // only config_file is required!!!
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    init_service_config(FLAGS_config_file,"home_timeline");
    init_specific_config();
    load_timeline_storage(data_file_path);

    std::thread leader_thread(leader_thread_func);
    rmem::bind_to_core(leader_thread, 1, get_bind_core(1));
    leader_thread.join();

    store_timeline_storage(data_file_path);
}