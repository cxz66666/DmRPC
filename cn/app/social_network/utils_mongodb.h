#pragma once

#include <mongoc/mongoc.h>
#include <bson/bson.h>
#include <nlohmann/json.hpp>
#include <app_helpers.h>


#define SERVER_SELECTION_TIMEOUT_MS 300

mongoc_client_pool_t* init_mongodb_client_pool(
        const nlohmann::json &config_json,
        const std::string &service_name,
        uint32_t max_size
        ){
    std::string addr = config_json[service_name + "_mongodb"]["addr"];
    std::string port = config_json[service_name + "_mongodb"]["port"];
    std::string uri_str = "mongodb://" + addr + ":" +
        port + "/?appname=" + service_name + "-service";

    mongoc_init();
    bson_error_t error;
    mongoc_uri_t *mongodb_uri =
            mongoc_uri_new_with_error(uri_str.c_str(), &error);

    if (!mongodb_uri) {
        RMEM_ERROR("failed to parse URI, error message is %s",error.message);
        return nullptr;
    } else {
        mongoc_client_pool_t *client_pool= mongoc_client_pool_new(mongodb_uri);
        mongoc_client_pool_max_size(client_pool, max_size);
        return client_pool;
    }
}