#ifndef CHANNEL_H
#define CHANNEL_H

#include <iostream>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

class ChannelBase {
public:
    enum class Result {
        OK,
        CLOSED,
        FULL,
        EMPTY
    };
protected:
    std::mutex sync_mutex_;
    bool closed_ = false;
    std::condition_variable consumer_cv_;
    std::condition_variable producer_cv_;

    inline static Result dummy_result_;
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
    Result add(U&& var) {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        return adder(std::forward<U>(var), std::move(lock));
    }

    template <typename U>
    Result try_add(U&& var) {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        if (closed_) {
            return Result::CLOSED; // Channel is closed
        } else if (is_full()) {
            return Result::FULL; // Channel is full
        }
        return adder(std::forward<U>(var), std::move(lock));
    }

    std::unique_ptr<Type> get(Result& result = dummy_result_) {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        return getter(std::move(lock), result);
    }

    std::unique_ptr<Type> try_get(Result& result = dummy_result_) {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        if (closed_) {
            result = Result::CLOSED; // Channel is closed
            return nullptr; // Channel is closed
        } else if (is_empty()) {
            result = Result::EMPTY; // Channel is empty
            return nullptr; // Channel is empty
        }
        return getter(std::move(lock), result);
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
    std::unique_ptr<Type> getter(std::unique_lock<std::mutex> lock, Result& result) {
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

            result = Result::OK;
        } else {
            result = Result::CLOSED;
        }

        return item;
    }

    template <typename U>
    Result adder(U&& var, std::unique_lock<std::mutex> lock) {
        producer_cv_.wait(lock, [this] { return closed_ || toBeClosed_ || !is_full(); });
        
        size_t head_local = head_;
        head_ = (head_ + 1) % N;

        if (closed_ || toBeClosed_) {
            return Result::CLOSED;
        }

        if constexpr (std::is_move_constructible_v<Type>) {
            array[head_local] = std::make_unique<Type>(std::forward<U>(var));
        } else if constexpr (std::is_copy_constructible_v<Type>) {
            array[head_local] = std::make_unique<Type>(var);
        } else {
            array[head_local] = nullptr; // Clear the slot
            static_assert(false, "Type is neither move nor copy constructible");
        }
        
        lock.unlock(); // Unlock the mutex before notifying
        
        consumer_cv_.notify_one();
        return Result::OK;
    }
};




template <typename Type>
class Channel<Type, 0> : public ChannelBase {
public:
    template <typename U>
    Result add(U&& var) {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        return adder(std::forward<U>(var), std::move(lock));
    }

    template <typename U>
    Result try_add(U&& var) {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        if (closed_) {
            return Result::CLOSED;  // Channel is closed
        } else if (consumer_waiting_ == 0) {
            return Result::FULL;  // no consumer waiting
        }
        return adder(std::forward<U>(var), std::move(lock));
    }


    std::unique_ptr<Type> get(Result& result = dummy_result_) {
        std::unique_lock<std::mutex> lock(sync_mutex_);

        return getter(std::move(lock), result);
    }


    std::unique_ptr<Type> try_get(Result& result = dummy_result_) {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        if (closed_) {
            result = Result::CLOSED;  // Channel is closed
            return nullptr;  // Channel is closed
        } else if(producer_waiting_ == 0) {
            result = Result::EMPTY;  // no producer waiting
            return nullptr;  // no producer waiting
        }
        return getter(std::move(lock), result);
    }


void close() {
    std::unique_lock<std::mutex> lock(sync_mutex_);

    closed_ = true;

    lock.unlock(); // Unlock the mutex before notifying
    
    consumer_cv_.notify_one();
    producer_cv_.notify_one();
}

private:

    std::unique_ptr<Type> getter(std::unique_lock<std::mutex> lock, Result& result) {
        consumer_waiting_++;

        // Notify producers we're ready
        producer_cv_.notify_one();

        // Wait until producer sends
        consumer_cv_.wait(lock, [this] { return closed_ || handoff_; });

        consumer_waiting_--;

        std::unique_ptr<Type> item = nullptr;
        if (handoff_) {
            item = std::move(handoff_);
            handoff_ = nullptr; // Clear the handoff
            result = Result::OK;
        } else {
            item = nullptr;  // Channel closed
            result = Result::CLOSED;
        }

        lock.unlock(); // Unlock the mutex before notifying

        producer_cv_.notify_one();

        return item;
    }

    template <typename U>
    Result adder(U&& var, std::unique_lock<std::mutex> lock) {

        producer_waiting_++;

        // Wait until consumer is waiting
        producer_cv_.wait(lock, [this] { return closed_ || (consumer_waiting_ > 0 && !handoff_); });

        if (closed_) {
            return Result::CLOSED;
        }

        if constexpr (std::is_move_constructible_v<Type>) {
            handoff_ = std::make_unique<Type>(std::forward<U>(var));
        } else if constexpr (std::is_copy_constructible_v<Type>) {
            handoff_ = std::make_unique<Type>(var);
        } else {
            handoff_ = nullptr; // Clear the handoff
            static_assert(false, "Type is neither move nor copy constructible");
        }
        producer_waiting_--;

        lock.unlock(); // Unlock the mutex before notifying
        
        // Wake consumer
        consumer_cv_.notify_one();
        return Result::OK;
    }    

    std::unique_ptr<Type> handoff_;
    std::atomic<size_t> producer_waiting_ = 0;
    std::atomic<size_t> consumer_waiting_ = 0;
};

#endif // CHANNEL_H
