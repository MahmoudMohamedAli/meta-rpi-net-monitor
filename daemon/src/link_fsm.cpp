#include "link_fsm.hpp"
#include <syslog.h>
#include <stdexcept>

namespace netmon {

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------
const char* to_string(LinkState s) noexcept {
    switch (s) {
        case LinkState::UNKNOWN:    return "UNKNOWN";
        case LinkState::UP:         return "UP";
        case LinkState::DEGRADED:   return "DEGRADED";
        case LinkState::RECOVERING: return "RECOVERING";
        case LinkState::DOWN:       return "DOWN";
    }
    return "??";
}

const char* to_string(LinkEvent e) noexcept {
    switch (e) {
        case LinkEvent::CARRIER_UP:      return "CARRIER_UP";
        case LinkEvent::CARRIER_DOWN:    return "CARRIER_DOWN";
        case LinkEvent::ERROR_RATE_HIGH: return "ERROR_RATE_HIGH";
        case LinkEvent::ERROR_RATE_OK:   return "ERROR_RATE_OK";
        case LinkEvent::STABLE_TIMEOUT:  return "STABLE_TIMEOUT";
    }
    return "??";
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
LinkFSM::LinkFSM(std::string iface, TransitionCb cb)
    : iface_(std::move(iface))
    , on_transition_(std::move(cb))
    , entered_state_(std::chrono::steady_clock::now())
{}

// ---------------------------------------------------------------------------
// process() — apply event to current state, return true if transitioned
// ---------------------------------------------------------------------------
bool LinkFSM::process(LinkEvent event) {
    LinkState next = state_;

    switch (state_) {
        case LinkState::UNKNOWN:
            if (event == LinkEvent::CARRIER_UP)   next = LinkState::UP;
            if (event == LinkEvent::CARRIER_DOWN)  next = LinkState::DOWN;
            break;

        case LinkState::UP:
            if (event == LinkEvent::CARRIER_DOWN)    next = LinkState::DOWN;
            if (event == LinkEvent::ERROR_RATE_HIGH) next = LinkState::DEGRADED;
            break;

        case LinkState::DEGRADED:
            if (event == LinkEvent::CARRIER_DOWN)  next = LinkState::DOWN;
            if (event == LinkEvent::ERROR_RATE_OK) next = LinkState::UP;
            break;

        case LinkState::DOWN:
            if (event == LinkEvent::CARRIER_UP) next = LinkState::RECOVERING;
            break;

        case LinkState::RECOVERING:
            if (event == LinkEvent::CARRIER_DOWN)    next = LinkState::DOWN;
            if (event == LinkEvent::STABLE_TIMEOUT)  next = LinkState::UP;
            if (event == LinkEvent::ERROR_RATE_HIGH) next = LinkState::DEGRADED;
            break;
    }

    if (next != state_) {
        transition(next, event);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// transition() — update state, log, fire callback
// ---------------------------------------------------------------------------
void LinkFSM::transition(LinkState next, LinkEvent via) {
    const LinkState from = state_;
    state_               = next;
    entered_state_       = std::chrono::steady_clock::now();

    syslog(LOG_INFO, "netmon: [%s] %s --%s--> %s",
           iface_.c_str(),
           to_string(from),
           to_string(via),
           to_string(next));

    if (on_transition_) {
        on_transition_(iface_, from, next, via);
    }
}

// ---------------------------------------------------------------------------
// ms_in_state()
// ---------------------------------------------------------------------------
long long LinkFSM::ms_in_state() const noexcept {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now() - entered_state_).count();
}

} // namespace netmon
