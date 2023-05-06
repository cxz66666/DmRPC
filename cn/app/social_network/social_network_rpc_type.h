#pragma once
#include <cstdint>
#include <cstddef>

enum class RPC_TYPE : uint8_t
{
    RPC_PING = 0,
    RPC_PING_RESP,


    // unique_id_service
    RPC_UNIQUE_ID,

    // url_shorten_service
    RPC_URL_SHORTEN,

    // user_mention_service
    RPC_USER_MENTION,

    // compose_post_service
    RPC_COMPOSE_POST_WRITE_REQ,
    RPC_COMPOSE_POST_WRITE_RESP,


    // user_timeline_service
    RPC_USER_TIMELINE_WRITE_REQ,
    RPC_USER_TIMELINE_WRITE_RESP,
    RPC_USER_TIMELINE_READ_REQ,
    RPC_USER_TIMELINE_READ_RESP,

    // post_storage_service
    RPC_POST_STORAGE_READ_REQ,
    RPC_POST_STORAGE_READ_RESP,
    RPC_POST_STORAGE_WRITE_REQ,
    RPC_POST_STORAGE_WRITE_RESP,

    // home_timeline_service
    RPC_HOME_TIMELINE_WRITE_REQ,
    RPC_HOME_TIMELINE_WRITE_RESP,
    RPC_HOME_TIMELINE_READ_REQ,
    RPC_HOME_TIMELINE_READ_RESP,

    // user_service
    RPC_COMPOSE_CREATOR_WITH_USER_ID,

    // rmem_param_service
    RPC_RMEM_PARAM,
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
    explicit RPCMsgReq(RPC_TYPE t, uint32_t num, T req) : req_common{t, num}, req_control(req) {}
}__attribute__((packed));

template<class T>
class RPCMsgResp
{
public:
    CommonResp resp_common;
    T resp_control;
    explicit RPCMsgResp(RPC_TYPE t, uint32_t num, int status, T resp) : resp_common{t, num, status}, resp_control(resp) {}
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
            int64_t user_id;
            int start;
            int stop;
        };


class HomeTimeLineReq
        {
        public:
            size_t data_length; //most time is not 0, because data field is not empty
            int64_t user_id;
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
    int64_t post_id;
};

class UserTimeLineWriteReq {
public:
    int64_t post_id;
    int64_t user_id;
    int64_t timestamp;
};

class RmemParamReq {
public:
    size_t dummy;
};