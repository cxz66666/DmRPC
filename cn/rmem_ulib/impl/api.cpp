#include "commons.h"
#include "api.h"

#include <utility>
#include "extern.h"
#include "transport_impl/dpdk/dpdk_transport.h"
#include "req_type.h"
#include "page.h"
#include "rpc_type.h"
namespace rmem
{
    // init eRPC and DPDK, will exit when error
    void rmem_init(std::string host, size_t numa_node)
    {
        std::lock_guard<std::mutex> lock(g_lock);

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
            g_nexus = new erpc::Nexus(std::move(host), numa_node);
            g_numa_node = numa_node;
            RMEM_INFO("init success!");
        }
    }

    Rmem::Rmem(uint8_t phy_port) : Context(phy_port)
    {
        RMEM_INFO("create rmem ctx success!");
    }

    // try to create a server on a session, store session num to ctx
    int Rmem::connect_session(const std::string &host, uint8_t remote_rpc_id, int timeout_ms)
    {
        rt_assert(concurrent_store_->get_session_num() == -1, "can only have one session on a context, don't use connect_session twice before first one disconnect");
        rt_assert(timeout_ms > 0, "timeout must > 0");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_CONNECT;
        elem.ctx = this;
        elem.connect.host = host.c_str();
        elem.connect.remote_rpc_id = remote_rpc_id;

        while (unlikely(!RingBuf_put(ringbuf_, elem)))
            ;
        int res = condition_resp_->waiting_resp(timeout_ms);

        // TODO add extra check at here;

        if (unlikely(res != 0))
        {
            RMEM_WARN("connect session failed, res is %d", res);
        }

        return res;
    }

    int Rmem::disconnect_session(int timeout_ms)
    {
        rt_assert(concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");
        rt_assert(timeout_ms > 0, "timeout must > 0");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_DISCONNECT;
        elem.ctx = this;

        while (unlikely(!RingBuf_put(ringbuf_, elem)))
            ;
        int res = condition_resp_->waiting_resp(timeout_ms);

        // TODO add extra check at here;
        if (unlikely(res != 0))
        {
            RMEM_WARN("disconnect session failed, res is %d", res);
        }

        return res;
    }

    unsigned long Rmem::rmem_alloc(size_t size, unsigned long vm_flags)
    {
        rt_assert(concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");
        if (!IS_PAGE_ALIGN(size))
        {
            RMEM_ERROR("size is not page aligned, please use aligined alloc size");
            return EINVAL;
        }
        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_ALLOC;
        elem.ctx = this;
        elem.alloc.alloc_size = size;
        elem.alloc.vm_flags = vm_flags;

        while (unlikely(!RingBuf_put(ringbuf_, elem)))
            ;
        auto res = condition_resp_->waiting_resp_extra(DefaultTimeoutMS);

        // TODO add extra check at here for res.first;
        return res.second;
    }

    int Rmem::rmem_free(unsigned long addr, size_t size)
    {
        rt_assert(concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_FREE;
        elem.ctx = this;
        elem.alloc.alloc_size = size;
        elem.alloc.alloc_addr = addr;

        while (unlikely(!RingBuf_put(ringbuf_, elem)))
            ;
        int res = condition_resp_->waiting_resp(DefaultTimeoutMS);

        // TODO add extra check at here for res.first;

        if (unlikely(res != 0))
        {
            RMEM_WARN("free failed, res is %d", res);
        }

        return res;
    }

    int Rmem::rmem_read_sync(void *recv_buf, unsigned long addr, size_t size)
    {
        rt_assert(concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");
        rt_assert(recv_buf != nullptr, "recv buffer can't be empty");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_READ_SYNC;
        elem.ctx = this;
        elem.rw.rw_addr = addr;
        elem.rw.rw_size = size;
        elem.rw.rw_buffer = recv_buf;

        while (unlikely(!RingBuf_put(ringbuf_, elem)))
            ;
        int res = condition_resp_->waiting_resp(DefaultTimeoutMS);

        // TODO add extra check at here for res.first;
        if (unlikely(res != 0))
        {
            RMEM_WARN("read failed, res is %d", res);
        }

        return res;
    }

    int Rmem::rmem_read_async(void *recv_buf, unsigned long addr, size_t size)
    {
        rt_assert(concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");
        rt_assert(recv_buf != nullptr, "recv buffer can't be empty");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_READ_ASYNC;
        elem.ctx = this;
        elem.rw.rw_addr = addr;
        elem.rw.rw_size = size;
        elem.rw.rw_buffer = recv_buf;

        while (unlikely(!RingBuf_put(ringbuf_, elem)))
            ;
        return 0;
    }

    int Rmem::rmem_write_sync(void *send_buf, unsigned long addr, size_t size)
    {
        rt_assert(concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");
        rt_assert(send_buf != nullptr, "send buffer can't be empty");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_WRITE_SYNC;
        elem.ctx = this;
        elem.rw.rw_addr = addr;
        elem.rw.rw_size = size;
        elem.rw.rw_buffer = send_buf;

        while (unlikely(!RingBuf_put(ringbuf_, elem)))
            ;
        int res = condition_resp_->waiting_resp(DefaultTimeoutMS);

        // TODO add extra check at here for res.first;

        if (unlikely(res != 0))
        {
            RMEM_WARN("write failed, res is %d", res);
        }
        return res;
    }

    int Rmem::rmem_write_async(void *send_buf, unsigned long addr, size_t size)
    {
        rt_assert(concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");
        rt_assert(send_buf != nullptr, "send buffer can't be empty");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_WRITE_ASYNC;
        elem.ctx = this;
        elem.rw.rw_addr = addr;
        elem.rw.rw_size = size;
        elem.rw.rw_buffer = send_buf;

        while (unlikely(!RingBuf_put(ringbuf_, elem)))
            ;
        return 0;
    }

    // int rmem_dist_barrier(Context *ctx);

    unsigned long Rmem::rmem_fork(unsigned long addr, size_t size)
    {
        rt_assert(concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");

        if (!IS_PAGE_ALIGN(size) || !IS_PAGE_ALIGN(addr))
        {
            RMEM_ERROR("size or addr is not page aligned, please use aligined alloc size and addr");
            return EINVAL;
        }

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_FORK;

        elem.ctx = this;
        elem.alloc = {size, 0, addr};

        while (unlikely(!RingBuf_put(ringbuf_, elem)))
            ;
        auto res = condition_resp_->waiting_resp_extra(DefaultTimeoutMS);

        // TODO add extra check at here for res.first;
        rt_assert(res.first==0, "join failed, res is"+ std::to_string(res.first));

        return res.second;
    }

    unsigned long Rmem::rmem_join(unsigned long addr, uint16_t thread_id, uint16_t session_id)
    {
        rt_assert(concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");
        if (!IS_PAGE_ALIGN(addr))
        {
            RMEM_ERROR("size or addr is not page aligned, please use aligined alloc size and addr");
            return EINVAL;
        }
        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_JOIN;

        elem.ctx = this;
        elem.join = {addr, thread_id, session_id};

        while (unlikely(!RingBuf_put(ringbuf_, elem)))
            ;
        auto res = condition_resp_->waiting_resp_extra(DefaultTimeoutMS);

        rt_assert(res.first==0, "join failed, res is"+ std::to_string(res.first));
        // TODO add extra check at here for res.first;
        return res.second;
    }

    int Rmem::rmem_poll(int *results, int max_num)
    {
        rt_assert(concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");

        int res= static_cast<int>(concurrent_store_->spsc_queue->was_size());
        if (res > max_num)
        {
            res = max_num;
        }
        for(int i=0;i<res;i++){
            results[i]=concurrent_store_->spsc_queue->pop();
        }
        // TODO add extra check at here for res.first;
        return res;
    }

    void *Rmem::rmem_get_msg_buffer(size_t size)
    {
        rt_assert(rpc_ != nullptr, "rpc is nullptr");
        erpc::MsgBuffer msg_buf = rpc_->alloc_msg_buffer_or_die(size + sizeof(WriteReq));

        void *data_buf = reinterpret_cast<char *>(msg_buf.buf_) + sizeof(WriteReq);
//        printf("alloc buffer %p\n", data_buf);
        rt_assert(alloc_buffer.count(data_buf) == 0, "buffer is already allocated");

        alloc_buffer[data_buf] = msg_buf;

        return data_buf;
    }

    int Rmem::rmem_free_msg_buffer(void *buf)
    {
        rt_assert(rpc_ != nullptr, "rpc is nullptr");

        if (alloc_buffer.count(buf) == 0)
        {
            RMEM_ERROR("buffer is not allocated or already be free");
            return ENXIO;
        }
//        printf("free buffer %p\n", buf);

        erpc::MsgBuffer msg_buf = alloc_buffer[buf];
        alloc_buffer.erase(buf);

        rpc_->free_msg_buffer(msg_buf);

        return 0;
    }
    void Rmem::rmem_dist_barrier_init(size_t size)
    {
        rt_assert(concurrent_store_->get_session_num() != -1, "don't use disconnect_session twice before connect!");

        RingBufElement elem;
        elem.req_type = REQ_TYPE::RMEM_DIST_BARRIER;
        elem.ctx = this;
        elem.barrier.barrier_size = size;
        while (unlikely(!RingBuf_put(ringbuf_, elem)))
            ;
        return;
    }
    int Rmem::rmem_dist_barrier()
    {

        int res = condition_resp_->waiting_resp();

        // TODO add extra check at here for res.first;
        return res;
    }
}
