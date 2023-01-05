#pragma once
#include "../fork_test_common.h"
#include "page.h"
std::vector<std::vector<rmem::Timer>> timers(kAppMaxRPC, std::vector<rmem::Timer>(kAppMaxConcurrency));
hdr_histogram *latency_hist_;

double total_speed = 0;
spinlock_mutex total_speed_lock;

std::string folder_name;

static constexpr size_t create_file_size = 10000;

DEFINE_uint64(block_size, 4096, "block size");
DEFINE_string(cxl_fake_folder, "", "Mount tmpfs on this folder, which use another numa memory to fake cxl memory");
DEFINE_bool(no_cow, false, "Don't use cow");

std::string flags_get_cxl_fake_folder()
{
    rmem::rt_assert(!FLAGS_cxl_fake_folder.empty(), "please set cxl_fake_folder to assign a folder to mount tmpfs");

    return FLAGS_cxl_fake_folder;
}

class ClientContext : public BasicContext
{

public:
    ClientContext(size_t cid, size_t sid) : client_id_(cid), server_sender_id_(sid),
                                            stat_req_ping_tot(0), stat_req_tc_tot(0), stat_req_err_tot(0)
    {
        spsc_queue = new atomic_queue::AtomicQueueB2<REQ_MSG, std::allocator<REQ_MSG>, true, false, true>(kAppMaxConcurrency);
        resp_spsc_queue = new atomic_queue::AtomicQueueB2<RESP_MSG, std::allocator<RESP_MSG>, true, false, true>(kAppMaxConcurrency);
    }
    ~ClientContext()
    {
        delete spsc_queue;
        delete resp_spsc_queue;
    }
    //
    void PushNextTCReq()
    {
        spsc_queue->push(REQ_MSG{req_id_++, RPC_TYPE::RPC_TRANSCODE});
    }

    void reset_stat()
    {
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
};

class AppContext
{
public:
    AppContext()
    {
        for (size_t i = 0; i < FLAGS_client_num; i++)
        {
            client_contexts_.push_back(new ClientContext(i, i % FLAGS_server_forward_num));
        }
        char buf[4096] = "123456789";

        folder_name = flags_get_cxl_fake_folder();
        if (folder_name[folder_name.size() - 1] != '/')
        {
            folder_name += '/';
        }
        for (size_t c_i = 0; c_i < FLAGS_client_num; c_i++)
        {
            for (size_t i = 0; i < create_file_size; i++)
            {
                std::string filename = folder_name + "cxl_" + std::to_string(c_i) + "_" + std::to_string(i);
                std::ofstream file(filename, std::ios::out | std::ios::binary);
                if (!file.is_open())
                {
                    std::cout << "open file fail" << std::endl;
                    exit(1);
                }
                for (size_t j = 0; j < FLAGS_block_size / PAGE_SIZE; j++)
                {
                    file.write(buf, 4096);
                }
                file.close();
            }
        }
    }
    ~AppContext()
    {

        for (auto &ctx : client_contexts_)
        {
            delete ctx;
        }
    }

    std::vector<ClientContext *> client_contexts_;
};

class ServerContext : public BasicContext
{
public:
    ServerContext(size_t sid) : server_id_(sid), stat_req_ping_tot(0), stat_req_tc_tot(0), stat_req_err_tot(0)
    {
    }
    ~ServerContext()
    {
    }
    size_t server_id_;
    size_t stat_req_ping_tot;
    size_t stat_req_tc_tot;
    size_t stat_req_err_tot;

    void *read_buf;
    void *write_buf;

    void reset_stat()
    {
        stat_req_ping_tot = 0;
        stat_req_tc_tot = 0;
        stat_req_err_tot = 0;
    }
};

class AppContext_Server
{
public:
    AppContext_Server()
    {
        for (size_t i = 0; i < FLAGS_server_num; i++)
        {
            server_contexts_.push_back(new ServerContext(i));
        }
        folder_name = flags_get_cxl_fake_folder();
        if (folder_name[folder_name.size() - 1] != '/')
        {
            folder_name += '/';
        }
    }

    std::vector<ServerContext *> server_contexts_;
};
