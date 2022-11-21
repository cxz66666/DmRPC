#include "context.h"
#include "configs.h"
#include "worker.h"
#include "numautil.h"
namespace rmem
{

    Context::Context(uint8_t phy_port) : bind_core_index(SIZE_MAX), worker_stop_(false)
    {
        // lock the global lock
        std::unique_lock<std::mutex> lock(g_lock);

        if (!g_initialized)
        {
            RMEM_ERROR("please init before create context!");
            exit(-1);
        }
        uint8_t rpc_id = get_legal_rpc_id_unlock();
        rpc_ = new erpc::Rpc<erpc::CTransport>(g_nexus, this, rpc_id, basic_sm_handler, phy_port);
        if (rpc_ == nullptr)
        {
            RMEM_ERROR("create rpc error, rpc_id %u, phy_port %u", rpc_id, phy_port);
            exit(-1);
        }
        auto *rbe = static_cast<RingBufElement *>(malloc(sizeof(RingBufElement) * ClientRingBufSize));
        if (rbe == nullptr)
        {
            RMEM_ERROR("create ring buffer elements array error, rpc_id %u, phy_port %u", rpc_id, phy_port);
            exit(-1);
        }
        ringbuf_ = static_cast<RingBuf *>(malloc(sizeof(RingBuf)));
        if (ringbuf_ == nullptr)
        {
            RMEM_ERROR("create ring buffer error, rpc_id %u, phy_port %u", rpc_id, phy_port);
            exit(-1);
        }
        RingBuf_ctor(ringbuf_, rbe, ClientRingBufSize);
        RMEM_INFO("create context success, rpc_id %u, phy_port %u", rpc_id, phy_port);

        condition_resp_ = new ConditionResp();
        if (condition_resp_ == nullptr)
        {
            RMEM_ERROR("create condition resp error, rpc_id %u, phy_port %u", rpc_id, phy_port);
            exit(-1);
        }

        concurrent_store_ = new ConcurrentStroe();
        if (concurrent_store_ == nullptr)
        {
            RMEM_ERROR("create concurrent store error, rpc_id %u, phy_port %u", rpc_id, phy_port);
            exit(-1);
        }

        start_worker_thread();

        g_active_ctx.push_back(this);
    }
    Context::~Context()
    {
        std::unique_lock<std::mutex> lock(g_lock);
        if (concurrent_store_->get_session_num() != -1)
        {
            RMEM_ERROR("please disconnect session before close context");
            exit(-1);
        }

        bool found = false;
        for (size_t index = 0; index < g_active_ctx.size(); index++)
        {
            if (g_active_ctx[index] == this)
            {
                g_active_ctx.erase(g_active_ctx.begin() + static_cast<long>(index));
                found = true;
                break;
            }
        }
        rt_assert(found, "ctx must in the global context vector");
        rt_assert(ringbuf_ != nullptr, "ring buf is null");
        rt_assert(ringbuf_->buf != nullptr, "ring buffer elements is null");
        rt_assert(condition_resp_ != nullptr, "condition resp is null");

        stop_worker_thread();

        free(ringbuf_->buf);
        free(ringbuf_);
        free(condition_resp_);
    }
    uint8_t Context::get_legal_rpc_id_unlock()
    {
        std::unordered_set<uint8_t> used_id;

        for (auto c : g_active_ctx)
        {
            used_id.insert(static_cast<Context *>(c)->rpc_->get_rpc_id());
        }
        uint8_t legal_id;
        for (legal_id = 0; legal_id < UINT8_MAX; legal_id++)
        {
            if (used_id.count(legal_id))
            {
                continue;
            }
            RMEM_INFO("generate rpc id %u", legal_id);
            return legal_id;
        }
        _unreach();
    }

    void Context::start_worker_thread()
    {
        rt_assert(!worker_thread_.joinable(), "[start ]worker thread must not be joinalbe");
        rt_assert(g_active_ctx.size() < MaxContext);
        rt_assert(bind_core_index == SIZE_MAX);
        std::unordered_set<size_t> indexs;
        for (auto m : g_active_ctx)
        {
            indexs.insert(static_cast<Context *>(m)->get_core_index_unlock());
        }
        size_t core_index;
        for (core_index = 0; core_index < MaxContext; core_index++)
        {
            if (!indexs.count(core_index))
            {
                break;
            }
        }
        worker_stop_ = false;
        worker_thread_ = std::thread(worker_func, this);

        bind_to_core(worker_thread_, g_numa_node, core_index);

        bind_core_index = core_index;

        RMEM_INFO("worker thread start success");
    }

    void Context::stop_worker_thread()
    {
        rt_assert(worker_thread_.joinable(), "[stop]worker thread must be joinalbe");

        // TODO disconnect and handler unfinished request(ensure it!)
        worker_stop_ = true;
        worker_thread_.join();
        RMEM_INFO("worker thread stop success");
    }

    size_t Context::get_core_index_unlock() const
    {
        return bind_core_index;
    }
}
