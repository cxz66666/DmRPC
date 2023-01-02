#pragma once

#include <gflags/gflags.h>

DECLARE_uint32(rmem_numa_node);

DECLARE_uint64(rmem_size);

DECLARE_uint32(rmem_server_thread);

DECLARE_string(rmem_server_ip);

DECLARE_uint32(rmem_server_udp_port);

DECLARE_uint32(rmem_dpdk_port);

DECLARE_uint64(timeout_second);

DECLARE_bool(rmem_copy);