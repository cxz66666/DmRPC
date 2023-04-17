#pragma once

#include <gflags/gflags.h>
#include <nlohmann/json.hpp>
#include <csignal>
#include <app_helpers.h>

using json = nlohmann::json;

DEFINE_uint64(test_loop, 10, "Test loop");

DEFINE_uint64(concurrency, 0, "Concurrency for each request, 1 means sync methods, >1 means async methods");

DEFINE_string(numa_0_ports, "", "Fabric ports on NUMA node 0, CSV, no spaces");
// maybe empty
DEFINE_string(numa_1_ports, "", "Fabric ports on NUMA node 1, CSV, no spaces");

// this node server address
DEFINE_string(server_addr, "127.0.0.1:1234", "Server(self) address, format ip:port");

// this node
DEFINE_uint64(client_num, 1, "Client(forward usage) thread num, must >0 and <DPDK_QUEUE_NUM");
// this node
DEFINE_uint64(server_num, 1, "Server(self) thread num, must >0 and <DPDK_QUEUE_NUM");

DEFINE_uint64(bind_core_offset, 0, "Bind core offset, used for local test to bind different processes to different cores");

DEFINE_uint64(timeout_second, UINT64_MAX, "Timeout second for each request, default(UINT64_MAX) means no timeout");

DEFINE_string(latency_file, "latency.txt", "Latency file name");
DEFINE_string(bandwidth_file, "bandwidth.txt", "Bandwidth file name");


static constexpr size_t kAppMaxConcurrency = 128;       // Outstanding reqs per thread
static constexpr size_t kAppMaxRPC = 12;                // Outstanding rpcs per thread, used for RMEM_BASED

static constexpr size_t kAppMaxBuffer = kAppMaxConcurrency * kAppMaxRPC;

static constexpr size_t kAppEvLoopMs = 1000; // Duration of event loop

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

int load_config_file(const std::string& file_name, json* config_json){
    std::ifstream json_file;
    json_file.open(file_name);
    if (json_file.is_open()) {
        json_file >> *config_json;
        json_file.close();
        return 0;
    }
    else {
        RMEM_ERROR("Failed to open config file: %s", file_name.c_str());
        return -1;
    }
}

void init_config(const std::string& file_path, const std::string& service_name){
    json config_json;
    if (load_config_file(file_path, &config_json) != 0) {
        exit(1);
    }

    auto config = config_json["common"];
    if (config.is_null()) {
        RMEM_ERROR("Failed to find common config");
        exit(1);
    }
    FLAGS_test_loop = config["test_loop"];
    FLAGS_concurrency = config["concurrency"];
    FLAGS_numa_0_ports = config["numa_0_ports"];
    FLAGS_numa_1_ports = config["numa_1_ports"];
    FLAGS_server_addr = config["server_addr"];
    FLAGS_client_num = config["client_num"].get<uint64_t>();
    FLAGS_server_num = config["server_num"].get<uint64_t>();
    FLAGS_bind_core_offset = config["bind_core_offset"].get<uint64_t>();
    FLAGS_timeout_second = config["timeout_second"].get<uint64_t>();
    FLAGS_latency_file = config["latency_file"].get<std::string>();
    FLAGS_bandwidth_file = config["bandwidth_file"].get<std::string>();
}