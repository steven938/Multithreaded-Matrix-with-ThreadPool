#pragma once
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
    ThreadPool(size_t);
    template<class F, class... Args> // ... is variadic template (zero or more templates form Args)
    auto enqueue(F&& f, Args&&... args)
        ->std::future<typename std::result_of<F(Args...)>::type>;
    void wait_for_completion();
    ~ThreadPool();
private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue
    std::queue< std::function<void()> > tasks;

    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop; // stop indiciates whether threadpool should top

    // signal idleness
    int free_count=0; // counts number of free threads
    std::mutex count_mutex;
    std::condition_variable count_cond, queue_check_cond; // all_available indicates whether all threads are available
    
    int total_threads;
};


// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
    : stop(false)
{
    total_threads = threads;
    free_count = threads;

    for (size_t i = 0; i < threads; ++i)
        workers.emplace_back(
            // create a thread with function below;
            [this]
            {
                for (;;) // infinite loop - so keeps looking for tasks to process
                {
                    std::function<void()> task; // task that this thread is currently processing
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock,
                            [this] { return this->stop || !this->tasks.empty(); }); //unlock lock and wait to be notified when a task gets enqueued
                                                                                    //relock and continue when notified and stop = true or tasks is not empty

                        if (this->stop && this->tasks.empty()) // stop thread if stop = true and task queue is empty
                            return;

                        // decrement free count
                        {
                            std::unique_lock<std::mutex> lock(this->count_mutex);
                            this->free_count--;
                        }

                        task = std::move(this->tasks.front()); // takes on task at front of task queue
                        this->tasks.pop(); // pop the task from task queue
                    }
                    this->queue_check_cond.notify_one(); // notify to check if queue is empty

                    task(); // run the task
                    
                    // increment free count
                    {
                        std::unique_lock<std::mutex> lock(this->count_mutex);
                        this->free_count++;
                    }
                    this->count_cond.notify_one(); // notify that this thread has been freed
                }
            }
            );
}

// add new work item to the pool
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
-> std::future<typename std::result_of<F(Args...)>::type> // return type of enqueue function
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared< std::packaged_task<return_type()> >( // constructs pacakged task function (an asynchronous function) and returns shared pointer
        std::bind(std::forward<F>(f), std::forward<Args>(args)...) 
        ); 

    std::future<return_type> res = task->get_future(); 
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task]() { (*task)(); });
    }
    condition.notify_one(); // unblock one task thread so that it can take on task
    return res;
}

// used for synchronization so that threadpool user knows when all tasks are completed and threads are all free
void ThreadPool::wait_for_completion() {
    // wait till task queue is empty
    // queue_check_cond notifies this thread when items are popped from queue 
    {
        std::cout << "waiting for task queue to become empty" << std::endl;
        std::unique_lock<std::mutex> lock(this->queue_mutex);
        this->queue_check_cond.wait(lock, [this] {return this->tasks.empty(); }); // wait until queue is empty
        std::cout << "task queue is now empty" << std::endl;
    }
    
    // wait till all threads are free
    {
        std::cout << "waiting for all threads to finish processing" << std::endl;
        std::unique_lock<std::mutex> lock1(this->count_mutex);
        if (this->free_count == this->total_threads) {
            std::cout << "all threads finished processing" << std::endl;
            return;
        }
        this->count_cond.wait(lock1, [this] {return this->free_count == this->total_threads; });
    }
    std::cout << "all threads finished processing" << std::endl;
    
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread& worker : workers)
        worker.join();
}

#endif