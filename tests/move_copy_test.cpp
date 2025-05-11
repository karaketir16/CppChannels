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



struct CopyableOnly {
    int value;

    explicit CopyableOnly(int v = 0) : value(v) {}

    // Allow copy
    CopyableOnly(const CopyableOnly& other)
    {
        value = other.value;
        counter = other.counter;
        std::cerr << "CopyableOnly copied: " << ++counter << std::endl;
    }
    CopyableOnly& operator=(const CopyableOnly& other) {
        value = other.value;
        counter = other.counter;
        std::cerr << "CopyableOnly assigned: " << ++counter << std::endl;
        return *this;
    }

    // Delete move
    CopyableOnly(CopyableOnly&&) = delete;
    CopyableOnly& operator=(CopyableOnly&&) = delete;

    bool operator==(const CopyableOnly& other) const {
        return value == other.value;
    }

private:
    int counter = 0;
};

struct MoveableOnly {
    int value;

    explicit MoveableOnly(int v = 0) : value(v) {}

    // Delete copy
    MoveableOnly(const MoveableOnly&) = delete;
    MoveableOnly& operator=(const MoveableOnly&) = delete;

    // Allow move
    MoveableOnly(MoveableOnly&& other) noexcept : value(other.value), counter(other.counter) {
        other.value = -1; // indicate "moved from"
        std::cerr << "MoveableOnly moved: " << ++counter << std::endl;
    }

    MoveableOnly& operator=(MoveableOnly&& other) noexcept {
        if (this != &other) {
            value = other.value;
            other.value = -1;
            counter = other.counter;
            std::cerr << "MoveableOnly assigned: " << ++counter << std::endl;
        }
        return *this;
    }

    bool operator==(const MoveableOnly& other) const {
        return value == other.value;
    }

private:
    int counter = 0;
};


REGISTER_TYPED_TEST_SUITE_P(ChannelTest, AddAndGet);

using MyTypes = ::testing::Types<
    ChannelParams<CopyableOnly, 10>,
    ChannelParams<MoveableOnly, 10>,
    ChannelParams<CopyableOnly, 0>,
    ChannelParams<MoveableOnly, 0>
>;
// Register the test suite with the types
INSTANTIATE_TYPED_TEST_SUITE_P(My, ChannelTest, MyTypes);




int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
