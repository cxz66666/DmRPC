#pragma once
#include <stdint.h>
#include <stddef.h>

enum class RPC_TYPE : uint8_t
{
    RPC_PING = 0,
    RPC_PING_RESP,
    RPC_TRANSCODE,
    RPC_TRANSCODE_RESP,

};

class CommonReq
{
public:
    RPC_TYPE type;
    // for debug
    uint32_t req_number;
} __attribute__((packed));

class CommonResp
{
public:
    RPC_TYPE type;
    // for debug
    uint32_t req_number;
    int status;
} __attribute__((packed));

// TODO
class ExtraReqMsg
{
public:
    size_t length;
};

class ExtraRespMsg
{
public:
    size_t length;
};

class PingReq
{
public:
    CommonReq req;
    size_t timestamp;
    PingReq(RPC_TYPE t, uint32_t num) : req{t, num} {}
    PingReq(RPC_TYPE t, uint32_t num, size_t ts) : req{t, num}, timestamp(ts) {}
} __attribute__((packed));

class PingResp
{
public:
    CommonResp resp;
    size_t timestamp;
    PingResp(RPC_TYPE t, uint32_t num, int s) : resp{t, num, s} {}
    PingResp(RPC_TYPE t, uint32_t num, int s, size_t ts) : resp{t, num, s}, timestamp(ts) {}
} __attribute__((packed));

class TranscodeReq
{
public:
    CommonReq req;
    ExtraReqMsg extra;
    TranscodeReq(RPC_TYPE t, uint32_t num) : req{t, num}, extra{0} {}
    TranscodeReq(RPC_TYPE t, uint32_t num, size_t len) : req{t, num}, extra{len} {}

} __attribute__((packed));

class TranscodeResp
{
public:
    CommonResp resp;
    ExtraRespMsg extra;
    TranscodeResp(RPC_TYPE t, uint32_t num, int s) : resp{t, num, s}, extra{0} {}
    TranscodeResp(RPC_TYPE t, uint32_t num, int s, size_t len) : resp{t, num, s}, extra{len} {}
} __attribute__((packed));
