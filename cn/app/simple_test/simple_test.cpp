#include <api.h>
#include <app_helpers.h>
#include <bits/stdc++.h>
#include <page.h>
int main()
{

    char object[10] = "123456789";
    // char recv_buf[10];
    rmem::rmem_init(rmem::get_uri_for_process(0), 0);

    rmem::Context *ctx = rmem::open_context(0);
    rmem::Context *ctx2 = rmem::open_context(0);

    rmem::connect_session(ctx, rmem::get_uri_for_process(1), 0);

    rmem::connect_session(ctx2, rmem::get_uri_for_process(1), 1);

    usleep(100000);

    unsigned long raddr1 = rmem::rmem_alloc(ctx, GB(1), rmem::VM_FLAG_READ | rmem::VM_FLAG_WRITE);

    std::cout << "raddr: " << raddr1 << std::endl;

    for (size_t i = 0; i < GB(1); i += PAGE_SIZE)
    {
        rmem::rmem_write_async(ctx, object, i + raddr1, 10);
    }
    usleep(100000);
    unsigned long fork_addr1 = rmem::rmem_fork(ctx, raddr1, GB(1) / 2);
    std::cout << "fork_addr1: " << fork_addr1 << std::endl;

    rmem::rmem_join(ctx2, fork_addr1, 0, ctx->concurrent_store_->get_session_num());

    rmem::rmem_free(ctx, raddr1, GB(1));
    rmem::disconnect_session(ctx);
    rmem::disconnect_session(ctx2);
    rmem::close_context(ctx);
    rmem::close_context(ctx2);
    return 0;
}