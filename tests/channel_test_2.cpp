#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include "channel.hpp"  // Ensure your Channel is included

// Test for Channel<Type, N> where N != 0
TEST(ChannelTryAddTryGet, FixedSizeChannel) {
    constexpr size_t N = 5;
    Channel<int, N> ch;

    // Test try_add when the channel is not full
    for (int i = 0; i < N; ++i) {
        EXPECT_TRUE(ch.try_add(i));
    }

    // Test try_add when the channel is full
    EXPECT_FALSE(ch.try_add(100));

    // Test try_get when the channel is not empty
    for (int i = 0; i < N; ++i) {
        auto val = ch.try_get();
        ASSERT_TRUE(val);
        EXPECT_EQ(*val, i);
    }

    // Test try_get when the channel is empty
    EXPECT_FALSE(ch.try_get());

    // Test behavior after closing the channel
    ch.close();
    EXPECT_FALSE(ch.try_add(200));
    EXPECT_FALSE(ch.try_get());
}

// Test for Channel<Type, 0> (unbuffered channel)
TEST(ChannelTryAddTryGet, UnbufferedChannel) {
    Channel<int, 0> ch;

    std::atomic<bool> producer_started{false};
    std::atomic<bool> consumer_started{false};
    std::atomic<bool> producer_finished{false};
    std::atomic<bool> consumer_finished{false};


    // Consumer thread
    std::thread consumer([&]() {
        consumer_started = true;
        auto val = ch.get();
        ASSERT_TRUE(val);
        EXPECT_EQ(*val, 42);
        consumer_finished = true;
    });

    while(!consumer_started) {
        std::this_thread::yield();  // Wait for the consumer to start
    }

    // Producer thread
    std::thread producer([&]() {
        producer_started = true;
        EXPECT_TRUE(ch.try_add(42));  // Should succeed because the consumer is waiting
        producer_finished = true;
    });



    producer.join();
    consumer.join();

    EXPECT_TRUE(producer_started);
    EXPECT_TRUE(consumer_started);
    EXPECT_TRUE(producer_finished);
    EXPECT_TRUE(consumer_finished);

    // Test behavior after closing the channel
    ch.close();
    EXPECT_FALSE(ch.try_add(100));
    EXPECT_FALSE(ch.try_get());
}

// Test try_add and try_get with multiple producers and consumers
TEST(ChannelTryAddTryGet, MultiProducerConsumer) {
    constexpr size_t N = 3;
    Channel<int, N> ch;

    constexpr int NUM_PRODUCERS = 2;
    constexpr int NUM_CONSUMERS = 2;
    constexpr int MESSAGES_PER_PRODUCER = 5;

    std::atomic<int> sum_produced{0};
    std::atomic<int> sum_consumed{0};

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // Producers
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        producers.emplace_back([&, i]() {
            for (int j = 0; j < MESSAGES_PER_PRODUCER; ++j) {
                int value = i * MESSAGES_PER_PRODUCER + j;
                while (!ch.try_add(value)) {
                    std::this_thread::yield();  // Retry until successful
                }
                sum_produced.fetch_add(value, std::memory_order_relaxed);
            }
        });
    }

    // Consumers
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        consumers.emplace_back([&]() {
            while (true) {
                auto val = ch.get();
                if (!val) break;
                sum_consumed.fetch_add(*val, std::memory_order_relaxed);
            }
        });
    }

    for (auto& p : producers) p.join();
    ch.close();  // Close the channel to signal consumers
    for (auto& c : consumers) c.join();

    int expected_sum = NUM_PRODUCERS * MESSAGES_PER_PRODUCER * (NUM_PRODUCERS * MESSAGES_PER_PRODUCER - 1) / 2;
    EXPECT_EQ(sum_produced.load(), expected_sum);
    EXPECT_EQ(sum_consumed.load(), expected_sum);
}


TEST(ChannelStressTest, ProducerConsumerIntegrity) {
    constexpr size_t N = 10;
    constexpr int NUM_PRODUCERS = 30;
    constexpr int NUM_CONSUMERS = 20;
    constexpr int MESSAGES_PER_PRODUCER = 1000;

    Channel<int, N> ch;
    std::atomic<int> sum_produced{0};
    std::atomic<int> sum_consumed{0};
    std::atomic<int> count_received{0};

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        producers.emplace_back([&, i]() {
            for (int j = 0; j < MESSAGES_PER_PRODUCER; ++j) {
                int value = i * MESSAGES_PER_PRODUCER + j;
                ch.add(value);
                sum_produced.fetch_add(value, std::memory_order_relaxed);
            }
        });
    }

    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        consumers.emplace_back([&]() {
            while (true) {
                auto val = ch.get();
                if (!val) break;
                ASSERT_GE(*val, 0);
                sum_consumed.fetch_add(*val, std::memory_order_relaxed);
                count_received.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& p : producers) p.join();
    std::cerr << "Producers finished\n";
    ch.close();
    for (auto& c : consumers) c.join();

    int expected_messages = NUM_PRODUCERS * MESSAGES_PER_PRODUCER;

    EXPECT_EQ(count_received.load(), expected_messages);
    EXPECT_EQ(sum_produced.load(), sum_consumed.load());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
