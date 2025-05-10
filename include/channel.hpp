#ifndef CHANNEL_H
#define CHANNEL_H

#include <iostream>
#include <queue>
#include <semaphore>
#include <mutex>
#include <tuple>
#include <condition_variable>
#include <cassert>


class ChannelBase {
protected:
    std::mutex sync_mutex_;
    bool closed_ = false;
    std::condition_variable consumer_cv_;
    std::condition_variable producer_cv_;
};

// Channel class template
template <typename Type, size_t N>
class Channel : public ChannelBase {
public:

    template <typename U>
    bool add(U&& var) {
        std::unique_lock<std::mutex> lock(sync_mutex_);

        return adder(std::forward<U>(var), std::move(lock));
    }

    template <typename U>
    bool try_add(U&& var) {
        std::lock_guard<std::mutex> lock(sync_mutex_);

        if (queue_.size() == N) {
            return false; // Channel is full
        }
        return adder(std::forward<U>(var), std::move(lock));
    }

    std::unique_ptr<Type> get() {
        std::unique_lock<std::mutex> lock(sync_mutex_);

        return getter(std::move(lock));
    }

    std::unique_ptr<Type> try_get() {
        std::unique_lock<std::mutex> lock(sync_mutex_);

        if (queue_.empty()) {
            return nullptr; // Channel is empty
        }

        return getter(std::move(lock));
    }

    void close() {
        std::lock_guard<std::mutex> lock(sync_mutex_);
        toBeClosed_ = true;
        if (queue_.empty()) {
            closed_ = true;
        }
        consumer_cv_.notify_one();
        producer_cv_.notify_one();
    }
private:
    std::unique_ptr<Type> getter(std::unique_lock<std::mutex> lock) {
        consumer_cv_.wait(lock, [this] { return closed_ || !queue_.empty(); });

        if (closed_) {
            return nullptr;
        }

        if (toBeClosed_ && (queue_.size() == 1)) {
            closed_ = true;
        }

        producer_cv_.notify_one();

        if constexpr (std::is_move_constructible_v<Type>) {
            auto item = queue_.front().release();
            queue_.pop();
            return std::make_unique<Type>(std::move(*item));
        } else {
            auto item = queue_.front().release();
            queue_.pop();
            return std::make_unique<Type>(*item);
        }
    }
    
    template <typename U>
    bool adder(U&& var, std::unique_lock<std::mutex> lock) {
        producer_cv_.wait(lock, [this] { return closed_ || toBeClosed_ || queue_.size() < N; });

        if (closed_ || toBeClosed_) {
            return false;
        }

        if constexpr (std::is_move_constructible_v<Type>) {
            queue_.emplace(std::make_unique<Type>(std::forward<U>(var)));
        } else {
            queue_.emplace(std::make_unique<Type>(var));
        }
        
        consumer_cv_.notify_one();
        return true;
    }
    using QueueType = std::queue<std::unique_ptr<Type>>;

    QueueType queue_;
    bool toBeClosed_ = false;
};




template <typename Type>
class Channel<Type, 0> : public ChannelBase {
public:
    template <typename U>
    bool add(U&& var) {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        return adder(std::forward<U>(var), std::move(lock));
    }

    template <typename U>
    bool try_add(U&& var) {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        if (producer_waiting_ == 0) {
            return false;  // no producer waiting
        }
        return adder(std::forward<U>(var), std::move(lock));
    }


std::unique_ptr<Type> get() {
    std::unique_lock<std::mutex> lock(sync_mutex_);

    return getter(std::move(lock));
}


std::unique_ptr<Type> try_get() {
    std::unique_lock<std::mutex> lock(sync_mutex_);
    if(producer_waiting_ == 0) {
        return nullptr;  // no producer waiting
    }
    return getter(std::move(lock));
}


void close() {
    std::lock_guard<std::mutex> lock(sync_mutex_);

    closed_ = true;
    
    consumer_cv_.notify_one();
    producer_cv_.notify_one();
}

private:

    std::unique_ptr<Type> getter(std::unique_lock<std::mutex> lock) {
        consumer_waiting_++;

        // Notify producers we're ready
        producer_cv_.notify_one();

        // Wait until producer sends
        consumer_cv_.wait(lock, [this] { return closed_ || handoff_; });

        consumer_waiting_--;

        std::unique_ptr<Type> item;

        if (handoff_) {
            item = std::move(handoff_);
        } else {
            item = nullptr;  // Channel closed
        }

        producer_cv_.notify_one();

        return item;
    }

    template <typename U>
    bool adder(U&& var, std::unique_lock<std::mutex> lock) {

        producer_waiting_++;

        // Wait until consumer is waiting
        producer_cv_.wait(lock, [this] { return closed_ || (consumer_waiting_ > 0 && !handoff_); });

        if (closed_) {
            //assert(false && "Writing to Closed Channel\n");
            return false;
        }

        if constexpr (std::is_move_constructible_v<Type>) {
            handoff_ = std::make_unique<Type>(std::forward<U>(var));
        } else if constexpr (std::is_copy_constructible_v<Type>) {
            handoff_ = std::make_unique<Type>(var);
        } 
        
        // Wake consumer
        consumer_cv_.notify_one();
        producer_waiting_--;
        return true;
    }    

    std::unique_ptr<Type> handoff_;
    std::atomic<size_t> producer_waiting_ = 0;
    std::atomic<size_t> consumer_waiting_ = 0;
};

#endif // CHANNEL_H
