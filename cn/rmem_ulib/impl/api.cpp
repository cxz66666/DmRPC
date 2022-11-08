#include "../commons.h"
#include "api.h"
#include "extern.h"
#include "configs.h"
#include "transport_impl/dpdk/dpdk_transport.h"
namespace rmem
{
    // init eRPC and DPDK, will exit when error
    void rmem_init(std::string host, size_t numa_node)
    {
        g_lock.lock();
        if (g_initialized)
        {
            RMEM_INFO("already init!");
            return;
        }
        else
        {
            RMEM_INFO("begin to init!");
            if (IsDPDKDaemon)
            {
                const std::string memzone_name = erpc::DpdkTransport::get_memzone_name();
                auto dpdk_memzone = rte_memzone_lookup(memzone_name.c_str());
                if (dpdk_memzone == nullptr)
                {
                    RMEM_ERROR(
                        "Memzone %s not found. This can happen if another non-daemon "
                        "eRPC process is running.\n",
                        memzone_name.c_str());
                    exit(-1);
                }
            }
            else
            {
                RMEM_INFO("use primary mode, only this process can use!");
            }
            g_initialized = true;
            g_nexus = new erpc::Nexus(host, numa_node);
            g_numa_node = numa_node;
            RMEM_INFO("init success!");
        }
    }

    // register a rpc
    Context *open_context(uint8_t phy_port)
    {
        std::unique_lock<std::mutex> lock(g_lock);

        return new Context(phy_port);
    }

    // delete this rpc
    int close_context(Context *ctx)
    {
        std::unique_lock<std::mutex> lock(g_lock);
        if (ctx->concurrent_store_->get_session_num() != -1)
        {
            RMEM_WARN("please disconnect session before close context");
            return -1;
        }
        delete ctx;
        return 0;
    }

    // try to create a server on a session, store session num to ctx
    int connect_session(Context *ctx, std::string host, uint8_t remote_rpc_id, int timeout_ms)
    {
        rt_assert(ctx != nullptr, "context must not be empty");
        rt_assert(ctx->concurrent_store_->get_session_num() == -1, "can only have one session on a context, don't use connect_session twice before first one disconnect");
        rt_assert(timeout_ms > 0, "timeout must > 0");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_CONNECT;
        elem.ctx = ctx;
        elem.connect.host = host.c_str();
        elem.connect.remote_rpc_id = remote_rpc_id;

        RingBuf_put(ctx->ringbuf_, elem);
        int res = ctx->condition_resp_->waiting_resp(timeout_ms);

        // TODO add extra check at here;
        return res;
    }

    int disconnect_session(Context *ctx, int timeout_ms)
    {
        rt_assert(ctx != nullptr, "context must not be empty");
        rt_assert(ctx->concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");
        rt_assert(timeout_ms > 0, "timeout must > 0");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_DISCONNECT;
        elem.ctx = ctx;

        RingBuf_put(ctx->ringbuf_, elem);
        int res = ctx->condition_resp_->waiting_resp(timeout_ms);

        // TODO add extra check at here;

        return res;
    }

    unsigned long rmem_alloc(Context *ctx, size_t size, unsigned long vm_flags)
    {
        rt_assert(ctx != nullptr, "context must not be empty");
        rt_assert(ctx->concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_ALLOC;
        elem.ctx = ctx;
        elem.alloc.alloc_size = size;
        elem.alloc.vm_flags = vm_flags;

        RingBuf_put(ctx->ringbuf_, elem);
        auto res = ctx->condition_resp_->waiting_resp_extra(DefaultTimeoutMS);

        // TODO add extra check at here for res.first;
        return res.second;
    }

    int rmem_free(Context *ctx, unsigned long addr, size_t size)
    {
        rt_assert(ctx != nullptr, "context must not be empty");
        rt_assert(ctx->concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_FREE;
        elem.ctx = ctx;
        elem.alloc.alloc_size = size;
        elem.alloc.alloc_addr = addr;

        RingBuf_put(ctx->ringbuf_, elem);
        int res = ctx->condition_resp_->waiting_resp(DefaultTimeoutMS);

        // TODO add extra check at here for res.first;
        return res;
    }

    int rmem_read_sync(Context *ctx, void *recv_buf, unsigned long addr, size_t size)
    {
        rt_assert(ctx != nullptr, "context must not be empty");
        rt_assert(ctx->concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");
        rt_assert(recv_buf != nullptr, "recv buffer can't be empty");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_READ_SYNC;
        elem.ctx = ctx;
        elem.rw.rw_addr = addr;
        elem.rw.rw_size = size;
        elem.rw.rw_buffer = recv_buf;

        RingBuf_put(ctx->ringbuf_, elem);
        int res = ctx->condition_resp_->waiting_resp(DefaultTimeoutMS);

        // TODO add extra check at here for res.first;
        return res;
    }

    int rmem_read_async(Context *ctx, void *recv_buf, unsigned long addr, size_t size)
    {
        rt_assert(ctx != nullptr, "context must not be empty");
        rt_assert(ctx->concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");
        rt_assert(recv_buf != nullptr, "recv buffer can't be empty");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_READ_ASYNC;
        elem.ctx = ctx;
        elem.rw.rw_addr = addr;
        elem.rw.rw_size = size;
        elem.rw.rw_buffer = recv_buf;

        RingBuf_put(ctx->ringbuf_, elem);

        // TODO it this OK?
        return 0;
    }

    int rmem_write_sync(Context *ctx, void *send_buf, unsigned long addr, size_t size)
    {
        rt_assert(ctx != nullptr, "context must not be empty");
        rt_assert(ctx->concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");
        rt_assert(send_buf != nullptr, "send buffer can't be empty");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_WRITE_SYNC;
        elem.ctx = ctx;
        elem.rw.rw_addr = addr;
        elem.rw.rw_size = size;
        elem.rw.rw_buffer = send_buf;

        RingBuf_put(ctx->ringbuf_, elem);
        int res = ctx->condition_resp_->waiting_resp(DefaultTimeoutMS);

        // TODO add extra check at here for res.first;
        return res;
    }

    int rmem_write_async(Context *ctx, void *send_buf, unsigned long addr, size_t size)
    {
        rt_assert(ctx != nullptr, "context must not be empty");
        rt_assert(ctx->concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");
        rt_assert(send_buf != nullptr, "send buffer can't be empty");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_WRITE_ASYNC;
        elem.ctx = ctx;
        elem.rw.rw_addr = addr;
        elem.rw.rw_size = size;
        elem.rw.rw_buffer = send_buf;

        RingBuf_put(ctx->ringbuf_, elem);

        // TODO it this OK?
        return 0;
    }

    // int rmem_dist_barrier(Context *ctx);

    int rmem_fork(Context *ctx, unsigned long addr, size_t size, unsigned long vm_flags)
    {
        rt_assert(ctx != nullptr, "context must not be empty");
        rt_assert(ctx->concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_FORK;

        elem.ctx = ctx;
        elem.alloc = {size, vm_flags, addr};

        RingBuf_put(ctx->ringbuf_, elem);

        int res = ctx->condition_resp_->waiting_resp(DefaultTimeoutMS);

        // TODO check
        return res;
    }

    int rmem_poll(Context *ctx, int *results, int max_num)
    {
        rt_assert(ctx != nullptr, "context must not be empty");
        rt_assert(ctx->concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_POOL;
        elem.ctx = ctx;
        elem.poll.poll_results = results;
        elem.poll.poll_max_num = max_num;

        RingBuf_put(ctx->ringbuf_, elem);

        int res = ctx->condition_resp_->waiting_resp(DefaultTimeoutMS);

        // TODO add extra check at here for res.first;
        return res;
    }
}
