#pragma once
#include <hdr/hdr_histogram.h>
#include "atomic_queue/atomic_queue.h"
#include "../img_transcode_commons.h"
#include "api.h"
#include "page.h"
#include <hs_clock.h>

DEFINE_string(test_bitmap_file, "", "test file path for bitmap image");
DEFINE_uint64(rmem_self_index, SIZE_MAX, "Rmem self node index line for app_process_file, 2 means line 3 represent status");
DEFINE_string(rmem_server_indexs, "", "Rmem servers node index line for app_process_file, 2 means line 3 represent status");
DEFINE_uint64(rmem_session_num, 1, "Rmem session num");

std::streamsize file_size;
size_t file_size_aligned;

uint8_t *file_buf;

std::vector<rmem::Timer> timers;
hdr_histogram *latency_hist_;

struct REQ_MSG
{
    uint32_t req_id;
    RPC_TYPE req_type;
};

struct RESP_MSG
{
    uint32_t req_id;
    int status;
};
static_assert(sizeof(REQ_MSG) == 8, "REQ_MSG size is not 8");
static_assert(sizeof(RESP_MSG) == 8, "RESP_MSG size is not 8");

std::vector<size_t> flags_get_rmem_server_indexs()
{
    rmem::rt_assert(!FLAGS_rmem_server_indexs.empty(), "please set at least one load balance server");
    std::vector<size_t> ret;
    std::vector<std::string> split_vec = rmem::split(FLAGS_rmem_server_indexs, ',');
    rmem::rt_assert(!split_vec.empty());

    for (auto &s : split_vec)
        ret.push_back(std::stoull(s)); // stoull trims ' '

    return ret;
}

class ClientContext : public BasicContext
{

public:
    ClientContext(size_t cid, size_t sid, size_t rid) : client_id_(cid), server_sender_id_(sid), server_receiver_id_(rid)
    {
        spsc_queue = new atomic_queue::AtomicQueueB2<REQ_MSG, std::allocator<REQ_MSG>, true, false, true>(kAppMaxConcurrency * kAppMaxRPC);
    }
    ~ClientContext()
    {
        delete spsc_queue;
    }
    //
    void PushNextTCReq()
    {
        for (size_t i = 0; i < rmem_params_.size(); i++)
        {
            spsc_queue->push(REQ_MSG{static_cast<uint32_t>(i), RPC_TYPE::RPC_TRANSCODE});
        }
    }

    void PushPingReq()
    {
        for (size_t i = 0; i < rmem_params_.size(); i++)
        {
            spsc_queue->push(REQ_MSG{static_cast<uint32_t>(i), RPC_TYPE::RPC_PING});
        }
    }

    erpc::MsgBuffer req_msgbuf[kAppMaxRPC][kAppMaxConcurrency];
    erpc::MsgBuffer resp_msgbuf[kAppMaxRPC][kAppMaxConcurrency];

    erpc::MsgBuffer ping_msgbuf[kAppMaxRPC];
    erpc::MsgBuffer ping_resp_msgbuf[kAppMaxRPC];

    size_t client_id_{};
    size_t server_sender_id_;
    size_t server_receiver_id_;

    std::vector<RmemParam> rmem_params_;
    std::vector<size_t> rmem_flags_;
    std::vector<uint32_t> rmem_req_ids_;

    atomic_queue::AtomicQueueB2<REQ_MSG, std::allocator<REQ_MSG>, true, false, true> *spsc_queue;
    atomic_queue::AtomicQueueB2<RESP_MSG, std::allocator<RESP_MSG>, true, false, true> *resp_spsc_queue{};
};

class ServerContext : public BasicContext
{
public:
    ServerContext(size_t sid) : server_id_(sid), stat_req_ping_tot(0), stat_req_ping_resp_tot(0), stat_req_tc_tot(0), stat_req_tc_req_tot(0), stat_req_err_tot(0)
    {
        resp_spsc_queue = new atomic_queue::AtomicQueueB2<RESP_MSG, std::allocator<RESP_MSG>, true, false, true>(kAppMaxConcurrency * kAppMaxRPC);
    }
    ~ServerContext()
    {
        delete resp_spsc_queue;
    }

    size_t server_id_;
    size_t stat_req_ping_tot;
    size_t stat_req_ping_resp_tot;
    size_t stat_req_tc_tot;
    size_t stat_req_tc_req_tot;
    size_t stat_req_err_tot;

    void reset_stat()
    {
        stat_req_ping_tot = 0;
        stat_req_ping_resp_tot = 0;
        stat_req_tc_tot = 0;
        stat_req_tc_req_tot = 0;
        stat_req_err_tot = 0;
    }
    // used for tc send
    atomic_queue::AtomicQueueB2<REQ_MSG, std::allocator<REQ_MSG>, true, false, true> *spsc_queue;
    // used for ping receive
    atomic_queue::AtomicQueueB2<RESP_MSG, std::allocator<RESP_MSG>, true, false, true> *resp_spsc_queue;
};

class AppContext
{
public:
    AppContext()
    {
        for (size_t i = 0; i < FLAGS_client_num; i++)
        {
            client_contexts_.push_back(new ClientContext(i, i % FLAGS_server_forward_num, i % FLAGS_server_num));
        }
        for (size_t i = 0; i < FLAGS_server_num; i++)
        {
            server_contexts_.push_back(new ServerContext(i));
            server_contexts_[i]->spsc_queue = client_contexts_[i % FLAGS_client_num]->spsc_queue;
        }
        for (size_t i = 0; i < FLAGS_client_num; i++)
        {
            client_contexts_[i]->resp_spsc_queue = server_contexts_[i % FLAGS_server_num]->resp_spsc_queue;
        }

        for (size_t i = 0; i < FLAGS_client_num; i++)
        {
            // TODO add server flag number?
            size_t server_id = 0;
            for (auto s : flags_get_rmem_server_indexs())
            {
                for (size_t session_num = 0; session_num < FLAGS_rmem_session_num; session_num++)
                {
                    auto *rmem = new rmem::Rmem(0);
                    int session_id = rmem->connect_session(rmem::get_uri_for_process(s), session_num);
                    rmem::rt_assert(session_id >= 0, "connect session fail");
                    rmems_.push_back(rmem);
                    RmemParam tmp_param{};
                    tmp_param.rmem_thread_id_ = static_cast<int>(session_num);
                    tmp_param.rmem_session_id_ = session_id;
                    tmp_param.file_size = file_size_aligned;
                    memcpy(tmp_param.hosts, rmem::get_uri_for_process(s).c_str(), rmem::get_uri_for_process(s).size());
                    client_contexts_[i]->rmem_params_.push_back(tmp_param);
                    client_contexts_[i]->rmem_flags_.push_back(server_id << 32);
                    client_contexts_[i]->rmem_req_ids_.push_back(0);
                    server_id++;
                }
            }

            ;
        }

        size_t total_size = PAGE_ROUND_UP(FLAGS_concurrency * file_size_aligned);

        for (size_t c = 0; c < FLAGS_client_num; c++)
        {
            for (size_t i = 0; i < rmems_.size(); i++)
            {
                uint64_t base_addr = rmems_[i]->rmem_alloc(total_size, rmem::VM_FLAG_READ | rmem::VM_FLAG_WRITE);
                for (size_t j = 0; j < FLAGS_concurrency; j++)
                {
                    rmems_[i]->rmem_write_sync(file_buf, base_addr + j * file_size_aligned, file_size);
                }
                client_contexts_[c]->rmem_params_[i].fork_rmem_addr_ = rmems_[i]->rmem_fork(base_addr, total_size);
                client_contexts_[c]->rmem_params_[i].fork_size = total_size;
                rmem::rt_assert(rmems_[i]->rmem_free(base_addr, total_size) == 0, "rmem free fail");

                // TODO want to free this session at now, but can't because worker don't use join!
            }
        }
    }
    ~AppContext()
    {

        for (auto &ctx : client_contexts_)
        {
            delete ctx;
        }
        for (auto &ctx : server_contexts_)
        {
            delete ctx;
        }
    }

    std::vector<rmem::Rmem *> rmems_;
    std::vector<ClientContext *> client_contexts_;
    std::vector<ServerContext *> server_contexts_;
};
