/*
 *  Added on 17/10/2015 by Qingkai
 */

#ifndef PLATFORM_SCHEDULER_MTSCHEDULER_THREAD_POOL_H
#define PLATFORM_SCHEDULER_MTSCHEDULER_THREAD_POOL_H

#include <llvm/Support/ManagedStatic.h>

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>

class ThreadPool {
public:
    template<class F, class... Args>
    auto enqueue(F&&, Args&&...)
        -> std::future<typename std::result_of<F(Args...)>::type>;

    std::function<void()> dequeue();

    ~ThreadPool();

private:
    ThreadPool();
    friend void* llvm::object_creator<ThreadPool>();

    // need to keep track of threads so we can join them
    std::vector<std::thread> workers;
    // the task queue containing tasks
    std::queue<std::function<void()>> tasks;
    
    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;

public:
    static ThreadPool* getThreadPool();
};

// add new work item to the pool
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow to enqueue after stopping the pool
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();
    return res;
}

#endif
