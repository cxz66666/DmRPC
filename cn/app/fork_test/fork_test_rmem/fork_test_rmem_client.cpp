#include <thread>
#include "numautil.h"
#include "fork_test_rmem.h"

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

void callback_ping(void *_context, void *_tag)
{
    _unused(_tag);
    auto *ctx = static_cast<ClientContext *>(_context);
    ctx->stat_req_ping_tot++;

    auto *resp = reinterpret_cast<PingResp *>(ctx->ping_resp_msgbuf.buf_);

    if (resp->resp.status != 0)
    {
        printf("ping resp status is %d\n", resp->resp.status);
    }
    ctx->resp_spsc_queue->push(RESP_MSG{resp->resp.req_number, resp->resp.status});
}

void handler_ping(ClientContext *ctx, REQ_MSG req_msg)
{

    new (ctx->ping_msgbuf.buf_) PingReq(RPC_TYPE::RPC_PING, req_msg.req_id, SIZE_MAX, ctx->ping_param);
    ctx->rpc_->enqueue_request(ctx->session_num_vec_[0], static_cast<uint8_t>(RPC_TYPE::RPC_PING),
                               &ctx->ping_msgbuf, &ctx->ping_resp_msgbuf,
                               callback_ping, nullptr);
}

void callback_tc(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;

    auto *ctx = static_cast<ClientContext *>(_context);
    ctx->stat_req_tc_tot++;

    hdr_record_value_atomic(latency_hist_,
                            static_cast<int64_t>(timers[ctx->client_id_][req_id % FLAGS_concurrency].toc() * 10));
    auto *resp = reinterpret_cast<TranscodeResp *>(ctx->resp_msgbuf[req_id % kAppMaxConcurrency].buf_);

    if (resp->resp.status != 0)
    {
        ctx->resp_spsc_queue->push(RESP_MSG{resp->resp.req_number, resp->resp.status});
    }
    if (req_id + FLAGS_concurrency < FLAGS_test_loop)
    {
        ctx->spsc_queue->push(REQ_MSG{static_cast<uint32_t>(req_id + FLAGS_concurrency), RPC_TYPE::RPC_TRANSCODE});
    }
}
void handler_tc(ClientContext *ctx, REQ_MSG req_msg)
{
    erpc::MsgBuffer &req_msgbuf = ctx->req_msgbuf[req_msg.req_id % kAppMaxConcurrency];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_msgbuf[req_msg.req_id % kAppMaxConcurrency];
    uint64_t new_addr = ctx->rmem_->rmem_fork(ctx->raddr_begin + (req_msg.req_id * PAGE_SIZE) % alloc_size, PAGE_SIZE);

    // TODO don't know length, a hack method
    new (req_msgbuf.buf_) TranscodeReq(RPC_TYPE::RPC_TRANSCODE, req_msg.req_id, PAGE_SIZE, new_addr);

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
        ctx->req_msgbuf[i] = rpc.alloc_msg_buffer_or_die(sizeof(TranscodeReq));
        ctx->resp_msgbuf[i] = rpc.alloc_msg_buffer_or_die(sizeof(TranscodeResp));
    }
    ctx->ping_msgbuf = rpc.alloc_msg_buffer_or_die(sizeof(PingReq));
    ctx->ping_resp_msgbuf = rpc.alloc_msg_buffer_or_die(sizeof(PingResp));

    connect_sessions(ctx);

    using FUNC_HANDLER = std::function<void(ClientContext *, REQ_MSG)>;
    FUNC_HANDLER handlers[] = {handler_ping, nullptr, handler_tc, nullptr};

    printf("begin to worke \n");

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    size_t end = start.tv_sec * 1e9 + start.tv_nsec + 1e9;

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
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (unlikely(now.tv_sec * 1e9 + now.tv_nsec > end))
        {

            printf("thread %zu: ping_req : %.2ld, tc : %.2ld \n", thread_id,
                   ctx->stat_req_ping_tot, ctx->stat_req_tc_tot);

            clock_gettime(CLOCK_MONOTONIC, &start);
            end = start.tv_sec * 1e9 + start.tv_nsec + 1e9;
        }
    }
}

void leader_thread_func()
{
    erpc::Nexus nexus(rmem::get_uri_for_process(FLAGS_server_index),
                      FLAGS_numa_server_node, 0);

    std::vector<std::thread> clients(FLAGS_client_num);

    auto *context = new AppContext();

    clients[0] = std::thread(client_thread_func, 0, context->client_contexts_[0], &nexus);
    sleep(2);
    rmem::bind_to_core(clients[0], FLAGS_numa_client_node, get_bind_core(FLAGS_numa_client_node) + FLAGS_bind_core_offset);

    for (size_t i = 1; i < FLAGS_client_num; i++)
    {
        clients[i] = std::thread(client_thread_func, i, context->client_contexts_[i], &nexus);
        rmem::bind_to_core(clients[i], FLAGS_numa_client_node, get_bind_core(FLAGS_numa_client_node) + FLAGS_bind_core_offset);
    }

    sleep(3);

    for (size_t i = 0; i < FLAGS_client_num; i++)
    {
        context->client_contexts_[i]->spsc_queue->push(REQ_MSG{0, RPC_TYPE::RPC_PING});
    }
    for (size_t i = 0; i < FLAGS_client_num; i++)
    {
        // connect success
        RESP_MSG msg = context->client_contexts_[i]->resp_spsc_queue->pop();
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
    rmem::rmem_init(rmem::get_uri_for_process(FLAGS_rmem_self_index), FLAGS_numa_client_node);

    int ret = hdr_init(1, 1000 * 1000 * 10, 3,
                       &latency_hist_);
    rmem::rt_assert(ret == 0, "hdr_init failed");

    std::thread leader_thread(leader_thread_func);
    // rmem::bind_to_core(leader_thread, 1, get_bind_core(1));
    leader_thread.join();

    write_latency_and_reset(FLAGS_latency_file);
    write_bandwidth(FLAGS_bandwidth_file);
    hdr_close(latency_hist_);
}