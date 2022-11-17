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
#include <execinfo.h>
#include <unistd.h>

#include "log.h"

namespace rmem
{
#define _unused(x) ((void)(x)) // Make production build happy
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define _unreach() __builtin_unreachable()

#define Ki(x) (static_cast<size_t>(x) * 1000)
#define Mi(x) (static_cast<size_t>(x) * 1000 * 1000)
#define Gi(x) (static_cast<size_t>(x) * 1000 * 1000 * 1000)

#define KB(x) (static_cast<size_t>(x) << 10)
#define MB(x) (static_cast<size_t>(x) << 20)
#define GB(x) (static_cast<size_t>(x) << 30)

#define min_(x, y) x < y ? x : y

#define max_(x, y) x > y ? x : y

#define min_t(type, x, y) ({			\
	type __min1 = (x);			\
	type __min2 = (y);			\
	__min1 < __min2 ? __min1: __min2; })

#define max_t(type, x, y) ({			\
	type __max1 = (x);			\
	type __max2 = (y);			\
	__max1 > __max2 ? __max1: __max2; })

#define SWAP(x, y)             \
    do                         \
    {                          \
        typeof(x) ____val = x; \
        x = y;                 \
        y = ____val;           \
    } while (0)

#define is_log2(v) (((v) & ((v)-1)) == 0)

    static void print_bt()
    {
        void *array[10];
        size_t size;

        // get void*'s for all entries on the stack
        size = static_cast<size_t>(backtrace(array, 10));

        // print out all the frames to stderr
        backtrace_symbols_fd(array, size, STDERR_FILENO);
    }
    /// Check a condition at runtime. If the condition is false, throw exception.
    static inline void rt_assert(bool condition, const std::string &throw_str, char *s)
    {
        if (unlikely(!condition))
        {
            print_bt();
            throw std::runtime_error(throw_str + std::string(s));
        }
    }

    /// Check a condition at runtime. If the condition is false, throw exception.
    static inline void rt_assert(bool condition, const char *throw_str)
    {
        if (unlikely(!condition))
        {
            print_bt();
            throw std::runtime_error(std::string(throw_str));
        }
    }

    /// Check a condition at runtime. If the condition is false, throw exception.
    static inline void rt_assert(bool condition, const std::string &throw_str)
    {
        if (unlikely(!condition))
        {
            print_bt();
            throw std::runtime_error(throw_str);
        }
    }

    /// Check a condition at runtime. If the condition is false, throw exception.
    /// This is faster than rt_assert(cond, str) as it avoids string construction.
    static inline void rt_assert(bool condition)
    {
        if (unlikely(!condition))
        {
            print_bt();
            throw std::runtime_error("Error");
        }
    }

}
