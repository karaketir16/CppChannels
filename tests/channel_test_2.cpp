#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include "channel.hpp"  // Ensure your Channel is included

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
