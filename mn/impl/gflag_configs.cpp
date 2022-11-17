#include "gflag_configs.h"
#include "numautil.h"
#include <asio/ts/internet.hpp>
#include "commons.h"
#include "rpc_constants.h"

DEFINE_uint32(rmem_numa_node, 0, "allocated and manager huge_page memory on this numa");

DEFINE_uint32(rmem_size, 16, "Reserved huge_page size on \'rmem_numa_node\' numa node, unit is GB");

DEFINE_uint32(rmem_server_thread, 4, "Thread number in server thread");

DEFINE_string(rmem_server_ip, "", "Server ip for client to connect");

DEFINE_uint32(rmem_server_udp_port, 31851, "Server udp port for client to connect, must between 31850 and 31850+16");

DEFINE_uint32(rmem_dpdk_port, 0, "dpdk used port id to receive/send");

static bool ValidateNumaNode(const char *flag_name, uint32_t value)
{
    if (value < static_cast<uint32_t>(numa_num_configured_nodes()))
    {
        return true;
    }
    RMEM_ERROR("Invalid value for --%s: %u\n", flag_name, static_cast<unsigned>(value));
    return false;
}

static bool ValidateDpdkPort(const char *flag_name, uint32_t value)
{
    if (value < erpc::kMaxNumERpcProcesses)
    {
        return true;
    }
    RMEM_ERROR("Invalid value for --%s: %u\n", flag_name, static_cast<unsigned>(value));
    return false;
}

static bool ValidateSize(const char *flag_name, uint32_t value)
{
    if (value < static_cast<uint32_t>(rmem::get_2M_huagepages_free(FLAGS_rmem_numa_node)))
    {
        return true;
    }
    RMEM_ERROR("Invalid value for --%s: %u\n", flag_name, static_cast<unsigned>(value));
    return false;
}

static bool ValidateServerThread(const char *flag_name, uint32_t value)
{
    if (value < static_cast<uint32_t>(numa_num_configured_cpus()))
    {
        return true;
    }
    RMEM_ERROR("Invalid value for --%s: %u\n", flag_name, static_cast<unsigned>(value));
    return false;
}

static bool ValidateServerIp(const char *flag_name, const std::string &value)
{
    if (value.empty())
    {
        RMEM_ERROR("Invalid value for --%s: %s\n", flag_name, value.c_str());
        return false;
    }
    asio::error_code ec;
    asio::ip::address::from_string(value, ec);
    if (ec)
    {
        RMEM_ERROR("Invalid value for --%s: %s\n", flag_name, value.c_str());
        return false;
    }
    return true;
}

static bool ValidateServerUdpPort(const char *flag_name, uint32_t value)
{
    if (value >= erpc::kBaseSmUdpPort && value < erpc::kBaseSmUdpPort + erpc::kMaxNumERpcProcesses)
    {
        return true;
    }
    RMEM_ERROR("Invalid value for --%s: %u, must between %hu and %lu  \n", flag_name, static_cast<unsigned>(value), erpc::kBaseSmUdpPort, erpc::kBaseSmUdpPort + erpc::kMaxNumERpcProcesses);
    return false;
}

DEFINE_validator(rmem_numa_node, &ValidateNumaNode);
DEFINE_validator(rmem_dpdk_port, &ValidateDpdkPort);
DEFINE_validator(rmem_size, &ValidateSize);
DEFINE_validator(rmem_server_thread, &ValidateServerThread);
DEFINE_validator(rmem_server_ip, &ValidateServerIp);
DEFINE_validator(rmem_server_udp_port, &ValidateServerUdpPort);
