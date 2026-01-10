#pragma once
#ifndef FOUNDATION_THREAD_THREADPOOLEXECUTOR_H
#define FOUNDATION_THREAD_THREADPOOLEXECUTOR_H

// 防止 Windows.h 中的 min/max 宏
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "IExecutor.h"
#include <memory>
#include <functional>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <map>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace foundation {
namespace thread {

class ThreadPoolExecutor : public IExecutor {
public:
    // ============ 构造函数 ============
    
    // 基础构造函数
    explicit ThreadPoolExecutor(
        size_t corePoolSize,
        size_t maxPoolSize,
        std::chrono::milliseconds keepAliveTime = std::chrono::seconds(60),
        const std::string& name = ""
    );
    
    // 简化构造函数
    explicit ThreadPoolExecutor(size_t numThreads)
        : ThreadPoolExecutor(numThreads, numThreads) {}
    
    // 析构函数
    ~ThreadPoolExecutor() override;
    
    // ============ IExecutor 接口实现 ============
    
    // 基本任务提交
    void post(std::function<void()> task) override;
    bool isInExecutorThread() const override;
    
    // 执行器管理
    void shutdown(bool wait_for_completion = true) override;
    void shutdownNow() override;
    bool isShutdown() const override;
    bool isTerminated() const override;
    bool awaitTermination(std::chrono::milliseconds timeout) override;
    
    // 状态查询
    size_t getPendingTaskCount() const override;
    size_t getWorkerCount() const override;
    size_t getActiveCount() const override;
    size_t getCompletedTaskCount() const override;
    
    // 配置管理
    void setRejectionPolicy(RejectionPolicy policy) override;
    RejectionPolicy getRejectionPolicy() const override;
    void setName(const std::string& name) override;
    std::string getName() const override;
    
    // 监控和统计
    ExecutorMetrics getMetrics() const override;
    void resetMetrics() override;
    std::string getStatusReport() const override;
    
    // ============ 简化接口（其他接口提供空实现） ============
    void schedule(std::function<void()> task, 
                 std::chrono::milliseconds delay) override {
        // 简化实现：延迟后提交
        auto timer = std::make_shared<std::thread>([this, task, delay]() {
            std::this_thread::sleep_for(delay);
            post(task);
        });
        timer->detach();
    }
    
    void scheduleAtFixedRate(std::function<void()> task,
                           std::chrono::milliseconds initialDelay,
                           std::chrono::milliseconds period) override {
        // 简化：只执行一次
        schedule(task, initialDelay);
    }
    
    void scheduleWithFixedDelay(std::function<void()> task,
                              std::chrono::milliseconds initialDelay,
                              std::chrono::milliseconds delay) override {
        // 简化：只执行一次
        schedule(task, initialDelay);
    }
    
    void prestartAllCoreThreads() override {
        // 已在构造函数中实现
    }
    
    bool prestartCoreThread() override {
        // 简化：总是返回 true
        return true;
    }
    
    void allowCoreThreadTimeOut(bool value) override {
        allowCoreThreadTimeOut_ = value;
    }
    
    void setKeepAliveTime(std::chrono::milliseconds time) override {
        keepAliveTime_ = time;
    }
    
    std::chrono::milliseconds getKeepAliveTime() const override {
        return keepAliveTime_;
    }
    
    void setThreadFactory(
        std::function<std::thread(std::function<void()>)> factory) override {
        threadFactory_ = std::move(factory);
    }
    
    std::function<std::thread(std::function<void()>)> 
        getThreadFactory() const override {
        return threadFactory_;
    }
    
    void setUncaughtExceptionHandler(
        std::function<void(const std::exception_ptr&)> handler) override {
        exceptionHandler_ = std::move(handler);
    }
    
    std::function<void(const std::exception_ptr&)> 
        getUncaughtExceptionHandler() const override {
        return exceptionHandler_;
    }
    
    void resize(size_t numThreads) override {
        maxPoolSize_ = (std::max)(numThreads, corePoolSize_);
        
    }
    
    void purge() override {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::queue<std::function<void()>> empty;
        std::swap(taskQueue_, empty);
    }
    
    void waitForCompletion() override {
        while (getPendingTaskCount() > 0 || getActiveCount() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    std::vector<std::string> getThreadStackTraces() const override {
        return {};  // 简化：返回空
    }
    
    bool hasDeadlock() const override {
        return false;  // 简化：总是返回 false
    }
    
    std::vector<std::string> getDeadlockInfo() const override {
        return {};  // 简化：返回空
    }

private:
    // ============ 内部 Worker 结构体 ============
    struct Worker {
        std::thread thread;
        std::atomic<bool> running{false};
        std::atomic<bool> idle{true};
        std::chrono::steady_clock::time_point lastWorkTime;
        
        Worker() = default;
        
        Worker(Worker&& other) noexcept
            : thread(std::move(other.thread))
            , running(other.running.load())
            , idle(other.idle.load())
            , lastWorkTime(other.lastWorkTime) {
        }
        
        Worker& operator=(Worker&& other) noexcept {
            if (this != &other) {
                //stop();
                // 移动资源
                thread = std::move(other.thread);
                running.store(other.running.load());
                idle.store(other.idle.load());
                lastWorkTime = other.lastWorkTime;
            }
            return *this;
        }
        void stop() {
        if (thread.joinable()) {
            thread.detach();  // 或 thread.join() 如果可能的话
        }
    }
        ~Worker() {
           
        }
    };
    
    // ============ 成员变量 ============
    std::string name_;
    size_t corePoolSize_;
    size_t maxPoolSize_;
    size_t queueCapacity_;
    std::chrono::milliseconds keepAliveTime_;
    bool allowCoreThreadTimeOut_;
    RejectionPolicy rejectionPolicy_;
    
    std::vector<std::unique_ptr<Worker>> workers_;
    std::function<std::thread(std::function<void()>)> threadFactory_;
    
    std::queue<std::function<void()>> taskQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    
    std::atomic<bool> shutdown_{false};
    std::atomic<bool> shutdownNow_{false};
    std::atomic<bool> terminated_{false};
    std::atomic<size_t> poolSize_{0};
    std::atomic<size_t> activeCount_{0};
    
    std::atomic<size_t> submittedTasks_{0};
    std::atomic<size_t> completedTasks_{0};
    std::atomic<size_t> failedTasks_{0};
    std::atomic<size_t> rejectedTasks_{0};
    std::atomic<std::chrono::milliseconds::rep> totalTaskTime_{0};
    std::atomic<std::chrono::milliseconds::rep> maxTaskTime_{0};
    //std::atomic<std::chrono::milliseconds::rep> minTaskTime_{std::numeric_limits<std::chrono::milliseconds::rep>::max()};
    std::atomic<std::chrono::milliseconds::rep> minTaskTime_{(std::numeric_limits<std::chrono::milliseconds::rep>::max)()};
   
    std::function<void(const std::exception_ptr&)> exceptionHandler_;
    
    static thread_local bool isWorkerThread_;
    static thread_local const ThreadPoolExecutor* currentExecutor_;
    
    // ============ 私有方法 ============
    void initialize();
    void addWorker();
    void workerFunction(Worker* worker);
    bool addTaskToQueue(std::function<void()> task);
    void rejectTask(const std::function<void()>& task);
    void executeTask(const std::function<void()>& task);
    void updateTaskMetrics(std::chrono::milliseconds duration);
    void handleTaskException(const std::exception_ptr& e);
    void cleanupWorker(Worker* worker);
    std::chrono::milliseconds calculateAverageTaskTime() const;
};

} // namespace thread
} // namespace foundation

#endif // FOUNDATION_THREAD_THREADPOOLEXECUTOR_H