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
    std::string addr = config_json[service_name + "-mongodb"]["addr"];
    int port = config_json[service_name + "-mongodb"]["port"];
    std::string uri_str = "mongodb://" + addr + ":" +
        std::to_string(port) + "/?appname=" + service_name + "-service";

    mongoc_init();
    bson_error_t error;
    mongoc_uri_t *mongodb_uri =
            mongoc_uri_new_with_error(uri_str.c_str(), &error);

    if (!mongodb_uri) {
        RMEM_ERROR("failed to parse URI, error message is %s",error.message);
        return nullptr;
    } else {
        // maybe not use it
        if (config_json["ssl"]["enabled"]) {
            std::string ca_file = config_json["ssl"]["caPath"];

            mongoc_uri_set_option_as_bool(mongodb_uri, MONGOC_URI_TLS, true);
            mongoc_uri_set_option_as_utf8(mongodb_uri, MONGOC_URI_TLSCAFILE, ca_file.c_str());
            mongoc_uri_set_option_as_bool(mongodb_uri, MONGOC_URI_TLSALLOWINVALIDHOSTNAMES, true);
        }

        mongoc_client_pool_t *client_pool= mongoc_client_pool_new(mongodb_uri);
        mongoc_client_pool_max_size(client_pool, max_size);
        return client_pool;
    }
}