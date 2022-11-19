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
                RMEM_INFO("req %ld is also contain in ws", v.first);
            }
        }
    }
    size_t WorkerStore::generate_next_num()
    {
        return send_number++;
    }
}