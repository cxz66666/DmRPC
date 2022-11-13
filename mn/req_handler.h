#pragma once

#include "commons.h"
#include "rpc.h"
#include <unordered_map>
#include <list>
namespace rmem
{

    class BasicAppContext
    {
    public:
        erpc::Rpc<erpc::CTransport> *rpc_ = nullptr;

        size_t thread_id_;        // The ID of the thread that owns this context
        size_t num_sm_resps_ = 0; // Number of SM responses

        virtual ~BasicAppContext()
        {
        }
    };

    class ServerContext : public BasicAppContext
    {
    public:
        erpc::ChronoTimer tput_t0; // Start time for throughput measurement

        size_t stat_req_rx_tot;
        size_t stat_req_alloc_tot;
        size_t stat_req_free_tot;
        size_t stat_req_read_tot;
        size_t stat_req_write_tot;
        size_t stat_req_fork_tot;
        size_t stat_req_error_tot;

        // key = session_num
        std::unordered_map<uint16_t, mm_struct *> mm_struct_map_;
    };

    class mm_struct
    {

    public:
        mm_struct();
        ~mm_struct();
        // always return true now!
        //  TODO check if the address is valid
        unsigned long insert_range(size_t size, unsigned long vm_flags);

        // [addr, addr+size] belong to one vm area [start,end]
        vma_struct *find_vma_range(unsigned long addr, size_t size);
        // [addr, addr+size] equal to vm area [start,end]
        std::list<vma_struct *>::iterator find_vma(unsigned long addr, size_t size);

        // vm_pfn is the virtual page frame number, which vm_pfn<<21 belong to vma [start,end]
        inline void handler_page_map(vma_struct *vma, unsigned long vm_pfn);

        // vma
        std::list<vma_struct *> vma_list;

    private:
        unsigned long get_unmapped_area(size_t length);

        std::unordered_map<unsigned long, unsigned long> addr_map;

        std::list<vma_struct *> fork_list;
    };
    class vma_struct
    {
    public:
        vma_struct();
        ~vma_struct();
        unsigned long vm_start;
        unsigned long vm_end;
        unsigned long vm_flags;
        // TODO rb tree
    };

    void
    alloc_req_handler(erpc::ReqHandle *req_handle, void *_context);
    void free_req_handler(erpc::ReqHandle *req_handle, void *_context);
    void read_req_handler(erpc::ReqHandle *req_handle, void *_context);
    void write_req_handler(erpc::ReqHandle *req_handle, void *_context);
    void fork_req_handler(erpc::ReqHandle *req_handle, void *_context);
    void basic_sm_handler(int session_num, erpc::SmEventType sm_event_type,
                          erpc::SmErrType sm_err_type, void *_context);

}