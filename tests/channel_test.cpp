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

    T value = T();

    std::thread thread_([this,value](){    
        this->channel.add(value);
    });

    bool result;
    T retrieved = this->channel.get(result);

    thread_.join();

    EXPECT_TRUE(result);
    EXPECT_EQ(retrieved, value);
}

TYPED_TEST_P(ChannelTest, CloseChannel) {
    using T = typename TypeParam::Type;
    const size_t N = TypeParam::Size;
    this->channel.close();
    bool result;
    T retrieved = this->channel.get(result);
    EXPECT_FALSE(result);
    EXPECT_EQ(retrieved, T());
}

TYPED_TEST_P(ChannelTest, MultithreadedAddAndGet) {
    using T = typename TypeParam::Type;
    const size_t N = TypeParam::Size;
    const size_t num_threads = 10;
    const size_t num_elements = 100;
    std::vector<std::thread> threads;

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
                bool result;
                T retrieved = this->channel.get(result);
                EXPECT_TRUE(result);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    this->channel.close();
    bool result;
    T retrieved = this->channel.get(result);
    EXPECT_FALSE(result);
    EXPECT_EQ(retrieved, T());
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
        for (auto [val, res] = this->channel.get(); res; val = this->channel.get(res)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    for (auto& thread : threads) {
        thread.join();
    }
}

REGISTER_TYPED_TEST_SUITE_P(ChannelTest, AddAndGet, CloseChannel, MultithreadedAddAndGet, LoopTest);

using MyTypes = ::testing::Types<ChannelParams<int, 10>, ChannelParams<std::string, 10>, ChannelParams<int, 0>>;
INSTANTIATE_TYPED_TEST_SUITE_P(My, ChannelTest, MyTypes);

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
