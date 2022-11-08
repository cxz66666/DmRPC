#pragma once
#include <stdint.h>
#include <stddef.h>
namespace rmem
{
    enum class RPC_TYPE : uint8_t
    {
        RPC_ALLOC = 1,
        RPC_FREE,
        RPC_READ,
        RPC_WRITE,
        RPC_FORK,
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
        AllocReq(RPC_TYPE t, size_t num, size_t s, unsigned long v) : req{t, num}, size(s), vm_flags(v) {}
    } __attribute__((packed));
    class AllocResp
    {
    public:
        CommonResp resp;
        unsigned long raddr;
        AllocResp(RPC_TYPE t, size_t num, int s, unsigned long r) : resp{t, num, s}, raddr(r) {}
    } __attribute__((packed));

    class FreeReq
    {
    public:
        CommonReq req;
        unsigned long raddr;
        size_t rsize;
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
        ReadReq(RPC_TYPE t, size_t num, void *buf, unsigned long addr, size_t size) : req{t, num}, recv_buf(buf), raddr(addr), rsize(size) {}
    } __attribute__((packed));

    // followed by really data
    class ReadResp
    {
    public:
        CommonResp resp;
        void *recv_buf;
        size_t rsize;
        ReadResp(RPC_TYPE t, size_t num, int s, void *buf, size_t size) : resp{t, num, s}, recv_buf(buf), rsize(size) {}
    } __attribute__((packed));

    // followed by really data
    class WriteReq
    {
    public:
        CommonReq req;
        unsigned long raddr;
        size_t rsize;
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
        unsigned long vm_flags;
        ForkReq(RPC_TYPE t, size_t num, unsigned long addr, size_t size, unsigned long flags) : req{t, num}, raddr(addr), rsize(size), vm_flags(flags) {}
    } __attribute__((packed));

    class ForkResp
    {
    public:
        CommonResp resp;
        ForkResp(RPC_TYPE t, size_t num, int s) : resp{t, num, s} {}
    } __attribute__((packed));
}