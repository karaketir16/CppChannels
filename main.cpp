#include <iostream>
#include <span>
#include <vector>
#include <semaphore>
#include <queue>
#include <thread>
#include <chrono>
#include <mutex>
#include <tuple>
#include <condition_variable>
#include <cassert>


using namespace std::literals;

template <typename Type, size_t N>
struct Channel {

    const Type default_;

    Channel(Type DefaultObj = Type()) : default_(DefaultObj){}

    using QueueType = std::conditional_t<
        std::is_move_constructible_v<Type>,
        std::queue<std::unique_ptr<Type>>,
        std::queue<std::shared_ptr<Type>>
    >;

    QueueType queue_;

    std::counting_semaphore<N ? N : 1> emptySlots{N}; // Tracks empty slots
    std::counting_semaphore<N ? N : 1> fullSlots{0};  // Tracks filled slots
    std::mutex mutex_;
    bool closed_ = false;
    bool toBeClosed_ = false;
    std::condition_variable cv_;

    template <typename U>
    void add(U&& var) {
        if(closed_ || toBeClosed_)
        {
            assert(false && "Writing Closed Channel\n");
        }

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
        cv_.notify_one();
    }


    void close()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        toBeClosed_ = true;
        if(queue_.empty())
        {
            closed_ = true;
        }
        cv_.notify_all();
    }

    Type get(bool& result) {

        emptySlots.release();  // Signal that a slot is now empty

        std::unique_lock <std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return closed_ || fullSlots.try_acquire();}); // Wait until there's an item

        if(closed_)
        {
            result = false;
            return default_;
        }

        result = true;
        // if (!queue_.empty()) //Should not happen

        if(toBeClosed_ && (queue_.size() == 1))
        {
            closed_ = true;
        }

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

    using TupleType = std::tuple<Type, bool>;

    TupleType get() {
        bool tmp;
        return {get(tmp), tmp};
    }
};

struct Deneme
{
    int a;
    Deneme(int x) : a(x){
        // std::cout << __PRETTY_FUNCTION__ << std::endl;
    }
    Deneme(const Deneme& x){
        this->a = x.a;
        // std::cout << __PRETTY_FUNCTION__ << std::endl;
    }
    // Deneme(const Deneme&& x) = delete;
    // Deneme(const Deneme&& x){
    //     this->a = x.a;
    //     std::cout << __PRETTY_FUNCTION__ << std::endl;
    // }

    // Deneme& operator=(const Deneme& x) {
    //     this->a = x.a;
    //     std::cout << __PRETTY_FUNCTION__ << std::endl;
    //     return *this;
    // }

    // Deneme& operator=(Deneme&& x) noexcept {
    //     this->a = x.a;
    //     std::cout << __PRETTY_FUNCTION__ << std::endl;
    //     return *this;
    // }
};



int main() {
    Channel<Deneme, 2> ch(0);

    std::thread worker([&]() {
        for(int i = 0; i < 5;i++)
        {
            std::cout << "Writing... " << i;
            ch.add(Deneme(i));
            std::cout << " Writed\n";
        }
        ch.close();
    });

    for (auto [val, res] = ch.get(); res ; val = ch.get(res)) {
        std::this_thread::sleep_for(1s);
        std::cout << "Readed A value... "<< val.a << std::endl;
    }

    std::cout << "Loop done because channel closed... \n";

    worker.join(); // Ensure worker thread completes before exiting
    return 0;
}


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

Channel<Deneme2, 2> _chTmp(0);