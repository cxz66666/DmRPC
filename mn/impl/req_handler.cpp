#include "req_handler.h"
#include "rpc_type.h"
#include "page.h"
#include "server_extern.h"
namespace rmem
{
    unsigned long mm_struct::get_unmapped_area(size_t length)
    {
        unsigned long begin = 0;
        auto tmp = vma_list.begin();
        while (1)
        {
            int conflict = 0;
            while (tmp != vma_list.end())
            {
                if (begin < (*tmp)->vm_end && (*tmp)->vm_start < begin + length)
                {
                    conflict = 1;
                    begin = (*tmp)->vm_end;
                    break;
                }
                if ((*tmp)->vm_start >= begin + length)
                {
                    break;
                }
                tmp++;
            }
            if (!conflict)
            {
                break;
            }
        }

        return begin;
    }

    unsigned long mm_struct::insert_range(size_t size, unsigned long vm_flags)
    {
        unsigned long begin = get_unmapped_area(size);
        unsigned long end = begin + size;
        vma_struct *vma = new vma_struct();
        vma->vm_start = begin;
        vma->vm_end = end;
        vma->vm_flags = vm_flags;

        vma_list.insert(std::lower_bound(vma_list.begin(), vma_list.end(), vma, [](const vma_struct *a, const vma_struct *b)
                                         { return a->vm_start < b->vm_start; }),
                        vma);

        return begin;
    }

    std::list<vma_struct *>::iterator mm_struct::find_vma(unsigned long addr, size_t size)
    {
        auto tmp = vma_list.begin();
        while (tmp != vma_list.end())
        {
            if ((*tmp)->vm_start == addr && addr + size == (*tmp)->vm_end)
            {
                return tmp;
            }
            if ((*tmp)->vm_end >= addr + size)
            {
                break;
            }
            tmp++;
        }
        // use end() to indicate not found
        return vma_list.end();
    }

    vma_struct *mm_struct::find_vma_range(unsigned long addr, size_t size)
    {
        auto tmp = vma_list.begin();
        while (tmp != vma_list.end())
        {
            if ((*tmp)->vm_start <= addr && addr + size <= (*tmp)->vm_end)
            {
                return *tmp;
            }
            if ((*tmp)->vm_end >= addr + size)
            {
                break;
            }
            tmp++;
        }
        // use end() to indicate not found
        return nullptr;
    }

    inline void mm_struct::handler_page_map(vma_struct *vma, unsigned long vm_pfn)
    {
        if (unlikely(addr_map.count(vm_pfn)))
        {
            // already mapped
            RMEM_WARN("already mapped");
            return;
        }
        else
        {
            // not mapped
            // TODO when no necessary memory, it will trap in spin lock
            size_t phy_no = g_free_pages->pop();
            addr_map[vm_pfn] = phy_no;
            // TODO add to page table
        }
    }

#define RETURN_IF_ERROR(ERRNO, RESP_STRUCT, CTX, REQ_HANDLER, REQ)                         \
    {                                                                                      \
        RESP_STRUCT resp(REQ.type, REQ.req_number, ERRNO);                                 \
        memcpy(REQ_HANDLER->pre_resp_msgbuf_.buf_, &resp, sizeof(RESP_STRUCT));            \
        CTX->rpc_->resize_msg_buffer(&REQ_HANDLER->pre_resp_msgbuf_, sizeof(RESP_STRUCT)); \
        CTX->rpc_->enqueue_response(REQ_HANDLER, &REQ_HANDLER->pre_resp_msgbuf_);          \
        return;                                                                            \
    }

    void alloc_req_handler(erpc::ReqHandle *req_handle, void *_context)
    {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        auto *req_msgbuf = req_handle->get_req_msgbuf();

        rt_assert(req_msgbuf->get_data_size() == sizeof(AllocReq), "data size not match");

        AllocReq *req = reinterpret_cast<AllocReq *>(req_msgbuf->buf_);

        // check req validity
        if (!IS_PAGE_ALIGIN(req->size))
        {
            RETURN_IF_ERROR(EINVAL, AllocResp, ctx, req_handle, req->req);
        }
        // TODO check flags

        rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

        mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];

        unsigned long addr = mm->insert_range(req->size, req->vm_flags);

        AllocResp resp(req->req.type, req->req.req_number, 0, addr);
        memcpy(req_handle->pre_resp_msgbuf_.buf_, &resp, sizeof(AllocResp));
        ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(AllocResp));
        ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
    }
    void free_req_handler(erpc::ReqHandle *req_handle, void *_context)
    {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        auto *req_msgbuf = req_handle->get_req_msgbuf();
        rt_assert(req_msgbuf->get_data_size() == sizeof(FreeReq), "data size not match");

        FreeReq *req = reinterpret_cast<FreeReq *>(req_msgbuf->buf_);

        // check req validity
        //
        if (!IS_PAGE_ALIGIN(req->raddr) || !IS_PAGE_ALIGIN(req->rsize))
        {
            RETURN_IF_ERROR(EINVAL, FreeResp, ctx, req_handle, req->req);
        }

        rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

        mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];

        auto vma = mm->find_vma(req->raddr, req->rsize);

        if (vma == mm->vma_list.end())
        {
            RETURN_IF_ERROR(EINVAL, FreeResp, ctx, req_handle, req->req);
        }

        // TODO decrease ref_count in this range
        // TODO!!!
        // TODO remote map from mm_struct_map

        mm->vma_list.erase(vma);
        // don't forget delete vma_struct
        delete *vma;
    }
    void read_req_handler(erpc::ReqHandle *req_handle, void *_context)
    {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
    }
    void write_req_handler(erpc::ReqHandle *req_handle, void *_context)
    {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
    }
    void fork_req_handler(erpc::ReqHandle *req_handle, void *_context)
    {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
    }
    void basic_sm_handler(int session_num, erpc::SmEventType sm_event_type,
                          erpc::SmErrType sm_err_type, void *_context)
    {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        ctx->num_sm_resps_++;

        switch (sm_event_type)
        {
        case erpc::SmEventType::kConnected:
        {

            RMEM_INFO("Connect connected %d.\n", session_num);
            // TODO add timeout handler
            rt_assert(sm_err_type == erpc::SmErrType::kNoError);
            rt_assert(ctx->mm_struct_map_.count(session_num) == 0, "mm_struct_map_ already has this session_num");

            ctx->mm_struct_map_[session_num] = new mm_struct();
            break;
        }
        case erpc::SmEventType::kConnectFailed:
        {
            RMEM_WARN("Connect Error %s.\n",
                      sm_err_type_str(sm_err_type).c_str());

            if (ctx->mm_struct_map_.count(session_num) != 0)
            {
                // TODO deference ref_count and free memory when necessary

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

            // TODO deference ref_count and free memory

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