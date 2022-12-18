#pragma once
#include <hdr/hdr_histogram.h>
#include "atomic_queue/atomic_queue.h"
#include "../img_transcode_commons.h"

class ClientContext : public BasicContext
{
public:
    ClientContext(size_t cid, size_t sid, size_t rid) : client_id_(cid), server_sender_id_(sid), server_receiver_id_(rid)
    {
        forward_spsc_queue = new atomic_queue::AtomicQueueB2<erpc::MsgBuffer, std::allocator<erpc::MsgBuffer>, true, false, true>(kAppMaxConcurrency);
        backward_spsc_queue = new atomic_queue::AtomicQueueB2<erpc::MsgBuffer, std::allocator<erpc::MsgBuffer>, true, false, false>(kAppMaxConcurrency);
    }
    ~ClientContext()
    {
        delete forward_spsc_queue;
        delete backward_spsc_queue;
    }
    erpc::MsgBuffer req_forward_msgbuf[kAppMaxConcurrency];
    erpc::MsgBuffer req_backward_msgbuf[kAppMaxConcurrency];

    erpc::MsgBuffer resp_forward_msgbuf[kAppMaxConcurrency];
    erpc::MsgBuffer resp_backward_msgbuf[kAppMaxConcurrency];

    size_t client_id_;
    size_t server_sender_id_;
    size_t server_receiver_id_;

    atomic_queue::AtomicQueueB2<erpc::MsgBuffer, std::allocator<erpc::MsgBuffer>, true, false, true> *forward_spsc_queue;
    atomic_queue::AtomicQueueB2<erpc::MsgBuffer, std::allocator<erpc::MsgBuffer>, true, false, false> *backward_spsc_queue;
};

class ServerContext : public BasicContext
{
public:
    ServerContext(size_t sid) : server_id_(sid), stat_req_ping_tot(0), stat_req_ping_resp_tot(0), stat_req_tc_tot(0), stat_req_tc_req_tot(0), stat_req_err_tot(0)
    {
    }
    ~ServerContext()
    {
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

    atomic_queue::AtomicQueueB2<erpc::MsgBuffer, std::allocator<erpc::MsgBuffer>, true, false, true> *forward_spsc_queue;
    atomic_queue::AtomicQueueB2<erpc::MsgBuffer, std::allocator<erpc::MsgBuffer>, true, false, false> *backward_spsc_queue;

    erpc::MsgBuffer *req_forward_msgbuf_ptr;
    erpc::MsgBuffer *req_backward_msgbuf_ptr;
};

class AppContext
{
public:
    AppContext()
    {
        int ret = hdr_init(1, 1000 * 1000 * 10, 3,
                           &latency_hist_);
        rmem::rt_assert(ret == 0, "hdr_init failed");
        for (size_t i = 0; i < FLAGS_client_num; i++)
        {
#if defined(ERPC_PROGRAM)
            client_contexts_.push_back(new ClientContext(i, i % FLAGS_server_forward_num, i % FLAGS_server_backward_num));
#elif defined(RMEM_PROGRAM)
            client_contexts_.push_back(new ClientContext(i, (i % FLAGS_server_forward_num) + kAppMaxRPC , (i % FLAGS_server_backward_num) + kAppMaxRPC));
#elif defined(CXL_PROGRAM)
            client_contexts_.push_back(new ClientContext(i, i % FLAGS_server_forward_num, i % FLAGS_server_backward_num));
#endif
        }
        for (size_t i = 0; i < FLAGS_server_num; i++)
        {
            ServerContext *ctx = new ServerContext(i);
            ctx->forward_spsc_queue = client_contexts_[i]->forward_spsc_queue;
            ctx->backward_spsc_queue = client_contexts_[i]->backward_spsc_queue;
            ctx->req_forward_msgbuf_ptr = client_contexts_[i]->req_forward_msgbuf;
            ctx->req_backward_msgbuf_ptr = client_contexts_[i]->req_backward_msgbuf;
            server_contexts_.push_back(ctx);
        }
    }
    ~AppContext()
    {
        hdr_close(latency_hist_);

        for (auto &ctx : client_contexts_)
        {
            delete ctx;
        }
        for (auto &ctx : server_contexts_)
        {
            delete ctx;
        }
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

    uint32_t req_number_;

    std::vector<ClientContext *> client_contexts_;
    std::vector<ServerContext *> server_contexts_;

    hdr_histogram *latency_hist_;
};
