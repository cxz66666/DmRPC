#pragma once

#include <stdint.h>
#include "spinlock_mutex.h"

namespace rmem {
    /**
 * default page size is 2MB
 */
#define PAGE_SHIFT 21

#define PAGE_SIZE (1UL << PAGE_SHIFT)

#define PAGE_MASK (~(PAGE_SIZE - 1))

#define PAGE_ALIGN(x) ((static_cast<unsigned long>(x) + (PAGE_SIZE - 1)) & PAGE_MASK)

#define PAGE_ROUND_UP(x) ((static_cast<unsigned long>(x) + PAGE_SIZE - 1) & PAGE_MASK)

#define PAGE_ROUND_DOWN(x) (static_cast<unsigned long>(x)&PAGE_MASK)

#define PFN_2_PHYS(x) (static_cast<unsigned long>(x) << PAGE_SHIFT)

#define PHYS_2_PFN(x) (static_cast<unsigned long>(x) >> PAGE_SHIFT)

#define IS_PAGE_ALIGN(x) ((static_cast<unsigned long>(x) & (PAGE_SIZE - 1)) == 0)

    class page_elem
    {
    public:
        uint8_t data[PAGE_SIZE];
    };

#define PAGE_TABLE_SIZE 4

    class page_table
    {
    public:
        inline bool do_page_read(unsigned long pfn, void *recv_buf, size_t size, size_t offset, uint16_t tid, uint16_t sid);
        // must be use after handler cow or page fault
        inline bool do_page_write(unsigned long pfn, void *send_buf, size_t size, size_t offset, uint16_t tid, uint16_t sid);

        inline bool do_page_fork(unsigned long pfn);
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
    } __attribute__((packed));
    static_assert(sizeof(page_table) == 8);
}
