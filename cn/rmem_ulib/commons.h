#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cerrno>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include "log.h"

namespace rmem
{
#define _unused(x) ((void)(x)) // Make production build happy
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define _unreach() __builtin_unreachable()

#define KB(x) (static_cast<size_t>(x) << 10)
#define MB(x) (static_cast<size_t>(x) << 20)
#define GB(x) (static_cast<size_t>(x) << 30)

    /// Check a condition at runtime. If the condition is false, throw exception.
    static inline void rt_assert(bool condition, std::string throw_str, char *s)
    {
        if (unlikely(!condition))
        {
            throw std::runtime_error(throw_str + std::string(s));
        }
    }

    /// Check a condition at runtime. If the condition is false, throw exception.
    static inline void rt_assert(bool condition, const char *throw_str)
    {
        if (unlikely(!condition))
            throw std::runtime_error(throw_str);
    }

    /// Check a condition at runtime. If the condition is false, throw exception.
    static inline void rt_assert(bool condition, std::string throw_str)
    {
        if (unlikely(!condition))
            throw std::runtime_error(throw_str);
    }

    /// Check a condition at runtime. If the condition is false, throw exception.
    /// This is faster than rt_assert(cond, str) as it avoids string construction.
    static inline void rt_assert(bool condition)
    {
        if (unlikely(!condition))
            throw std::runtime_error("Error");
    }

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
