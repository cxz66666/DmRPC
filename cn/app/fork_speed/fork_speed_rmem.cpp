#include <apps_commons.h>
#include <app_helpers.h>
#include <page.h>
#include <iostream>
#include <hs_clock.h>
// do we need it ?
DEFINE_uint64(alloc_size, 0, "Alloc size for each request, unit is GB");
DEFINE_string(latency_file, "latency.txt", "Latency file name");
DEFINE_string(bandwidth_file, "bandwidth.txt", "Bandwidth file name");

double total_speed = 0;
hdr_histogram *latency_hist_;
std::atomic<uint64_t> sync_flag;


void test_fork(AppContext *c, unsigned long raddr) {
    std::vector<rmem::Timer> timers(FLAGS_concurrency);
    if (FLAGS_concurrency == 1) {
        sync_flag++;
        while (sync_flag != FLAGS_client_thread_num) {}

        std::cout << "begin thread " << c->thread_id_ << std::endl;
        rmem::Timer now_clock;
        now_clock.tic();
        size_t begin_addr = 0;

        size_t count = 0;
        for (size_t i = 0; i < FLAGS_test_loop; i++, begin_addr = (begin_addr + FLAGS_block_size) % FLAGS_alloc_size) {
            timers[0].tic();
            c->ctx->rmem_fork(begin_addr + raddr, FLAGS_block_size);
            hdr_record_value_atomic(latency_hist_,
                static_cast<int64_t>(timers[0].toc() * 10));
            count++;
            if (ctrl_c_pressed == 1) {
                break;
            }
        }
        total_speed += (double)count * 1e6 / now_clock.toc();
        std::cout << "end thread " << c->thread_id_ << std::endl;

    }
}

bool write_bandwidth(const std::string &filename) {
    FILE *fp = fopen(filename.c_str(), "w");
    if (fp == nullptr) {
        return false;
    }
    fprintf(fp, "%f\n", total_speed);
    fclose(fp);
    return true;
}
bool write_latency_and_reset(const std::string &filename) {

    FILE *fp = fopen(filename.c_str(), "w");
    if (fp == nullptr) {
        return false;
    }
    hdr_percentiles_print(latency_hist_, fp, 5, 10, CLASSIC);
    fclose(fp);
    hdr_reset(latency_hist_);
    return true;
}

void client_func(size_t thread_id) {
    AppContext c;
    c.thread_id_ = thread_id;

    c.ctx = new rmem::Rmem(FLAGS_numa_node);

    rmem::rt_assert(c.ctx != nullptr, "Failed to create rmem context");

    char buf[10] = "123456789";

    c.ctx->connect_session(rmem::get_uri_for_process(FLAGS_server_index), thread_id % FLAGS_server_thread_num);
    unsigned long raddr = c.ctx->rmem_alloc(FLAGS_alloc_size, rmem::VM_FLAG_READ | rmem::VM_FLAG_WRITE);

    for (size_t i = 0; i < FLAGS_alloc_size; i += PAGE_SIZE) {
        c.ctx->rmem_write_sync(buf, i + raddr, strlen(buf));
    }
    sleep(2);
    test_fork(&c, raddr);
    c.ctx->rmem_free(raddr, FLAGS_alloc_size);
    c.ctx->disconnect_session();

    delete c.ctx;
}

int main(int argc, char **argv) {
    signal(SIGINT, ctrl_c_handler);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    // check_common_gflags();
    rmem::rt_assert(kAppMaxConcurrency >= FLAGS_concurrency, "kAppMaxConcurrency must be >= FLAGS_concurrency");
    rmem::rt_assert(FLAGS_alloc_size != 0, "alloc_size must be set");
    rmem::rt_assert(rmem::AsyncReceivedReqSize >= FLAGS_concurrency, "AsyncReceivedReqSize must be >= FLAGS_concurrency");

    FLAGS_alloc_size = GB(FLAGS_alloc_size);
    std::cout << getpid() << std::endl;
    rmem::rmem_init(rmem::get_uri_for_process(FLAGS_client_index), FLAGS_numa_node);

    std::vector<std::thread> threads(FLAGS_client_thread_num);

    FLAGS_concurrency = 1;
    int ret = hdr_init(1, 1000 * 1000 * 10, 3,
        &latency_hist_);
    rmem::rt_assert(ret == 0, "hdr_init failed");

    threads[0] = std::thread(client_func, 0);
    usleep(2e6);

    rmem::bind_to_core(threads[0], FLAGS_numa_node_user_thread, 0);

    for (size_t i = 1; i < FLAGS_client_thread_num; i++) {
        threads[i] = std::thread(client_func, i);
        rmem::bind_to_core(threads[i], FLAGS_numa_node_user_thread, i);
    }

    for (auto &t : threads) {
        t.join();
    }
    write_bandwidth(FLAGS_bandwidth_file);
    write_latency_and_reset(FLAGS_latency_file);
    hdr_close(latency_hist_);
}