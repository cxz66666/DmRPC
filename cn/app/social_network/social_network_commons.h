#pragma once

#include <gflags/gflags.h>
#include <nlohmann/json.hpp>
#include <csignal>
#include <app_helpers.h>
#include "rpc.h"
#include "social_network_rpc_type.h"
#include "atomic_queue/atomic_queue.h"

using json = nlohmann::json;

DEFINE_string(config_file,"../cn/app/social_network/config/config.json","Config file path");

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

DEFINE_uint64(numa_client_node, 0, "NUMA node for client threads, must be 0 or 1");

DEFINE_uint64(numa_server_node, 0, "NUMA node for server threads, must be 0 or 1");

DEFINE_uint64(bind_core_offset, 0, "Bind core offset, used for local test to bind different processes to different cores");

DEFINE_uint64(timeout_second, UINT64_MAX, "Timeout second for each request, default(UINT64_MAX) means no timeout");

DEFINE_string(latency_file, "latency.txt", "Latency file name");
DEFINE_string(bandwidth_file, "bandwidth.txt", "Bandwidth file name");

using SPSC_QUEUE = atomic_queue::AtomicQueueB2<erpc::MsgBuffer, std::allocator<erpc::MsgBuffer>, true, false, true>;
using MPMC_QUEUE = atomic_queue::AtomicQueueB2<erpc::MsgBuffer, std::allocator<erpc::MsgBuffer>, true, true, false>;



static constexpr size_t kAppMaxConcurrency = 128;       // Outstanding reqs per thread
static constexpr size_t kAppMaxRPC = 12;                // Outstanding rpcs per thread, used for RMEM_BASED

static constexpr size_t kAppMaxBuffer = kAppMaxConcurrency * kAppMaxRPC;

static constexpr size_t kAppEvLoopMs = 1000; // Duration of event loop

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

json config_json_all;

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

void init_service_config(const std::string& file_path, const std::string& service_name){
    if (load_config_file(file_path, &config_json_all) != 0) {
        exit(1);
    }

    auto config = config_json_all["common"];
    if (config.is_null()) {
        RMEM_ERROR("Failed to find common config");
        exit(1);
    }
    FLAGS_test_loop = config["test_loop"];
    FLAGS_concurrency = config["concurrency"];
    FLAGS_numa_0_ports = config["numa_0_ports"];
    FLAGS_numa_1_ports = config["numa_1_ports"];
    FLAGS_server_addr = config["server_addr"];
    FLAGS_client_num = config["client_num"];
    FLAGS_server_num = config["server_num"];
    FLAGS_numa_client_node = config["numa_client_node"];
    FLAGS_numa_server_node = config["numa_server_node"];
    FLAGS_bind_core_offset = config["bind_core_offset"];
    FLAGS_timeout_second = config["timeout_second"];
    FLAGS_latency_file = config["latency_file"];
    FLAGS_bandwidth_file = config["bandwidth_file"];

    auto server_config = config_json_all[service_name];
    if (server_config.is_null()) {
        RMEM_ERROR("Failed to find %s config", service_name.c_str());
        exit(1);
    }

    FLAGS_server_addr = server_config["server_addr"];
    if(FLAGS_server_addr.empty()){
        RMEM_ERROR("Failed to find server_addr in %s config", service_name.c_str());
        exit(1);
    }
    // check server_config whether you have a key named "server_addr"
    if(server_config.contains("test_loop")){
        FLAGS_test_loop = server_config["test_loop"];
    }
    if(server_config.contains("concurrency")){
        FLAGS_concurrency = server_config["concurrency"];
    }
    if(server_config.contains("numa_0_ports")){
        FLAGS_numa_0_ports = server_config["numa_0_ports"];
    }
    if(server_config.contains("numa_1_ports")){
        FLAGS_numa_1_ports = server_config["numa_1_ports"];
    }
    if(server_config.contains("client_num")){
        FLAGS_client_num = server_config["client_num"];
    }
    if(server_config.contains("server_num")){
        FLAGS_server_num = server_config["server_num"];
    }
    if(server_config.contains("numa_client_node")){
        FLAGS_numa_client_node = server_config["numa_client_node"];
    }
    if(server_config.contains("numa_server_node")){
        FLAGS_numa_server_node = server_config["numa_server_node"];
    }
    if(server_config.contains("bind_core_offset")){
        FLAGS_bind_core_offset = server_config["bind_core_offset"];
    }
    if(server_config.contains("timeout_second")){
        FLAGS_timeout_second = server_config["timeout_second"];
    }
    if(server_config.contains("latency_file")){
        FLAGS_latency_file = server_config["latency_file"];
    }
    if(server_config.contains("bandwidth_file")){
        FLAGS_bandwidth_file = server_config["bandwidth_file"];
    }

}

std::vector<size_t> flags_get_numa_ports(size_t numa_node)
{
    rmem::rt_assert(numa_node <= 1); // Only NUMA 0 and 1 supported for now
    std::vector<size_t> ret;

    std::string port_str =
            numa_node == 0 ? FLAGS_numa_0_ports : FLAGS_numa_1_ports;
    if (port_str.empty())
        return ret;

    std::vector<std::string> split_vec = rmem::split(port_str, ',');
    rmem::rt_assert(!split_vec.empty());

    for (auto &s : split_vec)
        ret.push_back(std::stoull(s)); // stoull trims ' '

    return ret;
}

class BasicContext
{
public:
    erpc::Rpc<erpc::CTransport> *rpc_;
    std::vector<int> session_num_vec_;
    size_t num_sm_resps_ = 0; // Number of SM responses
};

/// A basic session management handler that expects successful responses
/// used for client side
void basic_sm_handler_client(int session_num, int remote_session_num, erpc::SmEventType sm_event_type,
                             erpc::SmErrType sm_err_type, void *_context)
{
    _unused(remote_session_num);
    printf("client sm_handler receive: session_num:%d\n", session_num);
    auto *c = static_cast<BasicContext *>(_context);
    c->num_sm_resps_++;
    for (auto m : c->session_num_vec_)
    {
        printf("session_num_vec_:%d\n", m);
    }
    rmem::rt_assert(
            sm_err_type == erpc::SmErrType::kNoError,
            "SM response with error " + erpc::sm_err_type_str(sm_err_type));

    if (!(sm_event_type == erpc::SmEventType::kConnected ||
          sm_event_type == erpc::SmEventType::kDisconnected))
    {
        throw std::runtime_error("Received unexpected SM event.");
    }

    // The callback gives us the eRPC session number - get the index in vector
    size_t session_idx = c->session_num_vec_.size();
    for (size_t i = 0; i < c->session_num_vec_.size(); i++)
    {
        if (c->session_num_vec_[i] == session_num)
            session_idx = i;
    }
    rmem::rt_assert(session_idx < c->session_num_vec_.size(),
                    "SM callback for invalid session number.");
}

/// A basic session management handler that expects successful responses
/// used for server side
void basic_sm_handler_server(int session_num, int remote_session_num, erpc::SmEventType sm_event_type,
                             erpc::SmErrType sm_err_type, void *_context)
{
    _unused(remote_session_num);

    auto *c = static_cast<BasicContext *>(_context);
    c->num_sm_resps_++;

    rmem::rt_assert(
            sm_err_type == erpc::SmErrType::kNoError,
            "SM response with error " + erpc::sm_err_type_str(sm_err_type));

    if (!(sm_event_type == erpc::SmEventType::kConnected ||
          sm_event_type == erpc::SmEventType::kDisconnected))
    {
        throw std::runtime_error("Received unexpected SM event.");
    }

    c->session_num_vec_.push_back(session_num);
    printf("Server id %" PRIu8 ": Got session %d\n", c->rpc_->get_rpc_id(), session_num);
}

size_t get_bind_core(size_t numa)
{
    static size_t numa0_core = 0;
    static size_t numa1_core = 0;
    static spinlock_mutex lock;
    size_t res;
    lock.lock();
    rmem::rt_assert(numa == 0 || numa == 1);
    if (numa == 0)
    {
        rmem::rt_assert(numa0_core <= rmem::num_lcores_per_numa_node());
        res = numa0_core++;
    }
    else
    {
        rmem::rt_assert(numa1_core <= rmem::num_lcores_per_numa_node());
        res = numa1_core++;
    }
    lock.unlock();
    return res;
}


std::string get_memory_node_addr(size_t node_index)
{
    auto value1 = config_json_all["memory_node"+std::to_string(node_index)]["addr"];
    auto value2 = config_json_all["memory_node"+std::to_string(node_index)]["port"];

    rmem::rt_assert(!value1.is_null()&&!value2.is_null(),"node addr or port is null");

    return value1.get<std::string>()+":"+value2.get<std::string>();
}