
#pragma once

#define ERPC_DPDK

#include <string>
namespace rmem
{

    static constexpr bool IsDPDKDaemon = true;

    // useless now
    static const std::string DPDKDaemonPath = "./dpdk_daemon";

    static constexpr uint16_t ClientRingBufSize = 65535;

    static constexpr uint16_t MaxPageSize = 4096;

    // perfer to set to be physical cpu core per socket
    static constexpr uint16_t MaxContext = 12;

    // default timeout 10 seconds
    static constexpr int DefaultTimeoutMS = 10000;
}
