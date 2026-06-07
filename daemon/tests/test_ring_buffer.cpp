#include "ring_buffer.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace netmon;

// ---------------------------------------------------------------------------
// Basic push / pop
// ---------------------------------------------------------------------------
TEST(RingBuffer, EmptyOnConstruct) {
    RingBuffer<int, 8> rb;
    EXPECT_TRUE(rb.empty());
    EXPECT_FALSE(rb.pop().has_value());
}

TEST(RingBuffer, PushPop_SingleItem) {
    RingBuffer<int, 8> rb;
    EXPECT_TRUE(rb.push(42));
    EXPECT_FALSE(rb.empty());
    auto v = rb.pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
    EXPECT_TRUE(rb.empty());
}

TEST(RingBuffer, PushPop_MultipleItems_FIFO) {
    RingBuffer<int, 8> rb;
    for (int i = 0; i < 5; ++i) rb.push(i);
    for (int i = 0; i < 5; ++i) {
        auto v = rb.pop();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, i);
    }
    EXPECT_TRUE(rb.empty());
}

TEST(RingBuffer, PushReturnsFalseWhenFull) {
    RingBuffer<int, 4> rb;  // capacity = N-1 = 3
    EXPECT_TRUE(rb.push(1));
    EXPECT_TRUE(rb.push(2));
    EXPECT_TRUE(rb.push(3));
    EXPECT_FALSE(rb.push(4));  // full
}

TEST(RingBuffer, PopAfterWrap) {
    RingBuffer<int, 4> rb;
    rb.push(1); rb.push(2); rb.push(3);
    rb.pop(); rb.pop();       // drain two
    rb.push(4); rb.push(5);   // wrap around
    EXPECT_EQ(*rb.pop(), 3);
    EXPECT_EQ(*rb.pop(), 4);
    EXPECT_EQ(*rb.pop(), 5);
    EXPECT_TRUE(rb.empty());
}

// ---------------------------------------------------------------------------
// Concurrent SPSC stress test
// ---------------------------------------------------------------------------
TEST(RingBuffer, SPSC_ConcurrentStress) {
    RingBuffer<int, 256> rb;
    constexpr int N = 10000;
    std::vector<int> consumed;
    consumed.reserve(N);

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            while (!rb.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&] {
        int count = 0;
        while (count < N) {
            if (auto v = rb.pop()) {
                consumed.push_back(*v);
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(static_cast<int>(consumed.size()), N);
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(consumed[i], i) << "Mismatch at index " << i;
    }
}
