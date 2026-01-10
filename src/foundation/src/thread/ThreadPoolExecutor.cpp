#include "foundation/thread/ThreadPoolExecutor.h"
#include <iostream>
namespace foundation {
namespace thread {
ThreadPoolExecutor::ThreadPoolExecutor(size_t threadCount)
    : running_(true)
{
    mainThreadId_ = std::this_thread::get_id();

    for (size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

ThreadPoolExecutor::~ThreadPoolExecutor()
{
    running_ = false;
    cv_.notify_all();

    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void ThreadPoolExecutor::post(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

bool ThreadPoolExecutor::isInExecutorThread() const
{
    return std::this_thread::get_id() == mainThreadId_;
}

void ThreadPoolExecutor::workerLoop()
{
    while (running_) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !tasks_.empty() || !running_; });

            if (!running_ && tasks_.empty())
                return;

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "[ThreadPoolExecutor] Exception: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[ThreadPoolExecutor] Unknown exception\n";
        }
    }
}
}}