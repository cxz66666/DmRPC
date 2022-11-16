#pragma once
#include "rpc.h"
#include "ring_buf.h"
#include "extern.h"
#include "commons.h"
#include "spinlock_mutex.h"
#include <thread>
#include <condition_variable>
#include <chrono>

namespace rmem
{
    // use for sync req response
    class ConditionResp
    {
    public:
        ConditionResp();
        ~ConditionResp();
        // user thread use for waiting response
        // no timeout
        int waiting_resp();
        // with timeout
        // TODO handle situation for those who connected but also timeout
        int waiting_resp(int timeout_ms);

        std::pair<int, unsigned long> waiting_resp_extra(int timeout_ms);

        // worker thread use to notify
        void notify_waiter(int resp_value, std::string msg_value);

        // worker thread use to notify with extra value
        void notify_waiter_extra(int resp_value, unsigned long extra, std::string msg_value);

    private:
        std::condition_variable cv;
        std::mutex mtx;
        bool notified;
        int resp;
        unsigned long extra_resp;
        std::string debug_msg;
    };

    // used in worker thread
    class WorkerStore
    {
    public:
        WorkerStore();
        ~WorkerStore();
        size_t generate_next_num();
        size_t set_dist_barrier();

        std::unordered_map<size_t, std::pair<erpc::MsgBuffer, erpc::MsgBuffer>> sended_req;
        // used for async received req
        std::map<size_t, int> async_received_req;

    private:
        size_t send_number;
        // size_t barrier_point;
    };

    // used for callback function second param
    class WorkerTag
    {
    public:
        WorkerStore *ws;
        size_t req_number;
    };

    class ConcurrentStroe
    {
        friend class Context;

    public:
        ConcurrentStroe();
        ~ConcurrentStroe();
        int get_session_num();
        void insert_session(int session);
        void clear_session();

    private:
        spinlock_mutex spin_lock;
        std::vector<int> session_num_vec_;
        size_t num_sm_resps_;
        size_t num_sm_reqs_;
    };

    class Context
    {
        friend void worker_func(Context *ctx);
        friend void handler_connect(Context *ctx, WorkerStore *ws, const RingBufElement &el);
        friend void handler_disconnnect(Context *ctx, WorkerStore *ws, const RingBufElement &el);
        friend void handler_alloc(Context *ctx, WorkerStore *ws, const RingBufElement &el);
        friend void handler_free(Context *ctx, WorkerStore *ws, const RingBufElement &el);
        friend void handler_read_sync(Context *ctx, WorkerStore *ws, const RingBufElement &el);
        friend void handler_read_async(Context *ctx, WorkerStore *ws, const RingBufElement &el);
        friend void handler_write_sync(Context *ctx, WorkerStore *ws, const RingBufElement &el);
        friend void handler_write_async(Context *ctx, WorkerStore *ws, const RingBufElement &el);
        friend void handler_fork(Context *ctx, WorkerStore *ws, const RingBufElement &el);
        friend void handler_join(Context *ctx, WorkerStore *ws, const RingBufElement &el);
        friend void handler_poll(Context *ctx, WorkerStore *ws, const RingBufElement &el);
        friend void callback_alloc(void *_context, void *_tag);
        friend void callback_free(void *_context, void *_tag);
        friend void callback_read_async(void *_context, void *_tag);
        friend void callback_read_sync(void *_context, void *_tag);
        friend void callback_write_async(void *_context, void *_tag);
        friend void callback_write_sync(void *_context, void *_tag);
        friend void callback_fork(void *_context, void *_tag);
        friend void callback_join(void *_context, void *_tag);
        friend Context *open_context(uint8_t phy_port);
        friend int close_context(class Context *ctx);

    public:
        // parallel data(need use atomic action)
        ConcurrentStroe *concurrent_store_;
        // ring buffer for convert msg from user thread to worker thread
        RingBuf *ringbuf_;

        // condition variable for sync request
        ConditionResp *condition_resp_;

    private:
        explicit Context(uint8_t phy_port);
        ~Context();
        // need have g_lock before call this function
        // return bind_core_index(0 to MaxContext-1)
        size_t get_core_index_unlock();

        // must be called by user thread
        void start_worker_thread();
        // must be called by user thread;
        void stop_worker_thread();
        // need have g_lock before call this function
        uint8_t get_legal_rpc_id_unlock();

        // rpc element
        erpc::Rpc<erpc::CTransport> *rpc_;

        size_t bind_core_index;

        std::thread worker_thread_;
        volatile bool worker_stop_;
    };

}