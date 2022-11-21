#include <api.h>
#include <app_helpers.h>
#include <bits/stdc++.h>
#include <page.h>
int main()
{

    char object[10] = "123456789";
    char object2[10] = "987654321";

    char recv_buf[10];
    rmem::rmem_init(rmem::get_uri_for_process(0), 0);

    rmem::Rmem *ctx = new rmem::Rmem(0);
    rmem::Rmem *ctx2 = new rmem::Rmem(0);

    ctx->connect_session(rmem::get_uri_for_process(1), 0);
    ctx2->connect_session(rmem::get_uri_for_process(1), 1);

    usleep(100000);

    unsigned long raddr1 = ctx->rmem_alloc(GB(1), rmem::VM_FLAG_READ | rmem::VM_FLAG_WRITE);

    std::cout << "raddr: " << raddr1 << std::endl;

    for (size_t i = 0; i < GB(1); i += PAGE_SIZE)
    {
        ctx->rmem_write_sync(object, i + raddr1, 10);
    }
    usleep(500000);
    unsigned long fork_addr1 = ctx->rmem_fork(raddr1, GB(1) / 2);
    std::cout << "fork_addr1: " << fork_addr1 << std::endl;

    unsigned long join_addr1 = ctx2->rmem_join(fork_addr1, 0, ctx->concurrent_store_->get_remote_session_num());
    std::cout << "join_addr1: " << join_addr1 << " remote session" << ctx->concurrent_store_->get_remote_session_num() << std::endl;

    for (size_t i = 0; i < GB(1) / 2; i += PAGE_SIZE)
    {
        ctx2->rmem_write_sync(object2, i + join_addr1, 10);
    }
    for (size_t i = 0; i < GB(1); i += PAGE_SIZE)
    {
        ctx->rmem_read_sync(recv_buf, i + raddr1, 10);
        std::cout << recv_buf << std::endl;
    }

    for (size_t i = 0; i < GB(1) / 2; i += PAGE_SIZE)
    {
        ctx2->rmem_read_sync(recv_buf, i + join_addr1, 10);
        std::cout << recv_buf << std::endl;
    }
    usleep(5000000);

    ctx->rmem_free(raddr1, GB(1));
    ctx2->rmem_free(join_addr1, GB(1) / 2);
    ctx->disconnect_session();
    ctx2->disconnect_session();
    delete ctx;
    delete ctx2;

    return 0;
}