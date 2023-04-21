#include <thread>
#include "numautil.h"
#include "spinlock_mutex.h"
#include "load_balance.h"



void connect_sessions(ClientContext *c)
{
    // connect to image server
    std::vector<std::string> forward_server_addr = flags_get_balance_servers_index();

    c->servers_num_ = static_cast<int>(forward_server_addr.size());
    for (const auto& m : forward_server_addr)
    {
        int session_num_forward = c->rpc_->create_session(m, c->server_sender_id_);
        rmem::rt_assert(session_num_forward >= 0, "Failed to create session");
        c->session_num_vec_.push_back(session_num_forward);
    }

    // connect to backward server
    c->backward_session_num_ = c->rpc_->create_session(client_addr, c->server_receiver_id_);
    rmem::rt_assert(c->backward_session_num_ >= 0, "Failed to create session");
    c->session_num_vec_.push_back(c->backward_session_num_);

    while (c->num_sm_resps_ != forward_server_addr.size() + 1)
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

    ctx->forward_spsc_queue->push(*req_msgbuf);

    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void ping_resp_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_ping_resp_tot++;
    auto *req_msgbuf = req_handle->get_req_msgbuf();
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<PingRPCReq>), "data size not match");

    auto *req = reinterpret_cast<RPCMsgReq<PingRPCReq> *>(req_msgbuf->buf_);

    new (req_handle->pre_resp_msgbuf_.buf_) RPCMsgResp<PingRPCResp>(req->req_common.type, req->req_common.req_number, 0, {req->req_control.timestamp});
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(RPCMsgResp<PingRPCResp>));

    ctx->backward_spsc_queue->push(*req_msgbuf);

    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void common_req_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    auto *req_msgbuf = req_handle->get_req_msgbuf();

    auto *req = reinterpret_cast<CommonReq *>(req_msgbuf->buf_);

    switch (req->type) {
        case RPC_TYPE::RPC_COMPOSE_POST:
            ctx->stat_req_compose_post_tot++;
            break;
        case RPC_TYPE::RPC_USER_TIMELINE:
            ctx->stat_req_user_timeline_tot++;
            break;
        case RPC_TYPE::RPC_HOME_TIMELINE:
            ctx->stat_req_home_timeline_tot;
        default:
        RMEM_ERROR("error req type %u\n", req->type);
    }

    // printf("receive new transcode resp, length is %zu, req number is %u\n", req->extra.length, req->req.req_number);

#if defined(ERPC_PROGRAM)
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(TranscodeReq) + req->extra.length, "data size not match");
    new (req_handle->pre_resp_msgbuf_.buf_) TranscodeResp(req->req.type, req->req.req_number, 0, req->extra.length);
#elif defined(RMEM_PROGRAM)
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(TranscodeReq), "data size not match");
    new (req_handle->pre_resp_msgbuf_.buf_) TranscodeResp(req->req.type, req->req.req_number, 0, req->extra.length, req->extra.offset, req->extra.worker_flag);

#elif defined(CXL_PROGRAM)
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(TranscodeReq), "data size not match");
    new (req_handle->pre_resp_msgbuf_.buf_) TranscodeResp(req->req.type, req->req.req_number, 0, req->extra.length, req->extra.offset, req->extra.worker_flag);
#else
    static_assert(false, "program type not defined");
#endif
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(TranscodeResp));

    if (likely(ctx->req_forward_msgbuf_ptr[req->req.req_number % kAppMaxBuffer].buf_ != nullptr))
    {
        ctx->rpc_->free_msg_buffer(ctx->req_forward_msgbuf_ptr[req->req.req_number % kAppMaxBuffer]);
    }

    ctx->forward_spsc_queue->push(*req_msgbuf);

    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void transcode_resp_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_tc_req_tot++;
    auto *req_msgbuf = req_handle->get_req_msgbuf();

    auto *req = reinterpret_cast<TranscodeReq *>(req_msgbuf->buf_);

    // printf("receive new transcode resp, length is %zu, req number is %u\n", req->extra.length, req->req.req_number);

#if defined(ERPC_PROGRAM)
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(TranscodeReq) + req->extra.length, "data size not match");
    new (req_handle->pre_resp_msgbuf_.buf_) TranscodeResp(req->req.type, req->req.req_number, 0, req->extra.length);
#elif defined(RMEM_PROGRAM)
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(TranscodeReq), "data size not match");
    new (req_handle->pre_resp_msgbuf_.buf_) TranscodeResp(req->req.type, req->req.req_number, 0, req->extra.length, req->extra.offset, req->extra.worker_flag);

#elif defined(CXL_PROGRAM)
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(TranscodeReq), "data size not match");
    new (req_handle->pre_resp_msgbuf_.buf_) TranscodeResp(req->req.type, req->req.req_number, 0, req->extra.length, req->extra.offset, req->extra.worker_flag);
#else
    static_assert(false, "program type not defined");
#endif
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(TranscodeResp));

    if (ctx->req_backward_msgbuf_ptr[req->req.req_number % kAppMaxBuffer].buf_ != nullptr)
    {
        ctx->rpc_->free_msg_buffer(ctx->req_backward_msgbuf_ptr[req->req.req_number % kAppMaxBuffer]);
    }

    ctx->backward_spsc_queue->push(*req_msgbuf);

    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void callback_ping(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    // erpc::MsgBuffer &req_msgbuf = ctx->req_forward_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_forward_msgbuf[req_id];

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

void handler_ping(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<RPCMsgReq<PingRPCReq> *>(req_msgbuf.buf_);

    // TODO load balance? it doesn't matter!

    for(int i=0;i<ctx->servers_num_;i++){

        ctx->req_forward_msgbuf[(req->req_common.req_number+i) % kAppMaxBuffer] = req_msgbuf;

        erpc::MsgBuffer &resp_msgbuf = ctx->resp_forward_msgbuf[(req->req_common.req_number+i) % kAppMaxBuffer];

        ctx->rpc_->enqueue_request(i, static_cast<uint8_t>(RPC_TYPE::RPC_PING),
                                   &ctx->req_forward_msgbuf[(req->req_common.req_number+i) % kAppMaxBuffer], &resp_msgbuf,
                                   callback_ping, reinterpret_cast<void *>((req->req_common.req_number+i) % kAppMaxBuffer));
        send_ping_req_num++;
    }
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

    ctx->rpc_->enqueue_request(ctx->backward_session_num_, static_cast<uint8_t>(RPC_TYPE::RPC_PING_RESP),
                               &ctx->req_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_ping_resp, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));
}

void callback_common_req(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    // erpc::MsgBuffer &req_msgbuf = ctx->req_forward_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_forward_msgbuf[req_id];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<CommonRPCResp>), "data size not match");

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
void handler_common_req(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<CommonReq *>(req_msgbuf.buf_);

    ctx->req_forward_msgbuf[req->req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_forward_msgbuf[req->req_number % kAppMaxBuffer];

    size_t session_num = 0;
#if defined(ERPC_PROGRAM)
    session_num = ctx->session_num_vec_[req->req.req_number % ctx->servers_num_];
#elif defined(RMEM_PROGRAM)
    session_num = ctx->session_num_vec_[req->req.req_number % ctx->servers_num_];
#endif

    ctx->rpc_->enqueue_request(session_num, static_cast<uint8_t>(req->type),
                               &ctx->req_forward_msgbuf[req->req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_common_req, reinterpret_cast<void *>(req->req_number % kAppMaxBuffer));
}

void callback_common_resp(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    // erpc::MsgBuffer &req_msgbuf = ctx->req_backward_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[req_id];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<CommonRPCResp>), "data size not match");

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

void handler_common_resp(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<CommonReq *>(req_msgbuf.buf_);

    ctx->req_backward_msgbuf[req->req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[req->req_number % kAppMaxBuffer];

    ctx->rpc_->enqueue_request(ctx->backward_session_num_, static_cast<uint8_t>(req->type),
                               &ctx->req_backward_msgbuf[req->req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_common_resp, reinterpret_cast<void *>(req->req_number % kAppMaxBuffer));
}

void client_thread_func(size_t thread_id, ClientContext *ctx, erpc::Nexus *nexus)
{
    ctx->client_id_ = thread_id;
    std::vector<size_t> port_vec = flags_get_numa_ports(0);
    uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

    uint8_t rpc_id = 0;
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
    for (size_t i = 0; i < kAppMaxBuffer; i++)
    {
        // TODO
        ctx->resp_forward_msgbuf[i] = rpc.alloc_msg_buffer_or_die(sizeof(RPCMsgResp<CommonRPCResp>));
        ctx->resp_backward_msgbuf[i] = rpc.alloc_msg_buffer_or_die(sizeof(RPCMsgResp<CommonRPCResp>));
    }

    connect_sessions(ctx);

    using FUNC_HANDLER = std::function<void(ClientContext *, erpc::MsgBuffer)>;
    FUNC_HANDLER handlers[] = {handler_ping, handler_ping_resp, handler_common_req, handler_common_resp,
                               handler_common_req, handler_common_resp,handler_common_req, handler_common_resp};

    while (true)
    {
        // only can have ping and tc in forward
        unsigned size = ctx->forward_spsc_queue->was_size();
        for (unsigned i = 0; i < size; i++)
        {
            erpc::MsgBuffer req_msg = ctx->forward_spsc_queue->pop();
            auto *req = reinterpret_cast<CommonReq *>(req_msg.buf_);
            rmem::rt_assert(req->type == RPC_TYPE::RPC_PING || req->type == RPC_TYPE::RPC_COMPOSE_POST
                            || req->type == RPC_TYPE::RPC_USER_TIMELINE || req->type == RPC_TYPE::RPC_HOME_TIMELINE, "only ping and tc in forward queue");
            handlers[static_cast<uint8_t>(req->type)](ctx, req_msg);
        }

        size = ctx->backward_spsc_queue->was_size();
        for (unsigned i = 0; i < size; i++)
        {
            erpc::MsgBuffer req_msg = ctx->backward_spsc_queue->pop();
            auto *req = reinterpret_cast<CommonReq *>(req_msg.buf_);
            if (req->type != RPC_TYPE::RPC_PING_RESP && req->type != RPC_TYPE::RPC_COMPOSE_POST_RESP &&
            req->type != RPC_TYPE::RPC_USER_TIMELINE_RESP && req->type != RPC_TYPE::RPC_HOME_TIMELINE_RESP)
                printf("req->type=%u\n", static_cast<uint32_t>(req->type));
            rmem::rt_assert(req->type == RPC_TYPE::RPC_PING_RESP || req->type == RPC_TYPE::RPC_COMPOSE_POST_RESP ||
                            req->type == RPC_TYPE::RPC_USER_TIMELINE_RESP || req->type == RPC_TYPE::RPC_HOME_TIMELINE_RESP, "only ping_resp and tc_resp in backward queue");
            handlers[static_cast<uint8_t>(req->type)](ctx, req_msg);
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

    uint8_t rpc_id = 0;
#if defined(ERPC_PROGRAM)
    rpc_id = thread_id;
#elif defined(RMEM_PROGRAM)
    rpc_id = thread_id + kAppMaxRPC;
#elif defined(CXL_PROGRAM)
    rpc_id = thread_id;
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
        printf("thread %zu: ping_req : %.2f, ping_resp : %.2f, tc : %.2f, tc_req : %.2f \n", thread_id,
               ctx->stat_req_ping_tot / seconds, ctx->stat_req_ping_resp_tot / seconds, ctx->stat_req_tc_tot / seconds, ctx->stat_req_tc_req_tot / seconds);

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
    erpc::Nexus nexus(rmem::extract_hostname_from_uri(FLAGS_server_addr),
                      rmem::extract_udp_port_from_uri(FLAGS_server_addr), 0);

    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_PING), ping_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_PING_RESP), ping_resp_handler);

    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_COMPOSE_POST), transcode_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_COMPOSE_POST_RESP), transcode_resp_handler);

    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_HOME_TIMELINE), transcode_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_HOME_TIMELINE_RESP), transcode_resp_handler);

    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_USER_TIMELINE), transcode_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_USER_TIMELINE_RESP), transcode_resp_handler);


    std::vector<std::thread> clients(FLAGS_client_num);
    std::vector<std::thread> servers(FLAGS_server_num);

    auto *context = new AppContext();

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

    init_service_config(FLAGS_config_file,"load_balance");
    init_specific_config();

    std::thread leader_thread(leader_thread_func);
    rmem::bind_to_core(leader_thread, 1, get_bind_core(1));
    leader_thread.join();
}