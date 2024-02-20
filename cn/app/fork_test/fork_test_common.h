#pragma once
#include <hdr/hdr_histogram.h>
#include "atomic_queue/atomic_queue.h"
#include <gflags/gflags.h>
#include <signal.h>
#include <hs_clock.h>
#include <spinlock_mutex.h>
#include "rpc.h"
#include <app_helpers.h>

static constexpr size_t alloc_size = 1024ul * 1024 * 1024;
static constexpr size_t kAppMaxConcurrency = 256; // Outstanding reqs per thread
static constexpr size_t kAppMaxRPC = 16;          // Outstanding rpcs per thread, used for RMEM_BASED
static constexpr size_t kAppEvLoopMs = 1000;      // Duration of event loop

enum class RPC_TYPE: uint8_t {
    RPC_PING = 0,
    RPC_TRANSCODE,
};

struct REQ_MSG {
    uint32_t req_id;
    RPC_TYPE req_type;
};

struct RESP_MSG {
    uint32_t req_id;
    int status;
};
static_assert(sizeof(REQ_MSG) == 8, "REQ_MSG size is not 8");
static_assert(sizeof(RESP_MSG) == 8, "RESP_MSG size is not 8");

class CommonReq {
public:
    RPC_TYPE type;
    // for debug
    uint32_t req_number;
} __attribute__((packed));

class CommonResp {
public:
    RPC_TYPE type;
    // for debug
    uint32_t req_number;
    int status;
} __attribute__((packed));

class ExtraReqMsg {
public:
    size_t length;
    uint64_t addr;
};

class CxlReqMsg {
public:
    char filename[16];
};

class PingParam {
public:
    char hosts[32];
    int rmem_session_id_;
    int rmem_thread_id_;
};
class PingReq {
public:
    CommonReq req;
    size_t timestamp{};
    PingParam ping_param;

    PingReq(RPC_TYPE t, uint32_t num): req{ t, num } {}
    PingReq(RPC_TYPE t, uint32_t num, size_t ts, PingParam p): req{ t, num }, timestamp(ts), ping_param(p) {}
} __attribute__((packed));

class PingResp {
public:
    CommonResp resp;
    size_t timestamp;
    PingResp(RPC_TYPE t, uint32_t num, int s): resp{ t, num, s } {}
    PingResp(RPC_TYPE t, uint32_t num, int s, size_t ts): resp{ t, num, s }, timestamp(ts) {}
} __attribute__((packed));

class RmemReq {
public:
    CommonReq req;
    ExtraReqMsg extra;
    RmemReq(RPC_TYPE t, uint32_t num): req{ t, num }, extra{ 0, 0 } {}
    RmemReq(RPC_TYPE t, uint32_t num, size_t len, uint64_t ad): req{ t, num }, extra{ len, ad } {}

} __attribute__((packed));

class RmemResp {
public:
    CommonResp resp;
    uint64_t join_time;
    RmemResp(RPC_TYPE t, uint32_t num, int s): resp{ t, num, s } {}
    RmemResp(RPC_TYPE t, uint32_t num, int s, uint64_t tt): resp{ t, num, s }, join_time{ tt } {}

} __attribute__((packed));

class CxlReq {
public:
    CommonReq req;
    CxlReqMsg extra;
    CxlReq(RPC_TYPE t, uint32_t num): req{ t, num } {}

    CxlReq(RPC_TYPE t, uint32_t num, char *p): req{ t, num } {
        memcpy(extra.filename, p, 16);
    }

} __attribute__((packed));

class CxlResp {
public:
    CommonResp resp;
    CxlResp(RPC_TYPE t, uint32_t num, int s): resp{ t, num, s } {}
} __attribute__((packed));

DEFINE_uint64(test_loop, 10, "Test loop");

DEFINE_uint64(concurrency, 0, "Concurrency for each request, 1 means sync methods, >1 means async methods");

DEFINE_string(numa_0_ports, "", "Fabric ports on NUMA node 0, CSV, no spaces");
// maybe empty
DEFINE_string(numa_1_ports, "", "Fabric ports on NUMA node 1, CSV, no spaces");
DEFINE_uint64(numa_client_node, 0, "NUMA node for client processes");
DEFINE_uint64(numa_server_node, 0, "NUMA node for server processes");

// maybe next node
DEFINE_uint64(server_forward_index, 0, "Server(forward usage) index line for app_process_file, 0 means line 1 represent status");
// maybe prev node
DEFINE_uint64(server_backward_index, 1, "Server(backward usage) index line for app_process_file, 1 means line 2 represent status");
// this node
DEFINE_uint64(server_index, 2, "Server(self) index line for app_process_file, 2 means line 3 represent status");

// this node
DEFINE_uint64(client_num, 1, "Client(forward usage) thread num, must >0 and <DPDK_QUEUE_NUM");
// maybe next node
DEFINE_uint64(server_forward_num, 1, "Server(forward usage) thread num, must >0 and <DPDK_QUEUE_NUM");
// maybe prev node
DEFINE_uint64(server_backward_num, 1, "Server(backward usage) thread num, must >0 and <DPDK_QUEUE_NUM");
// this node
DEFINE_uint64(server_num, 1, "Server(self) thread num, must >0 and <DPDK_QUEUE_NUM");

DEFINE_uint64(bind_core_offset, 0, "Bind core offset, used for local test to bind different processes to different cores");

DEFINE_uint64(timeout_second, UINT64_MAX, "Timeout second for each request, default(UINT64_MAX) means no timeout");

DEFINE_string(latency_file, "latency.txt", "Latency file name");
DEFINE_string(bandwidth_file, "bandwidth.txt", "Bandwidth file name");

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

void check_common_gflags() {
    if (FLAGS_test_loop == 0) {
        throw std::runtime_error("test_loop must be set");
    }
    if (FLAGS_numa_0_ports.empty()) {
        throw std::runtime_error("numa_0_ports must be set");
    }
    // if(FLAGS_numa_1_ports.empty()){
    //     throw std::runtime_error("numa_1_ports must be set");
    // }
    if (FLAGS_numa_client_node != 0 && FLAGS_numa_client_node != 1) {
        throw std::runtime_error("numa_client_node must be 0 or 1");
    }

    if (FLAGS_numa_server_node != 0 && FLAGS_numa_server_node != 1) {
        throw std::runtime_error("numa_server_node must be 0 or 1");
    }

    if (FLAGS_client_num == 0) {
        throw std::runtime_error("client_num must be >0 and <DPDK_QUEUE_NUM");
    }
    if (FLAGS_server_num == 0) {
        throw std::runtime_error("server_num must be >0 and <DPDK_QUEUE_NUM");
    }
}

std::vector<size_t> flags_get_numa_ports(size_t numa_node) {
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

class BasicContext {
public:
    erpc::Rpc<erpc::CTransport> *rpc_;
    std::vector<int> session_num_vec_;
    size_t num_sm_resps_ = 0; // Number of SM responses
};

/// A basic session management handler that expects successful responses
/// used for client side
void basic_sm_handler_client(int session_num, int remote_session_num, erpc::SmEventType sm_event_type,
    erpc::SmErrType sm_err_type, void *_context) {
    _unused(remote_session_num);
    printf("client sm_handler receive: session_num:%d\n", session_num);
    auto *c = static_cast<BasicContext *>(_context);
    c->num_sm_resps_++;
    for (auto m : c->session_num_vec_) {
        printf("session_num_vec_:%d\n", m);
    }
    rmem::rt_assert(
        sm_err_type == erpc::SmErrType::kNoError,
        "SM response with error " + erpc::sm_err_type_str(sm_err_type));

    if (!(sm_event_type == erpc::SmEventType::kConnected ||
        sm_event_type == erpc::SmEventType::kDisconnected)) {
        throw std::runtime_error("Received unexpected SM event.");
    }

    // The callback gives us the eRPC session number - get the index in vector
    size_t session_idx = c->session_num_vec_.size();
    for (size_t i = 0; i < c->session_num_vec_.size(); i++) {
        if (c->session_num_vec_[i] == session_num)
            session_idx = i;
    }
    rmem::rt_assert(session_idx < c->session_num_vec_.size(),
        "SM callback for invalid session number.");
}

/// A basic session management handler that expects successful responses
/// used for server side
void basic_sm_handler_server(int session_num, int remote_session_num, erpc::SmEventType sm_event_type,
    erpc::SmErrType sm_err_type, void *_context) {
    _unused(remote_session_num);

    auto *c = static_cast<BasicContext *>(_context);
    c->num_sm_resps_++;

    rmem::rt_assert(
        sm_err_type == erpc::SmErrType::kNoError,
        "SM response with error " + erpc::sm_err_type_str(sm_err_type));

    if (!(sm_event_type == erpc::SmEventType::kConnected ||
        sm_event_type == erpc::SmEventType::kDisconnected)) {
        throw std::runtime_error("Received unexpected SM event.");
    }

    c->session_num_vec_.push_back(session_num);
    printf("Server id %" PRIu8 ": Got session %d\n", c->rpc_->get_rpc_id(), session_num);
}