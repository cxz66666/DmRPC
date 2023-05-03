#include <thread>
#include "numautil.h"
#include "spinlock_mutex.h"
#include "compose_post.h"


void connect_sessions(ClientContext *c)
{

    // connect to backward server
    c->nginx_session_number = c->rpc_->create_session(nginx_addr, c->server_receiver_id_);
    rmem::rt_assert(c->nginx_session_number >= 0, "Failed to create session");

    // connect to forward server
    c->unique_id_session_number = c->rpc_->create_session(unique_id_addr, c->server_receiver_id_);
    rmem::rt_assert(c->unique_id_session_number >= 0, "Failed to create session");

    c->url_shorten_session_number = c->rpc_->create_session(url_shorten_addr, c->server_receiver_id_);
    rmem::rt_assert(c->url_shorten_session_number >= 0, "Failed to create session");

    c->user_mention_session_number = c->rpc_->create_session(user_mention_addr, c->server_receiver_id_);
    rmem::rt_assert(c->user_mention_session_number >= 0, "Failed to create session");

    c->user_timeline_session_number = c->rpc_->create_session(user_timeline_addr, c->server_receiver_id_);
    rmem::rt_assert(c->user_timeline_session_number >= 0, "Failed to create session");

    c->user_service_session_number = c->rpc_->create_session(user_service_addr, c->server_receiver_id_);
    rmem::rt_assert(c->user_service_session_number >= 0, "Failed to create session");

    c->home_timeline_session_number = c->rpc_->create_session(home_timeline_addr, c->server_receiver_id_);
    rmem::rt_assert(c->home_timeline_session_number>=0, "Failed to create session");

    c->post_storage_session_number = c->rpc_->create_session(post_storage_addr, c->server_receiver_id_);
    rmem::rt_assert(c->post_storage_session_number >= 0, "Failed to create session");

    while (c->num_sm_resps_ != 8)
    {
        c->rpc_->run_event_loop(kAppEvLoopMs);
        if (unlikely(ctrl_c_pressed == 1))
        {
            printf("Ctrl-C pressed. Exiting\n");
            return;
        }
    }
}

void ping_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_ping_tot++;
    auto *req_msgbuf = req_handle->get_req_msgbuf();
    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<PingRPCReq>), "data size not match");

    auto *req = reinterpret_cast<RPCMsgReq<PingRPCReq> *>(req_msgbuf->buf_);

    new (req_handle->pre_resp_msgbuf_.buf_) RPCMsgResp<PingRPCResp>(req->req_common.type, req->req_common.req_number, 0, {req->req_control.timestamp});
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(RPCMsgResp<PingRPCResp>));

    ctx->forward_all_mpmc_queue->push(*req_msgbuf);
    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void compose_post_write_req_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);
    ctx->stat_req_compose_post_write_req_tot++;

    auto *req_msgbuf = req_handle->get_req_msgbuf();
    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf->buf_);

    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<CommonRPCReq>) + req->req_control.data_length, "data size not match");
    new (req_handle->pre_resp_msgbuf_.buf_) RPCMsgResp<CommonRPCResp>(req->req_common.type, req->req_common.req_number, 0, {0});
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(RPCMsgResp<CommonRPCResp>));

    ctx->forward_all_mpmc_queue->push(*req_msgbuf);
    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void user_timeline_write_resp_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);

    auto *req_msgbuf = req_handle->get_req_msgbuf();
    auto *req = reinterpret_cast<RPCMsgReq<UserTimeLineWriteReq> *>(req_msgbuf->buf_);

    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<UserTimeLineWriteReq>), "data size not match");
    new (req_handle->pre_resp_msgbuf_.buf_) RPCMsgResp<CommonRPCResp>(req->req_common.type, req->req_common.req_number, 0, {0});
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(RPCMsgResp<CommonRPCResp>));

    ctx->forward_all_mpmc_queue->push(*req_msgbuf);
    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void post_storage_write_resp_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);

    auto *req_msgbuf = req_handle->get_req_msgbuf();
    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf->buf_);

    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<CommonRPCReq>) + req->req_control.data_length, "data size not match");
    new (req_handle->pre_resp_msgbuf_.buf_) RPCMsgResp<CommonRPCResp>(req->req_common.type, req->req_common.req_number, 0, {0});
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(RPCMsgResp<CommonRPCResp>));

    ctx->forward_all_mpmc_queue->push(*req_msgbuf);
    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void home_timeline_write_resp_handler(erpc::ReqHandle *req_handle, void *_context)
{
    auto *ctx = static_cast<ServerContext *>(_context);

    auto *req_msgbuf = req_handle->get_req_msgbuf();
    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf->buf_);

    rmem::rt_assert(req_msgbuf->get_data_size() == sizeof(RPCMsgReq<CommonRPCReq>) + req->req_control.data_length, "data size not match");
    new (req_handle->pre_resp_msgbuf_.buf_) RPCMsgResp<CommonRPCResp>(req->req_common.type, req->req_common.req_number, 0, {0});
    ctx->rpc_->resize_msg_buffer(&req_handle->pre_resp_msgbuf_, sizeof(RPCMsgResp<CommonRPCResp>));

    ctx->forward_all_mpmc_queue->push(*req_msgbuf);
    req_handle->get_hacked_req_msgbuf()->set_no_dynamic();

    ctx->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);

}


void callback_ping_resp(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    // erpc::MsgBuffer &req_msgbuf = ctx->req_backward_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[req_id];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<PingRPCResp>), "data size not match");

    // PingResp *resp = reinterpret_cast<PingResp *>(resp_msgbuf.buf_);

    // 如果返回值不为0，则认为后续不会有响应，直接将请求号和错误码放入队列
    // 如果返回值为0，则认为后续将有响应，不care
    // if (resp->resp.status != 0)
    // {
    // TODO
    // }

    // TODO check
    // ctx->rpc_->free_msg_buffer(req_msgbuf);
}

void handler_ping_resp(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{

    auto *req = reinterpret_cast<RPCMsgReq<PingRPCReq> *>(req_msgbuf.buf_);

    ctx->req_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer];

    ctx->rpc_->enqueue_request(ctx->nginx_session_number, static_cast<uint8_t>(RPC_TYPE::RPC_PING_RESP),
                               &ctx->req_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_ping_resp, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));
}

void callback_compose_post_write_resp(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    erpc::MsgBuffer &req_msgbuf = ctx->req_backward_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[req_id];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<CommonRPCResp>), "data size not match");

    ctx->rpc_->free_msg_buffer(req_msgbuf);
}

void handler_compose_post_write_resp(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf.buf_);

    ctx->req_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer];
    ctx->rpc_->enqueue_request(ctx->nginx_session_number, static_cast<uint8_t>(RPC_TYPE::RPC_COMPOSE_POST_WRITE_RESP),
                               &ctx->req_backward_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_compose_post_write_resp, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));

}

void callback_unique_id(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    erpc::MsgBuffer &req_msgbuf = ctx->req_unique_id_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_unique_id_msgbuf[req_id];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<UniqueIDResp>), "data size not match");

    auto resp = reinterpret_cast<RPCMsgResp<UniqueIDResp> *>(resp_msgbuf.buf_);

    ctx->rpc_->free_msg_buffer(req_msgbuf);

    ReqState* req_state = ctx->state_store->req_state_map[req_id];
    req_state->finished_unique_id = true;
    req_state->post.set_post_id(resp->resp_control.post_id);
    if(req_state->is_next_step()){
        req_state->send_second_step();
    }

}

void handler_unique_id(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<RPCMsgReq<UniqueIDReq> *>(req_msgbuf.buf_);

    ctx->req_unique_id_msgbuf[req->req_common.req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_unique_id_msgbuf[req->req_common.req_number % kAppMaxBuffer];
    ctx->rpc_->enqueue_request(ctx->unique_id_session_number, static_cast<uint8_t>(RPC_TYPE::RPC_UNIQUE_ID),
                               &ctx->req_unique_id_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_unique_id, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));
}

void callback_compose_creator_with_user_id(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    erpc::MsgBuffer &req_msgbuf = ctx->req_user_service_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_user_service_msgbuf[req_id];

    auto resp = reinterpret_cast<RPCMsgResp<CommonRPCResp> *>(resp_msgbuf.buf_);
    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<CommonRPCResp>) + resp->resp_control.data_length, "data size not match");


    ctx->rpc_->free_msg_buffer(req_msgbuf);

    ReqState* req_state = ctx->state_store->req_state_map[req_id];
    req_state->finished_user = true;
    req_state->post.mutable_creator()->ParseFromArray(resp+1, resp->resp_control.data_length);
    if(req_state->is_next_step()){
        req_state->send_second_step();
    }

}
void handler_compose_creator_with_user_id(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf.buf_);

    ctx->req_user_service_msgbuf[req->req_common.req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_user_service_msgbuf[req->req_common.req_number % kAppMaxBuffer];
    ctx->rpc_->enqueue_request(ctx->user_service_session_number, static_cast<uint8_t>(RPC_TYPE::RPC_COMPOSE_CREATOR_WITH_USER_ID),
                               &ctx->req_user_service_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_compose_creator_with_user_id, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));

}



void callback_user_mention(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    erpc::MsgBuffer &req_msgbuf = ctx->req_user_mention_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_user_mention_msgbuf[req_id];

    auto resp = reinterpret_cast<RPCMsgResp<CommonRPCResp> *>(resp_msgbuf.buf_);
    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<CommonRPCResp>) + resp->resp_control.data_length, "data size not match");


    ctx->rpc_->free_msg_buffer(req_msgbuf);

    ReqState* req_state = ctx->state_store->req_state_map[req_id];
    req_state->finished_user_mention = true;

    social_network::UserMentionResp user_mention_resp;
    user_mention_resp.ParseFromArray(resp+1, resp->resp_control.data_length);

    for(const auto& user_mention : user_mention_resp.user_mentions()){
        *req_state->post.mutable_user_mentions()->Add() = user_mention;
    }

    if(req_state->is_next_step()){
        req_state->send_second_step();
    }

}
void handler_user_mention(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf.buf_);

    ctx->req_user_mention_msgbuf[req->req_common.req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_user_mention_msgbuf[req->req_common.req_number % kAppMaxBuffer];
    ctx->rpc_->enqueue_request(ctx->user_mention_session_number, static_cast<uint8_t>(RPC_TYPE::RPC_USER_MENTION),
                               &ctx->req_user_mention_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_user_mention, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));

}

void callback_url_shorten(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    erpc::MsgBuffer &req_msgbuf = ctx->req_url_shorten_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_url_shorten_msgbuf[req_id];

    auto resp = reinterpret_cast<RPCMsgResp<CommonRPCResp> *>(resp_msgbuf.buf_);

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<CommonRPCResp>) + resp->resp_control.data_length, "data size not match");


    ctx->rpc_->free_msg_buffer(req_msgbuf);

    ReqState* req_state = ctx->state_store->req_state_map[req_id];
    req_state->finished_url_shorten = true;

    social_network::UrlShortenResp url_shorten_resp;
    url_shorten_resp.ParseFromArray(resp+1, resp->resp_control.data_length);

    for(const auto& url : url_shorten_resp.urls()){
        *req_state->post.mutable_urls()->Add() = url;
    }

    if(req_state->is_next_step()){
        req_state->send_second_step();
    }
}
void handler_url_shorten(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf.buf_);

    ctx->req_url_shorten_msgbuf[req->req_common.req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_url_shorten_msgbuf[req->req_common.req_number % kAppMaxBuffer];
    ctx->rpc_->enqueue_request(ctx->url_shorten_session_number, static_cast<uint8_t>(RPC_TYPE::RPC_URL_SHORTEN),
                               &ctx->req_url_shorten_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_url_shorten, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));

}

void callback_post_storage_write_req(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    erpc::MsgBuffer &req_msgbuf = ctx->req_url_shorten_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_url_shorten_msgbuf[req_id];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<CommonRPCResp>), "data size not match");

    ctx->rpc_->free_msg_buffer(req_msgbuf);
}
void handler_post_storage_write_req(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf.buf_);

    ctx->req_post_storage_msgbuf[req->req_common.req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_post_storage_msgbuf[req->req_common.req_number % kAppMaxBuffer];
    ctx->rpc_->enqueue_request(ctx->post_storage_session_number, static_cast<uint8_t>(RPC_TYPE::RPC_POST_STORAGE_WRITE_REQ),
                               &ctx->req_post_storage_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_post_storage_write_req, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));

}


void callback_user_timeline_write_req(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    erpc::MsgBuffer &req_msgbuf = ctx->req_user_timeline_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_user_timeline_msgbuf[req_id];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<CommonRPCResp>), "data size not match");

    ctx->rpc_->free_msg_buffer(req_msgbuf);
}
void handler_user_timeline_write_req(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<RPCMsgReq<UserTimeLineWriteReq> *>(req_msgbuf.buf_);

    ctx->req_user_timeline_msgbuf[req->req_common.req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_user_timeline_msgbuf[req->req_common.req_number % kAppMaxBuffer];
    ctx->rpc_->enqueue_request(ctx->user_timeline_session_number, static_cast<uint8_t>(RPC_TYPE::RPC_USER_TIMELINE_WRITE_REQ),
                               &ctx->req_user_timeline_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_user_timeline_write_req, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));

}


void callback_home_timeline_write_req(void *_context, void *_tag)
{
    auto req_id_ptr = reinterpret_cast<std::uintptr_t>(_tag);
    uint32_t req_id = req_id_ptr;
    auto *ctx = static_cast<ClientContext *>(_context);

    erpc::MsgBuffer &req_msgbuf = ctx->req_home_timeline_msgbuf[req_id];
    erpc::MsgBuffer &resp_msgbuf = ctx->resp_home_timeline_msgbuf[req_id];

    rmem::rt_assert(resp_msgbuf.get_data_size() == sizeof(RPCMsgResp<CommonRPCResp>), "data size not match");

    ctx->rpc_->free_msg_buffer(req_msgbuf);

}
void handler_home_timeline_write_req(ClientContext *ctx, const erpc::MsgBuffer &req_msgbuf)
{
    auto *req = reinterpret_cast<RPCMsgReq<CommonRPCReq> *>(req_msgbuf.buf_);

    ctx->req_home_timeline_msgbuf[req->req_common.req_number % kAppMaxBuffer] = req_msgbuf;

    erpc::MsgBuffer &resp_msgbuf = ctx->resp_home_timeline_msgbuf[req->req_common.req_number % kAppMaxBuffer];
    ctx->rpc_->enqueue_request(ctx->home_timeline_session_number, static_cast<uint8_t>(RPC_TYPE::RPC_HOME_TIMELINE_WRITE_REQ),
                               &ctx->req_home_timeline_msgbuf[req->req_common.req_number % kAppMaxBuffer], &resp_msgbuf,
                               callback_home_timeline_write_req, reinterpret_cast<void *>(req->req_common.req_number % kAppMaxBuffer));

}

void client_thread_func(size_t thread_id, ClientContext *ctx, erpc::Nexus *nexus)
{
    ctx->client_id_ = thread_id;
    std::vector<size_t> port_vec = flags_get_numa_ports(0);
    uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

    uint8_t rpc_id;
#if defined(ERPC_PROGRAM)
    rpc_id = thread_id + FLAGS_server_num;
#elif defined(RMEM_PROGRAM)
    rpc_id = thread_id + FLAGS_server_num + kAppMaxRPC;
#endif
    erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(ctx),
                                    rpc_id,
                                    basic_sm_handler_client, phy_port);
    rpc.retry_connect_on_invalid_rpc_id_ = true;
    ctx->rpc_ = &rpc;

    std::vector<erpc::MsgBuffer*> tmp_vec = {ctx->resp_backward_msgbuf, ctx->resp_unique_id_msgbuf, ctx->resp_url_shorten_msgbuf, ctx->resp_user_mention_msgbuf,
            ctx->resp_user_timeline_msgbuf, ctx->resp_user_service_msgbuf, ctx->resp_home_timeline_msgbuf, ctx->resp_post_storage_msgbuf};

    for(auto iter: tmp_vec){
        for(size_t i=0; i<kAppMaxBuffer; i++){
            iter[i] = rpc.alloc_msg_buffer_or_die(sizeof(RPCMsgResp<CommonRPCResp>));
        }
    }

    connect_sessions(ctx);

    using FUNC_HANDLER = std::function<void(ClientContext *, erpc::MsgBuffer)>;

    std::map<RPC_TYPE ,FUNC_HANDLER > handlers{
            {RPC_TYPE::RPC_PING_RESP, handler_ping_resp},
            {RPC_TYPE::RPC_COMPOSE_POST_WRITE_RESP, handler_compose_post_write_resp},
            {RPC_TYPE::RPC_UNIQUE_ID, handler_unique_id},
            {RPC_TYPE::RPC_COMPOSE_CREATOR_WITH_USER_ID, handler_compose_creator_with_user_id},
            {RPC_TYPE::RPC_USER_MENTION, handler_user_mention},
            {RPC_TYPE::RPC_URL_SHORTEN, handler_url_shorten},
            {RPC_TYPE::RPC_POST_STORAGE_WRITE_REQ, handler_post_storage_write_req},
            {RPC_TYPE::RPC_USER_TIMELINE_WRITE_REQ, handler_user_timeline_write_req},
            {RPC_TYPE::RPC_HOME_TIMELINE_WRITE_REQ, handler_home_timeline_write_req},
            };

    while (true)
    {
        unsigned size = ctx->forward_mpmc_queue->was_size();
        for (unsigned i = 0; i < size; i++)
        {
            erpc::MsgBuffer req_msg = ctx->forward_mpmc_queue->pop();
            auto *req = reinterpret_cast<CommonReq *>(req_msg.buf_);
            rmem::rt_assert(req->type == RPC_TYPE::RPC_UNIQUE_ID || req->type ==  RPC_TYPE::RPC_COMPOSE_CREATOR_WITH_USER_ID
                            || req->type == RPC_TYPE::RPC_USER_MENTION || req->type == RPC_TYPE::RPC_URL_SHORTEN
                            || req->type == RPC_TYPE::RPC_POST_STORAGE_WRITE_REQ || req->type == RPC_TYPE::RPC_USER_TIMELINE_WRITE_REQ
                            || req->type == RPC_TYPE::RPC_HOME_TIMELINE_WRITE_REQ, "only RPC_POST_STORAGE_READ_REQ in forward queue");
            handlers[req->type](ctx, req_msg);
        }

        size = ctx->backward_mpmc_queue->was_size();
        for (unsigned i = 0; i < size; i++)
        {
            erpc::MsgBuffer req_msg = ctx->backward_mpmc_queue->pop();
            auto *req = reinterpret_cast<CommonReq *>(req_msg.buf_);

            rmem::rt_assert(req->type == RPC_TYPE::RPC_PING_RESP || req->type == RPC_TYPE::RPC_COMPOSE_POST_WRITE_RESP
                            , "only ping_resp and tc_resp in backward queue");
            handlers[req->type](ctx, req_msg);
        }
        ctx->rpc_->run_event_loop_once();
        if (unlikely(ctrl_c_pressed))
        {
            break;
        }
    }
}

void server_thread_func(size_t thread_id, ServerContext *ctx, erpc::Nexus *nexus)
{
    ctx->server_id_ = thread_id;
    std::vector<size_t> port_vec = flags_get_numa_ports(0);
    uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

    uint8_t rpc_id;
#if defined(ERPC_PROGRAM)
    rpc_id = thread_id;
#elif defined(RMEM_PROGRAM)
    rpc_id = thread_id + kAppMaxRPC;
#endif
    erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(ctx),
                                    rpc_id,
                                    basic_sm_handler_server, phy_port);
    rpc.retry_connect_on_invalid_rpc_id_ = true;
    ctx->rpc_ = &rpc;

    while (true)
    {
        ctx->reset_stat();
        erpc::ChronoTimer start;
        start.reset();
        rpc.run_event_loop(kAppEvLoopMs);
        const double seconds = start.get_sec();
        printf("thread %zu: ping_req : %.2f,  compose_post: %.2f \n", thread_id,
               ctx->stat_req_ping_tot / seconds, ctx->stat_req_compose_post_write_req_tot / seconds);

        ctx->rpc_->reset_dpath_stats();
        // more handler
        if (ctrl_c_pressed == 1)
        {
            break;
        }
    }
}
void worker_thread_func(size_t thread_id, MPMC_QUEUE *producer, MPMC_QUEUE *consumer_back, MPMC_QUEUE *consumer_fwd, erpc::Rpc<erpc::CTransport> *rpc_, erpc::Rpc<erpc::CTransport> *server_rpc_,ReqStateStore* store )
{
    _unused(thread_id);
    while (true)
    {
        unsigned size = producer->was_size();
        for (unsigned i = 0; i < size; i++)
        {
            erpc::MsgBuffer req_msg = producer->pop();

            auto *req = reinterpret_cast<CommonReq *>(req_msg.buf_);
            rmem::rt_assert(req->type == RPC_TYPE::RPC_PING || req->type == RPC_TYPE::RPC_COMPOSE_POST_WRITE_REQ ||
                            req->type == RPC_TYPE::RPC_USER_TIMELINE_WRITE_RESP || req->type == RPC_TYPE::RPC_POST_STORAGE_WRITE_RESP
                            || req->type == RPC_TYPE::RPC_HOME_TIMELINE_WRITE_RESP, "req type error");

            if(req->type == RPC_TYPE::RPC_COMPOSE_POST_WRITE_REQ){
                compose_post_write_and_create(req_msg.buf_, store, consumer_fwd, rpc_);
                server_rpc_->free_msg_buffer(req_msg);
            } else if(req->type == RPC_TYPE::RPC_USER_TIMELINE_WRITE_RESP){
                rmem::rt_assert(store->req_state_map.count(req->req_number), "req_state_map.count(req->req_number) == 0");
                ReqState *now_state = store->req_state_map[req->req_number];
                now_state->finished_user_timeline = true;
                if(now_state->is_finished_all()){
                    consumer_back->push(now_state->generate_resp_msg());
                    delete now_state;
                    store->req_state_map.erase(req->req_number);
                }
                server_rpc_->free_msg_buffer(req_msg);
            } else if(req->type == RPC_TYPE::RPC_POST_STORAGE_WRITE_RESP){
                rmem::rt_assert(store->req_state_map.count(req->req_number), "req_state_map.count(req->req_number) == 0");
                ReqState *now_state = store->req_state_map[req->req_number];
                now_state->finished_post_storage = true;
                if(now_state->is_finished_all()){
                    consumer_back->push(now_state->generate_resp_msg());
                    delete now_state;
                    store->req_state_map.erase(req->req_number);
                }
                server_rpc_->free_msg_buffer(req_msg);
            }  else if(req->type == RPC_TYPE::RPC_HOME_TIMELINE_WRITE_RESP) {
                rmem::rt_assert(store->req_state_map.count(req->req_number), "req_state_map.count(req->req_number) == 0");
                ReqState *now_state = store->req_state_map[req->req_number];
                now_state->finished_home_timeline = true;
                if(now_state->is_finished_all()){
                    consumer_back->push(now_state->generate_resp_msg());
                    delete now_state;
                    store->req_state_map.erase(req->req_number);
                }
                server_rpc_->free_msg_buffer(req_msg);
            }
            else {
                req->type = RPC_TYPE::RPC_PING_RESP;
                consumer_back->push(req_msg);
            }
        }
        if (ctrl_c_pressed == 1)
        {
            break;
        }
    }
}

void leader_thread_func()
{
    erpc::Nexus nexus(FLAGS_server_addr, FLAGS_numa_server_node, 0);

    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_PING), ping_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_COMPOSE_POST_WRITE_REQ), compose_post_write_req_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_USER_TIMELINE_WRITE_RESP), user_timeline_write_resp_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_POST_STORAGE_WRITE_RESP), post_storage_write_resp_handler);
    nexus.register_req_func(static_cast<uint8_t>(RPC_TYPE::RPC_HOME_TIMELINE_WRITE_RESP), home_timeline_write_resp_handler);


    std::vector<std::thread> clients(FLAGS_client_num);
    std::vector<std::thread> servers(FLAGS_server_num);
    std::vector<std::thread> workers(FLAGS_client_num);

    auto *context = new AppContext();

    clients[0] = std::thread(client_thread_func, 0, context->client_contexts_[0], &nexus);
    sleep(2);
    rmem::bind_to_core(clients[0], FLAGS_numa_client_node, get_bind_core(FLAGS_numa_client_node) + FLAGS_bind_core_offset);

    for (size_t i = 1; i < FLAGS_client_num; i++)
    {
        clients[i] = std::thread(client_thread_func, i, context->client_contexts_[i], &nexus);

        rmem::bind_to_core(clients[i], FLAGS_numa_client_node, get_bind_core(FLAGS_numa_client_node) + FLAGS_bind_core_offset);
    }

    for (size_t i = 0; i < FLAGS_server_num; i++)
    {
        servers[i] = std::thread(server_thread_func, i, context->server_contexts_[i], &nexus);

        rmem::bind_to_core(servers[i], FLAGS_numa_server_node, get_bind_core(FLAGS_numa_server_node) + FLAGS_bind_core_offset);
    }
    sleep(3);

    for (size_t i = 0; i < FLAGS_client_num; i++)
    {
        rmem::rt_assert(context->server_contexts_[i]->rpc_ != nullptr, "server rpc is null");
        workers[i] = std::thread(worker_thread_func, i, context->client_contexts_[i]->forward_all_mpmc_queue,
                                 context->client_contexts_[i]->backward_mpmc_queue, context->client_contexts_[i]->forward_mpmc_queue,
                                 context->client_contexts_[i]->rpc_, context->server_contexts_[i]->rpc_, context->client_contexts_[i]->state_store);
//        uint64_t worker_offset = FLAGS_worker_bind_core_offset == UINT64_MAX ? FLAGS_bind_core_offset : FLAGS_worker_bind_core_offset;
//        rmem::bind_to_core(workers[i], FLAGS_numa_worker_node, get_bind_core(FLAGS_numa_worker_node) + worker_offset);
        rmem::bind_to_core(clients[i], FLAGS_numa_client_node, get_bind_core(FLAGS_numa_client_node) + FLAGS_bind_core_offset);

    }

    sleep(2);
    if (FLAGS_timeout_second != UINT64_MAX)
    {
        sleep(FLAGS_timeout_second);
        ctrl_c_pressed = true;
    }

    for (size_t i = 0; i < FLAGS_client_num; i++)
    {
        clients[i].join();
    }
    for (size_t i = 0; i < FLAGS_server_num; i++)
    {
        servers[i].join();
    }
    for (size_t i = 0; i < FLAGS_client_num; i++)
    {
        workers[i].join();
    }
}

int main(int argc, char **argv)
{

    signal(SIGINT, ctrl_c_handler);
    signal(SIGTERM, ctrl_c_handler);
    // only config_file is required!!!
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    init_service_config(FLAGS_config_file,"compose_post");
    init_specific_config();

    std::thread leader_thread(leader_thread_func);
    rmem::bind_to_core(leader_thread, 1, get_bind_core(1));
    leader_thread.join();
}