#pragma once

#include "proc_net_reader.hpp"
#include "link_fsm.hpp"
#include <string>
#include <cstdint>

namespace netmon {

// ---------------------------------------------------------------------------
// UdpPublisher — serialises telemetry to JSON and sends it over UDP
//
//  The Python bridge listens on 127.0.0.1:PORT and forwards to HTTP clients.
//  JSON frame format:
//  {
//    "iface":      "eth0",
//    "ts_ms":      1718000000000,
//    "state":      "UP",
//    "rx_bps":     1234567.0,
//    "tx_bps":     89012.0,
//    "rx_pps":     120.0,
//    "tx_pps":     15.0,
//    "error_rate": 0.0001
//  }
// ---------------------------------------------------------------------------
class UdpPublisher {
public:
    UdpPublisher(const std::string& host, uint16_t port);
    ~UdpPublisher();

    // Not copyable
    UdpPublisher(const UdpPublisher&)            = delete;
    UdpPublisher& operator=(const UdpPublisher&) = delete;

    // Publish one delta + current FSM state
    bool publish(const IfaceDelta& delta, LinkState state);

private:
    int      sockfd_ { -1 };
    uint16_t port_;
    std::string host_;

    bool open_socket();
};

} // namespace netmon
