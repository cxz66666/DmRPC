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

#if defined(ERPC_PROGRAM)
// TODO
class ExtraReqMsg
{
public:
    size_t length;
    size_t flags;
};

class ExtraRespMsg
{
public:
    size_t length;
    size_t flags;
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
    TranscodeReq(RPC_TYPE t, uint32_t num) : req{t, num}, extra{0, 0} {}
    TranscodeReq(RPC_TYPE t, uint32_t num, size_t len) : req{t, num}, extra{len, 0} {}
    TranscodeReq(RPC_TYPE t, uint32_t num, size_t len, uint64_t f) : req{t, num}, extra{len, f} {}

} __attribute__((packed));

class TranscodeResp
{
public:
    CommonResp resp;
    ExtraRespMsg extra;
    TranscodeResp(RPC_TYPE t, uint32_t num, int s) : resp{t, num, s}, extra{0, 0} {}
    TranscodeResp(RPC_TYPE t, uint32_t num, int s, size_t len) : resp{t, num, s}, extra{len, 0} {}
    TranscodeResp(RPC_TYPE t, uint32_t num, int s, size_t len, uint64_t f) : resp{t, num, s}, extra{len, f} {}

} __attribute__((packed));

#elif defined(RMEM_PROGRAM)

class ExtraReqMsg
{
public:
    size_t length;
    uint64_t offset;
    // high 32 bit is session id, used for select worker node
    // low 32 bit is req_id for this session, used for select buffer
    size_t worker_flag;
};

class ExtraRespMsg
{
public:
    size_t length;
    uint64_t offset;
    // high 32 bit is session id, used for select worker node
    // low 32 bit is req_id for this session, used for select buffer
    size_t worker_flag;
};
class RmemParam
{
public:
    char hosts[32];
    uint64_t fork_rmem_addr_;
    size_t fork_size;
    int rmem_session_id_;
    int rmem_thread_id_;
    size_t file_size;
};
class PingReq
{
public:
    CommonReq req;
    size_t timestamp{};
    RmemParam rmem_param;

    PingReq(RPC_TYPE t, uint32_t num) : req{t, num} {}
    PingReq(RPC_TYPE t, uint32_t num, size_t ts, RmemParam p) : req{t, num}, timestamp(ts), rmem_param(p) {}
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
    TranscodeReq(RPC_TYPE t, uint32_t num) : req{t, num}, extra{0, 0, 0} {}
    TranscodeReq(RPC_TYPE t, uint32_t num, size_t len, uint64_t offset, size_t flag) : req{t, num}, extra{len, offset, flag} {}

} __attribute__((packed));

class TranscodeResp
{
public:
    CommonResp resp;
    ExtraRespMsg extra;
    TranscodeResp(RPC_TYPE t, uint32_t num, int s) : resp{t, num, s}, extra{0, 0, 0} {}
    TranscodeResp(RPC_TYPE t, uint32_t num, int s, size_t len, uint64_t offset, size_t flag) : resp{t, num, s}, extra{len, offset, flag} {}
} __attribute__((packed));

#elif defined(CXL_PROGRAM)

class ExtraReqMsg
{
public:
    size_t length;
    uint64_t offset;
    // high 32 bit is session id, used for select worker node
    // low 32 bit is req_id for this session, used for select buffer
    size_t worker_flag;
};

class ExtraRespMsg
{
public:
    size_t length;
    uint64_t offset;
    // high 32 bit is session id, used for select worker node
    // low 32 bit is req_id for this session, used for select buffer
    size_t worker_flag;
};
class CxlParam
{
public:
    char filename[32];
    size_t total_size;
    size_t file_size;
    size_t file_size_aligned;
};
class PingReq
{
public:
    CommonReq req;
    size_t timestamp{};
    CxlParam cxl_param;

    PingReq(RPC_TYPE t, uint32_t num) : req{t, num} {}
    PingReq(RPC_TYPE t, uint32_t num, size_t ts, CxlParam p) : req{t, num}, timestamp(ts), cxl_param(p) {}
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
    TranscodeReq(RPC_TYPE t, uint32_t num) : req{t, num}, extra{0, 0, 0} {}
    TranscodeReq(RPC_TYPE t, uint32_t num, size_t len, uint64_t offset, size_t flag) : req{t, num}, extra{len, offset, flag} {}

} __attribute__((packed));

class TranscodeResp
{
public:
    CommonResp resp;
    ExtraRespMsg extra;
    TranscodeResp(RPC_TYPE t, uint32_t num, int s) : resp{t, num, s}, extra{0, 0, 0} {}
    TranscodeResp(RPC_TYPE t, uint32_t num, int s, size_t len, uint64_t offset, size_t flag) : resp{t, num, s}, extra{len, offset, flag} {}
} __attribute__((packed));

#else

static_assert(false, "please set at least one type")

#endif