#pragma once
#include "page.h"
#include "nexus.h"
#include "atomic_queue/atomic_queue.h"
#include "req_handler.h"

#include <mutex>
#include <memory>
namespace rmem
{
    extern std::mutex g_lock;
    extern bool g_initialized;

    extern page_elem *g_pages;
    extern page_table *g_page_tables;

    using AtomicQueue = atomic_queue::AtomicQueueB<size_t, std::allocator<size_t>, static_cast<size_t>(-1)>; // Use heap-allocated buffer.
    extern AtomicQueue *g_free_pages;

    extern uint64_t g_page_table_size;
    extern erpc::Nexus *g_nexus;

    extern volatile sig_atomic_t ctrl_c_pressed;

    extern ServerContext* g_server_context;

}