#include "page.h"
#include "commons.h"
#include "server_extern.h"

namespace rmem
{
    bool page_table::do_page_read(unsigned long pfn, void *recv_buf, size_t size, size_t offset, uint16_t tid, uint16_t sid)
    {
        if (unlikely(pfn >= g_page_table_size || offset + size >= PAGE_SIZE))
        {
            return false;
        }
        g_page_tables[pfn].lock.lock();
        if (unlikely(g_page_tables[pfn].valid == false || g_page_tables[pfn].r == false))
        {
            g_page_tables[pfn].lock.unlock();
            return false;
        }
        if (unlikely(g_page_tables[pfn].access_mode == 0 && (g_page_tables[pfn].session_id != sid || g_page_tables[pfn].thread_id != tid)))
        {
            g_page_tables[pfn].lock.unlock();
            return false;
        }
        g_page_tables[pfn].lock.unlock();
        // TODO much faster!
        memcpy(recv_buf, g_pages[pfn].data + offset, size);
        return true;
    }

    bool page_table::do_page_write(unsigned long pfn, void *recv_buf, size_t size, size_t offset, uint16_t tid, uint16_t sid)
    {
        if (unlikely(pfn >= g_page_table_size || offset + size >= PAGE_SIZE))
        {
            return false;
        }

        g_page_tables[pfn].lock.lock();
        if (unlikely(g_page_tables[pfn].valid == false || g_page_tables[pfn].w == false))
        {
            g_page_tables[pfn].lock.unlock();
            return false;
        }
        // TODO is it OK?
        // more extra check?
        if (unlikely(g_page_tables[pfn].access_mode == 0 && (g_page_tables[pfn].session_id != sid || g_page_tables[pfn].thread_id != tid)))
        {
            g_page_tables[pfn].lock.unlock();
            return false;
        }

        g_page_tables[pfn].lock.unlock();

        // TOOD much faster
        memcpy(g_pages[pfn].data + offset, recv_buf, size);

        return true;
    }
    bool page_table::do_page_fork(unsigned long pfn)
    {
        if (unlikely(pfn >= g_page_table_size))
        {
            return false;
        }
        g_page_tables[pfn].lock.lock();
        if (unlikely(g_page_tables[pfn].valid == false))
        {
            g_page_tables[pfn].lock.unlock();
            return false;
        }
        if (unlikely(g_page_tables[pfn].ref_count == 0))
        {
            RMEM_ERROR("pfn %ld ref_count is 0", pfn);
        }
        // TODO ref_count == 0 will happen?
        if (g_page_tables[pfn].ref_count == 1)
        {
            g_page_tables[pfn].cow = true;
            g_page_tables[pfn].access_mode = 0x3;
        }
        g_page_tables[pfn].ref_count++;
        g_page_tables[pfn].lock.unlock();

        return true;
    }

}
