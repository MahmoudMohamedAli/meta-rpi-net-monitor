#include "proc_net_reader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <filesystem>

namespace netmon {

namespace {

uint64_t epoch_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// read() — parse /proc/net/dev
//
// Format (after two header lines):
//   iface: rx_bytes rx_pkts rx_errs rx_drop rx_fifo rx_frame rx_compressed
//          rx_multicast tx_bytes tx_pkts tx_errs tx_drop ...
// ---------------------------------------------------------------------------
std::vector<IfaceStats> ProcNetReader::read(bool skip_loopback) {
    std::ifstream f("/proc/net/dev");
    if (!f) throw std::runtime_error("Cannot open /proc/net/dev");

    std::vector<IfaceStats> result;
    std::string line;

    // Skip two header lines
    std::getline(f, line);
    std::getline(f, line);

    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string iface;
        ss >> iface;
        if (iface.empty()) continue;

        // Strip trailing colon
        if (iface.back() == ':') iface.pop_back();

        if (skip_loopback && iface == "lo") continue;

        IfaceStats s;
        s.iface = iface;

        uint64_t dummy;
        ss >> s.rx_bytes >> s.rx_packets >> s.rx_errors >> s.rx_drop
           >> dummy >> dummy >> dummy >> dummy   // fifo,frame,compressed,multicast
           >> s.tx_bytes >> s.tx_packets >> s.tx_errors >> s.tx_drop;

        s.carrier = read_carrier(iface);
        result.push_back(std::move(s));
    }

    return result;
}

// ---------------------------------------------------------------------------
// read_carrier() — /sys/class/net/<iface>/carrier (1 = up, 0 = down)
// ---------------------------------------------------------------------------
bool ProcNetReader::read_carrier(const std::string& iface) {
    const std::string path = "/sys/class/net/" + iface + "/carrier";
    std::ifstream f(path);
    if (!f) return false;  // interface may not support carrier
    int v = 0;
    f >> v;
    return (v == 1);
}

// ---------------------------------------------------------------------------
// compute_delta() — derive per-second rates from two consecutive snapshots
// ---------------------------------------------------------------------------
IfaceDelta ProcNetReader::compute_delta(
    const IfaceStats& prev,
    const IfaceStats& curr,
    uint64_t dt_ms)
{
    IfaceDelta d;
    d.iface      = curr.iface;
    d.carrier    = curr.carrier;
    d.timestamp_ms = epoch_ms();

    if (dt_ms == 0) return d;

    const double dt_s = static_cast<double>(dt_ms) / 1000.0;

    auto diff = [](uint64_t a, uint64_t b) -> double {
        return (b >= a) ? static_cast<double>(b - a) : 0.0;
    };

    d.rx_bps = diff(prev.rx_bytes,   curr.rx_bytes)   * 8.0 / dt_s;
    d.tx_bps = diff(prev.tx_bytes,   curr.tx_bytes)   * 8.0 / dt_s;
    d.rx_pps = diff(prev.rx_packets, curr.rx_packets) / dt_s;
    d.tx_pps = diff(prev.tx_packets, curr.tx_packets) / dt_s;

    const double total_pkts =
        diff(prev.rx_packets, curr.rx_packets) +
        diff(prev.tx_packets, curr.tx_packets);
    const double total_errs =
        diff(prev.rx_errors,  curr.rx_errors)  +
        diff(prev.tx_errors,  curr.tx_errors);

    d.error_rate = (total_pkts > 0) ? (total_errs / total_pkts) : 0.0;

    return d;
}

} // namespace netmon
