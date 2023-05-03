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

    new (req_handler->pre_resp_msgbuf_.buf_) RPCMsgResp<CommonRPCResp>(req->req_common.type, req->req_common.req_number, 0, {0});
    ctx->rpc_->resize_msg_buffer(&req_handler->pre_resp_msgbuf_, sizeof(RPCMsgResp<CommonRPCResp>));
    ctx->rpc_->enqueue_response(req_handler, &req_handler->pre_resp_msgbuf_);

    ctx->queue_store->PushNextReq();
}

void user_timeline_read_resp_handler(erpc::ReqHandle *req_handler, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_user_timeline_tot++;
    auto *req_msgbuf = req_handler->get_req_msgbuf();

    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf->buf_);
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<CommonRPCReq>) + req->req_control.data_length, "data size not match");

    new (req_handler->pre_resp_msgbuf_.buf_) RPCMsgResp<CommonRPCResp>(req->req_common.type, req->req_common.req_number, 0, {0});
    ctx->rpc_->resize_msg_buffer(&req_handler->pre_resp_msgbuf_, sizeof(RPCMsgResp<CommonRPCResp>));
    ctx->rpc_->enqueue_response(req_handler, &req_handler->pre_resp_msgbuf_);

    ctx->queue_store->PushNextReq();
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
    ctx->rpc_->enqueue_response(req_handler, &req_handler->pre_resp_msgbuf_);

    ctx->queue_store->PushNextReq();
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

        rmem_base_addr[ctx->client_id_] = rmem->rmem_join(rmem_param.fork_rmem_addr(), rmem_param.rmem_thread_id(), rmem_param.rmem_session_id());
        RMEM_INFO("client thread %ld join success, based addr %ld\n", ctx->client_id_, rmem_base_addr[ctx->client_id_]);

        rmems_[ctx->client_id_] = rmem;
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

    auto *resp = reinterpret_cast<CommonResp *>(ctx->resp_msgbuf[req_id % kAppMaxConcurrency].buf_);

    rmem::rt_assert(resp->status == 0, "resp status error");

    switch(resp->type){
        case RPC_TYPE::RPC_COMPOSE_POST_WRITE_REQ:
            ctx->compose_post_req_queue->push(ctx->req_msgbuf[req_id % kAppMaxConcurrency]);
            break;
        case RPC_TYPE::RPC_USER_TIMELINE_READ_REQ:
            ctx->user_timeline_req_queue->push(ctx->req_msgbuf[req_id % kAppMaxConcurrency]);
            break;
        case RPC_TYPE::RPC_HOME_TIMELINE_READ_REQ:
            ctx->home_timeline_req_queue->push(ctx->req_msgbuf[req_id % kAppMaxConcurrency]);
            break;
        default:
            RMEM_ERROR("inlegal resp type");
            exit(1);
    }

}

void handler_compose_post_write_req(ClientContext *ctx, REQ_MSG req_msg)
{
    erpc::MsgBuffer &req_msgbuf = ctx->req_msgbuf[req_msg.req_id % kAppMaxConcurrency];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_msgbuf[req_msg.req_id % kAppMaxConcurrency];

    req_msgbuf = ctx->compose_post_req_queue->pop();
    new (req_msgbuf.buf_) CommonReq{RPC_TYPE::RPC_COMPOSE_POST_WRITE_REQ, req_msg.req_id};

    ctx->rpc_->enqueue_request(ctx->load_balance_session_num_, static_cast<uint8_t>(RPC_TYPE::RPC_COMPOSE_POST_WRITE_REQ),
                               &req_msgbuf, &resp_msgbuf,
                               callback_common, reinterpret_cast<void *>(req_msg.req_id));
}

void handler_user_timeline_read_req(ClientContext *ctx, REQ_MSG req_msg)
{
    erpc::MsgBuffer &req_msgbuf = ctx->req_msgbuf[req_msg.req_id % kAppMaxConcurrency];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_msgbuf[req_msg.req_id % kAppMaxConcurrency];

    req_msgbuf = ctx->user_timeline_req_queue->pop();
    new (req_msgbuf.buf_) CommonReq{RPC_TYPE::RPC_USER_TIMELINE_READ_REQ, req_msg.req_id};

    ctx->rpc_->enqueue_request(ctx->load_balance_session_num_, static_cast<uint8_t>(RPC_TYPE::RPC_USER_TIMELINE_READ_REQ),
                               &req_msgbuf, &resp_msgbuf,
                               callback_common, reinterpret_cast<void *>(req_msg.req_id));
}

void handler_home_timeline_read_req(ClientContext *ctx, REQ_MSG req_msg)
{
    erpc::MsgBuffer &req_msgbuf = ctx->req_msgbuf[req_msg.req_id % kAppMaxConcurrency];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_msgbuf[req_msg.req_id % kAppMaxConcurrency];

    req_msgbuf = ctx->home_timeline_req_queue->pop();
    new (req_msgbuf.buf_) CommonReq{RPC_TYPE::RPC_HOME_TIMELINE_READ_REQ, req_msg.req_id};

    ctx->rpc_->enqueue_request(ctx->load_balance_session_num_, static_cast<uint8_t>(RPC_TYPE::RPC_HOME_TIMELINE_READ_REQ),
                               &req_msgbuf, &resp_msgbuf,
                               callback_common, reinterpret_cast<void *>(req_msg.req_id));

}


void client_thread_func(size_t thread_id, ClientContext *ctx, erpc::Nexus *nexus)
{
    ctx->client_id_ = thread_id;
    std::vector<size_t> port_vec = flags_get_numa_ports(0);
    uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

    uint8_t rpc_id;
#if defined(ERPC_PROGRAM)
    rpc_id = thread_id + FLAGS_server_num + kAppMaxRPC;
#elif defined(RMEM_PROGRAM)
    rpc_id = thread_id + FLAGS_server_num + kAppMaxRPC;
#endif
    erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(ctx),
                                    rpc_id,
                                    basic_sm_handler_client, phy_port);
    rpc.retry_connect_on_invalid_rpc_id_ = true;
    ctx->rpc_ = &rpc;

    for (size_t i=0; i< kAppMaxConcurrency; i++)
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

    uint8_t rpc_id;
#if defined(ERPC_PROGRAM)
    rpc_id = thread_id + kAppMaxRPC;
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



    while(rmems_init_number != FLAGS_client_num) {
        for (size_t i = 0; i < FLAGS_client_num; i++)
        {
            context->client_contexts_[i]->queue_store->PushParamReq();
        }
        sleep(3);
    }


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

}

int main(int argc, char **argv)
{
    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);
    // only config_file is required!!!
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    init_service_config(FLAGS_config_file,"client");
    init_specific_config();

    std::thread leader_thread(leader_thread_func);
    rmem::bind_to_core(leader_thread, 1, get_bind_core(1));
    leader_thread.join();
}