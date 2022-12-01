#pragma once
#include "context.h"
namespace rmem
{
    void worker_func(Context *ctx);
    void enqueue_async_req(Context *ctx, WorkerStore *ws);
    bool handler_connect(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    bool handler_disconnect(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    bool handler_alloc(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    bool handler_free(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    bool handler_read_sync(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    bool handler_read_async(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    bool handler_write_sync(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    bool handler_write_async(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    bool handler_fork(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    bool handler_join(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    bool handler_barrier(Context *ctx, WorkerStore *ws, const RingBufElement &el);

    void callback_alloc(void *_context, void *_tag);
    void callback_free(void *_context, void *_tag);
    void callback_read_async(void *_context, void *_tag);
    void callback_read_sync(void *_context, void *_tag);
    void callback_write_async(void *_context, void *_tag);
    void callback_write_sync(void *_context, void *_tag);
    void callback_fork(void *_context, void *_tag);
    void callback_join(void *_context, void *_tag);

    void basic_sm_handler(int session_num, int remote_session_num, erpc::SmEventType sm_event_type,
                          erpc::SmErrType sm_err_type, void *_context);
}