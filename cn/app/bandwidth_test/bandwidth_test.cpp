#include "apps_commons.h"
#include <api.h>
#include <app_helpers.h>
#include <page.h>
#include <iostream>
#include <hs_clock.h>
// do we need it ?
DEFINE_uint64(alloc_size, 0, "Alloc size for each request, unit is GB");

void client_func(size_t thread_id)
{
    AppContext c;
    c.thread_id_ = thread_id;

    c.ctx = new rmem::Rmem(FLAGS_numa_node);

    rmem::rt_assert(c.ctx != nullptr, "Failed to create rmem context");

    for (size_t i = 0; i < FLAGS_concurrency; i++)
    {
        c.req_msgbuf[i] = c.ctx->rmem_get_msg_buffer(FLAGS_block_size);
    }

    for (size_t i = 0; i < FLAGS_concurrency; i++)
    {
        c.resp_msgbuf[i] = c.ctx->rmem_get_msg_buffer(FLAGS_block_size);
    }
    char buf[10] = "123456789";

    c.ctx->rmem_get_msg_buffer(FLAGS_block_size);
    c.ctx->connect_session(rmem::get_uri_for_process(FLAGS_server_index), thread_id % FLAGS_server_thread_num);
    unsigned long raddr = c.ctx->rmem_alloc(GB(FLAGS_alloc_size), rmem::VM_FLAG_READ | rmem::VM_FLAG_WRITE);

    for (size_t i = 0; i < GB(FLAGS_alloc_size); i += PAGE_SIZE)
    {
        c.ctx->rmem_write_sync(buf, i + raddr, strlen(buf));
    }
    int *res_buffer= new int[FLAGS_concurrency];
    int loop_num=0;
    char msg_buffer[100];

    while(!ctrl_c_pressed) {
        rmem::Timer timer;
        size_t begin_addr=0;
        size_t now_resp=0;
        size_t total_resp=GB(FLAGS_alloc_size)/FLAGS_block_size;
        loop_num++;
        timer.tic();

        for(size_t i=0;i<FLAGS_concurrency;i++,begin_addr+=FLAGS_block_size){
            c.ctx->rmem_read_async(c.resp_msgbuf[0], begin_addr + raddr, FLAGS_block_size);
        }
        while(true){
            int t=c.ctx->rmem_poll(res_buffer,static_cast<int>(FLAGS_concurrency));
            now_resp+=t;
            if(unlikely(now_resp==total_resp)){
                break;
            }
            while(t--&&begin_addr<GB(FLAGS_alloc_size)){
                c.ctx->rmem_read_async(c.resp_msgbuf[0], begin_addr + raddr, FLAGS_block_size);
                begin_addr+=FLAGS_block_size;
            }
        }
        sprintf(msg_buffer,"thread %ld, loop num %d\n",thread_id,loop_num);
        timer.toc(msg_buffer);

    }

    c.ctx->rmem_free(raddr, GB(FLAGS_alloc_size));
    c.ctx->disconnect_session();

    delete []res_buffer;
    for (size_t i = 0; i < FLAGS_concurrency; i++)
    {
        c.ctx->rmem_free_msg_buffer(c.req_msgbuf[i]);
    }

    for (size_t i = 0; i < FLAGS_concurrency; i++)
    {
        c.ctx->rmem_free_msg_buffer(c.resp_msgbuf[i]);
    }
    delete c.ctx;

}

int main(int argc, char **argv)
{
    signal(SIGINT, ctrl_c_handler);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    check_common_gflags();
    rmem::rt_assert(kAppMaxConcurrency >= FLAGS_concurrency, "kAppMaxConcurrency must be >= FLAGS_concurrency");
    rmem::rt_assert(FLAGS_alloc_size != 0, "alloc_size must be set");
    rmem::rt_assert(FLAGS_alloc_size%FLAGS_client_thread_num==0,"alloc_size must be divisible by client_thread_num");
    rmem::rt_assert(rmem::AsyncReceivedReqSize>=FLAGS_concurrency, "AsyncReceivedReqSize must be >= FLAGS_concurrency");

    std::cout<< getpid() <<std::endl;
    rmem::rmem_init(rmem::get_uri_for_process(FLAGS_client_index), FLAGS_numa_node);

    FLAGS_alloc_size/=FLAGS_client_thread_num;

    std::vector<std::thread> threads(FLAGS_client_thread_num);

    threads[0] = std::thread(client_func, 0);
    usleep(2e6);

    rmem::bind_to_core(threads[0], FLAGS_numa_node_user_thread, 0);

    for(size_t i=1;i<FLAGS_client_thread_num;i++){
        threads[i] = std::thread(client_func, i);
        rmem::bind_to_core(threads[i], FLAGS_numa_node_user_thread, i);
    }

    for (auto &t : threads)
    {
        t.join();
    }
}