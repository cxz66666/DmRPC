#pragma once
#include <hdr/hdr_histogram.h>
#include "../social_network_commons.h"

std::string load_balance_addr;
std::string compose_post_addr;
std::string user_timeline_addr;
std::string home_timeline_addr;

size_t send_ping_req_num = 0;
size_t send_ping_resp_num = 0;

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
    erpc::MsgBuffer req_forward_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer req_backward_msgbuf[kAppMaxBuffer];

    erpc::MsgBuffer resp_forward_msgbuf[kAppMaxBuffer];
    erpc::MsgBuffer resp_backward_msgbuf[kAppMaxBuffer];

    size_t client_id_;
    size_t server_sender_id_;
    size_t server_receiver_id_;

    int backward_session_num_;

    int servers_num_{};

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
    size_t stat_req_ping_resp_tot{};
    size_t stat_req_compose_post_tot{};
    size_t stat_req_compose_post_resp_tot{};
    size_t stat_req_user_timeline_tot{};
    size_t stat_req_user_timeline_resp_tot{};
    size_t stat_req_home_timeline_tot{};
    size_t stat_req_home_timeline_resp_tot{};
    size_t stat_req_err_tot{};

    void reset_stat()
    {
        stat_req_ping_tot = 0;
        stat_req_ping_resp_tot = 0;
        stat_req_compose_post_tot = 0;
        stat_req_compose_post_resp_tot =0;
        stat_req_user_timeline_tot = 0;
        stat_req_user_timeline_resp_tot = 0;
        stat_req_home_timeline_tot = 0;
        stat_req_home_timeline_resp_tot = 0;
        stat_req_err_tot = 0;
    }

    SPSC_QUEUE *forward_spsc_queue{};
    SPSC_QUEUE *backward_spsc_queue{};

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
    rmem::rt_assert(!config_json_all.is_null(),"must be used after init_service_config");
    // this is a hack
    auto value = config_json_all["load_balance"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    load_balance_addr = value;

    value = config_json_all["compose_pose"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    compose_post_addr = value;

    value = config_json_all["user_timeline"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    user_timeline_addr = value;

    value = config_json_all["home_timeline"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    home_timeline_addr = value;
}

std::vector<std::string> get_forward_addrs()
{
    return {compose_post_addr, user_timeline_addr, home_timeline_addr};
}
