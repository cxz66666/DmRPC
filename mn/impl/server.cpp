#include "commons.h"
#include <gflags/gflags.h>
#include "gflag_configs.h"
#include "server_extern.h"
#include "numautil.h"
#include <sys/mman.h>
#include "rpc_type.h"
#include "req_handler.h"
#include "page.h"

#include "hdr/hdr_histogram.h"
DEFINE_string(latency_file, "latency.txt", "Latency file name");
hdr_histogram *latency_hist_;

namespace rmem
{

    // must be used after
    void init_rmem()
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_initialized)
        {
            RMEM_ERROR("already init!");
            exit(-1);
        }
        // don't forget!
        FLAGS_rmem_size *= GB(1);

        g_page_table_size = FLAGS_rmem_size / PAGE_SIZE;

        // allocate pages from huge page
        void *tmp = get_huge_mem(FLAGS_rmem_numa_node, FLAGS_rmem_size);

        if (tmp == nullptr)
        {
            RMEM_ERROR("get_huge_mem failed, please check the hugepage configuration");
            exit(1);
        }
        g_pages = static_cast<page_elem *>(tmp);

        // allocate page tables from normal memory
        g_page_tables = new page_table[g_page_table_size];

        // construct free page queue
        g_free_pages = new AtomicQueue(g_page_table_size);

        for (size_t i = 0; i < g_page_table_size; i++)
        {
            g_free_pages->push(i);
        }

        if (g_page_tables == nullptr)
        {
            RMEM_ERROR("new page_table failed, please check the memory configuration");
            exit(1);
        }

        for (size_t i = 0; i < g_page_table_size; i++)
        {
            memset(g_pages + i, 0, PAGE_SIZE);
            memset(g_page_tables + i, 0, PAGE_TABLE_SIZE);
        }
        // neede to unlock
        rt_assert(mlock(g_pages, FLAGS_rmem_size) == 0, "mlock failed");
        rt_assert(mlock(g_page_tables, g_page_table_size * PAGE_TABLE_SIZE) == 0, "mlock failed");

        g_server_context = new ServerContext[FLAGS_rmem_server_thread];
        if (!g_server_context)
        {
            RMEM_ERROR("new g_server_context failed, please check the memory configuration");
            exit(1);
        }

        g_initialized = true;
    }

    void init_nexus()
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_initialized || g_nexus != nullptr)
        {
            RMEM_ERROR("must init rmem first!");
            exit(-1);
        }

        g_nexus = new erpc::Nexus(FLAGS_rmem_server_ip + ":" + std::to_string(FLAGS_rmem_server_udp_port), FLAGS_rmem_numa_node, 0);

        if (g_nexus == nullptr)
        {
            RMEM_ERROR("new Nexus failed!");
            exit(-1);
        }
        g_nexus->register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_ALLOC), alloc_req_handler);
        g_nexus->register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_FREE), free_req_handler);
        g_nexus->register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_READ), read_req_handler);
        g_nexus->register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_WRITE), write_req_handler);
        g_nexus->register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_FORK), fork_req_handler);
        g_nexus->register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_JOIN), join_req_handler);
    }

    void server_thread(size_t thread_id, erpc::Nexus *nexus)
    {
        ServerContext *c = g_server_context + thread_id;
        c->thread_id_ = thread_id;
        erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(c),
                                        static_cast<uint8_t>(thread_id),
                                        basic_sm_handler, FLAGS_rmem_numa_node);
        rpc.retry_connect_on_invalid_rpc_id_ = true;

        c->rpc_ = &rpc;
        while (true)
        {
            c->tput_t0.reset();
            rpc.run_event_loop(1000);
            //            printf("thread %zu: free_page size %d.\n", thread_id, g_free_pages->was_size());

            const double ns = c->tput_t0.get_ns();

            printf("thread %zu: total %.2f alloc: %.2f free: %.2f read: %.2f write: %.2f fork: %.2f join: %.2f err: %.2f M/s.\n", thread_id,
                   c->stat_req_rx_tot * Ki(1) / (ns), c->stat_req_alloc_tot * Ki(1) / (ns), c->stat_req_free_tot * Ki(1) / (ns),
                   c->stat_req_read_tot * Ki(1) / (ns), c->stat_req_write_tot * Ki(1) / (ns), c->stat_req_fork_tot * Ki(1) / (ns),
                   c->stat_req_join_tot * Ki(1) / (ns), c->stat_req_error_tot * Ki(1) / (ns));
            if (ctrl_c_pressed == 1)
            {
                break;
            }
            c->reset_stat();
        }
    }
}

bool write_latency_and_reset(const std::string &filename)
{

    FILE *fp = fopen(filename.c_str(), "w");
    if (fp == nullptr)
    {
        return false;
    }
    hdr_percentiles_print(latency_hist_, fp, 5, 10, CLASSIC);
    fclose(fp);
    hdr_reset(latency_hist_);
    return true;
}

int main(int argc, char **argv)
{
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    rmem::init_rmem();

    rmem::init_nexus();

    int ret = hdr_init(1, 1000 * 1000 * 10, 3,
                       &latency_hist_);
    rmem::rt_assert(ret == 0, "hdr_init failed");

    std::vector<std::thread> threads(FLAGS_rmem_server_thread);

    threads[0] = std::thread(rmem::server_thread, 0, rmem::g_nexus);

    // wait for dpdk init
    usleep(2e6);

    rmem::bind_to_core(threads[0], FLAGS_rmem_numa_node, 0);

    for (size_t i = 1; i < FLAGS_rmem_server_thread; i++)
    {
        threads[i] = std::thread(rmem::server_thread, i, rmem::g_nexus);
        rmem::bind_to_core(threads[i], FLAGS_rmem_numa_node, i);
    }
    if (FLAGS_timeout_second != UINT64_MAX)
    {
        sleep(FLAGS_timeout_second);
        rmem::ctrl_c_pressed = 1;
    }

    for (size_t i = 0; i < FLAGS_rmem_server_thread; i++)
    {
        threads[i].join();
    }

    write_latency_and_reset(FLAGS_latency_file);
    hdr_close(latency_hist_);
}