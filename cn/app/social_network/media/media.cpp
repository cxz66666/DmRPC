#include <hdr/hdr_histogram.h>
#include <mongoc.h>
#include "atomic_queue/atomic_queue.h"
#include "../social_network_commons.h"
#include "../social_network.pb.h"
#include "phmap.h"
#include "spinlock_mutex.h"
#include "../utils_mongodb.h"

int main(){
    social_network::Post post1,post2;

    // init post1 and post2 with random data
    post1.set_post_id(1);
    post1.set_timestamp(123123);
    post1.mutable_creator()->set_user_id(123);
    post1.mutable_creator()->set_username("user1");
    post1.set_text("text1");

    auto m1 = post1.add_user_mentions();
    m1->set_user_id(123);
    m1->set_username("user1");

    m1= post1.add_user_mentions();
    m1->set_user_id(456);
    m1->set_username("user2");


    post2 = post1;

    auto m2 = post2.add_user_mentions();
    m2->set_user_id(789);
    m2->set_username("user3");

    m2= post2.add_user_mentions();
    m2->set_user_id(101112);
    m2->set_username("user4");


    social_network::PostStorageReadResp resp;
    resp.MergeFromString(post1.SerializeAsString());
    resp.MergeFromString(post2.SerializeAsString());

    std::cout<< resp.posts_size() << std::endl;
    for(const auto& m: resp.posts()) {
        std::cout<< m.user_mentions_size() << std::endl;
    }

}

