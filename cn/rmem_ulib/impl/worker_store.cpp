#include "context.h"

namespace rmem
{
    WorkerStore::WorkerStore() = default;
    WorkerStore::~WorkerStore()
    {
        if (sended_req.size() != 0)
        {
            for (auto &v : sended_req)
            {
                RMEM_WARN("req %ld is also contain in ws", v.first);
            }
        }
    }
    size_t WorkerStore::generate_next_num()
    {
        return ++send_number;
    }
    size_t WorkerStore::get_send_number()
    {
        return send_number;
    }
    void WorkerStore::set_barrier_point(size_t size)
    {
        barrier_point = send_number + size;
        RMEM_INFO("set barrier point to %ld\n", barrier_point);
    }
    int WorkerStore::get_async_req()
    {
        int ret = 0;
        RMEM_INFO("async req range from %ld to %ld", (*async_received_req.begin()).first, (*async_received_req.rbegin()).first);
        for (auto m = async_received_req.begin(); m != async_received_req.end();)
        {
            rt_assert(m->first <= barrier_point, "async req num is larger than barrier point");
            if (m->second == INT_MAX)
            {
                RMEM_INFO("req %ld don't get ready", m->first);
            }
            else
            {
                ret++;
            }
            m++;
        }
        async_received_req.clear();
        return ret;
    }
}