#include "extern.h"
namespace rmem
{
    std::mutex g_lock;
    bool g_initialized;
    erpc::Nexus *g_nexus;
    size_t g_numa_node;
    std::vector<void *> g_active_ctx;
}