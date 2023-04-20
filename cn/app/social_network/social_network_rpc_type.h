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
    CommonResp resp_common;
    T resp_control;
    RPCMsgResp(RPC_TYPE t, uint32_t num, int status) : resp_common{t, num, status} {}
    RPCMsgResp(RPC_TYPE t, uint32_t num, int status, T resp) : resp_common{t, num, status}, resp_control(resp) {}
}__attribute__((packed));