#include <thread>
#include "numautil.h"
#include "spinlock_mutex.h"
#include "client_erpc.h"

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
    std::string remote_uri = rmem::get_uri_for_process(FLAGS_server_forward_index);
    int session_num = c->rpc_->create_session(remote_uri, c->server_sender_id_);
    rmem::rt_assert(session_num >= 0, "Failed to create session");
    c->session_num_vec_.push_back(session_num);
    while (c->num_sm_resps_ != 1)
    {
        c->rpc_->run_event_loop(kAppEvLoopMs);
        if (unlikely(ctrl_c_pressed))
        {
            printf("Ctrl-C pressed. Exiting\n");
            return;
        }
    }
}

void ping_resp_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_ping_resp_tot++;
    auto *req_msgbuf = req_handle->get_req_msgbuf();
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(PingReq), "data size not match");

    auto *req = reinterpret_cast<PingReq *>(req_msgbuf->buf_);

    new (req_handle->pre_resp_msgbuf_.buf_) PingResp(req->req.type, req->req.req_number, 0, req->timestamp);
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(PingResp));
    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);

    ctx->resp_spsc_queue->push(RESP_MSG{req->req.req_number, 0});
}

void transcode_resp_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_tc_req_tot++;
    auto *req_msgbuf = req_handle->get_req_msgbuf();

    auto *req = reinterpret_cast<TranscodeReq *>(req_msgbuf->buf_);

    hdr_record_value_atomic(latency_hist_,
                            static_cast<int64_t>(timers[ctx->server_id_][req->req.req_number % FLAGS_concurrency].toc() * 10));

    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(TranscodeReq) + req->extra.length, "data size not match");

    // printf("receive new transcode resp, length is %zu, req number is %u\n", req->extra.length, req->req.req_number);

    new (req_handle->pre_resp_msgbuf_.buf_) TranscodeResp(req->req.type, req->req.req_number, 0, req->extra.length);

    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(TranscodeResp));

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);

    ctx->spsc_queue->push(REQ_MSG{static_cast<uint32_t>(req->req.req_number + FLAGS_concurrency), RPC_TYPE::RPC_TRANSCODE});
}

void callback_ping(void *_context, void *_tag)
{
    _unused(_tag);
    auto *ctx = static_cast<ClientContext *>(_context);

    auto *resp = reinterpret_cast<PingResp *>(ctx->ping_resp_msgbuf.buf_);

    // 如果返回值不为0，则认为后续不会有响应，直接将请求号和错误码放入队列
    // 如果返回值为0，则认为后续将有响应，不care
    if (resp->resp.status != 0)
    {
        printf("ping resp status is %d\n", resp->resp.status);
        ctx->resp_spsc_queue->push(RESP_MSG{resp->resp.req_number, resp->resp.status});
    }
}

void handler_ping(ClientContext *ctx, REQ_MSG req_msg)
{

    new (ctx->ping_msgbuf.buf_) PingReq(RPC_TYPE::RPC_PING, req_msg.req_id, SIZE_MAX);
    ctx->rpc_->enqueue_request(ctx->session_num_vec_[0], static_cast<uint8_t>(RPC_TYPE::RPC_PING),
                               &ctx->ping_msgbuf, &ctx->ping_resp_msgbuf,
                               callback_ping, nullptr);
}

void callback_tc(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    auto *resp = reinterpret_cast<TranscodeResp *>(ctx->resp_msgbuf[req_id % kAppMaxConcurrency].buf_);

    if (resp->resp.status != 0)
    {
        ctx->resp_spsc_queue->push(RESP_MSG{resp->resp.req_number, resp->resp.status});
    }
}
void handler_tc(ClientContext *ctx, REQ_MSG req_msg)
{
    erpc::MsgBuffer &req_msgbuf = ctx->req_msgbuf[req_msg.req_id % kAppMaxConcurrency];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_msgbuf[req_msg.req_id % kAppMaxConcurrency];
    // TODO don't know length, a hack method
    new (req_msgbuf.buf_) TranscodeReq(RPC_TYPE::RPC_TRANSCODE, req_msg.req_id, req_msgbuf.get_data_size() - sizeof(TranscodeReq));

    timers[ctx->client_id_][req_msg.req_id % FLAGS_concurrency].tic();

    ctx->rpc_->enqueue_request(ctx->session_num_vec_[0], static_cast<uint8_t>(RPC_TYPE::RPC_TRANSCODE),
                               &req_msgbuf, &resp_msgbuf,
                               callback_tc, reinterpret_cast<void *>(req_msg.req_id));
}

void client_thread_func(size_t thread_id, ClientContext *ctx, erpc::Nexus *nexus)
{
    ctx->client_id_ = thread_id;
    std::vector<size_t> port_vec = flags_get_numa_ports(0);
    uint8_t phy_port = port_vec.at(thread_id % port_vec.size());
    erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(ctx),
                                    static_cast<uint8_t>(thread_id + FLAGS_server_num),
                                    basic_sm_handler_client, phy_port);
    printf("client %p\n", reinterpret_cast<void *>(ctx));
    rpc.retry_connect_on_invalid_rpc_id_ = true;
    ctx->rpc_ = &rpc;
    for (size_t i = 0; i < kAppMaxConcurrency; i++)
    {
        // TODO
        ctx->req_msgbuf[i] = rpc.alloc_msg_buffer_or_die(sizeof(TranscodeReq) + file_size);
        memcpy(ctx->req_msgbuf[i].buf_ + sizeof(TranscodeReq), file_buf, file_size);
        ctx->resp_msgbuf[i] = rpc.alloc_msg_buffer_or_die(sizeof(TranscodeResp));
    }
    ctx->ping_msgbuf = rpc.alloc_msg_buffer_or_die(sizeof(PingReq));
    ctx->ping_resp_msgbuf = rpc.alloc_msg_buffer_or_die(sizeof(PingResp));

    connect_sessions(ctx);

    using FUNC_HANDLER = std::function<void(ClientContext *, REQ_MSG)>;
    FUNC_HANDLER handlers[] = {handler_ping, nullptr, handler_tc, nullptr};

    printf("begin to worke \n");
    while (true)
    {
        unsigned size = ctx->spsc_queue->was_size();
        for (unsigned i = 0; i < size; i++)
        {
            REQ_MSG req_msg = ctx->spsc_queue->pop();

            handlers[static_cast<uint8_t>(req_msg.req_type)](ctx, req_msg);
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
    erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(ctx),
                                    static_cast<uint8_t>(thread_id),
                                    basic_sm_handler_server, phy_port);
    rpc.retry_connect_on_invalid_rpc_id_ = true;
    ctx->rpc_ = &rpc;
    printf("server %p\n", reinterpret_cast<void *>(ctx));

    size_t loops_num = FLAGS_test_loop;
    rmem::Timer now_timer;
    bool connected = false;
    std::vector<uint64_t> resp_total;
    while (true)
    {
        ctx->reset_stat();
        erpc::ChronoTimer start;
        start.reset();
        rpc.run_event_loop(kAppEvLoopMs);
        const double seconds = start.get_sec();
        printf("thread %zu: ping_req : %.2f, ping_resp : %.2f, tc : %.2f, tc_req : %.2f Gb \n", thread_id,
               ctx->stat_req_ping_tot / seconds, ctx->stat_req_ping_resp_tot / seconds, ctx->stat_req_tc_tot / seconds, ctx->stat_req_tc_req_tot * 8 * file_size / (GB(1) * seconds));

        if (connected)
        {
            loops_num--;
            resp_total.push_back(ctx->stat_req_tc_req_tot);
            if (loops_num == 0)
            {
                break;
            }
        }
        if (!connected)
        {
            if (ctx->stat_req_tc_req_tot > 0)
            {
                connected = true;
                now_timer.tic();
            }
        }
        ctx->rpc_->reset_dpath_stats();
        // more handler
        if (unlikely(ctrl_c_pressed))
        {
            break;
        }
    }
    if (connected)
    {
        total_speed_lock.lock();
        total_speed += std::accumulate(resp_total.begin(), resp_total.end(), 0.0) * 8 * file_size * 1e6 / (GB(1) * now_timer.toc());
        total_speed_lock.unlock();
    }
}
void leader_thread_func()
{
    erpc::Nexus nexus(rmem::get_uri_for_process(FLAGS_server_index),
                      FLAGS_numa_server_node, 0);

    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_PING_RESP), ping_resp_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_TRANSCODE_RESP), transcode_resp_handler);

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

    for (size_t i = 0; i < FLAGS_client_num; i++)
    {
        context->client_contexts_[i]->spsc_queue->push(REQ_MSG{0, RPC_TYPE::RPC_PING});
    }
    for (size_t i = 0; i < FLAGS_server_num; i++)
    {
        // connect success
        RESP_MSG msg = context->server_contexts_[i]->resp_spsc_queue->pop();
        // printf("server %zu: status %d, req_id %u\n", i, msg.status, msg.req_id);
        rmem::rt_assert(msg.status == 0 && msg.req_id == 0, "server connect failed");
    }

    for (size_t i = 0; i < FLAGS_client_num; i++)
    {
        size_t tmp = FLAGS_concurrency;
        while (tmp--)
        {
            context->client_contexts_[i]->PushNextTCReq();
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

bool write_latency_and_reset(const std::string &filename)
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

bool write_bandwidth(const std::string &filename)
{
    FILE *fp = fopen(filename.c_str(), "w");
    if (fp == nullptr)
    {
        return false;
    }
    fprintf(fp, "%f\n", total_speed);
    fclose(fp);
    return true;
}

int main(int argc, char **argv)
{
    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    check_common_gflags();

    if (FLAGS_test_bitmap_file.empty())
    {
        printf("please set bitmap file\n");
        exit(0);
    }

    std::ifstream file(FLAGS_test_bitmap_file, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        printf("open file %s failed\n", FLAGS_test_bitmap_file.c_str());
        exit(0);
    }
    file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    file_buf = new uint8_t[file_size];
    file.read(reinterpret_cast<char *>(file_buf), file_size);
    file.close();

    int ret = hdr_init(1, 1000 * 1000 * 10, 3,
                       &latency_hist_);
    rmem::rt_assert(ret == 0, "hdr_init failed");

    std::thread leader_thread(leader_thread_func);
    // rmem::bind_to_core(leader_thread, 1, get_bind_core(1));
    leader_thread.join();

    delete[] file_buf;
    write_latency_and_reset(FLAGS_latency_file);
    write_bandwidth(FLAGS_bandwidth_file);
    hdr_close(latency_hist_);
}