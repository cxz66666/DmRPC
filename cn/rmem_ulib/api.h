
#pragma once

#include "commons.h"
#include "context.h"
namespace rmem
{
    // init eRPC and DPDK, will exit when error
    void rmem_init(std::string host, size_t numa_node = 0);

    // register a rpc
    Context *open_context(uint8_t phy_port);

    // delete this rpc
    int close_context(Context *ctx);

    // try to create a server on a session, store session num to ctx
    int connect_session(Context *ctx, const std::string& host, uint8_t remote_rpc_id, int timeout_ms);

    int disconnect_session(Context *ctx, int timeout_ms);

    unsigned long rmem_alloc(Context *ctx, size_t size, unsigned long vm_flags);

    int rmem_free(Context *ctx, unsigned long addr, size_t size);

    int rmem_read_sync(Context *ctx, void *recv_buf, unsigned long addr, size_t size);

    int rmem_read_async(Context *ctx, void *recv_buf, unsigned long addr, size_t size);

    int rmem_write_sync(Context *ctx, void *send_buf, unsigned long addr, size_t size);

    int rmem_write_async(Context *ctx, void *send_buf, unsigned long addr, size_t size);

    // int rmem_dist_barrier(Context *ctx);
    unsigned long rmem_fork(Context *ctx, unsigned long addr, size_t size);

    int rmem_join(Context *ctx, unsigned long addr, uint16_t thread_id, uint16_t session_id);

    int rmem_poll(Context *ctx, int *results, int max_num);
}
