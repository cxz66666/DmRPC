#include <api.h>
#include <app_helpers.h>
int main()
{
    rmem::rmem_init(rmem::get_uri_for_process(0), 0);

    rmem::Context *ctx = rmem::open_context(0);

    rmem::close_context(ctx);

    return 0;
}