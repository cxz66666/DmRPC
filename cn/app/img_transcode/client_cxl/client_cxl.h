#pragma once
#include <hdr/hdr_histogram.h>
#include "atomic_queue/atomic_queue.h"
#include "../img_transcode_commons.h"
#include "page.h"
#include <hs_clock.h>

DEFINE_string(test_bitmap_file, "", "test file path for bitmap image");
DEFINE_string(cxl_fake_folder, "", "Mount tmpfs on this folder, which use another numa memory to fake cxl memory");
DEFINE_uint64(cxl_session_num, 1, "CXL session num");
DEFINE_uint64(cxl_concurrency_scale, 1, "To avoid cache in CPU, we will use scaled file size");

std::streamsize file_size;
size_t file_size_aligned;

uint8_t *file_buf;

std::vector<std::vector<rmem::Timer>> timers(kAppMaxCXLSession, std::vector<rmem::Timer>(kAppMaxConcurrency));
hdr_histogram *latency_hist_;

double total_speed = 0;
spinlock_mutex total_speed_lock;
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

std::string flags_get_cxl_fake_folder()
{
    rmem::rt_assert(!FLAGS_cxl_fake_folder.empty(), "please set cxl_fake_folder to assign a folder to mount tmpfs");

    return FLAGS_cxl_fake_folder;
}

class ClientContext : public BasicContext
{

public:
    ClientContext(size_t cid, size_t sid, size_t rid) : client_id_(cid), server_sender_id_(sid), server_receiver_id_(rid)
    {
        spsc_queue = new atomic_queue::AtomicQueueB2<REQ_MSG, std::allocator<REQ_MSG>, true, false, true>(kAppMaxConcurrency * kAppMaxCXLSession);
    }
    ~ClientContext()
    {
        delete spsc_queue;
    }
    //
    void PushNextTCReq()
    {
        for (size_t i = 0; i < cxl_params_.size(); i++)
        {
            spsc_queue->push(REQ_MSG{static_cast<uint32_t>(i), RPC_TYPE::RPC_TRANSCODE});
        }
    }

    void PushPingReq()
    {
        for (size_t i = 0; i < cxl_params_.size(); i++)
        {
            spsc_queue->push(REQ_MSG{static_cast<uint32_t>(i), RPC_TYPE::RPC_PING});
        }
    }

    erpc::MsgBuffer req_msgbuf[kAppMaxCXLSession][kAppMaxConcurrency];
    erpc::MsgBuffer resp_msgbuf[kAppMaxCXLSession][kAppMaxConcurrency];

    erpc::MsgBuffer ping_msgbuf[kAppMaxCXLSession];
    erpc::MsgBuffer ping_resp_msgbuf[kAppMaxCXLSession];

    size_t client_id_{};
    size_t server_sender_id_;
    size_t server_receiver_id_;

    std::vector<CxlParam> cxl_params_;
    std::vector<size_t> cxl_flags_;
    std::vector<uint32_t> cxl_req_ids_;

    atomic_queue::AtomicQueueB2<REQ_MSG, std::allocator<REQ_MSG>, true, false, true> *spsc_queue;
    atomic_queue::AtomicQueueB2<RESP_MSG, std::allocator<RESP_MSG>, true, false, true> *resp_spsc_queue{};
};

class ServerContext : public BasicContext
{
public:
    ServerContext(size_t sid) : server_id_(sid), stat_req_ping_tot(0), stat_req_ping_resp_tot(0), stat_req_tc_tot(0), stat_req_tc_req_tot(0), stat_req_err_tot(0)
    {
        resp_spsc_queue = new atomic_queue::AtomicQueueB2<RESP_MSG, std::allocator<RESP_MSG>, true, false, true>(kAppMaxConcurrency * kAppMaxCXLSession);
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
        std::string folder_name = flags_get_cxl_fake_folder();
        if (folder_name[folder_name.size() - 1] != '/')
        {
            folder_name += '/';
        }

        size_t total_size = PAGE_ROUND_UP(FLAGS_concurrency * FLAGS_cxl_concurrency_scale * file_size_aligned);

        size_t server_id = 0;

        for (size_t i = 0; i < FLAGS_client_num; i++)
        {
            for (size_t session_num = 0; session_num < FLAGS_cxl_session_num; session_num++)
            {
                std::string filename = folder_name + "client_" + std::to_string(i) + "_" + "session_" + std::to_string(session_num);
                std::ofstream file(filename, std::ios::out | std::ios::binary);
                if (!file.is_open())
                {
                    std::cout << "open file fail" << std::endl;
                    exit(1);
                }

                for (size_t j = 0; j < FLAGS_concurrency * FLAGS_cxl_concurrency_scale; j++)
                {
                    file.write(reinterpret_cast<char *>(file_buf), file_size_aligned);
                }

                CxlParam tmp_param{};
                tmp_param.file_size_aligned = file_size_aligned;
                tmp_param.file_size = file_size;
                tmp_param.total_size = total_size;
                memcpy(tmp_param.filename, filename.c_str(), filename.size());
                client_contexts_[i]->cxl_params_.push_back(tmp_param);
                client_contexts_[i]->cxl_flags_.push_back(server_id << 32);
                client_contexts_[i]->cxl_req_ids_.push_back(0);
                server_id++;
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

    std::vector<ClientContext *> client_contexts_;
    std::vector<ServerContext *> server_contexts_;
};
