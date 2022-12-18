#include <thread>
#include "numautil.h"
#include "spinlock_mutex.h"
#include "img_bitmap_worker_rmem.h"

size_t get_bind_core(size_t numa)
{
    static size_t numa0_core = 0;
    static size_t numa1_core = 0;
    static spinlock_mutex lock;
    size_t res;
    lock.lock();
    rmem::rt_assert(numa == 0 || numa == 1);
    if (numa == 0)
    {
        rmem::rt_assert(numa0_core <= rmem::num_lcores_per_numa_node());
        res = numa0_core++;
    }
    else
    {
        rmem::rt_assert(numa1_core <= rmem::num_lcores_per_numa_node());
        res = numa1_core++;
    }
    lock.unlock();
    return res;
}

void connect_sessions(ClientContext *c)
{

    // connect to backward server
    std::string remote_uri = rmem::get_uri_for_process(FLAGS_server_backward_index);
    int session_num_backwward = c->rpc_->create_session(remote_uri, c->server_sender_id_ + kAppMaxRPC);
    rmem::rt_assert(session_num_backwward >= 0, "Failed to create session");
    c->session_num_vec_.push_back(session_num_backwward);

    while (c->num_sm_resps_ != 1)
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
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(PingReq), "data size not match");

    auto *req = reinterpret_cast<PingReq *>(req_msgbuf->buf_);

    new (req_handle->pre_resp_msgbuf_.buf_) PingResp(req->req.type, req->req.req_number, 0, req->timestamp);
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(PingResp));

    // What fuck
    std::thread(worker_ping_thread, *req_msgbuf).detach();

    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void transcode_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_tc_tot++;
    auto *req_msgbuf = req_handle->get_req_msgbuf();

    auto *req = reinterpret_cast<TranscodeReq *>(req_msgbuf->buf_);

    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(TranscodeReq), "data size not match");

    // printf("receive new transcode resp, length is %zu, req number is %u\n", req->extra.length, req->req.req_number);

    new (req_handle->pre_resp_msgbuf_.buf_) TranscodeResp(req->req.type, req->req.req_number, 0, req->extra.length, req->extra.offset, req->extra.worker_flag);

    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(TranscodeResp));

    rmem::rt_assert(rpc_id_to_index.count(req->extra.worker_flag >> 32), "rpc id not found");
    uint32_t rpc_id = rpc_id_to_index[req->extra.worker_flag >> 32];
    uint32_t req_id = req->extra.worker_flag % kAppMaxConcurrency;

    // printf("worker_flag %lu original %u rpcid %u, req_id %u, global_req_id %u, total connect_num %u\n", req->extra.worker_flag, static_cast<uint32_t>(req->extra.worker_flag >> 32), rpc_id, req_id, req->req.req_number, rpc_connect_number);
    if (ctx->req_backward_msgbuf_ptr[rpc_id][req_id].buf_ != nullptr)
    {
        ctx->rpc_->free_msg_buffer(ctx->req_backward_msgbuf_ptr[rpc_id][req_id]);
    }

    forward_spsc_queue[rpc_id]->push(*req_msgbuf);
    //    printf("rpcid %u, base_addr %lu, offset %lu, length %lu, req number %u, worker flag %lu\n", rpc_id, rmem_base_addr[rpc_id], req->extra.offset, req->extra.length, req->req.req_number, req->extra.worker_flag);
    rmems_[rpc_id]->rmem_read_async(rmem_req_msgbuf[rpc_id][req_id], rmem_base_addr[rpc_id] + req->extra.offset, req->extra.length);

    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void callback_ping_resp(void *_context, void *_tag)
{
    auto rpc_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t rpc_id = rpc_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    // erpc::MsgBuffer &req_msgbuf = ctx->req_backward_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[rpc_id][0];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(PingResp), "data size not match");

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

    auto *req = reinterpret_cast<PingReq *>(req_msgbuf.buf_);

    ctx->req_backward_msgbuf[req->req.req_number][0] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[req->req.req_number][0];

    ctx->rpc_->enqueue_request(ctx->session_num_vec_[0], static_cast<uint8_t>(RPC_TYPE::RPC_PING_RESP),
                               &ctx->req_backward_msgbuf[req->req.req_number][0], &resp_msgbuf,
                               callback_ping_resp, reinterpret_cast<void *>(req->req.req_number));
}

void callback_tc_resp(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t rpc_id = req_id_ptr >> 32;
    uint32_t req_id = req_id_ptr % kAppMaxConcurrency;
    auto *ctx = static_cast<ClientContext *>(_context);

    // erpc::MsgBuffer &req_msgbuf = ctx->req_backward_msgbuf[rpc_id][req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[rpc_id][req_id];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(TranscodeResp), "data size not match");

    // PingResp *resp = reinterpret_cast<PingResp *>(resp_msgbuf.buf_);

    // 如果返回值不为0，则认为后续不会有响应，直接将请求号和错误码放入队列
    // 如果返回值为0，则认为后续将有响应，不care
    // if (resp->resp.status != 0)
    // {
    // TODO
    // }

    // ctx->rpc_->free_msg_buffer(req_msgbuf);
}

void handler_tc_resp(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<TranscodeReq *>(req_msgbuf.buf_);
    uint32_t rpc_id = req->extra.worker_flag >> 32;
    uint32_t req_id = req->extra.worker_flag % kAppMaxConcurrency;
    ctx->req_backward_msgbuf[rpc_id][req_id] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[rpc_id][req_id];

    ctx->rpc_->enqueue_request(ctx->session_num_vec_[0], static_cast<uint8_t>(RPC_TYPE::RPC_TRANSCODE_RESP),
                               &ctx->req_backward_msgbuf[rpc_id][req_id], &resp_msgbuf,
                               callback_tc_resp, reinterpret_cast<void *>(req->extra.worker_flag));
}

void client_thread_func(size_t thread_id, ClientContext *ctx, erpc::Nexus *nexus)
{
    ctx->client_id_ = thread_id;
    std::vector<size_t> port_vec = flags_get_numa_ports(0);
    uint8_t phy_port = port_vec.at(thread_id % port_vec.size());
    erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(ctx),
                                    static_cast<uint8_t>(thread_id + FLAGS_server_num + kAppMaxRPC),
                                    basic_sm_handler_client, phy_port);
    rpc.retry_connect_on_invalid_rpc_id_ = true;
    ctx->rpc_ = &rpc;
    for (auto &i : ctx->resp_backward_msgbuf)
    {
        for (auto &j : i)
        {
            j = rpc.alloc_msg_buffer_or_die(sizeof(TranscodeResp));
        }
    }

    connect_sessions(ctx);

    using FUNC_HANDLER = std::function<void(ClientContext *, erpc::MsgBuffer)>;
    FUNC_HANDLER handlers[] = {nullptr, handler_ping_resp, nullptr, handler_tc_resp};

    while (true)
    {
        // only can have ping and tc in forward
        unsigned size = backward_spsc_queue->was_size();
        for (unsigned i = 0; i < size; i++)
        {
            erpc::MsgBuffer req_msg = backward_spsc_queue->pop();
            auto *req = reinterpret_cast<CommonReq *>(req_msg.buf_);
            if (req->type == RPC_TYPE::RPC_PING || req->type == RPC_TYPE::RPC_TRANSCODE)
            {
                printf("WARN : receive ping or tc in backward queue, req number is %u\n", req->req_number);
                ctx->rpc_->free_msg_buffer(req_msg);
            }
            else
            {
                handlers[static_cast<uint8_t>(req->type)](ctx, req_msg);
            }
        }
        ctx->rpc_->run_event_loop_once();
        if (ctrl_c_pressed)
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
    erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(ctx),
                                    static_cast<uint8_t>(thread_id + kAppMaxRPC),
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

void worker_thread_func(size_t thread_id)
{
    int *results_buffer = new int[batch_size];
    while (true)
    {
        for (size_t i = thread_id; i < rpc_connect_number; i += FLAGS_worker_num)
        {
            if (rmems_[i] == nullptr)
            {
                continue;
            }
            size_t num = rmems_[i]->rmem_poll(results_buffer, batch_size);
            for (size_t j = 0; j < num; j++)
            {
                erpc::MsgBuffer req_msg = forward_spsc_queue[i]->pop();
                auto *req = reinterpret_cast<TranscodeReq *>(req_msg.buf_);

                uint32_t rpc_id = i;
                uint32_t req_id = req->extra.worker_flag % kAppMaxConcurrency;
                if (unlikely(!resize_bitmap(rmem_req_msgbuf[rpc_id][req_id], rmem_resp_msgbuf[rpc_id][req_id])))
                {
                    req->extra.length = req->extra.offset = SIZE_MAX;
                }
                req->req.type = RPC_TYPE::RPC_TRANSCODE_RESP;
                // TODO modify more fields
                // Don't forget to free buffer
                backward_spsc_queue->push(req_msg);
            }
        }

        if (ctrl_c_pressed == 1)
        {
            break;
        }
    }
    delete[] results_buffer;
}
void leader_thread_func()
{
    erpc::Nexus nexus(rmem::get_uri_for_process(FLAGS_server_index),
                      FLAGS_numa_server_node, 0);

    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_PING), ping_handler);

    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_TRANSCODE), transcode_handler);

    std::vector<std::thread> clients(FLAGS_client_num);
    std::vector<std::thread> servers(FLAGS_server_num);
    std::vector<std::thread> workers(FLAGS_worker_num);

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
    for (size_t i = 0; i < FLAGS_worker_num; i++)
    {
        workers[i] = std::thread(worker_thread_func, i);
        rmem::bind_to_core(workers[i], FLAGS_numa_client_node, get_bind_core(FLAGS_numa_client_node) + FLAGS_bind_core_offset);
    }

    sleep(10);

    // TODO
    for (size_t i = 0; i < FLAGS_client_num; i++)
    {
        clients[i].join();
    }
    for (size_t i = 0; i < FLAGS_server_num; i++)
    {
        servers[i].join();
    }

    for (size_t i = 0; i < FLAGS_worker_num; i++)
    {
        workers[i].join();
    }
}

int main(int argc, char **argv)
{
    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    check_common_gflags();

    rmem::rt_assert(FLAGS_client_num == 1, "client num must be 1");
    rmem::rt_assert(FLAGS_server_num == 1, "server num must be 1");

    rmem::rmem_init(rmem::get_uri_for_process(FLAGS_rmem_self_index), FLAGS_numa_client_node);

    if (FLAGS_resize_factor <= 0 || FLAGS_resize_factor > 1)
    {
        printf("resize factor must be in (0,1]\n");
        exit(1);
    }
    std::thread leader_thread(leader_thread_func);
    rmem::bind_to_core(leader_thread, 1, get_bind_core(1));
    leader_thread.join();
}
