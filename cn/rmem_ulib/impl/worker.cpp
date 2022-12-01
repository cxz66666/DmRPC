#include "worker.h"
#include "rpc_type.h"
#include "req_type.h"
namespace rmem
{
    void worker_func(Context *ctx)
    {
        rt_assert(ctx != nullptr);
        rt_assert(ctx->rpc_ != nullptr);

        WorkerStore *ws = new WorkerStore();

        using RMEM_HANDLER = std::function<bool(Context * ctx, WorkerStore * ws, const RingBufElement &el)>;

        RMEM_HANDLER rmem_handlers[] = {handler_connect, handler_disconnect, handler_alloc, handler_free,
                                        handler_read_sync, handler_read_async, handler_write_sync,
                                        handler_write_async, handler_fork, handler_join, handler_barrier};

        auto handler = [&](RingBufElement const el) -> bool
        {
            // rt_assert(el.req_type >= REQ_TYPE::RMEM_CONNECT);
            // rt_assert(el.req_type <= REQ_TYPE::RMEM_POOL);
            return rmem_handlers[static_cast<uint8_t>(el.req_type)](ctx, ws, el);
        };
        while (true)
        {
            RingBuf_process_all(ctx->ringbuf_, handler);
            ctx->rpc_->run_event_loop_once();
            if (unlikely(ctx->worker_stop_))
            {
                RMEM_INFO("worker thread exit");
                break;
            }
        }

        delete ws;
    }

    bool handler_connect(Context *ctx, WorkerStore *ws, const RingBufElement &el)
    {
        _unused(ws);
        rt_assert(ctx->rpc_->num_active_sessions() == 0, "one context can only have one connected session");
        rt_assert(ctx->concurrent_store_->get_session_num() == -1, "session num must be zero");
        int session_num = ctx->rpc_->create_session(std::string(el.connect.host), el.connect.remote_rpc_id);
        rt_assert(session_num >= 0, "get a negative session num");

        return true;
    }
    bool handler_disconnect(Context *ctx, WorkerStore *ws, const RingBufElement &el)
    {
        _unused(el);
        _unused(ws);
        rt_assert(ctx->rpc_->num_active_sessions() == 1, "one context can only disconnect after connnected");
        rt_assert(ctx->concurrent_store_->get_session_num() != -1, "session num must be 1");

        int res = ctx->rpc_->destroy_session(ctx->concurrent_store_->get_session_num());
        if (unlikely(res != 0))
        {
            ctx->condition_resp_->notify_waiter(res, "");
            return true;
        }
        // if res==0, then we will send a disconnect request, we will clear session at sm_handler
        return true;
    }
    bool handler_alloc(Context *ctx, WorkerStore *ws, const RingBufElement &el)
    {
        size_t req_number = ws->generate_next_num();
        erpc::MsgBuffer req = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(AllocReq));
        erpc::MsgBuffer resp = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(AllocResp));

        new (req.buf_) AllocReq(RPC_TYPE::RPC_ALLOC, req_number, el.alloc.alloc_size, el.alloc.vm_flags);
        ws->sended_req[req_number] = {req, resp};

        ctx->rpc_->enqueue_request(ctx->concurrent_store_->get_session_num(), static_cast<uint8_t>(RPC_TYPE::RPC_ALLOC),
                                   &ws->sended_req[req_number].first, &ws->sended_req[req_number].second,
                                   callback_alloc, reinterpret_cast<void *>(new WorkerTag{ws, req_number}));
        return true;
    }
    bool handler_free(Context *ctx, WorkerStore *ws, const RingBufElement &el)
    {
        size_t req_number = ws->generate_next_num();
        erpc::MsgBuffer req = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(FreeReq));
        erpc::MsgBuffer resp = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(FreeReq));
        new (req.buf_) FreeReq(RPC_TYPE::RPC_FREE, req_number, el.alloc.alloc_addr, el.alloc.alloc_size);
        ws->sended_req[req_number] = {req, resp};
        ctx->rpc_->enqueue_request(ctx->concurrent_store_->get_session_num(), static_cast<uint8_t>(RPC_TYPE::RPC_FREE),
                                   &ws->sended_req[req_number].first, &ws->sended_req[req_number].second,
                                   callback_free, reinterpret_cast<void *>(new WorkerTag{ws, req_number}));

        return true;
    }
    bool handler_read_sync(Context *ctx, WorkerStore *ws, const RingBufElement &el)
    {
        size_t req_number = ws->generate_next_num();
        erpc::MsgBuffer req = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(ReadReq));

        erpc::MsgBuffer resp;
        if (ctx->alloc_buffer.count(el.rw.rw_buffer))
        {
            resp = ctx->alloc_buffer[el.rw.rw_buffer];
            rt_assert(resp.buf_ + sizeof(ReadResp) == el.rw.rw_buffer, "buffer must be continuous");
        }
        else
        {
            resp = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(ReadResp) + sizeof(char) * el.rw.rw_size);
        }
        new (req.buf_) ReadReq(RPC_TYPE::RPC_READ, req_number, el.rw.rw_buffer, el.rw.rw_addr, el.rw.rw_size);
        ws->sended_req[req_number] = {req, resp};
        ctx->rpc_->enqueue_request(ctx->concurrent_store_->get_session_num(), static_cast<uint8_t>(RPC_TYPE::RPC_READ),
                                   &ws->sended_req[req_number].first, &ws->sended_req[req_number].second,
                                   callback_read_sync, reinterpret_cast<void *>(new WorkerTag{ws, req_number}));

        return ctx->rpc_->is_session_not_full(ctx->concurrent_store_->get_session_num());
    }
    bool handler_read_async(Context *ctx, WorkerStore *ws, const RingBufElement &el)
    {
        size_t req_number = ws->generate_next_num();
        erpc::MsgBuffer req = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(ReadReq));
        erpc::MsgBuffer resp;
        if (ctx->alloc_buffer.count(el.rw.rw_buffer))
        {
            resp = ctx->alloc_buffer[el.rw.rw_buffer];
            rt_assert(resp.buf_ + sizeof(ReadResp) == el.rw.rw_buffer, "buffer must be continuous");
        }
        else
        {
            resp = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(ReadResp) + sizeof(char) * el.rw.rw_size);
        }
        new (req.buf_) ReadReq(RPC_TYPE::RPC_READ, req_number, el.rw.rw_buffer, el.rw.rw_addr, el.rw.rw_size);
        ws->sended_req[req_number] = {req, resp};
        ws->async_received_req[req_number] = INT_MAX;
        ws->async_received_req_max=max_(ws->async_received_req_max,req_number);

        ctx->rpc_->enqueue_request(ctx->concurrent_store_->get_session_num(), static_cast<uint8_t>(RPC_TYPE::RPC_READ),
                                   &ws->sended_req[req_number].first, &ws->sended_req[req_number].second,
                                   callback_read_async, reinterpret_cast<void *>(new WorkerTag{ws, req_number}));

        return ctx->rpc_->is_session_not_full(ctx->concurrent_store_->get_session_num());
    }
    bool handler_write_sync(Context *ctx, WorkerStore *ws, const RingBufElement &el)
    {
        size_t req_number = ws->generate_next_num();
        erpc::MsgBuffer req;

        if (ctx->alloc_buffer.count(el.rw.rw_buffer))
        {
            req = ctx->alloc_buffer[el.rw.rw_buffer];
            rt_assert(req.buf_ + sizeof(WriteReq) == el.rw.rw_buffer, "buffer must be continuous");
        }
        else
        {
            req = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(WriteReq) + sizeof(char) * el.rw.rw_size);
            memcpy(req.buf_ + sizeof(WriteReq), el.rw.rw_buffer, el.rw.rw_size);
        }
        erpc::MsgBuffer resp = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(WriteResp));
        new (req.buf_) WriteReq(RPC_TYPE::RPC_WRITE, req_number, el.rw.rw_addr, el.rw.rw_size);

        ws->sended_req[req_number] = {req, resp};
        ctx->rpc_->enqueue_request(ctx->concurrent_store_->get_session_num(), static_cast<uint8_t>(RPC_TYPE::RPC_WRITE),
                                   &ws->sended_req[req_number].first, &ws->sended_req[req_number].second,
                                   callback_write_sync, reinterpret_cast<void *>(new WorkerTag{ws, req_number}));

        return ctx->rpc_->is_session_not_full(ctx->concurrent_store_->get_session_num());
    }
    bool handler_write_async(Context *ctx, WorkerStore *ws, const RingBufElement &el)
    {
        size_t req_number = ws->generate_next_num();
        erpc::MsgBuffer req;

        if (ctx->alloc_buffer.count(el.rw.rw_buffer))
        {
            req = ctx->alloc_buffer[el.rw.rw_buffer];
            rt_assert(req.buf_ + sizeof(WriteReq) == el.rw.rw_buffer, "buffer must be continuous");
        }
        else
        {
            req = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(WriteReq) + sizeof(char) * el.rw.rw_size);
            memcpy(req.buf_ + sizeof(WriteReq), el.rw.rw_buffer, el.rw.rw_size);
        }

        erpc::MsgBuffer resp = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(WriteResp));
        new (req.buf_) WriteReq(RPC_TYPE::RPC_WRITE, req_number, el.rw.rw_addr, el.rw.rw_size);

        ws->sended_req[req_number] = {req, resp};
        ws->async_received_req[req_number] = INT_MAX;
        ws->async_received_req_max=max_(ws->async_received_req_max,req_number);

        ctx->rpc_->enqueue_request(ctx->concurrent_store_->get_session_num(), static_cast<uint8_t>(RPC_TYPE::RPC_WRITE),
                                   &ws->sended_req[req_number].first, &ws->sended_req[req_number].second,
                                   callback_write_async, reinterpret_cast<void *>(new WorkerTag{ws, req_number}));
        return ctx->rpc_->is_session_not_full(ctx->concurrent_store_->get_session_num());
    }

    bool handler_fork(Context *ctx, WorkerStore *ws, const RingBufElement &el)
    {
        size_t req_number = ws->generate_next_num();
        erpc::MsgBuffer req = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(ForkReq));
        erpc::MsgBuffer resp = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(ForkResp));

        new (req.buf_) ForkReq(RPC_TYPE::RPC_FORK, req_number, el.alloc.alloc_addr, el.alloc.alloc_size);
        ws->sended_req[req_number] = {req, resp};

        ctx->rpc_->enqueue_request(ctx->concurrent_store_->get_session_num(), static_cast<uint8_t>(RPC_TYPE::RPC_FORK),
                                   &ws->sended_req[req_number].first, &ws->sended_req[req_number].second,
                                   callback_fork, reinterpret_cast<void *>(new WorkerTag{ws, req_number}));
        return true;
    }

    bool handler_join(Context *ctx, WorkerStore *ws, const RingBufElement &el)
    {
        size_t req_number = ws->generate_next_num();
        erpc::MsgBuffer req = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(JoinReq));
        erpc::MsgBuffer resp = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(JoinResp));

        new (req.buf_) JoinReq(RPC_TYPE::RPC_JOIN, req_number, el.join.addr, el.join.thread_id, el.join.session_id);
        ws->sended_req[req_number] = {req, resp};

        ctx->rpc_->enqueue_request(ctx->concurrent_store_->get_session_num(), static_cast<uint8_t>(RPC_TYPE::RPC_JOIN),
                                   &ws->sended_req[req_number].first, &ws->sended_req[req_number].second,
                                   callback_join, reinterpret_cast<void *>(new WorkerTag{ws, req_number}));
        return true;
    }
    inline void enqueue_async_req(Context *ctx, WorkerStore *ws)
    {
        while(ws->async_received_req_min<=ws->async_received_req_max){
            if(!ws->async_received_req.contains(ws->async_received_req_min)){
                ws->async_received_req_min++;
            } else {
                int status=ws->async_received_req[ws->async_received_req_min];
                if(status==INT_MAX){
                    break;
                } else {
                    while(unlikely(!ctx->concurrent_store_->spsc_queue->try_push(status))){
                        RMEM_INFO("push into spsc_queue error, req number %ld\n",ws->async_received_req_min);
                    }
                    ws->async_received_req.erase(ws->async_received_req_min++);
                }
            }
        }
    }

    bool handler_barrier(Context *ctx, WorkerStore *ws, const RingBufElement &el)
    {
        _unused(ctx);
        ws->set_barrier_point(el.barrier.barrier_size);
        return true;
    }

    void callback_alloc(void *_context, void *_tag)
    {
        Context *ctx = static_cast<Context *>(_context);
        WorkerTag *worker_tag = static_cast<WorkerTag *>(_tag);
        WorkerStore *ws = worker_tag->ws;
        size_t req_number = worker_tag->req_number;
        rt_assert(ws != nullptr, "worker store must not be empty!");

        if (unlikely(ws->sended_req.count(req_number) == 0))
        {
            RMEM_INFO("req number %ld not found, maybe discard", req_number);
            return;
        }

        erpc::MsgBuffer req_buffer = ws->sended_req[req_number].first;
        erpc::MsgBuffer resp_buffer = ws->sended_req[req_number].second;

        rt_assert(resp_buffer.get_data_size() == sizeof(AllocResp));

        AllocResp *resp = reinterpret_cast<AllocResp *>(resp_buffer.buf_);

        ctx->condition_resp_->notify_waiter_extra(resp->resp.status, resp->raddr, "");

        ctx->rpc_->free_msg_buffer(req_buffer);
        ctx->rpc_->free_msg_buffer(resp_buffer);
        ws->sended_req.erase(req_number);
    }
    void callback_free(void *_context, void *_tag)
    {
        Context *ctx = static_cast<Context *>(_context);
        WorkerTag *worker_tag = static_cast<WorkerTag *>(_tag);
        WorkerStore *ws = worker_tag->ws;
        size_t req_number = worker_tag->req_number;
        rt_assert(ws != nullptr, "worker store must not be empty!");

        if (unlikely(ws->sended_req.count(req_number) == 0))
        {
            RMEM_INFO("req number %ld not found, maybe discard", req_number);
            return;
        }

        erpc::MsgBuffer req_buffer = ws->sended_req[req_number].first;
        erpc::MsgBuffer resp_buffer = ws->sended_req[req_number].second;

        rt_assert(resp_buffer.get_data_size() == sizeof(FreeResp));

        FreeResp *resp = reinterpret_cast<FreeResp *>(resp_buffer.buf_);

        ctx->condition_resp_->notify_waiter(resp->resp.status, "");

        ctx->rpc_->free_msg_buffer(req_buffer);
        ctx->rpc_->free_msg_buffer(resp_buffer);
        ws->sended_req.erase(req_number);
    }
    void callback_read_async(void *_context, void *_tag)
    {
        Context *ctx = static_cast<Context *>(_context);
        WorkerTag *worker_tag = static_cast<WorkerTag *>(_tag);
        WorkerStore *ws = worker_tag->ws;
        size_t req_number = worker_tag->req_number;
        rt_assert(ws != nullptr, "worker store must not be empty!");

        if (unlikely(ws->sended_req.count(req_number) == 0))
        {
            RMEM_INFO("req number %ld not found, maybe discard", req_number);
            return;
        }

        erpc::MsgBuffer req_buffer = ws->sended_req[req_number].first;
        erpc::MsgBuffer resp_buffer = ws->sended_req[req_number].second;
    
        ReadResp *resp = reinterpret_cast<ReadResp *>(resp_buffer.buf_);

//        rt_assert(resp_buffer.get_data_size() == sizeof(ReadResp) + sizeof(char) * resp->rsize);

        // TODO enhance this copy!
        if (likely(ctx->alloc_buffer.count(resp_buffer.buf_ + sizeof(ReadResp)) == 0))
        {
            memcpy(resp->recv_buf, resp + 1, resp->rsize);
            ctx->rpc_->free_msg_buffer(resp_buffer);
        }

        ws->async_received_req[req_number] = resp->resp.status;

        ctx->rpc_->free_msg_buffer(req_buffer);
        ws->sended_req.erase(req_number);

        // find whether it have dist barrier
        if (unlikely(ws->barrier_point == req_number))
        {
            ctx->condition_resp_->notify_waiter(ws->get_async_req(), "");
        }
        enqueue_async_req(ctx,ws);
    }
    void callback_read_sync(void *_context, void *_tag)
    {
        Context *ctx = static_cast<Context *>(_context);
        WorkerTag *worker_tag = static_cast<WorkerTag *>(_tag);
        WorkerStore *ws = worker_tag->ws;
        size_t req_number = worker_tag->req_number;
        rt_assert(ws != nullptr, "worker store must not be empty!");

        if (unlikely(ws->sended_req.count(req_number) == 0))
        {
            RMEM_INFO("req number %ld not found, maybe discard", req_number);
            return;
        }

        erpc::MsgBuffer req_buffer = ws->sended_req[req_number].first;
        erpc::MsgBuffer resp_buffer = ws->sended_req[req_number].second;

        ReadResp *resp = reinterpret_cast<ReadResp *>(resp_buffer.buf_);

//        rt_assert(resp_buffer.get_data_size() == sizeof(ReadResp) + sizeof(char) * resp->rsize);

        if (likely(ctx->alloc_buffer.count(resp_buffer.buf_ + sizeof(ReadResp)) == 0))
        {
            memcpy(resp->recv_buf, resp + 1, resp->rsize);
            ctx->rpc_->free_msg_buffer(resp_buffer);
        }

        ctx->condition_resp_->notify_waiter(resp->resp.status, "");

        ctx->rpc_->free_msg_buffer(req_buffer);
        ws->sended_req.erase(req_number);
    }
    void callback_write_async(void *_context, void *_tag)
    {
        Context *ctx = static_cast<Context *>(_context);
        WorkerTag *worker_tag = static_cast<WorkerTag *>(_tag);
        WorkerStore *ws = worker_tag->ws;
        size_t req_number = worker_tag->req_number;

        rt_assert(ws != nullptr, "worker store must not be empty!");

        if (unlikely(ws->sended_req.count(req_number) == 0))
        {
            RMEM_INFO("req number %ld not found, maybe discard", req_number);
            return;
        }

        erpc::MsgBuffer req_buffer = ws->sended_req[req_number].first;
        erpc::MsgBuffer resp_buffer = ws->sended_req[req_number].second;

        WriteResp *resp = reinterpret_cast<WriteResp *>(resp_buffer.buf_);

        rt_assert(resp_buffer.get_data_size() == sizeof(WriteResp));

        ws->async_received_req[req_number] = resp->resp.status;

        // if this buffer is alloced by rmem_get_msg_buffer, don't free it!
        if (likely(ctx->alloc_buffer.count(req_buffer.buf_ + sizeof(WriteReq)) == 0))
        {
            ctx->rpc_->free_msg_buffer(req_buffer);
        }
        ctx->rpc_->free_msg_buffer(resp_buffer);
        ws->sended_req.erase(req_number);

        // find whether have dist barrier
        if (unlikely(ws->barrier_point == req_number))
        {
            ctx->condition_resp_->notify_waiter(ws->get_async_req(), "");
        }
        enqueue_async_req(ctx,ws);
    }
    void callback_write_sync(void *_context, void *_tag)
    {
        Context *ctx = static_cast<Context *>(_context);
        WorkerTag *worker_tag = static_cast<WorkerTag *>(_tag);
        WorkerStore *ws = worker_tag->ws;
        size_t req_number = worker_tag->req_number;
        rt_assert(ws != nullptr, "worker store must not be empty!");

        if (unlikely(ws->sended_req.count(req_number) == 0))
        {
            RMEM_INFO("req number %ld not found, maybe discard", req_number);
            return;
        }

        erpc::MsgBuffer req_buffer = ws->sended_req[req_number].first;
        erpc::MsgBuffer resp_buffer = ws->sended_req[req_number].second;

        WriteResp *resp = reinterpret_cast<WriteResp *>(resp_buffer.buf_);

        rt_assert(resp_buffer.get_data_size() == sizeof(WriteResp));

        ctx->condition_resp_->notify_waiter(resp->resp.status, "");

        // if this buffer is alloced by rmem_get_msg_buffer, don't free it!
        if (likely(ctx->alloc_buffer.count(req_buffer.buf_ + sizeof(WriteReq)) == 0))
        {
            ctx->rpc_->free_msg_buffer(req_buffer);
        }

        ctx->rpc_->free_msg_buffer(resp_buffer);
        ws->sended_req.erase(req_number);
    }

    void callback_fork(void *_context, void *_tag)
    {
        Context *ctx = static_cast<Context *>(_context);
        WorkerTag *worker_tag = static_cast<WorkerTag *>(_tag);
        WorkerStore *ws = worker_tag->ws;
        size_t req_number = worker_tag->req_number;
        rt_assert(ws != nullptr, "worker store must not be empty!");

        if (unlikely(ws->sended_req.count(req_number) == 0))
        {
            RMEM_INFO("req number %ld not found, maybe discard", req_number);
            return;
        }

        erpc::MsgBuffer req_buffer = ws->sended_req[req_number].first;
        erpc::MsgBuffer resp_buffer = ws->sended_req[req_number].second;

        ForkResp *resp = reinterpret_cast<ForkResp *>(resp_buffer.buf_);

        rt_assert(resp_buffer.get_data_size() == sizeof(ForkResp));

        ctx->condition_resp_->notify_waiter_extra(resp->resp.status, resp->new_raddr, "");

        ctx->rpc_->free_msg_buffer(req_buffer);
        ctx->rpc_->free_msg_buffer(resp_buffer);
        ws->sended_req.erase(req_number);
    }

    void callback_join(void *_context, void *_tag)
    {
        Context *ctx = static_cast<Context *>(_context);
        WorkerTag *worker_tag = static_cast<WorkerTag *>(_tag);
        WorkerStore *ws = worker_tag->ws;
        size_t req_number = worker_tag->req_number;
        rt_assert(ws != nullptr, "worker store must not be empty!");

        if (unlikely(ws->sended_req.count(req_number) == 0))
        {
            RMEM_INFO("req number %ld not found, maybe discard", req_number);
            return;
        }

        erpc::MsgBuffer req_buffer = ws->sended_req[req_number].first;
        erpc::MsgBuffer resp_buffer = ws->sended_req[req_number].second;

        JoinResp *resp = reinterpret_cast<JoinResp *>(resp_buffer.buf_);

        rt_assert(resp_buffer.get_data_size() == sizeof(JoinResp));
        ctx->condition_resp_->notify_waiter_extra(resp->resp.status, resp->raddr, "");

        ctx->rpc_->free_msg_buffer(req_buffer);
        ctx->rpc_->free_msg_buffer(resp_buffer);
        ws->sended_req.erase(req_number);
    }

    void basic_sm_handler(int session_num, int remote_session_num, erpc::SmEventType sm_event_type,
                          erpc::SmErrType sm_err_type, void *_context)
    {
        Context *ctx = static_cast<Context *>(_context);

        switch (sm_event_type)
        {
        case erpc::SmEventType::kConnected:
        {

            RMEM_INFO("Connect connected %d.\n", session_num);
            // TODO add timeout handler
            rt_assert(sm_err_type == erpc::SmErrType::kNoError);
            ctx->concurrent_store_->insert_session(session_num, remote_session_num);
            ctx->condition_resp_->notify_waiter(static_cast<int>(sm_err_type), "");
            break;
        }
        case erpc::SmEventType::kConnectFailed:
        {
            RMEM_WARN("Connect Error %s.\n",
                      sm_err_type_str(sm_err_type).c_str());
            ctx->condition_resp_->notify_waiter(static_cast<int>(sm_err_type), "");
            break;
        }
        case erpc::SmEventType::kDisconnected:
        {
            RMEM_INFO("Connect disconnected %d.\n", session_num);
            rt_assert(sm_err_type == erpc::SmErrType::kNoError);

            ctx->concurrent_store_->clear_session();
            ctx->condition_resp_->notify_waiter(static_cast<int>(sm_err_type), "");
            break;
        }
        case erpc::SmEventType::kDisconnectFailed:
        {
            RMEM_WARN("Connect Error %s.\n",
                      sm_err_type_str(sm_err_type).c_str());
            ctx->condition_resp_->notify_waiter(static_cast<int>(sm_err_type), "");
            break;
        }
        }
    }
}