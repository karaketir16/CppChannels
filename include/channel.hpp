#ifndef CHANNEL_H
#define CHANNEL_H

#include <iostream>
#include <queue>
#include <semaphore>
#include <mutex>
#include <tuple>
#include <condition_variable>
#include <cassert>

// Channel class template
template <typename Type, size_t N>
class Channel {
public:
    explicit Channel()
        : emptySlots(N), fullSlots(0) {}

    template <typename U>
    void add(U&& var) {
        if (closed_ || toBeClosed_) {
            assert(false && "Writing to Closed Channel\n");
        }

        emptySlots.acquire();  // Block if no empty slots
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if constexpr (std::is_move_constructible_v<Type>) {
                queue_.emplace(std::make_unique<Type>(std::forward<U>(var)));
            } else {
                queue_.emplace(std::make_shared<Type>(var));
            }
        }
        fullSlots.release();  // Signal that a slot is filled
        cv_.notify_one();
    }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        toBeClosed_ = true;
        if (queue_.empty()) {
            closed_ = true;
        }
        cv_.notify_all();
    }

    std::unique_ptr<Type> get() {
        emptySlots.release();

        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return closed_ || fullSlots.try_acquire(); });

        if (closed_) {
            return nullptr;
        }

        if (toBeClosed_ && (queue_.size() == 1)) {
            closed_ = true;
        }

        if constexpr (std::is_move_constructible_v<Type>) {
            auto item = queue_.front().release();
            queue_.pop();
            return std::make_unique<Type>(std::move(*item));
        } else {
            auto item = queue_.front();
            queue_.pop();
            return std::make_unique<Type>(*item);
        }
    }

private:
    using QueueType = std::conditional_t<
        std::is_move_constructible_v<Type>,
        std::queue<std::unique_ptr<Type>>,
        std::queue<std::shared_ptr<Type>>
    >;

    QueueType queue_;
    std::counting_semaphore<N ? N : 1> emptySlots;
    std::counting_semaphore<N ? N : 1> fullSlots;
    std::mutex mutex_;
    bool closed_ = false;
    bool toBeClosed_ = false;
    std::condition_variable cv_;
};

#endif // CHANNEL_H
