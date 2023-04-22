#pragma once
#include <cstdint>
#include <cstddef>

enum class RPC_TYPE : uint8_t
{
    RPC_PING = 0,
    RPC_PING_RESP,
    RPC_COMPOSE_POST,
    RPC_COMPOSE_POST_RESP,
    RPC_USER_TIMELINE,
    RPC_USER_TIMELINE_RESP,
    RPC_HOME_TIMELINE,
    RPC_HOME_TIMELINE_RESP,
    RPC_UNIQUE_ID,
    RPC_URL_SHORTEN,
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

template<class T>
class RPCMsgReq
{
public:
    CommonReq req_common;
    T req_control;
    RPCMsgReq(RPC_TYPE t, uint32_t num) : req_common{t, num} {}
    RPCMsgReq(RPC_TYPE t, uint32_t num, T req) : req_common{t, num}, req_control(req) {}
}__attribute__((packed));

template<class T>
class RPCMsgResp
{
public:
    CommonResp resp_common;
    T resp_control;
    RPCMsgResp(RPC_TYPE t, uint32_t num, int status) : resp_common{t, num, status} {}
    RPCMsgResp(RPC_TYPE t, uint32_t num, int status, T resp) : resp_common{t, num, status}, resp_control(resp) {}
}__attribute__((packed));


// also see ComposePostData in proto file
class ComposePostReq
        {
        public:
            size_t data_length;
            size_t flags;
        };

class UserTimeLineReq
        {
        public:
            size_t data_length;
            size_t user_id;
            int start;
            int stop;
        };


class HomeTimeLineReq
        {
        public:
            size_t data_length; //most time is not 0, because data field is not empty
            size_t user_id;
            int start_idx;
            int stop_idx;
        };

class CommonRPCReq {
public:
    size_t data_length;
};

class CommonRPCResp {
public:
    size_t data_length;
};

class PingRPCReq {
public:
    size_t timestamp;
};

class PingRPCResp {
public:
    size_t timestamp;
};

class UniqueIDReq {
public:
    size_t dummy; // it is useless!!
};

class UniqueIDResp {
public:
    size_t post_id;
};

#if defined(ERPC_PROGRAM)


#elif defined(RMEM_PROGRAM)


#endif