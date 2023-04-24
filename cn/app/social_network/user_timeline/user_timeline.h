#pragma once
#include <hdr/hdr_histogram.h>
#include <mongoc.h>
#include "../social_network_commons.h"
#include "../social_network.pb.h"
#include "phmap.h"
#include "spinlock_mutex.h"
#include "../utils_mongodb.h"

std::string compose_post_addr;

static mongoc_client_pool_t* mongodb_client_pool;
int mongodb_conns_num;
phmap::flat_hash_map<int64_t , std::vector<int64_t>> user_timeline_map;


class ClientContext : public BasicContext
{
public:
    ClientContext(size_t cid, size_t sid, size_t rid) : client_id_(cid), server_sender_id_(sid), server_receiver_id_(rid), backward_session_num_(-1)
    {
        forward_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);
        forward_all_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);
        backward_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);
    }
    ~ClientContext()
    {
        delete forward_mpmc_queue;
        delete forward_all_mpmc_queue;
        delete backward_mpmc_queue;
    }
    erpc::MsgBuffer req_forward_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer req_backward_msgbuf[kAppMaxBuffer];

    erpc::MsgBuffer resp_forward_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer resp_backward_msgbuf[kAppMaxBuffer];

    size_t client_id_;
    size_t server_sender_id_;
    size_t server_receiver_id_;

    int nginx_session_number;
    int compose_pose_session_number;

    MPMC_QUEUE *forward_mpmc_queue;
    MPMC_QUEUE *forward_all_mpmc_queue;
    MPMC_QUEUE *backward_mpmc_queue;
};

class ServerContext : public BasicContext
{
public:
    explicit ServerContext(size_t sid) : server_id_(sid)
    {
    }
    ~ServerContext()
    = default;
    size_t server_id_{};
    size_t stat_req_ping_tot{};
    size_t stat_req_user_timeline_write_req_tot{};
    size_t stat_req_user_timeline_read_req_tot{};
    size_t stat_req_post_storage_read_resp_tot{};
    size_t stat_req_err_tot{};

    spinlock_mutex init_mutex;
    bool is_pinged{false};
    bool mongodb_init_finished{false};

    void reset_stat()
    {
        stat_req_ping_tot = 0;
        stat_req_user_timeline_write_req_tot = 0;
        stat_req_user_timeline_read_req_tot = 0;
        stat_req_post_storage_read_resp_tot = 0;
        stat_req_err_tot = 0;
    }

    MPMC_QUEUE *forward_all_mpmc_queue{};
    MPMC_QUEUE *backward_mpmc_queue{};

    erpc::MsgBuffer *req_forward_msgbuf_ptr{};
    erpc::MsgBuffer *req_backward_msgbuf_ptr{};
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
            client_contexts_.push_back(new ClientContext(i, i % FLAGS_server_num, i % FLAGS_server_num));
#elif defined(RMEM_PROGRAM)
            client_contexts_.push_back(new ClientContext(i, (i % FLAGS_server_num) + kAppMaxRPC, (i % FLAGS_server_num) + kAppMaxRPC));
#endif
        }
        for (size_t i = 0; i < FLAGS_server_num; i++)
        {
            auto *ctx = new ServerContext(i);
            ctx->forward_all_mpmc_queue = client_contexts_[i]->forward_all_mpmc_queue;
            ctx->backward_mpmc_queue = client_contexts_[i]->backward_mpmc_queue;
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

    [[maybe_unused]] [[nodiscard]] bool write_latency_and_reset(const std::string &filename) const
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

    std::vector<ClientContext *> client_contexts_;
    std::vector<ServerContext *> server_contexts_;

    hdr_histogram *latency_hist_{};
};

// must be used after init_service_config

void init_specific_config(){
    auto value = config_json_all["compose_post"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    compose_post_addr = value;

    auto conns = config_json_all["user-mongodb"]["connections"];
    rmem::rt_assert(!conns.is_null(),"value is null");
    mongodb_conns_num = conns;
}

void read_post_details(void *buf_, erpc::Rpc<erpc::CTransport> *rpc_, MPMC_QUEUE *consumer_fwd) {

}

void write_post_ids_and_return(void *buf_, erpc::Rpc<erpc::CTransport> *rpc_, MPMC_QUEUE *consumer_back) {

}