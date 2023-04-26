#pragma once
#include <hdr/hdr_histogram.h>
#include <mongoc.h>
#include "atomic_queue/atomic_queue.h"
#include "../social_network_commons.h"
#include "../social_network.pb.h"
#include "phmap.h"
#include "spinlock_mutex.h"
#include "../utils_mongodb.h"

using SPSC_QUEUE = atomic_queue::AtomicQueueB2<erpc::MsgBuffer, std::allocator<erpc::MsgBuffer>, true, false, true>;

std::string compose_post_addr;

static mongoc_client_pool_t* mongodb_client_pool;
int mongodb_conns_num;
phmap::flat_hash_map<std::string , social_network::UserMention *> user_mention_map;


class ClientContext : public BasicContext
{
public:
    ClientContext(size_t cid, size_t sid, size_t rid) : client_id_(cid), server_sender_id_(sid), server_receiver_id_(rid), backward_session_num_(-1)
    {
        forward_spsc_queue = new SPSC_QUEUE(kAppMaxBuffer);
        backward_spsc_queue = new SPSC_QUEUE(kAppMaxBuffer);
    }
    ~ClientContext()
    {
        delete forward_spsc_queue;
        delete backward_spsc_queue;
    }
    erpc::MsgBuffer req_backward_msgbuf[kAppMaxBuffer];

    erpc::MsgBuffer resp_backward_msgbuf[kAppMaxBuffer];

    size_t client_id_;
    size_t server_sender_id_;
    size_t server_receiver_id_;

    int backward_session_num_;

    SPSC_QUEUE *forward_spsc_queue;
    SPSC_QUEUE *backward_spsc_queue;
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
    size_t stat_req_user_mention_tot{};
    size_t stat_req_err_tot{};

    spinlock_mutex init_mutex;
    bool is_pinged{false};
    bool mongodb_init_finished{false};

    void reset_stat()
    {
        stat_req_ping_tot = 0;
        stat_req_user_mention_tot = 0;
        stat_req_err_tot = 0;
    }

    SPSC_QUEUE *forward_spsc_queue{};
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
            ctx->forward_spsc_queue = client_contexts_[i]->forward_spsc_queue;
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

    auto conns = config_json_all["user_mongodb"]["connections"];
    rmem::rt_assert(!conns.is_null(),"value is null");
    mongodb_conns_num = conns;
}

social_network::UserMentionResp generate_user_mentions(const social_network::UserMentionReq& req)
{
    social_network::UserMentionResp resp;
    for(auto &name : req.names()) {
        if(user_mention_map.contains(name)) {
            social_network::UserMention *target_user_mention = user_mention_map[name];
            social_network::UserMention *new_user_mention = resp.add_user_mentions();
            new_user_mention->set_user_id(target_user_mention->user_id());
            new_user_mention->set_username(target_user_mention->username());
        } else {
            RMEM_WARN("don't find user: %s", name.c_str());
        }
    }
    return resp;
}
