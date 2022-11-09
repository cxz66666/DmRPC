#pragma once
#include <stdint.h>

namespace rmem
{
    enum class REQ_TYPE : uint8_t
    {
        RMEM_CONNECT,
        RMEM_DISCONNECT,
        RMEM_ALLOC,
        RMEM_FREE,
        RMEM_READ_SYNC,
        RMEM_READ_ASYNC,
        RMEM_WRITE_SYNC,
        RMEM_WRITE_ASYNC,
        // RMEM_DIST_BARRIER,
        RMEM_FORK,
        RMEM_POOL,
    };
}