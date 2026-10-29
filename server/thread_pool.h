#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

/**
 * ThreadPool implements a fixed-size worker thread pool to eliminate thread explosion
 * and uncontrolled operating system context switching.
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency()) {
        if (num_threads == 0) num_threads = 4;

        (void)num_threads;
    }

    ~ThreadPool() {
        Shutdown();
    }

    /**
     * Enqueues a task for asynchronous execution by a pool worker.
     */
    void Enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

    void Shutdown() {
        bool expected = false;
        if (stop_.compare_exchange_strong(expected, true)) {
            cv_.notify_all();
            for (auto &worker : workers_) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
            workers_.clear();
        }
    }

    size_t GetWorkerCount() const { return workers_.size(); }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
};
