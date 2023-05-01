#pragma once
#include <hdr/hdr_histogram.h>
#include <mongoc.h>
#include "atomic_queue/atomic_queue.h"
#include "../social_network_commons.h"
#include "../social_network.pb.h"
#include "phmap.h"
#include "spinlock_mutex.h"
#include "../utils_mongodb.h"
#include <future>

#include "api.h"
#include "page.h"

std::string compose_post_addr;
std::string user_timeline_addr;
std::string home_timeline_addr;


static mongoc_client_pool_t* mongodb_client_pool;
int mongodb_conns_num;

std::vector<rmem::Rmem *> rmems_;
std::vector<social_network::RmemParam> rmem_params_;
std::vector<phmap::flat_hash_map<int64_t , std::pair<size_t,size_t>>> post_id_to_addr_map_;

// unit is GB
size_t read_total_size_per_thread;
size_t write_total_size_per_thread;


class StorageHandler
{
public:
    uint32_t req_number{};
    uint32_t rpc_type{};
    bool is_read{};
    std::vector<int64_t > post_ids;
    std::vector<void*> rmem_bufs;
    std::vector<std::pair<size_t,size_t>> addrs_size;
    social_network::PostStorageReadResp resp;
};
using STORAGE_QUEUE = atomic_queue::AtomicQueueB2<StorageHandler*, std::allocator<StorageHandler*>, true, true, false>;

std::vector<STORAGE_QUEUE *> storage_queues;
std::vector<std::map<uint32_t, size_t>> req_num_to_session_num;

class ClientContext : public BasicContext
{
public:
    ClientContext(size_t cid, size_t sid, size_t rid) : client_id_(cid), server_sender_id_(sid), server_receiver_id_(rid)
    {
        forward_all_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);
        backward_mpmc_queue = new MPMC_QUEUE(kAppMaxBuffer);
    }
    ~ClientContext()
    {
        delete forward_all_mpmc_queue;
        delete backward_mpmc_queue;
    }

    size_t get_write_addr(size_t size) {
        size_t res;
        now_write_addr_mutex.lock();
        res = now_write_addr;
        now_write_addr += PAGE_ROUND_UP(size);
        if(now_write_addr>=GB(write_total_size_per_thread+read_total_size_per_thread)) {
            now_write_addr = GB(read_total_size_per_thread);
        }
        now_write_addr_mutex.unlock();
        return res;
    }
    erpc::MsgBuffer req_backward_msgbuf[kAppMaxBuffer];

    erpc::MsgBuffer resp_backward_msgbuf[kAppMaxBuffer];

    size_t client_id_;
    size_t server_sender_id_;
    size_t server_receiver_id_;

    size_t now_write_addr;
    spinlock_mutex now_write_addr_mutex;
    int user_timeline_session_num_;
    int home_timeline_session_num_;
    int compose_post_session_num_;

    MPMC_QUEUE *forward_all_mpmc_queue;
    MPMC_QUEUE *backward_mpmc_queue;
};

class ServerContext : public BasicContext
{
public:
    explicit ServerContext(size_t sid) : server_id_(sid)
    {
    }
    ~ServerContext()
    = default;
    size_t server_id_{};
    size_t stat_req_ping_tot{};
    size_t stat_req_post_storage_read_tot{};
    size_t stat_req_post_storage_write_tot{};
    size_t stat_req_err_tot{};

    spinlock_mutex init_mutex;
    bool is_pinged{false};
    bool mongodb_init_finished{false};

    void reset_stat()
    {
        stat_req_ping_tot = 0;
        stat_req_post_storage_read_tot = 0;
        stat_req_post_storage_write_tot = 0;
        stat_req_err_tot = 0;
    }

    MPMC_QUEUE *forward_all_mpmc_queue{};
};

std::vector<social_network::Post> mongodb_get_read_posts(){
    mongodb_client_pool = init_mongodb_client_pool(config_json_all, "post_storage", mongodb_conns_num);
    mongoc_client_t *mongodb_client =  mongoc_client_pool_pop(mongodb_client_pool);

    auto collection = mongoc_client_get_collection(mongodb_client, "post", "post");

    rmem::rt_assert(collection, "Failed to get post collection from DB Post");

    bson_t* query = bson_new();
    mongoc_cursor_t* cursor = mongoc_collection_find_with_opts(collection, query, nullptr, nullptr);

    const bson_t *doc;
    social_network::Post now_post;

    std::vector<social_network::Post> posts;
    while(mongoc_cursor_next(cursor,&doc)) {
        auto post_json_char = bson_as_json(doc, nullptr);
        json post_json = json::parse(post_json_char);

        now_post.set_req_id(post_json["req_id"]);
        now_post.set_timestamp(post_json["timestamp"]);
        now_post.set_post_id(post_json["post_id"]);


        auto creator = now_post.mutable_creator();
        creator->set_user_id(post_json["creator"]["user_id"]);
        creator->set_username(post_json["creator"]["username"]);

        now_post.set_post_type(post_json["post_type"]);
        now_post.set_text(post_json["text"]);

        for(auto &item : post_json["media"]){
            social_network::Media *media = now_post.add_media();
            media->set_media_id(item["media_id"]);
            media->set_media_type(item["media_type"]);
        }

        for(auto &item : post_json["user_mentions"]){
            social_network::UserMention *user_mention = now_post.add_user_mentions();
            user_mention->set_user_id(item["user_id"]);
            user_mention->set_username(item["username"]);
        }

        for(auto &item: post_json["urls"]) {
            social_network::Url *url = now_post.add_urls();
            url->set_shortened_url(item["shortened_url"]);
            url->set_expanded_url(item["expanded_url"]);
        }

        posts.push_back(now_post);
        if(ctrl_c_pressed){
            return {};
        }
    }

    bson_destroy(query);
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(collection);
    mongoc_client_pool_push(mongodb_client_pool, mongodb_client);

    RMEM_INFO("mongodb init finished! Total post num: %ld", posts.size());
    return posts;

}



class AppContext
{
public:
    AppContext()
    {
        int ret = hdr_init(1, 1000 * 1000 * 10, 3,
                           &latency_hist_);
        rmem::rt_assert(ret == 0, "hdr_init failed");
        for (size_t i = 0; i < FLAGS_client_num; i++)
        {
#if defined(ERPC_PROGRAM)
            //TODO WARNING
            client_contexts_.push_back(new ClientContext(i, i % FLAGS_server_num, i % FLAGS_server_num));
#elif defined(RMEM_PROGRAM)
            client_contexts_.push_back(new ClientContext(i, (i % FLAGS_server_num) + kAppMaxRPC, (i % FLAGS_server_num) + kAppMaxRPC));
#endif
        }
        for (size_t i = 0; i < FLAGS_server_num; i++)
        {
            auto *ctx = new ServerContext(i);
            ctx->forward_all_mpmc_queue = client_contexts_[i]->forward_all_mpmc_queue;
            server_contexts_.push_back(ctx);
        }

        std::string memory_node_addr = get_memory_node_addr(1);
        for (size_t i=0; i< FLAGS_server_num; i++)
        {
            auto *rmem = new rmem::Rmem(0);
            social_network::RmemParam param;
            param.set_addr(memory_node_addr);

            int session_id = rmem->connect_session(memory_node_addr, i);
            rmem::rt_assert(session_id >= 0, "connect_session failed");

            param.set_rmem_session_id(session_id);
            param.set_rmem_thread_id(static_cast<int>(i));

            rmems_.push_back(rmem);
            rmem_params_.push_back(param);
            post_id_to_addr_map_.emplace_back();

        }

        std::vector<social_network::Post> read_posts_from_mongodb = mongodb_get_read_posts();

        for(size_t i=0;i<FLAGS_server_num;i++)
        {
            size_t base_addr = rmems_[i]->rmem_alloc(GB(read_total_size_per_thread+ write_total_size_per_thread),
                                                       rmem::VM_FLAG_READ | rmem::VM_FLAG_WRITE);
            size_t begin = 0;
            for(auto &item:read_posts_from_mongodb){
                size_t item_size = item.ByteSizeLong();
                rmems_[i]->rmem_write_sync(const_cast<char*>(item.SerializeAsString().c_str()), base_addr+begin, item_size);

                post_id_to_addr_map_[i][item.post_id()] = {base_addr+begin,item_size};
                begin += item_size;
                begin = PAGE_ROUND_UP(begin);
            }
            rmem::rt_assert(begin<=GB(read_total_size_per_thread), "read_total_size_per_thread is too small");
            char tmp[10]="01234";
            for(size_t j= PAGE_ROUND_UP(begin);j<GB(read_total_size_per_thread+ write_total_size_per_thread);j+=PAGE_SIZE){
                rmems_[i]->rmem_write_sync(tmp, j, 1);
            }

            rmem_params_[i].set_fork_rmem_addr(rmems_[i]->rmem_fork(base_addr, GB(read_total_size_per_thread+ write_total_size_per_thread)));
            rmem_params_[i].set_fork_size(GB(read_total_size_per_thread+ write_total_size_per_thread));

            RMEM_INFO("init rmem scope %ld finished! Total post num: %ld", i, post_id_to_addr_map_[i].size());
        }

        for(size_t i=0;i<FLAGS_server_num;i++){
            storage_queues.push_back(new STORAGE_QUEUE(kAppMaxBuffer));
            req_num_to_session_num.emplace_back();
        }
        for(auto item : this->server_contexts_){
            item->init_mutex.lock();
            if(item->is_pinged){
                auto _buf = item->rpc_->alloc_msg_buffer(sizeof(RPCMsgReq<PingRPCReq>));
                auto *req = reinterpret_cast<RPCMsgReq<PingRPCReq> *>(_buf.buf_);
                req->req_common.type = RPC_TYPE::RPC_PING;
                req->req_common.req_number = 0;
                req->req_control.timestamp = 0;

                item->forward_all_mpmc_queue->push(_buf);
            }

            item->mongodb_init_finished = true;
            item->init_mutex.unlock();
        }
    }
    ~AppContext()
    {
        hdr_close(latency_hist_);

        for (auto &ctx : client_contexts_)
        {
            delete ctx;
        }
        for (auto &ctx : server_contexts_)
        {
            delete ctx;
        }
    }

    [[maybe_unused]] [[nodiscard]] bool write_latency_and_reset(const std::string &filename) const
    {

        FILE *fp = fopen(filename.c_str(), "w");
        if (fp == nullptr)
        {
            return false;
        }
        hdr_percentiles_print(latency_hist_, fp, 5, 10, CLASSIC);
        fclose(fp);
        hdr_reset(latency_hist_);
        return true;
    }

    std::vector<ClientContext *> client_contexts_;
    std::vector<ServerContext *> server_contexts_;

    hdr_histogram *latency_hist_{};
};

// must be used after init_service_config

void init_specific_config(){
    auto value = config_json_all["compose_post"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    compose_post_addr = value;

    value = config_json_all["user_timeline"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    user_timeline_addr = value;

    value = config_json_all["home_timeline"]["server_addr"];
    rmem::rt_assert(!value.is_null(),"value is null");
    home_timeline_addr = value;

    auto conns = config_json_all["post_storage_mongodb"]["connections"];
    rmem::rt_assert(!conns.is_null(),"value is null");
    mongodb_conns_num = conns;

    auto size = config_json_all["post_storage"]["read_total_size_per_thread"];
    rmem::rt_assert(!size.is_null(),"value is null");
    read_total_size_per_thread = size;

    size = config_json_all["post_storage"]["write_total_size_per_thread"];
    rmem::rt_assert(!size.is_null(),"value is null");
    write_total_size_per_thread = size;

}

void read_post_storage(size_t thread_id, void *buf_) {
    auto* req = static_cast<RPCMsgReq<CommonRPCReq> *>(buf_);
    social_network::PostStorageReadReq post_storage_read_req;

    post_storage_read_req.ParseFromArray(req+1, req->req_control.data_length);

    auto *storage_handler = new StorageHandler();
    storage_handler->req_number = req->req_common.req_number;
    storage_handler->rpc_type = post_storage_read_req.rpc_type();
    storage_handler->is_read =true;

    for(auto item: post_storage_read_req.post_ids()){
        storage_handler->post_ids.push_back(item);

        if(post_id_to_addr_map_[thread_id].count(item)){
            std::pair<size_t, size_t> addr_size = post_id_to_addr_map_[thread_id][item];
            storage_handler->rmem_bufs.push_back(rmems_[thread_id]->rmem_get_msg_buffer(addr_size.second));
            storage_handler->addrs_size.push_back(addr_size);
        } else {
            RMEM_WARN("thread %ld post_id %ld not found", thread_id, item);
            exit(1);
        }
    }

    storage_queues[thread_id]->push(storage_handler);
}

void write_post_storage(size_t thread_id, void *buf_, ClientContext* ctx, MPMC_QUEUE *consumer_back) {
    auto* req = static_cast<RPCMsgReq<CommonRPCReq> *>(buf_);

    auto *post = new social_network::Post();

    size_t data_length = req->req_control.data_length;
    post->ParseFromArray(req+1, static_cast<int>(data_length));

    void*rmem_buf =  rmems_[thread_id]->rmem_get_msg_buffer(data_length);
    memcpy(rmem_buf, req+1, data_length);


    auto *storage_handler = new StorageHandler();
    storage_handler->req_number = req->req_common.req_number;
    storage_handler->rpc_type = static_cast<uint32_t>(RPC_TYPE::RPC_POST_STORAGE_WRITE_REQ);
    storage_handler->is_read = false;
    storage_handler->rmem_bufs.push_back(rmem_buf);
    storage_handler->addrs_size.emplace_back(ctx->get_write_addr(data_length), data_length);

    storage_queues[thread_id]->push(storage_handler);

    erpc::MsgBuffer resp_buf = ctx->rpc_->alloc_msg_buffer_or_die(sizeof(RPCMsgReq<CommonRPCReq>));
    new (resp_buf.buf_) RPCMsgReq<CommonRPCReq>(RPC_TYPE::RPC_POST_STORAGE_WRITE_RESP, req->req_common.req_number, {0});

    std::future<void> write_post_storage_future =
            std::async(std::launch::async, [=](social_network::Post *post_ptr, const erpc::MsgBuffer resp_buffer,MPMC_QUEUE *c_back) {
                mongoc_client_t *mongodb_client = mongoc_client_pool_pop(mongodb_client_pool);
                auto collection = mongoc_client_get_collection(mongodb_client, "post", "post");

                bson_t *new_doc = bson_new();
                BSON_APPEND_INT64(new_doc, "post_id", post_ptr->post_id());
                BSON_APPEND_INT64(new_doc, "timestamp", post_ptr->timestamp());
                BSON_APPEND_UTF8(new_doc, "text", post_ptr->text().c_str());
                BSON_APPEND_INT64(new_doc, "req_id", post_ptr->req_id());
                BSON_APPEND_INT32(new_doc, "post_type", post_ptr->req_id());

                bson_t creator_doc;
                BSON_APPEND_DOCUMENT_BEGIN(new_doc, "creator", &creator_doc);
                BSON_APPEND_INT64(&creator_doc, "user_id", post_ptr->creator().user_id());
                BSON_APPEND_UTF8(&creator_doc, "username", post_ptr->creator().username().c_str());
                bson_append_document_end(new_doc, &creator_doc);

                const char *key;
                int idx = 0;
                char buf[16];

                bson_t url_list;
                BSON_APPEND_ARRAY_BEGIN(new_doc, "urls", &url_list);
                for (auto &url : post_ptr->urls()) {
                    bson_uint32_to_string(idx, &key, buf, sizeof buf);
                    bson_t url_doc;
                    BSON_APPEND_DOCUMENT_BEGIN(&url_list, key, &url_doc);
                    BSON_APPEND_UTF8(&url_doc, "shortened_url", url.shortened_url().c_str());
                    BSON_APPEND_UTF8(&url_doc, "expanded_url", url.expanded_url().c_str());
                    bson_append_document_end(&url_list, &url_doc);
                    idx++;
                }
                bson_append_array_end(new_doc, &url_list);

                bson_t user_mention_list;
                idx = 0;
                BSON_APPEND_ARRAY_BEGIN(new_doc, "user_mentions", &user_mention_list);
                for (auto &user_mention : post_ptr->user_mentions()) {
                    bson_uint32_to_string(idx, &key, buf, sizeof buf);
                    bson_t user_mention_doc;
                    BSON_APPEND_DOCUMENT_BEGIN(&user_mention_list, key, &user_mention_doc);
                    BSON_APPEND_INT64(&user_mention_doc, "user_id", user_mention.user_id());
                    BSON_APPEND_UTF8(&user_mention_doc, "username",
                                     user_mention.username().c_str());
                    bson_append_document_end(&user_mention_list, &user_mention_doc);
                    idx++;
                }
                bson_append_array_end(new_doc, &user_mention_list);

                bson_t media_list;
                idx = 0;
                BSON_APPEND_ARRAY_BEGIN(new_doc, "media", &media_list);
                for (auto &media : post_ptr->media()) {
                    bson_uint32_to_string(idx, &key, buf, sizeof buf);
                    bson_t media_doc;
                    BSON_APPEND_DOCUMENT_BEGIN(&media_list, key, &media_doc);
                    BSON_APPEND_INT64(&media_doc, "media_id", media.media_id());
                    BSON_APPEND_UTF8(&media_doc, "media_type", media.media_type().c_str());
                    bson_append_document_end(&media_list, &media_doc);
                    idx++;
                }
                bson_append_array_end(new_doc, &media_list);
                bson_error_t error;

                bool inserted = mongoc_collection_insert_one(collection, new_doc, nullptr,
                                                             nullptr, &error);

                if(!inserted){
                    RMEM_WARN("insert error, %s", error.message);
                }
                bson_destroy(new_doc);
                mongoc_collection_destroy(collection);
                mongoc_client_pool_push(mongodb_client_pool, mongodb_client);

                delete post_ptr;
                c_back->push(resp_buffer);
            }, post, resp_buf, consumer_back);

}