
#pragma once
#include "configs.h"
#include "commons.h"
#include "context.h"
namespace rmem
{
    // init eRPC and DPDK, will exit when error
    void rmem_init(std::string host, size_t numa_node = 0);

    class Rmem : public Context
    {
    public:
        explicit Rmem(uint8_t phy_port);
        ~Rmem() = default;
        // try to create a server on a session, store session num to ctx
        int connect_session(const std::string &host, uint8_t remote_rpc_id, int timeout_ms = DefaultTimeoutMS);

        int disconnect_session(int timeout_ms = DefaultTimeoutMS);

        unsigned long rmem_alloc(size_t size, unsigned long vm_flags);

        int rmem_free(unsigned long addr, size_t size);

        int rmem_read_sync(void *recv_buf, unsigned long addr, size_t size);

        int rmem_read_async(void *recv_buf, unsigned long addr, size_t size);

        int rmem_write_sync(void *send_buf, unsigned long addr, size_t size);

        int rmem_write_async(void *send_buf, unsigned long addr, size_t size);

        // int rmem_dist_barrier(Context *ctx);
        unsigned long rmem_fork(unsigned long addr, size_t size);

        int rmem_join(unsigned long addr, uint16_t thread_id, uint16_t session_id);

        int rmem_poll(int *results, int max_num);

        // 实验性质api
        // 获取一块至少具有size大小的buf，并自由使用，需要使用rmem_free_msg_buffer进行释放
        void *rmem_get_msg_buffer(size_t size);

        int rmem_free_msg_buffer(void *buf);
    };
}
