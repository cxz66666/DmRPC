#pragma once
#include <hdr/hdr_histogram.h>
#include "atomic_queue/atomic_queue.h"
#include "../social_network_commons.h"



class ClientContext : public BasicContext
{
public:
    ClientContext(size_t cid, size_t sid, size_t rid) : client_id_(cid), server_sender_id_(sid), server_receiver_id_(rid), backward_session_num_(-1)
    {}
    ~ClientContext()
    {}

    size_t client_id_;
    size_t server_sender_id_;
    size_t server_receiver_id_;

    int backward_session_num_;

    int servers_num_{};
};

class ServerContext : public BasicContext
{
public:
    ServerContext(size_t sid) : server_id_(sid)
    {
    }
    ~ServerContext()
    {
    }
    size_t server_id_{};
    size_t stat_req_ping_tot{};
    size_t stat_req_unique_id{};
    size_t stat_req_err_tot{};

    void reset_stat()
    {
        stat_req_ping_tot = 0;
        stat_req_unique_id = 0;
        stat_req_err_tot = 0;
    }
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
            client_contexts_.push_back(new ClientContext(i, (i % FLAGS_server_forward_num) + kAppMaxRPC, (i % FLAGS_server_backward_num) + kAppMaxRPC));
#elif defined(CXL_PROGRAM)
            client_contexts_.push_back(new ClientContext(i, i % FLAGS_server_forward_num, i % FLAGS_server_backward_num));
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

    [[maybe_unused]] bool write_latency_and_reset(const std::string &filename)
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
    auto value = config_json_all["nginx"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    FLAGS_load_balance_servers = value;

    value = config_json_all["client"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    client_addr = value;
}

std::vector<std::string> flags_get_balance_servers_index()
{

    rmem::rt_assert(!FLAGS_load_balance_servers.empty(), "please set at least one load balance server");
    std::vector<size_t> ret;
    std::vector<std::string> split_vec = rmem::split(FLAGS_load_balance_servers, ',');
    rmem::rt_assert(!split_vec.empty());

    return split_vec;
}
