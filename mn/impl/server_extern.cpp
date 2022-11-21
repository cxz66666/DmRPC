#include "server_extern.h"

namespace rmem
{

    std::mutex g_lock;
    bool g_initialized;

    AtomicQueue *g_free_pages;

    erpc::Nexus *g_nexus;

    volatile sig_atomic_t ctrl_c_pressed;

    ServerContext *g_server_context;
}