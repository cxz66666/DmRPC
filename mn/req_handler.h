#pragma once
#include "req_handle.h"
namespace rmem
{
    void alloc_req_handler(erpc::ReqHandle *req_handle, void *_context);
    void free_req_handler(erpc::ReqHandle *req_handle, void *_context);
    void read_req_handler(erpc::ReqHandle *req_handle, void *_context);
    void write_req_handler(erpc::ReqHandle *req_handle, void *_context);
    void fork_req_handler(erpc::ReqHandle *req_handle, void *_context);
    void join_req_handler(erpc::ReqHandle *req_handle, void *_context);

    void basic_sm_handler(int session_num, int remote_session_num, erpc::SmEventType sm_event_type,
                          erpc::SmErrType sm_err_type, void *_context);

}