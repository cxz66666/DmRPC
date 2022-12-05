
#pragma once

#define ERPC_DPDK

#include <string>
namespace rmem
{

    static constexpr bool IsDPDKDaemon = false;

    // useless now
    static const std::string DPDKDaemonPath = "./erpc_dpdk_daemon";

    static constexpr uint16_t ClientRingBufSize = 1024;

    static constexpr uint32_t AsyncReceivedReqSize = 1024;
    // perfer to set to be logical cpu core -1 per socket
    // we use the lastest logical core for handler nexus
    // so if you have 24 logical core, you perfer to set it to 23
    static constexpr uint16_t MaxContext = 23;

    // default timeout 10 seconds
    static constexpr int DefaultTimeoutMS = 10000;
}
