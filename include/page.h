#pragma once

#include <cstdint>
#include <cstddef>
#include "commons.h"
#include "spinlock_mutex.h"

namespace rmem
{
    /**
     * default page size is 2MB
     */
#define PAGE_SHIFT 12

#define PAGE_SIZE (1UL << PAGE_SHIFT)

#define PAGE_MASK (~(PAGE_SIZE - 1))

#define PAGE_ALIGN(x) ((static_cast<unsigned long>(x) + (PAGE_SIZE - 1)) & PAGE_MASK)

#define PAGE_ROUND_UP(x) ((static_cast<unsigned long>(x) + PAGE_SIZE - 1) & PAGE_MASK)

#define PAGE_ROUND_DOWN(x) (static_cast<unsigned long>(x) & PAGE_MASK)

#define PFN_2_PHYS(x) (static_cast<unsigned long>(x) << PAGE_SHIFT)

#define PHYS_2_PFN(x) (static_cast<unsigned long>(x) >> PAGE_SHIFT)

#define IS_PAGE_ALIGN(x) ((static_cast<unsigned long>(x) & (PAGE_SIZE - 1)) == 0)

#define PAGE_TABLE_SIZE 8

    class page_elem
    {
    public:
        uint8_t data[PAGE_SIZE];
    };
    extern page_elem *g_pages;
    extern uint64_t g_page_table_size;

    class page_table
    {
    public:
        inline bool do_page_read(unsigned long pfn, void *recv_buf, size_t size, size_t offset, uint16_t tid, uint16_t sid)
        {
            if (unlikely(offset + size > PAGE_SIZE))
            {
                return false;
            }
            lock.lock();
            if (unlikely(valid == false || r == false))
            {
                lock.unlock();
                return false;
            }
            if (unlikely(access_mode == 0 && (session_id != sid || thread_id != tid)))
            {
                lock.unlock();
                return false;
            }
            lock.unlock();
            // TODO much faster!
            memcpy(recv_buf, g_pages[pfn].data + offset, size);
            return true;
        }
        // must be use after handler cow or page fault
        inline bool do_page_write(unsigned long pfn, void *send_buf, size_t size, size_t offset, uint16_t tid, uint16_t sid)
        {
            if (unlikely(pfn >= g_page_table_size || offset + size > PAGE_SIZE))
            {
                return false;
            }

            lock.lock();
            if (unlikely(valid == false || w == false))
            {
                lock.unlock();
                return false;
            }
            // more extra check?
            if (unlikely(access_mode == 0 && (session_id != sid || thread_id != tid)))
            {
                lock.unlock();
                return false;
            }

            lock.unlock();

            // TOOD much faster
            memcpy(g_pages[pfn].data + offset, send_buf, size);

            return true;
        }

        inline bool do_page_fork(unsigned long pfn)
        {
            if (unlikely(pfn >= g_page_table_size))
            {
                return false;
            }
            lock.lock();
            if (unlikely(valid == false))
            {
                lock.unlock();
                return false;
            }
            if (unlikely(ref_count == 0))
            {
                RMEM_ERROR("pfn %ld ref_count is 0", pfn);
            }
            if (ref_count == 1)
            {
                w = false;
                cow = true;
                access_mode = 0x3;
            }
            ref_count++;
            lock.unlock();

            return true;
        }
        bool valid : 1;
        bool r : 1;
        bool w : 1;
        bool cow : 1;
        uint8_t access_mode : 2;
        uint8_t reserved1 : 2;

        uint8_t ref_count;

        uint8_t session_id;

        uint8_t thread_id;

        spinlock_mutex lock;

        uint32_t reserved2 : 24;
    };
    extern page_table *g_page_tables;

    static_assert(sizeof(page_table) == PAGE_TABLE_SIZE);
}
