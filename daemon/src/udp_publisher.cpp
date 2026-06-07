#include "udp_publisher.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <syslog.h> 
#include <cstring>
#include <cstdio>
#include <stdexcept>

namespace netmon {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
UdpPublisher::UdpPublisher(const std::string& host, uint16_t port)
    : port_(port), host_(host)
{
    if (!open_socket()) {
        throw std::runtime_error("UdpPublisher: failed to create UDP socket");
    }
}

UdpPublisher::~UdpPublisher() {
    if (sockfd_ >= 0) ::close(sockfd_);
}

bool UdpPublisher::open_socket() {
    sockfd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    return (sockfd_ >= 0);
}

// ---------------------------------------------------------------------------
// publish() — serialise delta + state to JSON and sendto()
// ---------------------------------------------------------------------------
bool UdpPublisher::publish(const IfaceDelta& delta, LinkState state) {
    // Hand-rolled JSON — avoids a heavy dependency just for one small frame.
    // Max frame size is well under 512 bytes.
    char buf[512];
    int n = std::snprintf(buf, sizeof(buf),
        "{"
        "\"iface\":\"%s\","
        "\"ts_ms\":%llu,"
        "\"state\":\"%s\","
        "\"rx_bps\":%.1f,"
        "\"tx_bps\":%.1f,"
        "\"rx_pps\":%.1f,"
        "\"tx_pps\":%.1f,"
        "\"error_rate\":%.6f,"
        "\"carrier\":%s"
        "}",
        delta.iface.c_str(),
        (unsigned long long)delta.timestamp_ms,
        to_string(state),
        delta.rx_bps,
        delta.tx_bps,
        delta.rx_pps,
        delta.tx_pps,
        delta.error_rate,
        delta.carrier ? "true" : "false"
    );

    if (n <= 0 || n >= static_cast<int>(sizeof(buf))) return false;

    struct sockaddr_in addr {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port_);
    addr.sin_addr.s_addr = inet_addr(host_.c_str());

    ssize_t sent = ::sendto(
        sockfd_, buf, static_cast<std::size_t>(n), 0,
        reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));

    if (sent < 0) {
        syslog(LOG_WARNING, "netmon: UDP sendto failed: %s", std::strerror(errno));
        return false;
    }
    return true;
}

} // namespace netmon
