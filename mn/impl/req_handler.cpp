#include "req_handler.h"
#include "rpc_type.h"
#include "page.h"
#include "server_extern.h"
#include "gflag_configs.h"
#include <cmath>

namespace rmem
{
    inline mm_struct* ServerContext::find_target_mm(uint16_t tid, uint16_t sid) {
        if(unlikely(tid>=FLAGS_rmem_server_thread)) {
            RMEM_WARN("tid > FLAGS_rmem_server_thread");
            return nullptr;
        }
        if(unlikely(!g_server_context)) {
            RMEM_WARN("g_server_context is nullptr!");
            return nullptr;
        }
        ServerContext*tmp=g_server_context+tid;

        if(unlikely(!tmp->mm_struct_map_.count(sid))) {
            RMEM_WARN("sid not found in mm_struct_map_");
            return nullptr;
        }
        return tmp->mm_struct_map_[sid];
    }

    mm_struct::mm_struct(uint16_t tid, uint16_t sid)
    {
        this->thread_id = tid;
        this->session_id = sid;
        this->vma_list.clear();
    }
    mm_struct::~mm_struct() {
        rt_assert(vma_list.empty(), "not empty vma_list");
        rt_assert(fork_list.empty(), "not empty fork_list");
    }
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

    unsigned long mm_struct::insert_range(size_t size, unsigned long vm_flags) {
        vma_list_lock.lock();
        unsigned long begin = get_unmapped_area(size);
        unsigned long end = begin + size;
        vma_struct *vma = new vma_struct();
        vma->vm_start = begin;
        vma->vm_end = end;
        vma->vm_flags = vm_flags;

        vma_list.insert(std::lower_bound(vma_list.begin(), vma_list.end(), vma,
                                         [](const vma_struct *a, const vma_struct *b) {
                                             return a->vm_start < b->vm_start;
                                         }),
                        vma);

        vma_list_lock.unlock();

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

    std::list<fork_struct *>::iterator mm_struct::find_fork_vma(unsigned long addr) {
        auto tmp = fork_list.begin();
        while (tmp != fork_list.end())
        {
            if ((*tmp)->vm_start == addr)
            {
                return tmp;
            }
            if ((*tmp)->vm_end >= addr)
            {
                break;
            }
            tmp++;
        }
        // use end() to indicate not found
        return fork_list.end();

    }


    vma_struct *mm_struct::find_vma_range(unsigned long addr, size_t size)
    {
        vma_list_lock.lock();
        auto tmp = vma_list.begin();
        while (tmp != vma_list.end())
        {
            if ((*tmp)->vm_start <= addr && addr + size <= (*tmp)->vm_end)
            {
                vma_list_lock.unlock();
                return *tmp;
            }
            if ((*tmp)->vm_end >= addr + size)
            {
                break;
            }
            tmp++;
        }
        // use nullptr to indicate not found
        vma_list_lock.unlock();
        return nullptr;
    }

    bool mm_struct::do_read(vma_struct *vma, unsigned long addr, size_t size, void *buf)
    {
        size_t buf_index = 0;
        size_t addr_index = addr;
        for (size_t vfn = PHYS_2_PFN(PAGE_ROUND_DOWN(addr)); vfn < PHYS_2_PFN(PAGE_ROUND_UP(addr + size)); vfn++)
        {
            if (!addr_map.count(vfn))
            {
                handler_page_map(vma, vfn);
            }
            unsigned long pfn = addr_map[vfn];
            size_t copy_size = min_(PAGE_SIZE - (addr_index % PAGE_SIZE), size - buf_index);
            if (unlikely(!g_page_tables[pfn].do_page_read(pfn, static_cast<char*>(buf) + buf_index, copy_size, addr_index % PAGE_SIZE, thread_id, session_id)))
            {
                RMEM_WARN("do_read failed, addr: %lx, size: %lx, buf: %p, buf_index: %lx, addr_index: %lx, vfn: %lx, pfn: %lx, copy_size: %lx, thread_id: %d, session_id: %d", addr, size, buf, buf_index, addr_index, vfn, pfn, copy_size, thread_id, session_id);
                return false;
            }
            buf_index += copy_size;
            addr_index += copy_size;
        }
        return true;
    }
    // TODO 饿的时候写的，得再看一遍
    bool mm_struct::do_write(vma_struct *vma, unsigned long addr, size_t size, void *buf)
    {
        size_t buf_index = 0;
        size_t addr_index = addr;
        for (size_t vfn = PHYS_2_PFN(PAGE_ROUND_DOWN(addr)); vfn < PHYS_2_PFN(PAGE_ROUND_UP(addr + size)); vfn++)
        {
            if (!addr_map.count(vfn))
            {
                handler_page_map(vma, vfn);
            }

            unsigned long pfn = addr_map[vfn];
            size_t copy_size = min_(PAGE_SIZE - (addr_index % PAGE_SIZE), size - buf_index);

            g_page_tables[pfn].lock.lock();

            rt_assert(g_page_tables[pfn].valid, "page is not valid");

            // do copy on write
            if (unlikely(!g_page_tables[pfn].w))
            {
                if (unlikely(!g_page_tables[pfn].cow))
                {
                    g_page_tables[pfn].lock.unlock();
                    RMEM_WARN("do_write failed, without cow flag, addr: %lx, size: %lx, buf: %p, buf_index: %lx, addr_index: %lx, vfn: %lx, pfn: %lx, thread_id: %d, session_id: %d", addr, size, buf, buf_index, addr_index, vfn, pfn, thread_id, session_id);
                    return false;
                }

                if (g_page_tables[pfn].ref_count == 1)
                {
                    g_page_tables[pfn].w = true;
                    g_page_tables[pfn].cow = false;
                    // TODO need it?
                    g_page_tables[pfn].access_mode = 0;
                    g_page_tables[pfn].thread_id = thread_id;
                    g_page_tables[pfn].session_id = session_id;

                    g_page_tables[pfn].lock.unlock();
                }
                else
                {
                    // TODO need try_pop or pop?
                    unsigned long new_pfn = g_free_pages->pop();
                    if (new_pfn == static_cast<unsigned long>(-1))
                    {
                        RMEM_WARN("do_write failed, no free page, addr: %lx, size: %lx, buf: %p, buf_index: %lx, addr_index: %lx, vfn: %lx, pfn: %lx, thread_id: %d, session_id: %d", addr, size, buf, buf_index, addr_index, vfn, pfn, thread_id, session_id);
                        g_page_tables[pfn].lock.unlock();
                        return false;
                    }
                    g_page_tables[new_pfn].valid = true;
                    g_page_tables[new_pfn].r = vma->vm_flags & VM_FLAG_READ;
                    g_page_tables[new_pfn].w = vma->vm_flags & VM_FLAG_WRITE;
                    g_page_tables[new_pfn].cow = false;
                    g_page_tables[new_pfn].access_mode = 0;
                    g_page_tables[new_pfn].ref_count = 1;
                    g_page_tables[new_pfn].session_id = session_id;
                    g_page_tables[new_pfn].thread_id = thread_id;

                    g_page_tables[pfn].ref_count--;
                    g_page_tables[pfn].lock.unlock();

                    addr_map[vfn] = new_pfn;
                    pfn = new_pfn;

                    if (copy_size != PAGE_SIZE)
                    {
                        // if don't copy PAGE_SIZE, need copy old data
                        // TODO can much faster? 2MB copy can use more tricks!

                        memcpy(g_pages[new_pfn].data, g_pages[pfn].data, PAGE_SIZE);
                    }
                }
            }

            if (unlikely(!g_page_tables[pfn].do_page_write(pfn, static_cast<char*>(buf) + buf_index, copy_size, addr_index % PAGE_SIZE, thread_id, session_id)))
            {
                RMEM_WARN("do_write failed, addr: %lx, size: %lx, buf: %p, buf_index: %lx, addr_index: %lx, vfn: %lx, pfn: %lx, copy_size: %lx, thread_id: %d, session_id: %d", addr, size, buf, buf_index, addr_index, vfn, pfn, copy_size, thread_id, session_id);
                return false;
            }
            buf_index += copy_size;
            addr_index += copy_size;
        }
        return true;
    }
    inline void mm_struct::do_free(vma_struct *vma)
    {
        // TODO: it will faster if we loop from addr_map?
        for (size_t vfn = PHYS_2_PFN(PAGE_ROUND_DOWN(vma->vm_start)); vfn < PHYS_2_PFN(PAGE_ROUND_UP(vma->vm_end)); vfn++) {
            if(addr_map.count(vfn)) {
                unsigned long pfn = addr_map[vfn];
                addr_map.erase(vfn);

                g_page_tables[pfn].lock.lock();
                rt_assert(g_page_tables[pfn].valid, "page is not valid");
                g_page_tables[pfn].ref_count--;
                // free physical page
                if(!g_page_tables[pfn].ref_count) {
                    g_page_tables[pfn].valid=false;
                    g_free_pages->push(pfn);
                }
                g_page_tables[pfn].lock.unlock();

            }
        }
    }


    inline unsigned long mm_struct::do_fork(vma_struct *vma, unsigned long addr, size_t size)
    {
        unsigned long new_addr = insert_range(size,vma->vm_flags);

        auto *fs=new fork_struct();
        fs->vm_start=new_addr;
        fs->vm_end=new_addr+size;
        fs->vm_flags=vma->vm_flags;

        size_t offset=new_addr<addr?PHYS_2_PFN(addr-new_addr):PHYS_2_PFN(new_addr-addr);
        bool flag= new_addr < addr;
        for (size_t vfn = PHYS_2_PFN(PAGE_ROUND_DOWN(addr)); vfn < PHYS_2_PFN(PAGE_ROUND_UP(addr + size)); vfn++) {
            if(addr_map.count(vfn)) {
                unsigned long pfn=addr_map[vfn];
                size_t new_vfn= (flag?vfn-offset:vfn+offset);
                fs->addr_map[new_vfn]=pfn;

                if (unlikely(!g_page_tables[pfn].do_page_fork(pfn)))
                {
                    RMEM_ERROR("do_fork failed, addr: %lx, size: %lx, new_addr: %lx, vfn: %lx, pfn: %lx, offset: %lx, thread_id: %d, session_id: %d", addr, size, new_addr, vfn, pfn, offset, thread_id, session_id);
                    exit(-1);
                }

            }
        }

        vma_list_lock.lock();
        // TODO whether need convert it to a function?
        fork_list.insert(std::lower_bound(fork_list.begin(), fork_list.end(), fs,
                                        [](const fork_struct *a, const fork_struct *b) {
                                            return a->vm_start < b->vm_start;
                                        }),
                       fs);
        vma_list_lock.unlock();

        return new_addr;
    }

    inline bool mm_struct::do_join(mm_struct*target_mm, unsigned long addr) {
        target_mm->vma_list_lock.lock();
        auto old_vma = target_mm->find_fork_vma(addr);
        if (old_vma == target_mm->fork_list.end()) {
            target_mm->vma_list_lock.unlock();
            return false;
        }
        target_mm->vma_list_lock.unlock();


        size_t size = (*old_vma)->vm_end - (*old_vma)->vm_start;
        // already insert new vma to vma_list, so we just need adjust addr_map
        unsigned long new_addr = insert_range(size, (*old_vma)->vm_flags);

        size_t offset =
                new_addr < addr ? PHYS_2_PFN(addr - new_addr) : PHYS_2_PFN(new_addr - addr);
        bool flag= new_addr < addr;

        for (auto &[vfn, v]: (*old_vma)->addr_map) {
            addr_map[flag?vfn-offset:vfn+offset] = v;
        }

        // don't forget to delete old vma and fork_vma
        target_mm->vma_list_lock.lock();
        target_mm->vma_list.erase(target_mm->find_vma(addr,size));
        target_mm->fork_list.erase(old_vma);
        target_mm->vma_list_lock.unlock();

        delete *old_vma;
        return true;
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

            // don't need to lock it! Because it is only just poped
            g_page_tables[phy_no].valid = true;
            g_page_tables[phy_no].r = vma->vm_flags & VM_FLAG_READ;
            g_page_tables[phy_no].w = vma->vm_flags & VM_FLAG_WRITE;
            g_page_tables[phy_no].cow = false;
            g_page_tables[phy_no].access_mode = 0;
            g_page_tables[phy_no].ref_count = 1;
            g_page_tables[phy_no].session_id = session_id;
            g_page_tables[phy_no].thread_id = thread_id;
        }
    }



#define RETURN_IF_ERROR(ERRNO, RESP_STRUCT, CTX, REQ_HANDLER, REQ)                         \
    {                                                                                      \
        RESP_STRUCT resp(REQ.type, REQ.req_number, ERRNO);                                 \
        memcpy(REQ_HANDLER->pre_resp_msgbuf_.buf_, &resp, sizeof(RESP_STRUCT));            \
        CTX->rpc_->resize_msg_buffer(&REQ_HANDLER->pre_resp_msgbuf_, sizeof(RESP_STRUCT)); \
        CTX->rpc_->enqueue_response(REQ_HANDLER, &REQ_HANDLER->pre_resp_msgbuf_);          \
        CTX->stat_req_error_tot++;                                                         \
        return;                                                                            \
    }

    void alloc_req_handler(erpc::ReqHandle *req_handle, void *_context)
    {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        ctx->stat_req_rx_tot++;
        ctx->stat_req_alloc_tot++;
        auto *req_msgbuf = req_handle->get_req_msgbuf();

        rt_assert(req_msgbuf->get_data_size() == sizeof(AllocReq), "data size not match");

        AllocReq *req = reinterpret_cast<AllocReq *>(req_msgbuf->buf_);

        // check req validity
        if (!IS_PAGE_ALIGN(req->size))
        {
            RETURN_IF_ERROR(EINVAL, AllocResp, ctx, req_handle, req->req)
        }
        // TODO check flags

        rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

        mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];

        unsigned long addr = mm->insert_range(req->size, req->vm_flags);

        AllocResp resp(req->req.type, req->req.req_number, 0, addr);
        memcpy(req_handle->pre_resp_msgbuf_.buf_, &resp, sizeof(AllocResp));
        ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(AllocResp));
        ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);

        return;
    }
    void free_req_handler(erpc::ReqHandle *req_handle, void *_context)
    {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        ctx->stat_req_rx_tot++;
        ctx->stat_req_free_tot++;
        auto *req_msgbuf = req_handle->get_req_msgbuf();
        rt_assert(req_msgbuf->get_data_size() == sizeof(FreeReq), "data size not match");

        FreeReq *req = reinterpret_cast<FreeReq *>(req_msgbuf->buf_);

        // check req validity
        //
        if (!IS_PAGE_ALIGN(req->raddr) || !IS_PAGE_ALIGN(req->rsize))
        {
            RETURN_IF_ERROR(EINVAL, FreeResp, ctx, req_handle, req->req)
        }

        rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

        mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];

        // we need delete information in vma_list/fork_list before modify addr_map
        mm->vma_list_lock.lock();

        auto vma = mm->find_vma(req->raddr, req->rsize);
        if (vma == mm->vma_list.end())
        {
            // illegal addr!
            mm->vma_list_lock.unlock();
            RETURN_IF_ERROR(EINVAL, FreeResp, ctx, req_handle, req->req)
        }

        // get the ptr
        vma_struct* vma_ptr = *vma;
        // delete vma from vma_list and fork_list(it will exist when this vma is forked)
        mm->vma_list.erase(vma);
        auto fork_vma= mm->find_fork_vma(vma_ptr->vm_start);
        if(unlikely(( fork_vma != mm->fork_list.end()))) {
            mm->fork_list.erase(fork_vma);
        }
        mm->vma_list_lock.unlock();

        // reduce ref_count and remove addr_map entry
        mm->do_free(vma_ptr);

        // don't forget delete vma_struct
        delete vma_ptr;

        FreeResp resp(req->req.type, req->req.req_number, 0);
        memcpy(req_handle->pre_resp_msgbuf_.buf_, &resp, sizeof(FreeResp));
        ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(FreeResp));
        ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);

        return;
    }
    void read_req_handler(erpc::ReqHandle *req_handle, void *_context)
    {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        ctx->stat_req_rx_tot++;
        ctx->stat_req_read_tot++;
        auto *req_msgbuf = req_handle->get_req_msgbuf();
        rt_assert(req_msgbuf->get_data_size() == sizeof(ReadReq), "data size not match");

        ReadReq *req = reinterpret_cast<ReadReq *>(req_msgbuf->buf_);
        rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

        mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];

        auto vma = mm->find_vma_range(req->raddr, req->rsize);

        if (!vma)
        {
            RETURN_IF_ERROR(EINVAL, ReadResp, ctx, req_handle, req->req)
        }
        // TODO check permission

        if ((vma->vm_flags & VM_FLAG_READ) == 0)
        {
            RETURN_IF_ERROR(EACCES, ReadResp, ctx, req_handle, req->req)
        }

        erpc::MsgBuffer &resp_msgbuf = req_handle->dyn_resp_msgbuf_;
        resp_msgbuf = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(ReadResp) + sizeof(char) * req->rsize);

        // check whether read success
        if (!mm->do_read(vma, req->raddr, req->rsize, resp_msgbuf.buf_ + sizeof(ReadResp)))
        {
            ctx->rpc_->free_msg_buffer(resp_msgbuf);
            RETURN_IF_ERROR(EFAULT, ReadResp, ctx, req_handle, req->req)
        }

        ReadResp resp(req->req.type, req->req.req_number, 0, req->recv_buf, req->rsize);

        memcpy(resp_msgbuf.buf_, &resp, sizeof(ReadResp));

        ctx->rpc_->resize_msg_buffer(&resp_msgbuf, sizeof(ReadResp) + sizeof(char) * req->rsize);
        ctx->rpc_->enqueue_response(req_handle, &resp_msgbuf);
   }
    void write_req_handler(erpc::ReqHandle *req_handle, void *_context)
    {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        ctx->stat_req_rx_tot++;
        ctx->stat_req_write_tot++;
        auto *req_msgbuf = req_handle->get_req_msgbuf();
        WriteReq *req = reinterpret_cast<WriteReq *>(req_msgbuf->buf_);

        rt_assert(req_msgbuf->get_data_size() == sizeof(WriteReq)+ req->rsize, "data size not match");

        rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

        mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];
        auto vma = mm->find_vma_range(req->raddr, req->rsize);

        if (!vma)
        {
            RETURN_IF_ERROR(EINVAL, WriteResp, ctx, req_handle, req->req)
        }

        // TODO check permission whether success

        if ((vma->vm_flags & VM_FLAG_WRITE) == 0)
        {
            RETURN_IF_ERROR(EACCES, WriteResp, ctx, req_handle, req->req)
        }

        if (!mm->do_write(vma, req->raddr, req->rsize, req_msgbuf->buf_ + sizeof(WriteReq)))
        {
            RETURN_IF_ERROR(EFAULT, WriteResp, ctx, req_handle, req->req)
        }

        WriteResp resp(req->req.type, req->req.req_number, 0);
        memcpy(req_handle->pre_resp_msgbuf_.buf_, &resp, sizeof(WriteResp));
        ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(WriteResp));
        ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);

        // TODO check COW success
    }
    void fork_req_handler(erpc::ReqHandle *req_handle, void *_context)
    {
        ServerContext *ctx = static_cast<ServerContext *>(_context);
        ctx->stat_req_rx_tot++;
        ctx->stat_req_fork_tot++;
        auto *req_msgbuf = req_handle->get_req_msgbuf();
        rt_assert(req_msgbuf->get_data_size() == sizeof(ForkReq), "data size not match");

        auto *req = reinterpret_cast<ForkReq *>(req_msgbuf->buf_);
        rt_assert(ctx->mm_struct_map_.count(req_handle->get_server_session_num()), "session not found");

        mm_struct *mm = ctx->mm_struct_map_[req_handle->get_server_session_num()];

        // check req validity
        if (!IS_PAGE_ALIGN(req->rsize) || !IS_PAGE_ALIGN(req->raddr))
        {
            RETURN_IF_ERROR(EINVAL, ForkResp, ctx, req_handle, req->req)
        }

        auto vma = mm->find_vma_range(req->raddr, req->rsize);

        if (!vma)
        {
            RETURN_IF_ERROR(EINVAL, ForkResp, ctx, req_handle, req->req)
        }
        unsigned long new_addr = mm->do_fork(vma, req->raddr, req->rsize);

        ForkResp resp(req->req.type, req->req.req_number, 0, new_addr);
        memcpy(req_handle->pre_resp_msgbuf_.buf_, &resp, sizeof(ForkResp));
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

        mm_struct *target_mm = ctx->find_target_mm(req->thread_id,req->session_id);
        if(!target_mm) {
            RETURN_IF_ERROR(EINVAL, JoinResp, ctx, req_handle, req->req)
        }

        if(!mm->do_join(target_mm, req->raddr)) {
            RETURN_IF_ERROR(EINVAL, JoinResp, ctx, req_handle, req->req)
        }

        JoinResp resp(req->req.type, req->req.req_number, 0);
        memcpy(req_handle->pre_resp_msgbuf_.buf_, &resp, sizeof(JoinResp));
        ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(JoinResp));
        ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);

    }

    void basic_sm_handler(int session_num, erpc::SmEventType sm_event_type,
                          erpc::SmErrType sm_err_type, void *_context)
    {
        auto *ctx = static_cast<ServerContext *>(_context);
        ctx->num_sm_resps_++;

        switch (sm_event_type)
        {
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