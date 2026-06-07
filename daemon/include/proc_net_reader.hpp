#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace netmon {

// ---------------------------------------------------------------------------
// IfaceStats — raw counters from /proc/net/dev for one interface
// ---------------------------------------------------------------------------
struct IfaceStats {
    std::string iface;

    // RX
    uint64_t rx_bytes   { 0 };
    uint64_t rx_packets { 0 };
    uint64_t rx_errors  { 0 };
    uint64_t rx_drop    { 0 };

    // TX
    uint64_t tx_bytes   { 0 };
    uint64_t tx_packets { 0 };
    uint64_t tx_errors  { 0 };
    uint64_t tx_drop    { 0 };

    bool     carrier    { false };  // /sys/class/net/<iface>/carrier
    int      operstate  { -1 };     // /sys/class/net/<iface>/operstate (raw int)
};

// ---------------------------------------------------------------------------
// IfaceDelta — per-second rates derived from two consecutive snapshots
// ---------------------------------------------------------------------------
struct IfaceDelta {
    std::string iface;
    double rx_bps        { 0.0 };
    double tx_bps        { 0.0 };
    double rx_pps        { 0.0 };
    double tx_pps        { 0.0 };
    double error_rate    { 0.0 };  // (rx_errors+tx_errors) / (rx_pkts+tx_pkts)
    bool   carrier       { false };
    uint64_t timestamp_ms { 0 };   // epoch ms at sample time
};

// ---------------------------------------------------------------------------
// ProcNetReader — reads /proc/net/dev (and sysfs carrier) synchronously
// ---------------------------------------------------------------------------
class ProcNetReader {
public:
    // Returns stats for all interfaces (skips "lo" by default)
    static std::vector<IfaceStats> read(bool skip_loopback = true);

    // Returns carrier state for one interface
    static bool read_carrier(const std::string& iface);

    // Compute delta between two snapshots taken dt_ms apart
    static IfaceDelta compute_delta(
        const IfaceStats& prev,
        const IfaceStats& curr,
        uint64_t dt_ms);
};

} // namespace netmon
