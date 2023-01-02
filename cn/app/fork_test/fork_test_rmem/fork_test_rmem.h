#pragma once
#include "../fork_test_common.h"
#include "api.h"
#include "page.h"
std::vector<std::vector<rmem::Timer>> timers(kAppMaxRPC, std::vector<rmem::Timer>(kAppMaxConcurrency));
hdr_histogram *latency_hist_;

double total_speed = 0;
spinlock_mutex total_speed_lock;

DEFINE_uint64(rmem_self_index, SIZE_MAX, "Rmem self node index line for app_process_file, 2 means line 3 represent status");
DEFINE_uint64(rmem_server_index, SIZE_MAX, "Rmem servers node index line for app_process_file, 2 means line 3 represent status");

DEFINE_uint64(read_number, 0, "read ratio in cow");
DEFINE_uint64(write_number, 0, "read ratio in cow");

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

    PingParam ping_param{};
    uint64_t raddr_begin{};
    rmem::Rmem *rmem_{};
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
        char buf[10] = "123456789";

        for (size_t i = 0; i < FLAGS_client_num; i++)
        {
            // TODO add server flag number?

            auto *rmem = new rmem::Rmem(0);
            int session_id = rmem->connect_session(rmem::get_uri_for_process(FLAGS_rmem_server_index), i);
            rmem::rt_assert(session_id >= 0, "connect session fail");
            unsigned long raddr = rmem->rmem_alloc(alloc_size, rmem::VM_FLAG_READ | rmem::VM_FLAG_WRITE);
            for (size_t i = 0; i < alloc_size; i += PAGE_SIZE)
            {
                rmem->rmem_write_sync(buf, i + raddr, strlen(buf));
            }

            rmems_.push_back(rmem);
            PingParam tmp_param{};
            tmp_param.rmem_thread_id_ = static_cast<int>(i);
            tmp_param.rmem_session_id_ = session_id;
            memcpy(tmp_param.hosts, rmem::get_uri_for_process(FLAGS_rmem_server_index).c_str(), rmem::get_uri_for_process(FLAGS_rmem_server_index).size());
            client_contexts_[i]->ping_param = tmp_param;
            client_contexts_[i]->raddr_begin = raddr;
            client_contexts_[i]->rmem_ = rmem;
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
    std::vector<rmem::Rmem *> rmems_;
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

    rmem::Rmem *rmem_{};
    int rmem_thread_id_;
    int rmem_session_id_;
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
    }

    std::vector<ServerContext *> server_contexts_;
};
