#pragma once
#include "commons.h"
#include "spinlock_mutex.h"
namespace rmem
{
    class vma_struct
    {
    public:
        unsigned long vm_start;
        unsigned long vm_end;
        unsigned long vm_flags;
        // TODO rb tree
    };

    class fork_struct : public vma_struct
    {
    public:
        std::unordered_map<unsigned long, unsigned long> addr_map;
    };

    class mm_struct
    {

    public:
        mm_struct(uint16_t tid, uint16_t sid);
        ~mm_struct();
        // always return true now!
        unsigned long insert_range(size_t size, unsigned long vm_flags);

        // [addr, addr+size] belong to one vm area [start,end]
        vma_struct *find_vma_range(unsigned long addr, size_t size);
        // [addr, addr+size] equal to vm area [start,end]
        std::list<vma_struct *>::iterator find_vma(unsigned long addr, size_t size);

        // don't need size, find the vma which start == addr
        std::list<fork_struct *>::iterator find_fork_vma(unsigned long addr);

        // if dont't exist [addr, addr+size], retunr EINVAL
        int free_vma_list(unsigned long addr, size_t size, bool locked = true);

        // read virtual addr [addr, addr+size] into buf[0,size], also will do page_fault if not mapped
        bool do_read(vma_struct *vma, unsigned long addr, size_t size, void *buf);

        bool do_write(vma_struct *vma, unsigned long addr, size_t size, void *buf);

        void do_free(vma_struct *vma);

        unsigned long do_fork(vma_struct *vma, unsigned long addr, size_t size);

        unsigned long do_join(mm_struct *target_mm, unsigned long addr);
        // vma
        std::list<vma_struct *> vma_list;
        // this will also include in vma_list,
        // and will be deleted when other progress install it!
        std::list<fork_struct *> fork_list;

        // lock for vma_list and fork_list
        spinlock_mutex vma_list_lock;

    private:
        // vm_pfn is the virtual page frame number, which vm_pfn<<21 belong to vma [start,end]
        // it's used for pages **which pfn is not exist**, and we will allocate a pfn from queue and init it's page table
        void handler_page_map(vma_struct *vma, unsigned long vm_pfn);

        unsigned long get_unmapped_area(size_t length);

        // used for destroy mm_struct
        void free_all_vma_list();
        std::unordered_map<unsigned long, unsigned long> addr_map;

        uint16_t thread_id;
        uint16_t session_id;
    };
}
