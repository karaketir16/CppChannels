#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <chrono>
#include "channel.hpp"

// Wrapper struct to encapsulate the two template parameters
template <typename T, size_t N>
struct ChannelParams {
    using Type = T;
    static const size_t Size = N;
};

// Test fixture for Channel
template <typename Params>
class ChannelTest : public ::testing::Test {
protected:
    using T = typename Params::Type;
    static const size_t N = Params::Size;
    Channel<T, N> channel;
};

TYPED_TEST_SUITE_P(ChannelTest);

TYPED_TEST_P(ChannelTest, AddAndGet) {
    using T = typename TypeParam::Type;
    const size_t N = TypeParam::Size;


    std::thread thread_([this](){    
        this->channel.add(T());
    });

    std::unique_ptr<T> retrieved = this->channel.get();

    thread_.join();
    
    EXPECT_TRUE(retrieved);
    EXPECT_EQ(*retrieved, T());
}

TYPED_TEST_P(ChannelTest, CloseChannel) {
    using T = typename TypeParam::Type;
    const size_t N = TypeParam::Size;
    this->channel.close();

    std::unique_ptr<T> retrieved = this->channel.get();
    EXPECT_FALSE(retrieved); // Should return nullptr
    EXPECT_EQ(this->channel.add(T()), ChannelBase::Result::CLOSED); // Should fail to add after close
}

TYPED_TEST_P(ChannelTest, MultithreadedAddAndGet) {
    using T = typename TypeParam::Type;
    const size_t N = TypeParam::Size;
    const size_t num_threads = 10;
    const size_t num_elements = 100;
    std::vector<std::thread> threads;

    std::cerr << "Starting multithreaded test with " << num_threads << " threads and " << num_elements << " elements.";
    std::cerr << "Channel size: " << N;

    // Producer threads
    for (size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, num_elements]() {
            for (size_t j = 0; j < num_elements; ++j) {
                this->channel.add(T());
            }
        });
    }

    // Consumer threads
    for (size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, num_elements]() {
            for (size_t j = 0; j < num_elements; ++j) {
                std::unique_ptr<T> retrieved = this->channel.get();
                EXPECT_TRUE(retrieved);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    this->channel.close();

    std::unique_ptr<T> retrieved = this->channel.get();
    EXPECT_FALSE(retrieved);
}

TYPED_TEST_P(ChannelTest, LoopTest) {
    using T = typename TypeParam::Type;
    const size_t N = TypeParam::Size;
    const size_t num_elements = 10;
    std::vector<std::thread> threads;

    // Producer thread
    threads.emplace_back([this, num_elements]() {
        for (size_t j = 0; j < num_elements; ++j) {
            this->channel.add(T());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        this->channel.close();
    });

    // Consumer thread
    threads.emplace_back([this]() {
        for (auto val = this->channel.get(); val.operator bool(); val = this->channel.get()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    for (auto& thread : threads) {
        thread.join();
    }
}


struct CopyableOnly {
    int value;

    explicit CopyableOnly(int v = 0) : value(v) {}

    // Allow copy
    CopyableOnly(const CopyableOnly&) = default;
    CopyableOnly& operator=(const CopyableOnly&) = default;

    // Delete move
    CopyableOnly(CopyableOnly&&) = delete;
    CopyableOnly& operator=(CopyableOnly&&) = delete;

    bool operator==(const CopyableOnly& other) const {
        return value == other.value;
    }
};

struct MoveableOnly {
    int value;

    explicit MoveableOnly(int v = 0) : value(v) {}

    // Delete copy
    MoveableOnly(const MoveableOnly&) = delete;
    MoveableOnly& operator=(const MoveableOnly&) = delete;

    // Allow move
    MoveableOnly(MoveableOnly&& other) noexcept : value(other.value) {
        other.value = -1; // indicate "moved from"
    }

    MoveableOnly& operator=(MoveableOnly&& other) noexcept {
        if (this != &other) {
            value = other.value;
            other.value = -1;
        }
        return *this;
    }

    bool operator==(const MoveableOnly& other) const {
        return value == other.value;
    }
};



REGISTER_TYPED_TEST_SUITE_P(ChannelTest, AddAndGet, CloseChannel, MultithreadedAddAndGet, LoopTest);

using MyTypes = ::testing::Types<
    ChannelParams<int, 10>, 
    ChannelParams<std::string, 10>, 
    ChannelParams<int, 0>, 
    ChannelParams<CopyableOnly, 10>,
    ChannelParams<MoveableOnly, 10>,
    ChannelParams<std::unique_ptr<int>, 10>,
    ChannelParams<std::shared_ptr<int>, 10>,
    ChannelParams<std::vector<int>, 10>,
    ChannelParams<std::vector<std::string>, 10>,
    ChannelParams<std::vector<MoveableOnly>, 10>,
    ChannelParams<std::vector<CopyableOnly>, 10>
>;
// Register the test suite with the types
INSTANTIATE_TYPED_TEST_SUITE_P(My, ChannelTest, MyTypes);


















TEST(ChannelTryAddTryGet, FixedSizeChannel) {
    constexpr size_t N = 5;
    Channel<int, N> ch;

    // Test try_add when the channel is not full
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(ch.try_add(i), ChannelBase::Result::OK);
    }

    // Test try_add when the channel is full
    EXPECT_EQ(ch.try_add(100), ChannelBase::Result::FULL);

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
    EXPECT_EQ(ch.try_add(200), ChannelBase::Result::CLOSED);
    EXPECT_EQ(ch.try_get(), nullptr);
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
        EXPECT_EQ(ch.try_add(42), ChannelBase::Result::OK);  // Should succeed because the consumer is waiting
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
    EXPECT_EQ(ch.try_add(100), ChannelBase::Result::CLOSED);
    EXPECT_EQ(ch.try_get(), nullptr);
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
                while (ch.try_add(value) != ChannelBase::Result::OK) {
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



TEST(ChannelBehavior, AddAfterCloseFails) {
    Channel<int, 5> ch;
    ch.close();
    EXPECT_EQ(ch.add(10), ChannelBase::Result::CLOSED); // Cannot add after close
}

TEST(ChannelBehavior, GetAfterCloseReturnsNull) {
    Channel<int, 5> ch;
    ch.close();
    auto result = ch.get();
    EXPECT_EQ(result, nullptr);  // Should return nullptr
}

TEST(ChannelUnbuffered, MultipleProducerConsumer) {
    Channel<int, 0> ch;

    constexpr int num_values = 10;
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::atomic<int> sum{0};

    for (int i = 0; i < num_values; ++i) {
        consumers.emplace_back([&]() {
            auto val = ch.get();
            if (val) {
                sum.fetch_add(*val);
            }
        });

        producers.emplace_back([i, &ch]() {
            ch.add(i);
        });
    }

    for (auto& p : producers) p.join();
    for (auto& c : consumers) c.join();

    int expected_sum = num_values * (num_values - 1) / 2;
    EXPECT_EQ(sum, expected_sum);
}


TEST(ChannelTryMethods, StressWithTryAddTryGet) {
    constexpr size_t N = 1;
    Channel<int, N> ch;

    EXPECT_EQ(ch.try_add(1), ChannelBase::Result::OK); // Should succeed
    EXPECT_EQ(ch.try_add(2), ChannelBase::Result::FULL); // Should be full

    auto val = ch.try_get();
    EXPECT_TRUE(val);
    EXPECT_FALSE(ch.try_get()); // Should be empty
}

TEST(ChannelOrder, FIFOBehavior) {
    Channel<int, 5> ch;
    for (int i = 0; i < 5; ++i) {
        ch.add(i);
    }
    for (int i = 0; i < 5; ++i) {
        auto val = ch.get();
        ASSERT_TRUE(val);
        EXPECT_EQ(*val, i);
    }
}

TEST(ChannelSmartPtr, SharedPtrCopy) {
    using T = std::shared_ptr<int>;
    Channel<T, 2> ch;
    auto sp = std::make_shared<int>(99);
    ch.add(sp);

    auto val = ch.get();
    ASSERT_TRUE(val);
    EXPECT_EQ(**val, 99);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
