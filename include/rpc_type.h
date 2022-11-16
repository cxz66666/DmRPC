#pragma once
#include <stdint.h>
#include <stddef.h>
namespace rmem
{
    // used for vm_flags fields
#define VM_FLAG_READ 0x1
#define VM_FLAG_WRITE 0x2
    enum class RPC_TYPE : uint8_t
    {
        RPC_ALLOC = 1,
        RPC_FREE,
        RPC_READ,
        RPC_WRITE,
        RPC_FORK,
        RPC_JOIN,
    };

    class CommonReq
    {
    public:
        RPC_TYPE type;
        // for debug
        size_t req_number;
    } __attribute__((packed));

    class CommonResp
    {
    public:
        RPC_TYPE type;
        // for debug
        size_t req_number;
        int status;
    } __attribute__((packed));

    class AllocReq
    {
    public:
        CommonReq req;
        size_t size;
        unsigned long vm_flags;
        AllocReq(RPC_TYPE t, size_t num) : req{t, num} {}
        AllocReq(RPC_TYPE t, size_t num, size_t s, unsigned long v) : req{t, num}, size(s), vm_flags(v) {}
    } __attribute__((packed));
    class AllocResp
    {
    public:
        CommonResp resp;
        unsigned long raddr;
        AllocResp(RPC_TYPE t, size_t num, int s) : resp{t, num, s} {}
        AllocResp(RPC_TYPE t, size_t num, int s, unsigned long r) : resp{t, num, s}, raddr(r) {}
    } __attribute__((packed));
    class FreeReq
    {
    public:
        CommonReq req;
        unsigned long raddr;
        size_t rsize;
        FreeReq(RPC_TYPE t, size_t num) : req{t, num} {}
        FreeReq(RPC_TYPE t, size_t num, unsigned long addr, size_t size) : req{t, num}, raddr(addr), rsize(size) {}
    } __attribute__((packed));

    class FreeResp
    {
    public:
        CommonResp resp;
        FreeResp(RPC_TYPE t, size_t num, int s) : resp{t, num, s} {}
    } __attribute__((packed));

    class ReadReq
    {
    public:
        CommonReq req;
        void *recv_buf;
        unsigned long raddr;
        size_t rsize;
        ReadReq(RPC_TYPE t, size_t num) : req{t, num} {}
        ReadReq(RPC_TYPE t, size_t num, void *buf, unsigned long addr, size_t size) : req{t, num}, recv_buf(buf), raddr(addr), rsize(size) {}
    } __attribute__((packed));

    // followed by really data
    class ReadResp
    {
    public:
        CommonResp resp;
        void *recv_buf;
        size_t rsize;
        ReadResp(RPC_TYPE t, size_t num, int s) : resp{t, num, s} {}
        ReadResp(RPC_TYPE t, size_t num, int s, void *buf, size_t size) : resp{t, num, s}, recv_buf(buf), rsize(size) {}
    } __attribute__((packed));

    // followed by really data
    class WriteReq
    {
    public:
        CommonReq req;
        unsigned long raddr;
        size_t rsize;
        WriteReq(RPC_TYPE t, size_t num) : req{t, num} {}
        WriteReq(RPC_TYPE t, size_t num, unsigned long addr, size_t size) : req{t, num}, raddr(addr), rsize(size) {}
    } __attribute__((packed));

    class WriteResp
    {
    public:
        CommonResp resp;
        WriteResp(RPC_TYPE t, size_t num, int s) : resp{t, num, s} {}
    } __attribute__((packed));

    class ForkReq
    {
    public:
        CommonReq req;
        unsigned long raddr;
        size_t rsize;
        ForkReq(RPC_TYPE t, size_t num) : req{t, num} {}
        ForkReq(RPC_TYPE t, size_t num, unsigned long addr, size_t size) : req{t, num}, raddr(addr), rsize(size) {}
    } __attribute__((packed));

    class ForkResp
    {
    public:
        CommonResp resp;
        unsigned  long new_raddr;
        ForkResp(RPC_TYPE t, size_t num, int s) : resp{t, num, s} {}
        ForkResp(RPC_TYPE t, size_t num, int s, unsigned long addr) : resp{t, num, s}, new_raddr(addr) {}
    } __attribute__((packed));

    class JoinReq
    {
    public:
        CommonReq req;
        unsigned long raddr;
        uint16_t  thread_id;
        uint16_t  session_id;
        JoinReq(RPC_TYPE t, size_t num) : req{t, num} {}
        JoinReq(RPC_TYPE t, size_t num, unsigned long addr, uint16_t tid, uint16_t sid) : req{t, num}, raddr(addr), thread_id(tid), session_id(sid) {}
    } __attribute__((packed));

    class JoinResp
    {
    public:
        CommonResp resp;
        JoinResp(RPC_TYPE t, size_t num, int s) : resp{t, num, s} {}
    };
}