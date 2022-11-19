#include <api.h>
#include <app_helpers.h>
#include <bits/stdc++.h>

int main()
{
    rmem::rmem_init(rmem::get_uri_for_process(0), 0);

    rmem::Context *ctx = rmem::open_context(0);

    rmem::connect_session(ctx, rmem::get_uri_for_process(1), 0);
    std::cout << 123 << std::endl;

    usleep(100000);

    unsigned long raddr1 = rmem::rmem_alloc(ctx, GB(1), rmem::VM_FLAG_READ | rmem::VM_FLAG_WRITE);

    std::cout << "raddr: " << raddr1 << std::endl;

    unsigned long raddr2 = rmem::rmem_alloc(ctx, GB(1), rmem::VM_FLAG_READ | rmem::VM_FLAG_WRITE);

    std::cout << "raddr: " << raddr2 << std::endl;

    unsigned long raddr3 = rmem::rmem_alloc(ctx, GB(1), rmem::VM_FLAG_READ | rmem::VM_FLAG_WRITE);

    std::cout << "raddr: " << raddr3 << std::endl;
    rmem::rmem_free(ctx, raddr3, GB(1));
    std::cout
        << 456 << std::endl;
    rmem::disconnect_session(ctx);
    rmem::close_context(ctx);

    return 0;
}