#include <plasma/client.h>
#include <arrow/util/logging.h>
#include <plasma/test_util.h>
#include <vector>
#include <iostream>
#include <chrono>
#include <thread>
#include <numa.h>
#include <mutex>
#include <gflags/gflags.h>

using namespace plasma;

DEFINE_uint64(thread_num, 1, "thread num");
DEFINE_uint64(test_loop, 100000, "test loop per thread");
DEFINE_uint64(data_size, 1000, "data size");

ObjectID random_object_id_self()
{
    std::random_device rd;
    std::uniform_int_distribution<uint32_t> d(0, std::numeric_limits<uint8_t>::max());
    ObjectID result;
    uint8_t *data = result.mutable_data();
    std::generate(data, data + kUniqueIDSize,
                  [&d, &rd]
                  { return static_cast<uint8_t>(d(rd)); });
    return result;
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

void client_func(size_t thread_id)
{
    // Start up and connect a Plasma client.
    PlasmaClient client;
    ARROW_CHECK_OK(client.Connect("/tmp/plasma"));
    std::vector<ObjectID> object_ids;

    for (size_t i = 0; i < FLAGS_test_loop; i++)
    {
        object_ids.push_back(random_object_id_self());
    }
    std::cout << "thread " << thread_id << " begin" << std::endl;
    Timer timer;
    timer.tic();
    for (size_t i = 0; i < FLAGS_test_loop; i++)
    {
        std::shared_ptr<Buffer> data;
        ARROW_CHECK_OK(client.Create(object_ids[i], FLAGS_data_size, nullptr, 0, &data));
        // Write some data into the object.
        // auto d = data->mutable_data();
        // for (int64_t i = 0; i < data_size; i++)
        // {
        //     d[i] = 1;
        // }
        // Seal the object.
        ARROW_CHECK_OK(client.Seal(object_ids[i]));
    }
    std::cout << timer.toc() * 1.0 / FLAGS_test_loop << std::endl;
    std::cout << "thread " << thread_id << " end" << std::endl;

    // Disconnect the Plasma client.
    ARROW_CHECK_OK(client.Disconnect());
}
std::vector<size_t> get_lcores_for_numa_node(size_t numa_node)
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

int main(int argc, char **argv)
{
    gflags::ParseCommandLineFlags(&argc, &argv, true);

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
}
