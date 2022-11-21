#include "server_context.h"
#include "server_extern.h"
#include "gflag_configs.h"

namespace rmem
{
    mm_struct *ServerContext::find_target_mm(uint16_t tid, uint16_t sid)
    {
        if (unlikely(tid >= FLAGS_rmem_server_thread))
        {
            RMEM_WARN("tid > FLAGS_rmem_server_thread");
            return nullptr;
        }
        if (unlikely(!g_server_context))
        {
            RMEM_WARN("g_server_context is nullptr!");
            return nullptr;
        }
        ServerContext *tmp = g_server_context + tid;

        if (unlikely(!tmp->mm_struct_map_.count(sid)))
        {
            RMEM_WARN("sid not found in mm_struct_map_");
            return nullptr;
        }
        return tmp->mm_struct_map_[sid];
    }
}