// include/thread/thread_pool.hpp
#pragma once
#include "IExecutor.h"
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
namespace foundation {
namespace thread {
    class ThreadPoolExecutor : public IExecutor {
    public:
        explicit ThreadPoolExecutor(size_t threadCount = std::thread::hardware_concurrency());
        ~ThreadPoolExecutor();

        void post(std::function<void()> task) override;
        bool isInExecutorThread() const override;

    private:
        void workerLoop();

    private:
        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::atomic<bool> running_;
        std::thread::id mainThreadId_; // 用来标记创建线程
    };
}
}