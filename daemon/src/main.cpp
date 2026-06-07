#include "link_fsm.hpp"
#include "proc_net_reader.hpp"
#include "ring_buffer.hpp"
#include "udp_publisher.hpp"

#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <unistd.h>
#include <syslog.h>

#include <thread>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <iostream>

// ---------------------------------------------------------------------------
// Configuration (could be loaded from /etc/netmon.conf)
// ---------------------------------------------------------------------------
static constexpr uint16_t    UDP_PORT           = 9999;
static constexpr const char* UDP_HOST           = "127.0.0.1";
static constexpr int         POLL_INTERVAL_MS   = 1000;   // 1 s
static constexpr double      ERROR_RATE_THRESH  = 0.001;  // 0.1 %
static constexpr int         RECOVERING_SECS    = 5;      // stable window

// ---------------------------------------------------------------------------
// Shared state between threads
// ---------------------------------------------------------------------------
struct IpcRecord {
    netmon::IfaceDelta delta;
    netmon::LinkState  state;
};

static netmon::RingBuffer<IpcRecord, 64> g_ring;
static std::atomic<bool>                g_running { true };

// ---------------------------------------------------------------------------
// poller_thread — reads /proc/net/dev every POLL_INTERVAL_MS,
//                 drives FSMs, pushes records to ring buffer
// ---------------------------------------------------------------------------
static void poller_thread() {
    openlog("netmon-poller", LOG_PID, LOG_DAEMON);

    std::unordered_map<std::string, netmon::LinkFSM>      fsms;
    std::unordered_map<std::string, netmon::IfaceStats>   prev_stats;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
                                                          recovering_since;

    auto last_poll = std::chrono::steady_clock::now();

    while (g_running.load(std::memory_order_relaxed)) {
        auto now = std::chrono::steady_clock::now();
        auto dt_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_poll).count());
        last_poll = now;

        // ── Read current stats ──────────────────────────────────────────────
        std::vector<netmon::IfaceStats> curr_stats;
        try {
            curr_stats = netmon::ProcNetReader::read();
        } catch (const std::exception& e) {
            syslog(LOG_ERR, "netmon: ProcNetReader::read failed: %s", e.what());
            std::this_thread::sleep_for(
                std::chrono::milliseconds(POLL_INTERVAL_MS));
            continue;
        }

        for (const auto& cs : curr_stats) {
            const std::string& iface = cs.iface;

            // Create FSM if this is a new interface
            if (fsms.find(iface) == fsms.end( )) {
                fsms.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(iface),
                    std::forward_as_tuple(iface,
                        [](const std::string& i,
                           netmon::LinkState from,
                           netmon::LinkState to,
                           netmon::LinkEvent via)
                        {
                            syslog(LOG_NOTICE,
                                "netmon: transition [%s] %s -> %s (event: %s)",
                                i.c_str(),
                                netmon::to_string(from),
                                netmon::to_string(to),
                                netmon::to_string(via));
                        })
                );
            }

            auto& fsm = fsms.at(iface);

            // ── Drive carrier events ────────────────────────────────────────
            fsm.process(cs.carrier
                ? netmon::LinkEvent::CARRIER_UP
                : netmon::LinkEvent::CARRIER_DOWN);

            // ── Compute delta and drive error-rate events ───────────────────
            netmon::IfaceDelta delta {};
            if (prev_stats.count(iface) && dt_ms > 0) {
                delta = netmon::ProcNetReader::compute_delta(
                    prev_stats.at(iface), cs, dt_ms);

                fsm.process(delta.error_rate > ERROR_RATE_THRESH
                    ? netmon::LinkEvent::ERROR_RATE_HIGH
                    : netmon::LinkEvent::ERROR_RATE_OK);
            }

            // ── RECOVERING stable timeout ───────────────────────────────────
            if (fsm.state() == netmon::LinkState::RECOVERING) {
                if (recovering_since.find(iface) == recovering_since.end())
                    recovering_since[iface] = now;

                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - recovering_since.at(iface)).count();
                if (elapsed >= RECOVERING_SECS)
                    fsm.process(netmon::LinkEvent::STABLE_TIMEOUT);
            } else {
                recovering_since.erase(iface);
            }

            // ── Push to ring buffer ─────────────────────────────────────────
            if (prev_stats.count(iface)) {
                IpcRecord rec { delta, fsm.state() };
                if (!g_ring.push(rec)) {
                    syslog(LOG_WARNING, "netmon: ring buffer full, dropping [%s]",
                           iface.c_str());
                }
            }

            prev_stats[iface] = cs;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(POLL_INTERVAL_MS));
    }

    closelog();
}

// ---------------------------------------------------------------------------
// ipc_thread — drains ring buffer, publishes via UDP to Python bridge
// ---------------------------------------------------------------------------
static void ipc_thread() {
    openlog("netmon-ipc", LOG_PID, LOG_DAEMON);

    netmon::UdpPublisher pub(UDP_HOST, UDP_PORT);

    while (g_running.load(std::memory_order_relaxed) || !g_ring.empty()) {
        while (auto rec = g_ring.pop()) {
            pub.publish(rec->delta, rec->state);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    closelog();
}

// ---------------------------------------------------------------------------
// main — set up signalfd + epoll, start threads, block on signals
// ---------------------------------------------------------------------------
int main() {
    openlog("netmon", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "netmon: starting up (pid %d)", static_cast<int>(getpid()));

    // ── Block SIGTERM / SIGINT so signalfd can receive them ─────────────────
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0) {
        syslog(LOG_ERR, "netmon: pthread_sigmask failed: %s", strerror(errno));
        return 1;
    }

    int sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sfd < 0) {
        syslog(LOG_ERR, "netmon: signalfd failed: %s", strerror(errno));
        return 1;
    }

    int efd = epoll_create1(EPOLL_CLOEXEC);
    if (efd < 0) {
        syslog(LOG_ERR, "netmon: epoll_create1 failed: %s", strerror(errno));
        return 1;
    }

    struct epoll_event ev {};
    ev.events  = EPOLLIN;
    ev.data.fd = sfd;
    epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &ev);

    // ── Launch worker threads ────────────────────────────────────────────────
    std::thread t_poller(poller_thread);
    std::thread t_ipc(ipc_thread);

    syslog(LOG_INFO, "netmon: polling loop started, listening on UDP %s:%d",
           UDP_HOST, UDP_PORT);

    // ── Block until SIGTERM / SIGINT ─────────────────────────────────────────
    struct epoll_event events[4];
    while (true) {
        int n = epoll_wait(efd, events, 4, -1);
        if (n < 0 && errno == EINTR) continue;
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == sfd) {
                struct signalfd_siginfo si {};
                ssize_t _ignored = read(sfd, &si, sizeof(si)); (void)_ignored;
                syslog(LOG_INFO, "netmon: received signal %u, shutting down",
                       si.ssi_signo);
                g_running.store(false, std::memory_order_relaxed);
                goto shutdown;
            }
        }
    }

shutdown:
    t_poller.join();
    t_ipc.join();

    close(efd);
    close(sfd);
    syslog(LOG_INFO, "netmon: clean shutdown");
    closelog();
    return 0;
}
