#pragma once
#include <hdr/hdr_histogram.h>
#include "atomic_queue/atomic_queue.h"
#include "../img_transcode_commons.h"
#include <hs_clock.h>

DEFINE_string(test_bitmap_file, "", "test file path for bitmap image");

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

class ClientContext : public BasicContext
{

public:
    ClientContext(size_t cid, size_t sid, size_t rid) : client_id_(cid), server_sender_id_(sid), server_receiver_id_(rid)
    {
        spsc_queue = new atomic_queue::AtomicQueueB2<REQ_MSG, std::allocator<REQ_MSG>, true, false, true>(kAppMaxConcurrency);
    }
    ~ClientContext()
    {
        delete spsc_queue;
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
    size_t server_receiver_id_;
    uint32_t req_id_;
    atomic_queue::AtomicQueueB2<REQ_MSG, std::allocator<REQ_MSG>, true, false, true> *spsc_queue;
    atomic_queue::AtomicQueueB2<RESP_MSG, std::allocator<RESP_MSG>, true, false, true> *resp_spsc_queue;
};

class ServerContext : public BasicContext
{
public:
    ServerContext(size_t sid) : server_id_(sid), stat_req_ping_tot(0), stat_req_ping_resp_tot(0), stat_req_tc_tot(0), stat_req_tc_req_tot(0), stat_req_err_tot(0)
    {
        resp_spsc_queue = new atomic_queue::AtomicQueueB2<RESP_MSG, std::allocator<RESP_MSG>, true, false, true>(kAppMaxConcurrency);
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

    uint32_t req_number_;

    std::vector<ClientContext *> client_contexts_;
    std::vector<ServerContext *> server_contexts_;
};
