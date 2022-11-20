#pragma once
#include "context.h"
namespace rmem
{
    void worker_func(Context *ctx);
    void handler_connect(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    void handler_disconnnect(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    void handler_alloc(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    void handler_free(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    void handler_read_sync(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    void handler_read_async(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    void handler_write_sync(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    void handler_write_async(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    void handler_fork(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    void handler_join(Context *ctx, WorkerStore *ws, const RingBufElement &el);
    void handler_poll(Context *ctx, WorkerStore *ws, const RingBufElement &el);

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