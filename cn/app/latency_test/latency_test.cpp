#include <apps_commons.h>
#include <api.h>
#include <app_helpers.h>
#include <page.h>
#include <iostream>
#include <hs_clock.h>
// do we need it ?
DEFINE_uint64(alloc_size, 0, "Alloc size for each request, unit is GB");
DEFINE_string(read_result_file, "/tmp/read_latency", "Output result to special file");
DEFINE_string(write_result_file, "/tmp/write_latency", "Output result to special file");

void test_read(AppContext *c, unsigned long raddr)
{
    std::vector<rmem::Timer> timers(FLAGS_concurrency);
    if (FLAGS_concurrency == 1)
    {
        size_t begin_addr = 0;
        for (size_t i = 0; i < FLAGS_test_loop; i++, begin_addr = (begin_addr + FLAGS_block_size) % FLAGS_alloc_size)
        {
            timers[0].tic();
            c->ctx->rmem_read_sync(c->resp_msgbuf[0], begin_addr + raddr, FLAGS_block_size);
            hdr_record_value(c->latency_hist_,
                             static_cast<int64_t>(timers[0].toc() * 10));
        }
    }
    else
    {
        int *res_buffer = new int[FLAGS_concurrency];
        size_t begin_addr = 0;

        size_t send_req = 0;
        size_t send_req_loop = 0;
        for (size_t i = 0; i < FLAGS_concurrency; i++, begin_addr = (begin_addr + FLAGS_block_size) % FLAGS_alloc_size)
        {
            timers[send_req_loop].tic();
            c->ctx->rmem_read_async(c->resp_msgbuf[0], begin_addr + raddr, FLAGS_block_size);
            send_req_loop = (send_req_loop + 1) % FLAGS_concurrency;
            send_req++;
        }

        size_t now_resp_loop = 0;
        size_t now_resp = 0;
        size_t total_resp = FLAGS_concurrency * FLAGS_test_loop;
        while (true)
        {
            int t = c->ctx->rmem_poll(res_buffer, static_cast<int>(FLAGS_concurrency));
            for (int i = 0; i < t; i++)
            {
                hdr_record_value(c->latency_hist_,
                                 static_cast<int64_t>(timers[now_resp_loop].toc() * 10));
                now_resp_loop = (now_resp_loop + 1) % FLAGS_concurrency;
            }
            now_resp += t;
            if (unlikely(now_resp == total_resp))
            {
                break;
            }
            for (int i = 0; i < t && send_req < total_resp; i++, begin_addr = (begin_addr + FLAGS_block_size) % FLAGS_alloc_size)
            {
                timers[send_req_loop].tic();
                c->ctx->rmem_read_async(c->resp_msgbuf[0], begin_addr + raddr, FLAGS_block_size);
                send_req_loop = (send_req_loop + 1) % FLAGS_concurrency;
                send_req++;
            }
        }
        delete[] res_buffer;
    }
}

void test_write(AppContext *c, unsigned long raddr)
{
    std::vector<rmem::Timer> timers(FLAGS_concurrency);
    if (FLAGS_concurrency == 1)
    {
        size_t begin_addr = 0;
        for (size_t i = 0; i < FLAGS_test_loop; i++, begin_addr = (begin_addr + FLAGS_block_size) % FLAGS_alloc_size)
        {
            timers[0].tic();
            c->ctx->rmem_write_sync(c->resp_msgbuf[0], begin_addr + raddr, FLAGS_block_size);
            hdr_record_value(c->latency_hist_,
                             static_cast<int64_t>(timers[0].toc() * 10));
        }
    }
    else
    {
        int *res_buffer = new int[FLAGS_concurrency];
        size_t begin_addr = 0;
        size_t send_req_loop = 0;
        size_t send_req = 0;
        for (size_t i = 0; i < FLAGS_concurrency; i++, begin_addr = (begin_addr + FLAGS_block_size) % FLAGS_alloc_size)
        {
            timers[send_req_loop].tic();
            c->ctx->rmem_write_async(c->resp_msgbuf[send_req_loop], begin_addr + raddr, FLAGS_block_size);
            send_req_loop = (send_req_loop + 1) % FLAGS_concurrency;
            send_req++;
        }

        size_t now_resp_loop = 0;
        size_t now_resp = 0;
        size_t total_resp = FLAGS_concurrency * FLAGS_test_loop;
        while (true)
        {
            int t = c->ctx->rmem_poll(res_buffer, static_cast<int>(FLAGS_concurrency));
            for (int i = 0; i < t; i++)
            {
                hdr_record_value(c->latency_hist_,
                                 static_cast<int64_t>(timers[now_resp_loop].toc() * 10));
                now_resp_loop = (now_resp_loop + 1) % FLAGS_concurrency;
            }
            now_resp += t;
            if (unlikely(now_resp == total_resp))
            {
                break;
            }
            for (int i = 0; i < t && likely(send_req < total_resp); i++, begin_addr = (begin_addr + FLAGS_block_size) % FLAGS_alloc_size)
            {
                timers[send_req_loop].tic();
                c->ctx->rmem_write_async(c->resp_msgbuf[send_req_loop], begin_addr + raddr, FLAGS_block_size);
                send_req_loop = (send_req_loop + 1) % FLAGS_concurrency;
                send_req++;
            }
        }
        delete[] res_buffer;
    }
}

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
    unsigned long raddr = c.ctx->rmem_alloc(FLAGS_alloc_size, rmem::VM_FLAG_READ | rmem::VM_FLAG_WRITE);
    for (size_t i = 0; i < FLAGS_alloc_size; i += PAGE_SIZE)
    {
        c.ctx->rmem_write_sync(buf, i + raddr, strlen(buf));
    }

    test_read(&c, raddr);
    rmem::rt_assert(c.write_latency_and_reset(FLAGS_read_result_file));

    test_write(&c, raddr);
    rmem::rt_assert(c.write_latency_and_reset(FLAGS_write_result_file));

    c.ctx->rmem_free(raddr, FLAGS_alloc_size);
    c.ctx->disconnect_session();

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
    rmem::rt_assert(FLAGS_alloc_size % FLAGS_client_thread_num == 0, "alloc_size must be divisible by client_thread_num");
    rmem::rt_assert(rmem::AsyncReceivedReqSize >= FLAGS_concurrency, "AsyncReceivedReqSize must be >= FLAGS_concurrency");

    FLAGS_alloc_size = GB(FLAGS_alloc_size);
    std::cout << getpid() << std::endl;
    rmem::rmem_init(rmem::get_uri_for_process(FLAGS_client_index), FLAGS_numa_node);

    FLAGS_alloc_size /= FLAGS_client_thread_num;

    std::vector<std::thread> threads(FLAGS_client_thread_num);

    threads[0] = std::thread(client_func, 0);
    usleep(2e6);

    rmem::bind_to_core(threads[0], FLAGS_numa_node_user_thread, 0);

    for (size_t i = 1; i < FLAGS_client_thread_num; i++)
    {
        threads[i] = std::thread(client_func, i);
        rmem::bind_to_core(threads[i], FLAGS_numa_node_user_thread, i);
    }

    for (auto &t : threads)
    {
        t.join();
    }
}