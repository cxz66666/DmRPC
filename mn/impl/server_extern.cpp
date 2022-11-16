#include "server_extern.h"


namespace rmem
{

    std::mutex g_lock;
    bool g_initialized;

    page_elem *g_pages;
    page_table *g_page_tables;
    AtomicQueue *g_free_pages;

    uint64_t g_page_table_size;
    erpc::Nexus *g_nexus;

    volatile sig_atomic_t ctrl_c_pressed;

    ServerContext* g_server_context;
}