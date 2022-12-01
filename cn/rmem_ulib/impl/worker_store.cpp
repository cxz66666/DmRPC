#include "context.h"

namespace rmem
{
    WorkerStore::WorkerStore():async_received_req_min(1),async_received_req_max(0),send_number(0) {}
    WorkerStore::~WorkerStore()
    {
        if (!sended_req.empty())
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
    size_t WorkerStore::get_send_number() const
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
        RMEM_INFO("async req range from %ld to %ld", async_received_req_min,async_received_req_max);
        rt_assert(async_received_req_max <= barrier_point, "async req num is larger than barrier point");

        while(async_received_req_min<=async_received_req_max)
        {
            if(!async_received_req.contains(async_received_req_min)){
                async_received_req_min++;
            } else {
                int status=async_received_req[async_received_req_min];
                if(status==INT_MAX){
                    RMEM_INFO("req %ld don't get ready", async_received_req_min);
                } else {
                    ret++;
                }
            }
        }
        async_received_req.clear();
        return ret;
    }
}