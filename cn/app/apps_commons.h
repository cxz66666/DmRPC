#pragma once

#include <gflags/gflags.h>
#include <app_helpers.h>
#include <api.h>
#include <hdr/hdr_histogram.h>

DEFINE_uint64(test_loop, 10, "Test loop");
DEFINE_uint64(block_size, 0, "Block size for each request");
DEFINE_uint64(concurrency, 0, "Concurrency for each request, 1 means sync methods, >1 means async methods");

DEFINE_string(numa_0_ports, "", "Fabric ports on NUMA node 0, CSV, no spaces");
DEFINE_string(numa_1_ports, "", "Fabric ports on NUMA node 1, CSV, no spaces");
DEFINE_uint64(numa_node, 0, "NUMA node for this process");
DEFINE_uint64(numa_node_user_thread, 1, "NUMA node for tested user thread");

DEFINE_uint64(client_index, 0, "Client index line for app_process_file, 0 means line 1 represent status");
DEFINE_uint64(server_index, 1, "Server index line for app_process_file, 1 means line 2 represent status");

DEFINE_uint64(client_thread_num, 1, "client thread num, must >0 and <DPDK_QUEUE_NUM");
DEFINE_uint64(server_thread_num, 4, "server thread num, must >0 and <DPDK_QUEUE_NUM");
static constexpr size_t kAppMaxConcurrency = 256; // Outstanding reqs per thread

// Globals
volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

void check_common_gflags()
{
    if (FLAGS_test_loop == 0)
    {
        throw std::runtime_error("test_loop must be set");
    }
    if (FLAGS_block_size == 0)
    {
        throw std::runtime_error("block_size must be set");
    }
    if (FLAGS_concurrency == 0)
    {
        throw std::runtime_error("concurrency must be set");
    }
    if (FLAGS_numa_0_ports.empty())
    {
        throw std::runtime_error("numa_0_ports must be set");
    }
    // if(FLAGS_numa_1_ports.empty()){
    //     throw std::runtime_error("numa_1_ports must be set");
    // }
    if (FLAGS_numa_node != 0 && FLAGS_numa_node != 1)
    {
        throw std::runtime_error("numa_node must be 0 or 1");
    }
    if (FLAGS_numa_node_user_thread != 0 && FLAGS_numa_node_user_thread != 1)
    {
        throw std::runtime_error("numa_node_user_thread must be 0 or 1");
    }

    if (FLAGS_client_index == FLAGS_server_index)
    {
        throw std::runtime_error("client_index and server_index must be different");
    }
    if (FLAGS_client_thread_num == 0)
    {
        throw std::runtime_error("client_thread_num must >0");
    }
    if (FLAGS_server_thread_num == 0)
    {
        throw std::runtime_error("server_thread_num must >0");
    }
}

std::vector<size_t> flags_get_numa_ports(size_t numa_node)
{
    rmem::rt_assert(numa_node <= 1); // Only NUMA 0 and 1 supported for now
    std::vector<size_t> ret;

    std::string port_str =
        numa_node == 0 ? FLAGS_numa_0_ports : FLAGS_numa_1_ports;
    if (port_str.size() == 0)
        return ret;

    std::vector<std::string> split_vec = rmem::split(port_str, ',');
    rmem::rt_assert(split_vec.size() > 0);

    for (auto &s : split_vec)
        ret.push_back(std::stoull(s)); // stoull trims ' '

    return ret;
}

class AppContext
{
public:
    // range from 1 us to 10s, Three significant digits
    AppContext()
    {
        int ret = hdr_init(1, 1000 * 1000 * 10, 3,
                           &latency_hist_);
        rmem::rt_assert(ret == 0, "hdr_init failed");
    }
    ~AppContext()
    {
        hdr_close(latency_hist_);
    }
    bool write_latency_and_reset(std::string filename)
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
    rmem::Rmem *ctx;
    size_t thread_id_;
    size_t stat_rx_bytes_tot = 0; // Total bytes received
    size_t stat_tx_bytes_tot = 0; // Total bytes transmitted

    void *req_msgbuf[kAppMaxConcurrency];
    void *resp_msgbuf[kAppMaxConcurrency];
    hdr_histogram *latency_hist_;
};
