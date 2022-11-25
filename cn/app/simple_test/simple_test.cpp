#include <api.h>
#include <app_helpers.h>
#include <bits/stdc++.h>
#include <page.h>
int main()
{

    rmem::rmem_init(rmem::get_uri_for_process(0), 0);

    rmem::Rmem *ctx = new rmem::Rmem(0);
    rmem::Rmem *ctx2 = new rmem::Rmem(0);

    void *object = ctx->rmem_get_msg_buffer(10);
    memcpy(object, "123456789", 10);

    void *object2 = ctx2->rmem_get_msg_buffer(10);
    memcpy(object2, "987654321", 10);

    ctx->connect_session(rmem::get_uri_for_process(1), 0);
    ctx2->connect_session(rmem::get_uri_for_process(1), 1);

    unsigned long raddr1 = ctx->rmem_alloc(GB(8), rmem::VM_FLAG_READ | rmem::VM_FLAG_WRITE);

    std::cout << "raddr: " << raddr1 << std::endl;

    for (size_t i = 0; i < GB(8); i += PAGE_SIZE)
    {
        ctx->rmem_write_sync(object, i + raddr1, 10);
    }
    unsigned long fork_addr1 = ctx->rmem_fork(raddr1, GB(8) / 2);
    std::cout << "fork_addr1: " << fork_addr1 << std::endl;

    unsigned long join_addr1 = ctx2->rmem_join(fork_addr1, 0, ctx->concurrent_store_->get_remote_session_num());
    std::cout << "join_addr1: " << join_addr1 << " remote session" << ctx->concurrent_store_->get_remote_session_num() << std::endl;

    for (size_t i = 0; i < GB(8) / 2; i += PAGE_SIZE)
    {
        ctx2->rmem_write_sync(object2, i + join_addr1, 10);
    }
    for (size_t i = 0; i < GB(8); i += PAGE_SIZE)
    {
        ctx->rmem_read_sync(object, i + raddr1, 10);
        std::cout << static_cast<char *>(object) << std::endl;
    }

    for (size_t i = 0; i < GB(8) / 2; i += PAGE_SIZE)
    {
        ctx2->rmem_read_sync(object2, i + join_addr1, 10);
        std::cout << static_cast<char *>(object2) << std::endl;
    }
    usleep(5000000);

    std::cout << ctx2->rmem_free_msg_buffer(object2) << std::endl;
    std::cout << ctx->rmem_free_msg_buffer(object) << std::endl;
    ctx->rmem_free(raddr1, GB(8));
    ctx2->rmem_free(join_addr1, GB(8) / 2);
    ctx->disconnect_session();
    ctx2->disconnect_session();
    delete ctx;
    delete ctx2;

    return 0;
}