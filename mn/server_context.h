#pragma once

#include "mm_struct.h"
#include "commons.h"
#include "rpc.h"

namespace rmem
{
    class BasicAppContext
    {
    public:
        erpc::Rpc<erpc::CTransport> *rpc_ = nullptr;

        size_t thread_id_;        // The ID of the thread that owns this context
        size_t num_sm_resps_ = 0; // Number of SM responses

        virtual ~BasicAppContext() = default;
    };

    class ServerContext : public BasicAppContext
    {
    public:
        erpc::ChronoTimer tput_t0; // Start time for throughput measurement

        size_t stat_req_rx_tot;
        size_t stat_req_alloc_tot;
        size_t stat_req_free_tot;
        size_t stat_req_read_tot;
        size_t stat_req_write_tot;
        size_t stat_req_fork_tot;
        size_t stat_req_join_tot;
        size_t stat_req_error_tot;

        void reset_stat(){
            stat_req_rx_tot= 0;
            stat_req_alloc_tot= 0;
            stat_req_free_tot= 0;
            stat_req_read_tot= 0;
            stat_req_write_tot= 0;
            stat_req_fork_tot= 0;
            stat_req_join_tot= 0;
            stat_req_error_tot= 0;
        }
        // key = session_num
        phmap::flat_hash_map<uint16_t, mm_struct *> mm_struct_map_;
        static mm_struct *find_target_mm(uint16_t tid, uint16_t sid);
    };
}