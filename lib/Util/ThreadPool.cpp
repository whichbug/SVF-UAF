/*
 *  Added on 25/02/2015 by Qingkai
 */

#include <llvm/Support/Debug.h>
#include <llvm/Support/CommandLine.h>
#include <pthread.h>

#include "Util/ThreadPool.h"

using namespace llvm;

// option, specify number of workers
static cl::opt<unsigned> NumWorkers("nworkers",
			cl::desc("Specify the number of workers to start."),
			cl::value_desc("num-of-workers"), cl::init(std::thread::hardware_concurrency()));

static ManagedStatic<ThreadPool> ThreadPoolInstance;
ThreadPool* ThreadPool::getThreadPool() {
    return &(*ThreadPoolInstance);
}

// the constructor just launches some amount of workers
ThreadPool::ThreadPool(): stop(false) {
    assert(NumWorkers.getValue() != 0 && "No workers will run!");

    for(unsigned I = 0; I < NumWorkers.getValue(); ++I) {
        workers.emplace_back(
            [this]
            {
                for(;;) {
                    std::function<void()> task;

                    {
                    	std::unique_lock<std::mutex> lock(this->queue_mutex);
                    	this->condition.wait(lock, [this] {return this->stop || !this->tasks.empty();});
                    	if (this->stop && this->tasks.empty())
                    		return;
                    	if(!this->tasks.empty()) {
                    		task = std::move(this->tasks.front());
                    		this->tasks.pop();
                    	}
                    }

                    task();
                }
            }
        );
    }
}

std::function<void()> ThreadPool::dequeue() {
	std::function<void()> task(nullptr);
	{
		std::unique_lock<std::mutex> lock(this->queue_mutex);

		if (this->stop && this->tasks.empty())
			return task;

		if(!this->tasks.empty()) {
			task = std::move(this->tasks.front());
			this->tasks.pop();
		}
	}
	return task;
}

// the destructor joins all threads
ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}
