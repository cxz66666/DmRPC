#include <phmap.h>
#include <gflags/gflags.h>
#include <hdr/hdr_histogram.h>
#include <page.h>
#include <bits/stdc++.h>
#include <hs_clock.h>
phmap::flat_hash_map<unsigned long, unsigned long> addr_map;

DEFINE_uint64(total_size_gb, 10, "total address gb");
DEFINE_uint64(test_loop, 10, "Test loop");
DEFINE_uint64(per_loop_num, 100, "per test loop access number");

int main(int argc, char **argv)
{
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, RAND_MAX);

    FLAGS_total_size_gb *= GB(1);

    for (size_t i = 0; i < FLAGS_total_size_gb; i += PAGE_SIZE)
    {
        addr_map[i] = i;
    }
    size_t total_key_num = FLAGS_total_size_gb / PAGE_SIZE;
    std::vector<std::vector<unsigned long>> random_nums(FLAGS_test_loop);

    for (size_t i = 0; i < FLAGS_test_loop; i++)
    {
        for (size_t j = 0; j < FLAGS_per_loop_num; j++)
        {
            random_nums[i].push_back((dist(rd) % total_key_num) * PAGE_SIZE);
        }
    }
    rmem::Timer timer;
    std::cout << "begin test" << std::endl;
    for (size_t i = 0; i < FLAGS_test_loop; i++)
    {
        timer.tic();
        for (size_t j = 0; j < FLAGS_per_loop_num; j++)
        {
            unsigned long key = random_nums[i][j];
            unsigned long value = addr_map[key];
            if (unlikely(key != value))
            {
                std::runtime_error("doesn't match");
            }
        }
        std::cout << 1.0 * timer.toc() / FLAGS_per_loop_num << std::endl;
    }
    std::cout << "end test" << std::endl;
}