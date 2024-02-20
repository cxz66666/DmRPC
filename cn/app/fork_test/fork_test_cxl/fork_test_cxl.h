#pragma once
#include "../fork_test_common.h"
#include "page.h"
#include <sys/mman.h>

std::vector<std::vector<rmem::Timer>> timers(kAppMaxRPC, std::vector<rmem::Timer>(128));
hdr_histogram *latency_hist_;

double total_speed = 0;
spinlock_mutex total_speed_lock;

std::string folder_name;

static constexpr size_t create_file_size = 10000;

DEFINE_uint64(block_size, 4096, "block size");
DEFINE_string(cxl_fake_folder, "", "Mount tmpfs on this folder, which use another numa memory to fake cxl memory");
DEFINE_bool(no_cow, false, "Don't use cow");
DEFINE_uint64(write_num, 0, "write num");
DEFINE_uint64(write_page_size, 2048, "write page size");

std::string flags_get_cxl_fake_folder() {
    rmem::rt_assert(!FLAGS_cxl_fake_folder.empty(), "please set cxl_fake_folder to assign a folder to mount tmpfs");

    return FLAGS_cxl_fake_folder;
}

void *malloc_2m_hugepage(size_t size) {
    int flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | ((21 & MAP_HUGE_MASK) << MAP_HUGE_SHIFT); // 2^21 == 2M
    int protection = PROT_READ | PROT_WRITE;
    void *p = mmap(NULL, size, protection, flags, -1, 0);
    if (p == MAP_FAILED) {
        std::runtime_error("MAP_FAILED");
    }
    return p;
}

void *malloc_2m_numa(size_t buf_size, int node_id) {
    int page_size = 2 * 1024 * 1024;
    int num_pages = buf_size / page_size;
    void *buf = malloc_2m_hugepage(buf_size);
    for (size_t i = 0; i < buf_size / sizeof(int); i++) {
        (static_cast<int *>(buf))[i] = 0;
    }
    int *status = new int[num_pages];
    int *nodes = new int[num_pages];
    void **bufs = new void *[num_pages];
    for (int i = 0; i < num_pages; i++) {
        status[i] = 0;
        nodes[i] = node_id;
        bufs[i] = reinterpret_cast<void *>(reinterpret_cast<uint64_t>(buf) + static_cast<uint64_t>(i) * page_size);
    }
    int rc = move_pages(getpid(), num_pages, bufs, nodes, status, MPOL_MF_MOVE_ALL);
    if (rc != 0) {
        std::runtime_error("Move page failed, maybe you forget to use 'sudo'");
    }
    for (int i = 0; i < num_pages; i++) {
        assert(status[i] == node_id);
    }

    delete[] status;
    delete[] nodes;
    delete[] bufs;

    return buf;
}

class ClientContext: public BasicContext {

public:
    ClientContext(size_t cid, size_t sid): client_id_(cid), server_sender_id_(sid),
        stat_req_ping_tot(0), stat_req_tc_tot(0), stat_req_err_tot(0) {
        spsc_queue = new atomic_queue::AtomicQueueB2<REQ_MSG, std::allocator<REQ_MSG>, true, false, true>(kAppMaxConcurrency);
        resp_spsc_queue = new atomic_queue::AtomicQueueB2<RESP_MSG, std::allocator<RESP_MSG>, true, false, true>(kAppMaxConcurrency);

        // char buf[4096] = "123456789";

        // std::string filename = folder_name + "file_file" + std::to_string(cid);
        // std::ofstream file(filename, std::ios::out | std::ios::binary);
        // if (!file.is_open())
        // {
        //     std::cout << "open file fail" << std::endl;
        //     exit(1);
        // }
        // for (size_t j = 0; j < alloc_size / PAGE_SIZE; j++)
        // {
        //     file.write(buf, 4096);
        // }
        // file.close();

        // FILE *target_file = fopen(filename.c_str(), "r+");
        // void *addr;
        // addr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(target_file), 0);
        // rmem::rt_assert(addr != MAP_FAILED, "mmap failed");

        // char tmp[10] = "123456789";
        // for (size_t j = 0; j < alloc_size / PAGE_SIZE; j++)
        // {
        //     memcpy(static_cast<char *>(addr) + j * PAGE_SIZE, tmp, 5);
        // }
        // file_addr = addr;

        file_addr = malloc_2m_numa(alloc_size, 1);
        char tmp[10] = "123456789";
        for (size_t j = 0; j < alloc_size / PAGE_SIZE; j++) {
            memcpy(static_cast<char *>(file_addr) + j * PAGE_SIZE, tmp, 5);
        }
    }
    ~ClientContext() {
        delete spsc_queue;
        delete resp_spsc_queue;
    }
    //
    void PushNextTCReq() {
        spsc_queue->push(REQ_MSG{ req_id_++, RPC_TYPE::RPC_TRANSCODE });
    }

    void reset_stat() {
        stat_req_ping_tot = 0;
        stat_req_tc_tot = 0;
        stat_req_err_tot = 0;
    }

    erpc::MsgBuffer req_msgbuf[kAppMaxConcurrency];
    erpc::MsgBuffer resp_msgbuf[kAppMaxConcurrency];

    erpc::MsgBuffer ping_msgbuf;
    erpc::MsgBuffer ping_resp_msgbuf;

    size_t client_id_;
    size_t server_sender_id_;
    uint32_t req_id_{};
    atomic_queue::AtomicQueueB2<REQ_MSG, std::allocator<REQ_MSG>, true, false, true> *spsc_queue;
    atomic_queue::AtomicQueueB2<RESP_MSG, std::allocator<RESP_MSG>, true, false, true> *resp_spsc_queue{};

    size_t stat_req_ping_tot;
    size_t stat_req_tc_tot;
    size_t stat_req_err_tot;

    void *file_addr;
};

class AppContext {
public:
    AppContext() {
        for (size_t i = 0; i < FLAGS_client_num; i++) {
            client_contexts_.push_back(new ClientContext(i, i % FLAGS_server_forward_num));
        }
        char buf[4096] = "123456789";

        folder_name = flags_get_cxl_fake_folder();
        if (folder_name[folder_name.size() - 1] != '/') {
            folder_name += '/';
        }
        for (size_t c_i = 0; c_i < FLAGS_client_num; c_i++) {
            for (size_t i = 0; i < create_file_size; i++) {
                std::string filename = folder_name + "cxl_" + std::to_string(c_i) + "_" + std::to_string(i);
                std::ofstream file(filename, std::ios::out | std::ios::binary);
                if (!file.is_open()) {
                    std::cout << "open file fail" << std::endl;
                    exit(1);
                }
                for (size_t j = 0; j < FLAGS_block_size / PAGE_SIZE; j++) {
                    file.write(buf, 4096);
                }
                file.close();
            }
        }
    }

    ~AppContext() {

        for (auto &ctx : client_contexts_) {
            delete ctx;
        }
    }

    std::vector<ClientContext *> client_contexts_;
};

class ServerContext: public BasicContext {
public:
    ServerContext(size_t sid): server_id_(sid), stat_req_ping_tot(0), stat_req_tc_tot(0), stat_req_err_tot(0) {
        std::random_device rd;
        std::uniform_int_distribution<int> dist(0, RAND_MAX);

        file_addr = malloc_2m_numa(alloc_size, 1);
        char tmp[10] = "123456789";
        for (size_t j = 0; j < alloc_size / PAGE_SIZE; j++) {
            memcpy(static_cast<char *>(file_addr) + j * PAGE_SIZE, tmp, 5);
        }
        max_num = alloc_size / PAGE_SIZE;
        for (size_t i = 0; i < max_num; i++) {
            random_1.push_back(static_cast<char *>(file_addr) + (dist(rd) % max_num) * PAGE_SIZE);
            random_2.push_back(static_cast<char *>(file_addr) + (dist(rd) % max_num) * PAGE_SIZE);
        }
        max_num_copy = alloc_size / FLAGS_block_size;
        for (size_t i = 0; i < max_num_copy; i++) {
            random_3.push_back(static_cast<char *>(file_addr) + (dist(rd) % max_num_copy) * FLAGS_block_size);
        }
    }
    ~ServerContext() {
    }
    size_t server_id_;
    size_t stat_req_ping_tot;
    size_t stat_req_tc_tot;
    size_t stat_req_err_tot;

    void *read_buf;
    void *write_buf;
    void *file_addr;
    size_t max_num;
    size_t max_num_copy;
    std::vector<void *> random_1;
    std::vector<void *> random_2;
    std::vector<void *> random_3;

    void reset_stat() {
        stat_req_ping_tot = 0;
        stat_req_tc_tot = 0;
        stat_req_err_tot = 0;
    }
};

class AppContext_Server {
public:
    AppContext_Server() {
        for (size_t i = 0; i < FLAGS_server_num; i++) {
            server_contexts_.push_back(new ServerContext(i));
        }
        folder_name = flags_get_cxl_fake_folder();
        if (folder_name[folder_name.size() - 1] != '/') {
            folder_name += '/';
        }
    }

    std::vector<ServerContext *> server_contexts_;
};
