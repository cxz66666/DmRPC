#pragma once
#include "nexus.h"
#include "atomic_queue/atomic_queue.h"

#include "server_context.h"
#include <mutex>
#include <memory>
namespace rmem
{
    extern std::mutex g_lock;
    extern bool g_initialized;

    using AtomicQueue = atomic_queue::AtomicQueueB<size_t, std::allocator<size_t>, static_cast<size_t>(-1)>; // Use heap-allocated buffer.
    extern AtomicQueue *g_free_pages;

    extern erpc::Nexus *g_nexus;

    extern volatile sig_atomic_t ctrl_c_pressed;

    extern ServerContext *g_server_context;

}