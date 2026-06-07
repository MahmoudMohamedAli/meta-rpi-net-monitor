#pragma once

#include <string>
#include <functional>
#include <chrono>

namespace netmon {

// ---------------------------------------------------------------------------
// LinkState — all states a network interface can occupy
// ---------------------------------------------------------------------------
enum class LinkState {
    UNKNOWN,       // initial / not yet polled
    UP,            // carrier present, no errors
    DEGRADED,      // carrier present but error rate above threshold
    RECOVERING,    // was DOWN, carrier just restored — waiting for stability
    DOWN           // carrier lost
};

const char* to_string(LinkState s) noexcept;

// ---------------------------------------------------------------------------
// LinkEvent — inputs that drive state transitions
// ---------------------------------------------------------------------------
enum class LinkEvent {
    CARRIER_UP,
    CARRIER_DOWN,
    ERROR_RATE_HIGH,
    ERROR_RATE_OK,
    STABLE_TIMEOUT   // fired after RECOVERING_STABLE_SECS in RECOVERING state
};

const char* to_string(LinkEvent e) noexcept;

// ---------------------------------------------------------------------------
// LinkFSM — deterministic FSM for one network interface
//
//  Transition table:
//  ┌─────────────┬────────────────┬──────────────┐
//  │ State       │ Event          │ Next state   │
//  ├─────────────┼────────────────┼──────────────┤
//  │ UNKNOWN     │ CARRIER_UP     │ UP           │
//  │ UNKNOWN     │ CARRIER_DOWN   │ DOWN         │
//  │ UP          │ CARRIER_DOWN   │ DOWN         │
//  │ UP          │ ERROR_RATE_HIGH│ DEGRADED     │
//  │ DEGRADED    │ CARRIER_DOWN   │ DOWN         │
//  │ DEGRADED    │ ERROR_RATE_OK  │ UP           │
//  │ DOWN        │ CARRIER_UP     │ RECOVERING   │
//  │ RECOVERING  │ CARRIER_DOWN   │ DOWN         │
//  │ RECOVERING  │ STABLE_TIMEOUT │ UP           │
//  │ RECOVERING  │ ERROR_RATE_HIGH│ DEGRADED     │
//  └─────────────┴────────────────┴──────────────┘
// ---------------------------------------------------------------------------
class LinkFSM {
public:
    using TransitionCb = std::function<void(
        const std::string& iface,
        LinkState from,
        LinkState to,
        LinkEvent via)>;

    explicit LinkFSM(std::string iface, TransitionCb cb = nullptr);

    // Feed an event into the FSM; returns true if state changed
    bool process(LinkEvent event);

    LinkState state() const noexcept { return state_; }
    const std::string& iface() const noexcept { return iface_; }

    // Milliseconds spent in current state
    long long ms_in_state() const noexcept;

private:
    std::string  iface_;
    LinkState    state_ { LinkState::UNKNOWN };
    TransitionCb on_transition_;
    std::chrono::steady_clock::time_point entered_state_;

    void transition(LinkState next, LinkEvent via);
};

} // namespace netmon
