#pragma once
#ifndef FOUNDATION_THREAD_IEXECUTOR_H
#define FOUNDATION_THREAD_IEXECUTOR_H

#include <functional>
#include <memory>
#include <future>
#include <vector>
#include <chrono>
#include <string>
#include <exception>

namespace foundation {
namespace thread {

// ============ 前置声明 ============
class IExecutor;

// ============ 智能指针别名 ============
using IExecutorPtr = std::shared_ptr<IExecutor>;

// ============ 指标结构 ============
struct ExecutorMetrics {
    size_t submittedTasks = 0;
    size_t completedTasks = 0;
    size_t failedTasks = 0;
    size_t pendingTasks = 0;
    size_t activeThreads = 0;
    size_t idleThreads = 0;
    std::chrono::milliseconds avgTaskTime{0};
    std::chrono::milliseconds maxTaskTime{0};
    std::chrono::milliseconds minTaskTime{0};
    std::chrono::milliseconds totalExecutionTime{0};
    
    std::string toString() const {
        return "Metrics{"
               "submitted=" + std::to_string(submittedTasks) +
               ", completed=" + std::to_string(completedTasks) +
               ", failed=" + std::to_string(failedTasks) +
               ", pending=" + std::to_string(pendingTasks) +
               ", active=" + std::to_string(activeThreads) +
               ", idle=" + std::to_string(idleThreads) + "}";
    }
};

// ============ 拒绝策略 ============
enum class RejectionPolicy {
    ABORT,          // 抛出异常
    DISCARD,        // 丢弃任务
    DISCARD_OLDEST, // 丢弃最旧的任务
    CALLER_RUNS     // 在调用者线程执行
};

// ============ IExecutor 接口 ============
class IExecutor {
public:
    virtual ~IExecutor() = default;

    // ============ 基本任务提交 ============
    
    // 提交任务执行（立即返回）
    virtual void post(std::function<void()> task) = 0;
    
    // 提交任务并返回 future
  template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)-> std::future<std::invoke_result_t<F, Args...>> {
    
        using ResultType = std::invoke_result_t<F, Args...>;
    
        auto task = std::make_shared<std::packaged_task<ResultType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
    
        std::future<ResultType> future = task->get_future();
    
        post([task]() {
            (*task)();  // packaged_task 会自动处理异常
        });
    
        return future;
    }
    // ============ 批量任务操作 ============
    
    // 批量提交任务
    template<typename F>
    void invokeAll(const std::vector<F>& tasks) {
        for (const auto& task : tasks) {
            post(task);
        }
    }
    
    // 批量提交并获取 futures
    template<typename F>
    std::vector<std::future<void>> submitAll(const std::vector<F>& tasks) {
        std::vector<std::future<void>> futures;
        futures.reserve(tasks.size());
        
        for (const auto& task : tasks) {
            futures.push_back(submit(task));
        }
        
        return futures;
    }
    
    // ============ 调度任务 ============
    
    // 延迟执行任务
    virtual void schedule(std::function<void()> task, 
                         std::chrono::milliseconds delay) = 0;
    
    // 周期性执行任务
    virtual void scheduleAtFixedRate(std::function<void()> task,
                                   std::chrono::milliseconds initialDelay,
                                   std::chrono::milliseconds period) = 0;
    
    // 任务完成后延迟执行下一次
    virtual void scheduleWithFixedDelay(std::function<void()> task,
                                      std::chrono::milliseconds initialDelay,
                                      std::chrono::milliseconds delay) = 0;
    
    // ============ 执行器管理 ============
    
    // 优雅关闭执行器
    virtual void shutdown(bool wait_for_completion = true) = 0;
    
    // 立即关闭执行器
    virtual void shutdownNow() = 0;
    
    // 检查是否已关闭
    virtual bool isShutdown() const = 0;
    
    // 检查是否已终止（所有任务已完成）
    virtual bool isTerminated() const = 0;
    
    // 等待执行器终止
    virtual bool awaitTermination(std::chrono::milliseconds timeout) = 0;
    
    // ============ 状态查询 ============
    
    // 是否在 executor 所在线程
    virtual bool isInExecutorThread() const = 0;
    
    // 获取待处理任务数量
    virtual size_t getPendingTaskCount() const = 0;
    
    // 获取工作线程数量
    virtual size_t getWorkerCount() const = 0;
    
    // 获取活动线程数量
    virtual size_t getActiveCount() const = 0;
    
    // 获取已完成任务数量
    virtual size_t getCompletedTaskCount() const = 0;
    
    // ============ 线程管理 ============
    
    // 预启动核心线程
    virtual void prestartAllCoreThreads() = 0;
    
    // 预启动单个核心线程
    virtual bool prestartCoreThread() = 0;
    
    // 允许核心线程超时
    virtual void allowCoreThreadTimeOut(bool value) = 0;
    
    // 设置线程空闲时间
    virtual void setKeepAliveTime(std::chrono::milliseconds time) = 0;
    
    // 获取线程空闲时间
    virtual std::chrono::milliseconds getKeepAliveTime() const = 0;
    
    // ============ 配置管理 ============
    
    // 设置拒绝策略
    virtual void setRejectionPolicy(RejectionPolicy policy) = 0;
    
    // 获取拒绝策略
    virtual RejectionPolicy getRejectionPolicy() const = 0;
    
    // 设置线程工厂
    virtual void setThreadFactory(
        std::function<std::thread(std::function<void()>)> factory) = 0;
    
    // 获取线程工厂
    virtual std::function<std::thread(std::function<void()>)> 
        getThreadFactory() const = 0;
    
    // 设置未捕获异常处理器
    virtual void setUncaughtExceptionHandler(
        std::function<void(const std::exception_ptr&)> handler) = 0;
    
    // 获取未捕获异常处理器
    virtual std::function<void(const std::exception_ptr&)> 
        getUncaughtExceptionHandler() const = 0;
    
    // 设置执行器名称
    virtual void setName(const std::string& name) = 0;
    
    // 获取执行器名称
    virtual std::string getName() const = 0;
    
    // ============ 监控和统计 ============
    
    // 获取执行器指标
    virtual ExecutorMetrics getMetrics() const = 0;
    
    // 重置指标
    virtual void resetMetrics() = 0;
    
    // 获取状态报告
    virtual std::string getStatusReport() const = 0;
    
    // ============ 高级功能 ============
    
    // 调整线程池大小
    virtual void resize(size_t numThreads) = 0;
    
    // 清空任务队列
    virtual void purge() = 0;
    
    // 等待所有任务完成
    virtual void waitForCompletion() = 0;
    
    // 获取线程堆栈跟踪
    virtual std::vector<std::string> getThreadStackTraces() const = 0;
    
    // 检查死锁
    virtual bool hasDeadlock() const = 0;
    
    // 获取死锁信息
    virtual std::vector<std::string> getDeadlockInfo() const = 0;
};

} // namespace thread
} // namespace foundation

#endif // FOUNDATION_THREAD_IEXECUTOR_H