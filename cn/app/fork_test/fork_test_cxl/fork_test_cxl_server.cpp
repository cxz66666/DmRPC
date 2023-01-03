#include <thread>
#include "numautil.h"
#include "spinlock_mutex.h"
#include "fork_test_cxl.h"
#include <sys/mman.h>

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

void ping_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_ping_tot++;
    auto *req_msgbuf = req_handle->get_req_msgbuf();
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(PingReq), "data size not match");

    auto *req = reinterpret_cast<PingReq *>(req_msgbuf->buf_);

    ctx->read_buf = malloc(KB(4));
    ctx->write_buf = malloc(KB(4));

    new (req_handle->pre_resp_msgbuf_.buf_) PingResp(req->req.type, req->req.req_number, 0, req->timestamp);
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(PingResp));

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void transcode_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_tc_tot++;
    auto *req_msgbuf = req_handle->get_req_msgbuf();

    auto *req = reinterpret_cast<CxlReq *>(req_msgbuf->buf_);
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(CxlReq), "data size not match");

    // printf("receive new transcode resp, length is %zu, req number is %u\n", req->extra.length, req->req.req_number);
    FILE *file = fopen(req->extra.filename, "r+");
    void *addr;
    if (!FLAGS_no_cow)
    {
        addr = mmap(NULL, KB(40), PROT_READ | PROT_WRITE, MAP_PRIVATE, fileno(file), 0);
    }
    else
    {
        addr = mmap(NULL, KB(40), PROT_READ | PROT_WRITE, MAP_SHARED, fileno(file), 0);
    }
    rmem::rt_assert(addr != MAP_FAILED, "mmap failed");
    for (size_t i = 0; i < FLAGS_read_number; i++)
    {
        memcpy(ctx->read_buf, static_cast<char *>(addr) + KB(4) * i, KB(4));
    }
    for (size_t i = 0; i < FLAGS_write_number; i++)
    {
        memcpy(static_cast<char *>(addr) + KB(4) * (i + FLAGS_read_number), ctx->write_buf, KB(4));
    }
    new (req_handle->pre_resp_msgbuf_.buf_) CxlResp(req->req.type, req->req.req_number, 0);

    munmap(addr, KB(40));
    fclose(file);
    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
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
    while (true)
    {
        ctx->reset_stat();
        erpc::ChronoTimer start;
        start.reset();
        rpc.run_event_loop(kAppEvLoopMs);
        const double seconds = start.get_sec();
        printf("thread %zu: ping_req : %.2f, tc : %.2f \n", thread_id,
               ctx->stat_req_ping_tot / seconds, ctx->stat_req_tc_tot / seconds);

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
    erpc::Nexus nexus(rmem::get_uri_for_process(FLAGS_server_index),
                      FLAGS_numa_server_node, 0);

    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_PING), ping_handler);

    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_TRANSCODE), transcode_handler);

    std::vector<std::thread> servers(FLAGS_server_num);

    auto *context = new AppContext_Server();

    servers[0] = std::thread(server_thread_func, 0, context->server_contexts_[0], &nexus);
    sleep(2);
    rmem::bind_to_core(servers[0], FLAGS_numa_server_node, get_bind_core(FLAGS_numa_server_node) + FLAGS_bind_core_offset);

    for (size_t i = 1; i < FLAGS_server_num; i++)
    {
        servers[i] = std::thread(server_thread_func, i, context->server_contexts_[i], &nexus);

        rmem::bind_to_core(servers[i], FLAGS_numa_server_node, get_bind_core(FLAGS_numa_server_node) + FLAGS_bind_core_offset);
    }
    sleep(2);

    if (FLAGS_timeout_second != UINT64_MAX)
    {
        sleep(FLAGS_timeout_second);
        ctrl_c_pressed = true;
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
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    check_common_gflags();

    std::thread leader_thread(leader_thread_func);
    leader_thread.join();
}
