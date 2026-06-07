#pragma once

#include <array>
#include <atomic>
#include <optional>
#include <cstddef>

namespace netmon {

// ---------------------------------------------------------------------------
// RingBuffer<T, N> — single-producer / single-consumer lock-free ring buffer
//
//  The poller thread (producer) writes IfaceDelta records.
//  The IPC/UDP thread (consumer) reads them and forwards to the Python bridge.
//
//  Constraints:
//    - Exactly ONE producer thread and ONE consumer thread.
//    - N must be a power of two.
//    - T must be trivially copyable.
// ---------------------------------------------------------------------------
template <typename T, std::size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

public:
    // Producer: returns false if buffer is full (record is dropped)
    bool push(const T& item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & (N - 1);
        if (next == tail_.load(std::memory_order_acquire))
            return false;  // full — drop
        buf_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer: returns nullopt if buffer is empty
    std::optional<T> pop() noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return std::nullopt;  // empty
        T item = buf_[tail];
        tail_.store((tail + 1) & (N - 1), std::memory_order_release);
        return item;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

private:
    alignas(64) std::array<T, N> buf_ {};
    alignas(64) std::atomic<std::size_t> head_ { 0 };
    alignas(64) std::atomic<std::size_t> tail_ { 0 };
};

} // namespace netmon
