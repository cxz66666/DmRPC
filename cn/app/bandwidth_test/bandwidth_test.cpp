#include "apps_commons.h"
#include <api.h>
#include <app_helpers.h>
#include <page.h>
#include <iostream>
// do we need it ?
DEFINE_uint64(server_thread_id, 0, "Server thread rpc id");
DEFINE_uint64(alloc_size, 0, "Alloc size for each request, unit is GB");

void client_func(size_t thread_id)
{
    AppContext c;
    c.thread_id_ = thread_id;

    c.ctx = new rmem::Rmem(FLAGS_numa_node);

    rmem::rt_assert(c.ctx != nullptr, "Failed to create rmem context");

    for (size_t i = 0; i < FLAGS_concurrency; i++)
    {
        c.req_msgbuf[i] = c.ctx->rmem_get_msg_buffer(FLAGS_block_size);
    }

    for (size_t i = 0; i < FLAGS_concurrency; i++)
    {
        c.resp_msgbuf[i] = c.ctx->rmem_get_msg_buffer(FLAGS_block_size);
    }
    char buf[10] = "123456789";

    c.ctx->rmem_get_msg_buffer(FLAGS_block_size);
    c.ctx->connect_session(rmem::get_uri_for_process(FLAGS_server_index), FLAGS_server_thread_id);
    unsigned long raddr = c.ctx->rmem_alloc(GB(FLAGS_alloc_size), rmem::VM_FLAG_READ | rmem::VM_FLAG_WRITE);

    for (size_t i = 0; i < GB(FLAGS_alloc_size); i += PAGE_SIZE)
    {
        c.ctx->rmem_write_sync(buf, i + raddr, strlen(buf));
    }

    for (size_t i = 0; i < GB(FLAGS_alloc_size); i += FLAGS_block_size)
    {
        c.ctx->rmem_read_async(c.resp_msgbuf[0], i + raddr, FLAGS_block_size);
    }
    c.ctx->rmem_dist_barrier();
    c.ctx->rmem_free(raddr, GB(FLAGS_alloc_size));
    c.ctx->disconnect_session();
}

int main(int argc, char **argv)
{
    signal(SIGINT, ctrl_c_handler);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    check_common_gflags();
    rmem::rt_assert(kAppMaxConcurrency >= FLAGS_concurrency, "kAppMaxConcurrency must be >= FLAGS_concurrency");
    rmem::rt_assert(FLAGS_alloc_size != 0, "alloc_size must be set");

    rmem::rmem_init(rmem::get_uri_for_process(FLAGS_client_index), FLAGS_numa_node);

    std::vector<std::thread> threads(1);

    threads[0] = std::thread(client_func, 0);
    rmem::bind_to_core(threads[0], FLAGS_numa_node, 0);

    for (auto &t : threads)
    {
        t.join();
    }
}