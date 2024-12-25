#include <iostream>
#include <span>
#include <vector>
#include <semaphore>
#include <queue>
#include <thread>
#include <chrono>
#include <mutex>

using namespace std::literals;

template <typename Type, size_t N>
struct Channel {
    using QueueType = std::conditional_t<
        std::is_move_constructible_v<Type>,
        std::queue<std::unique_ptr<Type>>,
        std::queue<std::shared_ptr<Type>>
    >;

    QueueType queue_;

    std::counting_semaphore<N ? N : 1> emptySlots{N}; // Tracks empty slots
    std::counting_semaphore<N ? N : 1> fullSlots{0};  // Tracks filled slots
    std::mutex mutex_;

    template <typename U>
    void add(U&& var) {
        emptySlots.acquire();  // Block if no empty slots
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if constexpr (std::is_move_constructible_v<Type>) {
                // Use move semantics for rvalue or copy for lvalue
                queue_.emplace(std::make_unique<Type>(std::forward<U>(var)));
            } else {
                // Use shared_ptr if move is not supported
                queue_.emplace(std::make_shared<Type>(var));
            }
        }
        fullSlots.release();  // Signal that a slot is filled
    }

    template <typename U>
    void operator<<(U&& var) {
        add(std::forward<U>(var));
    }


    Type get() {
        emptySlots.release();  // Signal that a slot is now empty
        fullSlots.acquire();  // Wait until there's an item
        
        std::lock_guard<std::mutex> lock(mutex_);
        // if (!queue_.empty()) //Should not happen

        if constexpr (std::is_move_constructible_v<Type>) {
            // Move the object out of the queue if move constructible
            auto item = queue_.front().release();
            queue_.pop();  // Remove the front element
            return std::move(*item);
        } else {
            // Copy the object out of the queue if not move constructible
            auto item = queue_.front();
            queue_.pop();  // Remove the front element
            return *item;
        }
    }

    void operator>>(Type& var) {
        var = get();
    }
};

struct Deneme
{
    int a;
    Deneme(int x) : a(x){
        std::cout << __PRETTY_FUNCTION__ << std::endl;
    }
    Deneme(const Deneme& x){
        this->a = x.a;
        std::cout << __PRETTY_FUNCTION__ << std::endl;
    }
    // Deneme(const Deneme&& x) = delete;
    Deneme(const Deneme&& x){
        this->a = x.a;
        std::cout << __PRETTY_FUNCTION__ << std::endl;
    }
};


struct Deneme2
{
    int a;
    Deneme2(int x) : a(x){
        std::cout << __PRETTY_FUNCTION__ << std::endl;
    }
    Deneme2(const Deneme2& x){
        this->a = x.a;
        std::cout << __PRETTY_FUNCTION__ << std::endl;
    }
    Deneme2(const Deneme2&& x) = delete;

};


int main() {
    Channel<int, 0> ch;

    Channel<Deneme, 1> ch2;
    Channel<Deneme2, 1> ch3;

    ch2.add(Deneme(5));
    ch3 << Deneme2(3);

    std::thread worker([&]() {
        std::this_thread::sleep_for(2s);
        std::cout << "Get\n";
        Deneme var = ch2.get();
        std::cout << "TMP: " << var.a << std::endl;
        Deneme2 var2 = ch3.get();
        std::cout << "TMP: " << var2.a << std::endl;
        ch.get();
        std::cout << "Get\n";
    });

    std::this_thread::sleep_for(1s);

    ch.add(1);
    std::cout << "Add 1\n";

    ch.add(2);
    std::cout << "Add 2\n";

    worker.join(); // Ensure worker thread completes before exiting
    return 0;
}
