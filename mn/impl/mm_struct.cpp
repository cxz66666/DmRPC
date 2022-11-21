#include "mm_struct.h"
#include "page.h"
#include "server_extern.h"
namespace rmem
{
    mm_struct::mm_struct(uint16_t tid, uint16_t sid)
    {
        this->thread_id = tid;
        this->session_id = sid;
        this->vma_list.clear();
    }
    mm_struct::~mm_struct()
    {
        free_all_vma_list();
        rt_assert(vma_list.empty(), "not empty vma_list");
        rt_assert(fork_list.empty(), "not empty fork_list");
        rt_assert(addr_map.empty(), "not empty addr_map");
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

    unsigned long mm_struct::insert_range(size_t size, unsigned long vm_flags)
    {
        vma_list_lock.lock();
        unsigned long begin = get_unmapped_area(size);
        unsigned long end = begin + size;
        vma_struct *vma = new vma_struct();
        vma->vm_start = begin;
        vma->vm_end = end;
        vma->vm_flags = vm_flags;

        vma_list.insert(std::lower_bound(vma_list.begin(), vma_list.end(), vma,
                                         [](const vma_struct *a, const vma_struct *b)
                                         {
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

    std::list<fork_struct *>::iterator mm_struct::find_fork_vma(unsigned long addr)
    {
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

        size_t vfn_start = PHYS_2_PFN(PAGE_ROUND_DOWN(addr)), vfn_end = PHYS_2_PFN(PAGE_ROUND_UP(addr + size));
        for (size_t vfn = vfn_start; vfn < vfn_end; vfn++)
        {
            if (!addr_map.count(vfn))
            {
                handler_page_map(vma, vfn);
            }
            unsigned long pfn = addr_map[vfn];
            size_t copy_size = min_(PAGE_SIZE - (addr_index % PAGE_SIZE), size - buf_index);
            if (unlikely(!g_page_tables[pfn].do_page_read(pfn, static_cast<char *>(buf) + buf_index, copy_size, addr_index % PAGE_SIZE, thread_id, session_id)))
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

        size_t vfn_start = PHYS_2_PFN(PAGE_ROUND_DOWN(addr)), vfn_end = PHYS_2_PFN(PAGE_ROUND_UP(addr + size));
        for (size_t vfn = vfn_start; vfn < vfn_end; vfn++)
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
            else
            {
                g_page_tables[pfn].lock.unlock();
            }

            if (unlikely(!g_page_tables[pfn].do_page_write(pfn, static_cast<char *>(buf) + buf_index, copy_size, addr_index % PAGE_SIZE, thread_id, session_id)))
            {
                RMEM_WARN("do_write failed, addr: %lx, size: %lx, buf: %p, buf_index: %lx, addr_index: %lx, vfn: %lx, pfn: %lx, copy_size: %lx, thread_id: %d, session_id: %d", addr, size, buf, buf_index, addr_index, vfn, pfn, copy_size, thread_id, session_id);
                return false;
            }
            buf_index += copy_size;
            addr_index += copy_size;
        }
        return true;
    }
    void mm_struct::do_free(vma_struct *vma)
    {
        size_t vfn_start = PHYS_2_PFN(PAGE_ROUND_DOWN(vma->vm_start)), vfn_end = PHYS_2_PFN(PAGE_ROUND_UP(vma->vm_end));

        // TODO: it will faster if we loop from addr_map?
        for (size_t vfn = vfn_start; vfn < vfn_end; vfn++)
        {
            if (addr_map.count(vfn))
            {
                unsigned long pfn = addr_map[vfn];
                addr_map.erase(vfn);

                g_page_tables[pfn].lock.lock();
                rt_assert(g_page_tables[pfn].valid, "page is not valid");
                g_page_tables[pfn].ref_count--;
                // free physical page
                if (!g_page_tables[pfn].ref_count)
                {
                    g_page_tables[pfn].valid = false;
                    g_free_pages->push(pfn);
                }
                g_page_tables[pfn].lock.unlock();
            }
        }
    }

    unsigned long mm_struct::do_fork(vma_struct *vma, unsigned long addr, size_t size)
    {
        unsigned long new_addr = insert_range(size, vma->vm_flags);

        auto *fs = new fork_struct();
        fs->vm_start = new_addr;
        fs->vm_end = new_addr + size;
        fs->vm_flags = vma->vm_flags;

        size_t offset = new_addr < addr ? PHYS_2_PFN(addr - new_addr) : PHYS_2_PFN(new_addr - addr);
        bool flag = new_addr < addr;

        size_t vfn_start = PHYS_2_PFN(PAGE_ROUND_DOWN(addr)), vfn_end = PHYS_2_PFN(PAGE_ROUND_UP(addr + size));

        for (size_t vfn = vfn_start; vfn < vfn_end; vfn++)
        {
            if (addr_map.count(vfn))
            {
                unsigned long pfn = addr_map[vfn];
                size_t new_vfn = (flag ? vfn - offset : vfn + offset);
                fs->addr_map[new_vfn] = pfn;

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
                                          [](const fork_struct *a, const fork_struct *b)
                                          {
                                              return a->vm_start < b->vm_start;
                                          }),
                         fs);
        vma_list_lock.unlock();

        return new_addr;
    }

    unsigned long mm_struct::do_join(mm_struct *target_mm, unsigned long addr)
    {
        // this lock
        target_mm->vma_list_lock.lock();

        auto old_fork_vma = target_mm->find_fork_vma(addr);
        size_t size = (*old_fork_vma)->vm_end - (*old_fork_vma)->vm_start;

        auto old_vma = target_mm->find_vma(addr, size);
        if (old_fork_vma == target_mm->fork_list.end() || old_vma == target_mm->vma_list.end())
        {
            RMEM_WARN("do_join failed, addr: %lx, size: %lx, thread_id: %d, session_id: %d", addr, size, thread_id, session_id);
            target_mm->vma_list_lock.unlock();
            return UINT64_MAX;
        }

        // don't forget to delete old vma and fork_vma
        target_mm->vma_list.erase(old_vma);
        target_mm->fork_list.erase(old_fork_vma);

        auto *old_fork_vma_ptr = *old_fork_vma;
        auto *old_vma_ptr = *old_vma;
        target_mm->vma_list_lock.unlock();

        // already insert new vma to vma_list, so we just need adjust addr_map
        unsigned long new_addr = insert_range(size, old_fork_vma_ptr->vm_flags);

        size_t offset =
            new_addr < addr ? PHYS_2_PFN(addr - new_addr) : PHYS_2_PFN(new_addr - addr);
        bool flag = new_addr < addr;

        for (auto &[vfn, v] : old_fork_vma_ptr->addr_map)
        {
            addr_map[flag ? vfn - offset : vfn + offset] = v;
        }

        delete old_fork_vma_ptr;
        delete old_vma_ptr;
        return new_addr;
    }

    void mm_struct::handler_page_map(vma_struct *vma, unsigned long vm_pfn)
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

    int mm_struct::free_vma_list(unsigned long addr, size_t size, bool locked)
    {
        if (locked)
        {
            vma_list_lock.lock();
        }

        auto vma = find_vma(addr, size);
        if (vma == vma_list.end())
        {
            // illegal addr!
            if (locked)
            {
                vma_list_lock.unlock();
            }
            return EINVAL;
        }

        // get the ptr
        vma_struct *vma_ptr = *vma;
        // delete vma from vma_list and fork_list(it will exist when this vma is forked)
        vma_list.erase(vma);
        auto fork_vma = find_fork_vma(vma_ptr->vm_start);
        if (unlikely((fork_vma != fork_list.end())))
        {
            delete *fork_vma;
            fork_list.erase(fork_vma);
        }
        if (locked)
        {
            vma_list_lock.unlock();
        }

        // reduce ref_count and remove addr_map entry
        do_free(vma_ptr);

        // don't forget delete vma_struct
        delete vma_ptr;

        return 0;
    }

    void mm_struct::free_all_vma_list()
    {
        // TODO
        vma_list_lock.lock();
        for (auto vma : vma_list)
        {
            do_free(vma);
            delete vma;
        }
        vma_list.clear();

        for (auto vma : fork_list)
        {
            delete vma;
        }
        fork_list.clear();
        vma_list_lock.unlock();
    }

}