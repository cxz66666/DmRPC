/// This is an example of Ray C++ application. Please visit
/// `https://docs.ray.io/en/master/ray-core/walkthrough.html#installation`
/// for more details.

/// including the `<ray/api.h>` header
#include <ray/api.h>
#include <vector>
#include <iostream>
#include <chrono>
#include <thread>
#include <numa.h>
#include <mutex>
#include <gflags/gflags.h>
#include <array>
#include <msgpack.hpp>

DEFINE_uint64(thread_num, 2, "thread num");
DEFINE_uint64(test_loop, 10000, "test loop per thread");
DEFINE_uint64(write_num, 1, "write num");

class Object_32k
{
public:
    uint8_t data[32768];
    MSGPACK_DEFINE(data);
};
Object_32k global_object_32k;
uint8_t global_4k_buffer[4096];

std::vector<size_t>
get_lcores_for_numa_node(size_t numa_node)
{

    std::vector<size_t> ret;
    auto num_lcores = static_cast<size_t>(numa_num_configured_cpus());

    for (size_t i = 0; i < num_lcores; i++)
    {
        if (numa_node == static_cast<size_t>(numa_node_of_cpu(static_cast<int>(i))))
        {
            ret.push_back(i);
        }
    }

    return ret;
}
void bind_to_core(std::thread &thread, size_t numa_node,
                  size_t numa_local_index)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    const std::vector<size_t> lcore_vec = get_lcores_for_numa_node(numa_node);
    if (numa_local_index >= lcore_vec.size())
    {
        std::runtime_error("numa_local_index is out of range");
        return;
    }

    const size_t global_index = lcore_vec.at(numa_local_index);

    CPU_SET(global_index, &cpuset);
    int rc = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t),
                                    &cpuset);
    if (rc != 0)
    {
        std::runtime_error("Error setting thread affinity");
    }
}

class Timer
{
public:
    Timer()
        : t1(res::zero()), t2(res::zero())
    {
        tic();
    }

    ~Timer() = default;

    void tic()
    {
        t1 = clock::now();
    }

    void toc(size_t thread_id)
    {
        t2 = clock::now();
        res_vec.push_back(std::chrono::duration_cast<res>(t2 - t1).count());
        std::cout << "thread:" << thread_id << "  time: "
                  << std::chrono::duration_cast<res>(t2 - t1).count() / 1e6 << "s." << std::endl;
    }
    long toc()
    {
        t2 = clock::now();
        return std::chrono::duration_cast<res>(t2 - t1).count();
    }

private:
    typedef std::chrono::high_resolution_clock clock;
    typedef std::chrono::microseconds res;
    std::vector<long> res_vec;
    clock::time_point t1;
    clock::time_point t2;
};

bool write_latency_and_reset(const std::string &filename)
{

    FILE *fp = fopen(filename.c_str(), "w");
    if (fp == nullptr)
    {
        return false;
    }
    fclose(fp);
    return true;
}

/// common function
int handler_copy(Object_32k object)
{
    for (size_t i = 0; i < FLAGS_write_num; i++)
    {
        memcpy(object.data + 4096 * i, global_4k_buffer, 4096);
    }
    // print ray node id
    return 0;
}
/// Declare remote function
RAY_REMOTE(handler_copy);

int test(int a)
{
    return a;
}
RAY_REMOTE(test);

void client_func(size_t thread_id)
{
    std::cout << "thread " << thread_id << " begin" << std::endl;
    std::vector<ray::ObjectRef<int>> refs(FLAGS_test_loop);
    Timer timer;
    timer.tic();

    for (size_t i = 0; i < FLAGS_test_loop; i++)
    {
        auto object = ray::Put(global_object_32k);
        auto task_object = ray::Task(handler_copy).SetResource("ip:10.0.0.2", 0.001).Remote(object);
        refs[i] = task_object;
        ray::Get(task_object);
    }
    // ray::Wait(refs, FLAGS_test_loop, 10000000);
    std::cout << timer.toc() * 1.0 / FLAGS_test_loop << std::endl;
    std::cout << "thread " << thread_id << " end" << std::endl;
}

int main(int argc, char **argv)
{
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    ray::RayConfig config;
    config.address = "10.0.0.3:6379";
    /// initialization
    ray::Init(config);

    // /// put and get object
    // auto object = ray::Put(100);
    // auto put_get_result = *(ray::Get(object));
    // std::cout << "put_get_result = " << put_get_result << std::endl;

    // /// common task

    // auto task_object = ray::Task(Plus).SetResource("ip:10.0.0.2", 0.001).Remote(1, 2);
    // int task_result = *(ray::Get(task_object));
    // std::cout << "task_result = " << task_result << std::endl;

    // task_object = ray::Task(Plus).SetResource("ip:10.0.0.2", 0.001).Remote(task_object, 10);
    // task_result = *(ray::Get(task_object));
    // std::cout << "task_result = " << task_result << std::endl;

    std::vector<std::thread> threads(FLAGS_thread_num);
    for (size_t i = 0; i < FLAGS_thread_num; i++)
    {
        threads[i] = std::thread(client_func, i);
        bind_to_core(threads[i], 0, i);
    }
    for (auto &t : threads)
    {
        t.join();
    }

    /// shutdown
    ray::Shutdown();
    write_latency_and_reset("latency.txt");

    return 0;
}
