#include "numautil.h"
#include "commons.h"
#include <numa.h>

namespace rmem
{
    size_t num_lcores_per_numa_node()
    {
        return static_cast<size_t>(numa_num_configured_cpus() /
                                   numa_num_configured_nodes());
    }

    std::vector<size_t> get_lcores_for_numa_node(size_t numa_node)
    {
        rt_assert(numa_node <= static_cast<size_t>(numa_max_node()));

        std::vector<size_t> ret;
        size_t num_lcores = static_cast<size_t>(numa_num_configured_cpus());

        for (size_t i = 0; i < num_lcores; i++)
        {
            if (numa_node == static_cast<size_t>(numa_node_of_cpu(i)))
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
            RMEM_ERROR(
                "eRPC: Requested binding to core %zu (zero-indexed) on NUMA node %zu, "
                "which has only %zu cores. Ignoring, but this can cause very low "
                "performance.\n",
                numa_local_index, numa_node, lcore_vec.size());
            return;
        }

        const size_t global_index = lcore_vec.at(numa_local_index);

        CPU_SET(global_index, &cpuset);
        int rc = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t),
                                        &cpuset);
        rt_assert(rc == 0, "Error setting thread affinity");
    }
}
