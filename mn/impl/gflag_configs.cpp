#include "gflag_configs.h"

DEFINE_uint32(rmem_numa_node, 0, "allocated and manager hugepage memory on this numa");

DEFINE_uint32(rmem_dpdk_port, 0, "dpdk used port id to receive/send");

DEFINE_uint32(rmem_size, 32, "Reserved hugepage size on \'rmem_numa_node\' numa node, unit is GB");

DEFINE_uint32(rmem_server_thread, 2, "Thread number in server thread");
