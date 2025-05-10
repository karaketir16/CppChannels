#ifndef CHANNEL_H
#define CHANNEL_H

#include <iostream>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

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
    std::unique_ptr<Type> array[N];
    std::atomic<size_t> head_ = 0;
    std::atomic<size_t> tail_ = 0;

    bool is_full() const {
        return array[head_] != nullptr;
    }

    bool is_empty() const {
        return array[tail_] == nullptr;
    }

    bool toBeClosed_ = false;

public:

    template <typename U>
    bool add(U&& var) {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        return adder(std::forward<U>(var), std::move(lock));
    }

    template <typename U>
    bool try_add(U&& var) {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        if (is_full()) {
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
        if (is_empty()) {
            return nullptr; // Channel is empty
        }
        return getter(std::move(lock));
    }

    void close() {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        toBeClosed_ = true;
        
        if (is_empty()) {
            closed_ = true;
        }

        lock.unlock(); // Unlock the mutex before notifying

        consumer_cv_.notify_all();
        producer_cv_.notify_all();
    }
private:
    std::unique_ptr<Type> getter(std::unique_lock<std::mutex> lock) {
        std::unique_ptr<Type> item = nullptr;
        
        consumer_cv_.wait(lock, [this] { return closed_ || !is_empty(); });

        if (!closed_) {
            size_t tail_current = tail_;
            tail_ = (tail_ + 1) % N;

            bool lastOne = is_empty(); //if next is empty this one is the last one

            if (toBeClosed_ && lastOne) {
                closed_ = true;
                consumer_cv_.notify_all();
            }

            item = std::move(array[tail_current]);

            lock.unlock(); // Unlock the mutex before notifying 

            producer_cv_.notify_one();
        }

        return item;
    }

    template <typename U>
    bool adder(U&& var, std::unique_lock<std::mutex> lock) {
        
        producer_cv_.wait(lock, [this] { return closed_ || toBeClosed_ || !is_full(); });
        
        size_t head_local = head_;
        head_ = (head_ + 1) % N;

        if (closed_ || toBeClosed_) {
            return false;
        }
        
        if constexpr (std::is_move_constructible_v<Type>) {
            array[head_local] = std::make_unique<Type>(std::forward<U>(var));
        } else {
            array[head_local] = std::make_unique<Type>(var);
        }
        
        lock.unlock(); // Unlock the mutex before notifying
        
        consumer_cv_.notify_one();
        return true;
    }
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
        if (consumer_waiting_ == 0) {
            return false;  // no consumer waiting
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
    std::unique_lock<std::mutex> lock(sync_mutex_);

    closed_ = true;

    lock.unlock(); // Unlock the mutex before notifying
    
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

        lock.unlock(); // Unlock the mutex before notifying

        producer_cv_.notify_one();

        return item;
    }

    template <typename U>
    bool adder(U&& var, std::unique_lock<std::mutex> lock) {

        producer_waiting_++;

        // Wait until consumer is waiting
        producer_cv_.wait(lock, [this] { return closed_ || (consumer_waiting_ > 0 && !handoff_); });

        if (closed_) {
            return false;
        }

        if constexpr (std::is_move_constructible_v<Type>) {
            handoff_ = std::make_unique<Type>(std::forward<U>(var));
        } else if constexpr (std::is_copy_constructible_v<Type>) {
            handoff_ = std::make_unique<Type>(var);
        } 

        producer_waiting_--;

        lock.unlock(); // Unlock the mutex before notifying
        
        // Wake consumer
        consumer_cv_.notify_one();
        return true;
    }    

    std::unique_ptr<Type> handoff_;
    std::atomic<size_t> producer_waiting_ = 0;
    std::atomic<size_t> consumer_waiting_ = 0;
};

#endif // CHANNEL_H
