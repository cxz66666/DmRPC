#include <apps_commons.h>
#include <app_helpers.h>
#include <page.h>
#include <iostream>
#include <hs_clock.h>
#include <sys/mman.h>
#include <numa.h>

// do we need it ?
DEFINE_uint64(alloc_size, 0, "Alloc size for each request, unit is MB");
DEFINE_string(latency_file, "latency.txt", "Latency file name");
DEFINE_string(bandwidth_file, "bandwidth.txt", "Bandwidth file name");
DEFINE_bool(no_cow, false, "Don't use cow");

double total_speed = 0;
hdr_histogram *latency_hist_;

static size_t file_num;

std::atomic<uint64_t> sync_flag;

void *malloc_2m_hugepage(size_t size) {
    int flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | ((21 & MAP_HUGE_MASK) << MAP_HUGE_SHIFT); // 2^21 == 2M
    int protection = PROT_READ | PROT_WRITE;
    void *p = mmap(NULL, size, protection, flags, -1, 0);
    if (p == MAP_FAILED) {
        std::runtime_error("MAP_FAILED");
    }
    return p;
}

void *malloc_2m_numa(size_t buf_size, int node_id) {
    int page_size = 2 * 1024 * 1024;
    int num_pages = buf_size / page_size;
    void *buf = malloc_2m_hugepage(buf_size);
    for (size_t i = 0; i < buf_size / sizeof(int); i++) {
        (static_cast<int *>(buf))[i] = 0;
    }
    int *status = new int[num_pages];
    int *nodes = new int[num_pages];
    void **bufs = new void *[num_pages];
    for (int i = 0; i < num_pages; i++) {
        status[i] = 0;
        nodes[i] = node_id;
        bufs[i] = reinterpret_cast<void *>(reinterpret_cast<uint64_t>(buf) + static_cast<uint64_t>(i) * page_size);
    }
    int rc = move_pages(getpid(), num_pages, bufs, nodes, status, MPOL_MF_MOVE_ALL);
    if (rc != 0) {
        std::runtime_error("Move page failed, maybe you forget to use 'sudo'");
    }
    for (int i = 0; i < num_pages; i++) {
        assert(status[i] == node_id);
    }

    delete[] status;
    delete[] nodes;
    delete[] bufs;

    return buf;
}

void test_fork(AppContext *c, size_t *raddr, void *file_addr) {
    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, RAND_MAX);

    size_t write_num = FLAGS_block_size / 4096;
    size_t max_num = FLAGS_alloc_size / sizeof(size_t);

    // std::vector<rmem::Timer> timers(FLAGS_concurrency);
    if (FLAGS_concurrency == 1) {
        rmem::Timer now_clock;
        std::vector<std::vector<int>> now_nums(FLAGS_test_loop, std::vector<int>(write_num));
        for (size_t i = 0; i < FLAGS_test_loop; i++) {
            for (size_t j = 0; j < write_num; j++) {
                now_nums[i][j] = dist(rd) % max_num;
            }
        }
        std::vector<size_t> rand_file_nums1(FLAGS_test_loop);
        std::vector<size_t> rand_file_nums2(FLAGS_test_loop);

        for (size_t i = 0; i < FLAGS_test_loop; i++) {
            rand_file_nums1[i] = dist(rd) % file_num;
            rand_file_nums2[i] = dist(rd) % file_num;
        }
        sync_flag++;
        while (sync_flag != FLAGS_client_thread_num) {}

        std::cout << "begin thread " << c->thread_id_ << std::endl;
        now_clock.tic();
        size_t count = 0;
        for (size_t i = 0; i < FLAGS_test_loop; i++) {

            // timers[0].tic();

            for (size_t t = 0; t < write_num; t++) {
                int now_num = now_nums[i][t];
                __sync_fetch_and_add(&raddr[now_num], 1);
                // size_t tmp = raddr[now_num];
                // raddr[now_num] = tmp + 1;
            }

            if (FLAGS_no_cow) {
                memcpy(static_cast<char *>(file_addr) + rand_file_nums1[i] * FLAGS_block_size, static_cast<char *>(file_addr) + rand_file_nums2[i] * FLAGS_block_size, FLAGS_block_size);
            }

            // hdr_record_value_atomic(latency_hist_,
            //                         static_cast<int64_t>(timers[0].toc() * 10));
            count++;
            // if (ctrl_c_pressed == 1)
            // {
            //     break;
            // }
        }
        hdr_record_value_atomic(latency_hist_,
            static_cast<int64_t>(now_clock.toc() * 10));
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

    void *addr = numa_alloc_onnode(FLAGS_alloc_size, 1);
    for (size_t i = 0; i < FLAGS_alloc_size / 4096; i++) {
        ((char *)addr)[i * 4096] = '1';
    }
    void *file_addr = numa_alloc_onnode(file_num * FLAGS_block_size, 1);
    for (size_t i = 0; i < file_num * FLAGS_block_size / 4096; i++) {
        ((char *)file_addr)[i * 4096] = '1';
    }
    test_fork(&c, static_cast<size_t *>(addr), file_addr);
}

int main(int argc, char **argv) {
    signal(SIGINT, ctrl_c_handler);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    // check_common_gflags();
    rmem::rt_assert(kAppMaxConcurrency >= FLAGS_concurrency, "kAppMaxConcurrency must be >= FLAGS_concurrency");
    rmem::rt_assert(FLAGS_alloc_size != 0, "alloc_size must be set");
    rmem::rt_assert(rmem::AsyncReceivedReqSize >= FLAGS_concurrency, "AsyncReceivedReqSize must be >= FLAGS_concurrency");

    FLAGS_alloc_size = MB(FLAGS_alloc_size);
    std::cout << getpid() << std::endl;

    std::vector<std::thread> threads(FLAGS_client_thread_num);

    FLAGS_concurrency = 1;

    int ret = hdr_init(1, 1000 * 1000 * 10, 3,
        &latency_hist_);
    rmem::rt_assert(ret == 0, "hdr_init failed");

    file_num = (1024 << 20) / FLAGS_block_size;

    for (size_t i = 0; i < FLAGS_client_thread_num; i++) {
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