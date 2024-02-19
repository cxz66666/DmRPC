#include "rpc_type.h"
#include "req_handler.h"
#include "mm_struct.h"
#include "server_context.h"
#include "page.h"
#include <cmath>
#include <hs_clock.h>
#include "hdr/hdr_histogram.h"
extern hdr_histogram *latency_hist_;
namespace rmem {

#define RETURN_IF_ERROR(ERRNO, RESP_STRUCT, CTX, REQ_HANDLER, REQ)                             \
    {                                                                                          \
        RMEM_WARN("error response %ld, status %d\n", REQ.req_number, ERRNO);                   \
        new (REQ_HANDLER->pre_resp_msgbuf_.buf_) RESP_STRUCT(REQ.type, REQ.req_number, ERRNO); \
        CTX->rpc_->resize_msg_buffer(&REQ_HANDLER->pre_resp_msgbuf_, sizeof(RESP_STRUCT));     \
        CTX->rpc_->enqueue_response(REQ_HANDLER, &REQ_HANDLER->pre_resp_msgbuf_);              \
        CTX->stat_req_error_tot++;                                                             \
        return;                                                                                \
    }

    void alloc_req_handler(erpc::ReqHandle *req_handle, void *_context) {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        ctx->stat_req_rx_tot++;
        ctx->stat_req_alloc_tot++;
        auto *req_msgbuf = req_handle->get_req_msgbuf();

        rt_assert(req_msgbuf->get_data_size() == sizeof(AllocReq), "data size not match");

        AllocReq *req = reinterpret_cast<AllocReq *>(req_msgbuf->buf_);

        // check req validity
        if (!IS_PAGE_ALIGN(req->size)) {
            RETURN_IF_ERROR(EINVAL, AllocResp, ctx, req_handle, req->req)
        }
        // TODO check flags

        rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

        mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];

        unsigned long addr = mm->insert_range(req->size, req->vm_flags);

        new (req_handle->pre_resp_msgbuf_.buf_) AllocResp(req->req.type, req->req.req_number, 0, addr);
        ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(AllocResp));
        ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);

        return;
    }
    void free_req_handler(erpc::ReqHandle *req_handle, void *_context) {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        ctx->stat_req_rx_tot++;
        ctx->stat_req_free_tot++;
        auto *req_msgbuf = req_handle->get_req_msgbuf();
        rt_assert(req_msgbuf->get_data_size() == sizeof(FreeReq), "data size not match");

        FreeReq *req = reinterpret_cast<FreeReq *>(req_msgbuf->buf_);

        // check req validity
        //
        if (!IS_PAGE_ALIGN(req->raddr) || !IS_PAGE_ALIGN(req->rsize)) {
            RETURN_IF_ERROR(EINVAL, FreeResp, ctx, req_handle, req->req)
        }

        rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

        mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];

        // we need delete information in vma_list/fork_list before modify addr_map
        int res = mm->free_vma_list(req->raddr, req->rsize);
        if (unlikely(res != 0)) {
            RMEM_WARN("free_vma_list failed, errno: %d, addr %ld, size %ld", res, req->raddr, req->rsize);
            RETURN_IF_ERROR(res, FreeResp, ctx, req_handle, req->req)
        }

        new (req_handle->pre_resp_msgbuf_.buf_) FreeResp(req->req.type, req->req.req_number, 0);
        ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(FreeResp));
        ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);

        return;
    }
    void read_req_handler(erpc::ReqHandle *req_handle, void *_context) {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        ctx->stat_req_rx_tot++;
        ctx->stat_req_read_tot++;
        auto *req_msgbuf = req_handle->get_req_msgbuf();
        rt_assert(req_msgbuf->get_data_size() == sizeof(ReadReq), "data size not match");

        ReadReq *req = reinterpret_cast<ReadReq *>(req_msgbuf->buf_);
        rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

        mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];

        auto vma = mm->find_vma_range(req->raddr, req->rsize);

        if (!vma) {
            RETURN_IF_ERROR(EINVAL, ReadResp, ctx, req_handle, req->req)
        }

        if ((vma->vm_flags & VM_FLAG_READ) == 0) {
            RETURN_IF_ERROR(EACCES, ReadResp, ctx, req_handle, req->req)
        }

        erpc::MsgBuffer &resp_msgbuf = req_handle->dyn_resp_msgbuf_;
        resp_msgbuf = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(ReadResp) + sizeof(char) * req->rsize);

        // check whether read success
        if (!mm->do_read(vma, req->raddr, req->rsize, resp_msgbuf.buf_ + sizeof(ReadResp))) {
            ctx->rpc_->free_msg_buffer(resp_msgbuf);
            RETURN_IF_ERROR(EFAULT, ReadResp, ctx, req_handle, req->req)
        }

        new (resp_msgbuf.buf_) ReadResp(req->req.type, req->req.req_number, 0, req->recv_buf, req->rsize);

        ctx->rpc_->resize_msg_buffer(&resp_msgbuf, sizeof(ReadResp) + sizeof(char) * req->rsize);
        ctx->rpc_->enqueue_response(req_handle, &resp_msgbuf);
    }
    void write_req_handler(erpc::ReqHandle *req_handle, void *_context) {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        ctx->stat_req_rx_tot++;
        ctx->stat_req_write_tot++;
        auto *req_msgbuf = req_handle->get_req_msgbuf();

        WriteReq *req = reinterpret_cast<WriteReq *>(req_msgbuf->buf_);

        rt_assert(req_msgbuf->get_data_size() == sizeof(WriteReq) + req->rsize, "data size not match");

        rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

        mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];

        auto vma = mm->find_vma_range(req->raddr, req->rsize);

        if (!vma) {
            RETURN_IF_ERROR(EINVAL, WriteResp, ctx, req_handle, req->req)
        }

        if ((vma->vm_flags & VM_FLAG_WRITE) == 0) {
            RETURN_IF_ERROR(EACCES, WriteResp, ctx, req_handle, req->req)
        }
        if (!mm->do_write(vma, req->raddr, req->rsize, req_msgbuf->buf_ + sizeof(WriteReq))) {
            RETURN_IF_ERROR(EFAULT, WriteResp, ctx, req_handle, req->req)
        }

        new (req_handle->pre_resp_msgbuf_.buf_) WriteResp(req->req.type, req->req.req_number, 0);
        ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(WriteResp));
        ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
    }
    // 这个fork_req_handler 用于 fork_speed_rmem 测试， 同时需要配合mm_struct::do_fork
    // void fork_req_handler(erpc::ReqHandle *req_handle, void *_context) {
    //     Timer timer;
    //     timer.tic();
    //     ServerContext *ctx = static_cast<ServerContext *>(_context);
    //     ctx->stat_req_rx_tot++;
    //     ctx->stat_req_fork_tot++;

    //     for (size_t i = 0; i < 32; i++) {
    //         auto *req_msgbuf = req_handle->get_req_msgbuf();
    //         rt_assert(req_msgbuf->get_data_size() == sizeof(ForkReq), "data size not match");

    //         auto *req = reinterpret_cast<ForkReq *>(req_msgbuf->buf_);
    //         rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

    //         mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];

    //         // check req validity
    //         if (!IS_PAGE_ALIGN(req->rsize) || !IS_PAGE_ALIGN(req->raddr)) {
    //             RETURN_IF_ERROR(EINVAL, ForkResp, ctx, req_handle, req->req)
    //         }

    //         auto vma = mm->find_vma_range(req->raddr, req->rsize);

    //         if (!vma) {
    //             RETURN_IF_ERROR(EINVAL, ForkResp, ctx, req_handle, req->req)
    //         }
    //         unsigned long new_addr = mm->do_fork(vma, req->raddr, req->rsize);

    //         new (req_handle->pre_resp_msgbuf_.buf_) ForkResp(req->req.type, req->req.req_number, 0, new_addr);
    //         ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(ForkResp));
    //     }
    //     hdr_record_value_atomic(latency_hist_,
    //         static_cast<int64_t>(timer.toc() * 10));

    //     ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
    // }

    void fork_req_handler(erpc::ReqHandle *req_handle, void *_context) {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        ctx->stat_req_rx_tot++;
        ctx->stat_req_fork_tot++;
        auto *req_msgbuf = req_handle->get_req_msgbuf();
        rt_assert(req_msgbuf->get_data_size() == sizeof(ForkReq), "data size not match");

        auto *req = reinterpret_cast<ForkReq *>(req_msgbuf->buf_);
        rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

        mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];

        // check req validity
        if (!IS_PAGE_ALIGN(req->rsize) || !IS_PAGE_ALIGN(req->raddr)) {
            RETURN_IF_ERROR(EINVAL, ForkResp, ctx, req_handle, req->req)
        }

        auto vma = mm->find_vma_range(req->raddr, req->rsize);

        if (!vma) {
            RETURN_IF_ERROR(EINVAL, ForkResp, ctx, req_handle, req->req)
        }
        unsigned long new_addr = mm->do_fork(vma, req->raddr, req->rsize);

        new (req_handle->pre_resp_msgbuf_.buf_) ForkResp(req->req.type, req->req.req_number, 0, new_addr);
        ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(ForkResp));
        ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
    }

    void join_req_handler(erpc::ReqHandle *req_handle, void *_context) {
        // 这里不妨假设不会出现thread的并发冲突，只会出现session的并发冲突，否则性能还挺可惜的
        // 后面有时间肯定写，得多加几个spin lock
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        ctx->stat_req_rx_tot++;
        ctx->stat_req_join_tot++;
        auto *req_msgbuf = req_handle->get_req_msgbuf();
        rt_assert(req_msgbuf->get_data_size() == sizeof(JoinReq), "data size not match");

        auto *req = reinterpret_cast<JoinReq *>(req_msgbuf->buf_);
        rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

        mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];

        mm_struct *target_mm = ctx->find_target_mm(req->thread_id, req->session_id);
        if (!target_mm || mm == target_mm) {
            RETURN_IF_ERROR(EINVAL, JoinResp, ctx, req_handle, req->req)
        }

        unsigned long new_addr = mm->do_join(target_mm, req->raddr);
        if (unlikely(new_addr == UINT64_MAX)) {
            RETURN_IF_ERROR(EINVAL, JoinResp, ctx, req_handle, req->req)
        }

        new (req_handle->pre_resp_msgbuf_.buf_) JoinResp(req->req.type, req->req.req_number, 0, new_addr);
        ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(JoinResp));
        ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
    }

    void basic_sm_handler(int session_num, int remote_session_num, erpc::SmEventType sm_event_type,
        erpc::SmErrType sm_err_type, void *_context) {
        _unused(remote_session_num);
        auto *ctx = static_cast<ServerContext *>(_context);
        ctx->num_sm_resps_++;

        switch (sm_event_type) {
        case erpc::SmEventType::kConnected:
        {

            RMEM_INFO("Connect connected %d.\n", session_num);
            // TODO add timeout handler
            rt_assert(sm_err_type == erpc::SmErrType::kNoError);
            rt_assert(ctx->mm_struct_map_.count(session_num) == 0, "mm_struct_map_ already has this session_num");

            ctx->mm_struct_map_[session_num] = new mm_struct(ctx->thread_id_, session_num);
            break;
        }
        case erpc::SmEventType::kConnectFailed:
        {
            RMEM_WARN("Connect Error %s.\n",
                sm_err_type_str(sm_err_type).c_str());

            if (ctx->mm_struct_map_.count(session_num) != 0) {
                delete ctx->mm_struct_map_[session_num];
                ctx->mm_struct_map_.erase(session_num);
            }
            break;
        }
        case erpc::SmEventType::kDisconnected:
        {
            RMEM_INFO("Connect disconnected %d.\n", session_num);
            rt_assert(sm_err_type == erpc::SmErrType::kNoError);

            rt_assert(ctx->mm_struct_map_.count(session_num) == 1, "mm_struct_map_ does not have this session_num");

            delete ctx->mm_struct_map_[session_num];
            ctx->mm_struct_map_.erase(session_num);

            break;
        }
        case erpc::SmEventType::kDisconnectFailed:
        {
            RMEM_WARN("Connect disconnected Error %s.\n",
                sm_err_type_str(sm_err_type).c_str());
            rt_assert(false, "always failed when disconnect failed");
            break;
        }
        }
    }
}