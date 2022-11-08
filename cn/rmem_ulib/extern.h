
#pragma once
#include "commons.h"
#include "nexus.h"

namespace rmem
{
    extern std::mutex g_lock;
    extern bool g_initialized;
    extern erpc::Nexus *g_nexus;
    extern size_t g_numa_node;
    extern std::vector<void *> g_active_ctx;
}