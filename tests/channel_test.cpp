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
    EXPECT_EQ(*retrieved.get(), T());
}

TYPED_TEST_P(ChannelTest, CloseChannel) {
    using T = typename TypeParam::Type;
    const size_t N = TypeParam::Size;
    this->channel.close();

    std::unique_ptr<T> retrieved = this->channel.get();
    EXPECT_FALSE(retrieved);
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
        for (auto val = this->channel.get(); val; val = this->channel.get()) {
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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
