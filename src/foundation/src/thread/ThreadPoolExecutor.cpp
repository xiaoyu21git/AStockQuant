#include "foundation/thread/ThreadPoolExecutor.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <limits>

namespace foundation {
namespace thread {

// ============ 线程本地存储初始化 ============
thread_local bool ThreadPoolExecutor::isWorkerThread_ = false;
thread_local const ThreadPoolExecutor* ThreadPoolExecutor::currentExecutor_ = nullptr;

// ============ 构造函数 ============
ThreadPoolExecutor::ThreadPoolExecutor(
    size_t corePoolSize,
    size_t maxPoolSize,
    std::chrono::milliseconds keepAliveTime,
    const std::string& name)
    : name_(name.empty() ? "ThreadPoolExecutor" : name)
    , corePoolSize_(corePoolSize)
    , maxPoolSize_(maxPoolSize)
    , queueCapacity_(1000)
    , keepAliveTime_(keepAliveTime)
    , allowCoreThreadTimeOut_(false)
    , rejectionPolicy_(RejectionPolicy::ABORT) {
    
    if (corePoolSize_ > maxPoolSize_) {
        corePoolSize_ = maxPoolSize_;
    }
    
    initialize();
}

ThreadPoolExecutor::~ThreadPoolExecutor() {
    shutdown(true);   // ⭐ 唯一正确入口
}

// ============ 初始化 ============
void ThreadPoolExecutor::initialize() {
    // 设置默认线程工厂
    threadFactory_ = [](std::function<void()> task) {
        return std::thread([task = std::move(task)]() {
            task();
        });
    };
    
    // 设置默认异常处理器
    exceptionHandler_ = [](const std::exception_ptr& e) {
        try {
            if (e) std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            std::cerr << "[ThreadPool] Uncaught exception: " << ex.what() << std::endl;
        }
    };
    
    // 创建核心线程
    for (size_t i = 0; i < corePoolSize_; ++i) {
        addWorker();
    }
}

// ============ 添加 Worker ============
void ThreadPoolExecutor::addWorker() {
    if (poolSize_.load() >= maxPoolSize_) {
        return;
    }
    
    auto worker = std::make_unique<Worker>();
    worker->running.store(true);
    worker->idle.store(true);
    worker->lastWorkTime = std::chrono::steady_clock::now();
    
    // 创建线程
    worker->thread = threadFactory_([this, worker = worker.get()]() {
        workerFunction(worker);
    });
    
    workers_.push_back(std::move(worker));
    poolSize_++;
}

// ============ Worker 线程函数 ============
void ThreadPoolExecutor::workerFunction(Worker* worker) {
    // 设置线程本地存储
    isWorkerThread_ = true;
    currentExecutor_ = this;
    
    while (worker->running.load()) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            
            // 等待任务或关闭信号
            queueCondition_.wait(lock, [this]() {
                return !taskQueue_.empty() || shutdown_.load();
            });
            
            // 检查是否应该退出
            if (shutdown_.load() && taskQueue_.empty()) {
                break;
            }
            
            // 获取任务
            if (!taskQueue_.empty()) {
                task = std::move(taskQueue_.front());
                taskQueue_.pop();
                worker->idle.store(false);
                worker->lastWorkTime = std::chrono::steady_clock::now();
                activeCount_++;
            }
        }
        
        // 执行任务
        if (task) {
            auto startTime = std::chrono::steady_clock::now();
            
            try {
                task();
                completedTasks_++;
            } catch (...) {
                failedTasks_++;
                handleTaskException(std::current_exception());
            }
            
            auto endTime = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                endTime - startTime
            );
            
            updateTaskMetrics(duration);
            
            worker->idle.store(true);
            activeCount_--;
        }
    }
    
    // 清理
    cleanupWorker(worker);
    isWorkerThread_ = false;
    currentExecutor_ = nullptr;
}

// ============ 清理 Worker ============
void ThreadPoolExecutor::cleanupWorker(Worker* worker) {
    // worker->running.store(false);
    // poolSize_--;
     worker->running.store(false);  // 可选
    // 从 workers_ 列表中移除
    // workers_.erase(
    //     std::remove_if(workers_.begin(), workers_.end(),
    //         [worker](const std::unique_ptr<Worker>& w) {
    //             return w.get() == worker;
    //         }),
    //     workers_.end()
    // );
}

// ============ 提交任务 ============
void ThreadPoolExecutor::post(std::function<void()> task) {
    if (shutdown_.load()) {
        rejectTask(task);
        return;
    }
    
    submittedTasks_++;
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        
        // 检查队列容量
        if (taskQueue_.size() >= queueCapacity_) {
            rejectTask(task);
            return;
        }
        
        taskQueue_.push(std::move(task));
        
        // 如果需要，创建新线程
        if (activeCount_.load() < poolSize_.load() && poolSize_.load() < maxPoolSize_) {
            addWorker();
        }
    }
    
    queueCondition_.notify_one();
}

// ============ 拒绝任务 ============
void ThreadPoolExecutor::rejectTask(const std::function<void()>& task) {
    rejectedTasks_++;
    
    switch (rejectionPolicy_) {
        case RejectionPolicy::ABORT:
            throw std::runtime_error("Task rejected: thread pool queue is full");
            
        case RejectionPolicy::DISCARD:
            // 直接丢弃任务
            break;
            
        case RejectionPolicy::DISCARD_OLDEST:
            // 丢弃最旧的任务，然后加入新任务
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (!taskQueue_.empty()) {
                    taskQueue_.pop();
                    taskQueue_.push(task);
                }
            }
            break;
            
        case RejectionPolicy::CALLER_RUNS:
            // 在调用者线程执行
            try {
                task();
            } catch (...) {
                handleTaskException(std::current_exception());
            }
            break;
    }
}

// ============ 异常处理 ============
void ThreadPoolExecutor::handleTaskException(const std::exception_ptr& e) {
    if (exceptionHandler_) {
        try {
            exceptionHandler_(e);
        } catch (...) {
            // 忽略异常处理器自身的异常
        }
    }
}

// ============ 更新指标 ============
void ThreadPoolExecutor::updateTaskMetrics(std::chrono::milliseconds duration) {
    auto durationCount = duration.count();
    totalTaskTime_.fetch_add(durationCount);
    
    // 更新最大任务时间
    auto currentMax = maxTaskTime_.load();
    while (durationCount > currentMax && 
           !maxTaskTime_.compare_exchange_weak(currentMax, durationCount)) {
        // CAS 失败，重试
    }
    
    // 更新最小任务时间
    auto currentMin = minTaskTime_.load();
    while (durationCount < currentMin && 
           !minTaskTime_.compare_exchange_weak(currentMin, durationCount)) {
        // CAS 失败，重试
    }
}

// ============ 计算平均任务时间 ============
std::chrono::milliseconds ThreadPoolExecutor::calculateAverageTaskTime() const {
    auto completed = completedTasks_.load();
    if (completed == 0) {
        return std::chrono::milliseconds(0);
    }
    
    auto total = totalTaskTime_.load();
    return std::chrono::milliseconds(total / completed);
}

// ============ 其他接口实现 ============
bool ThreadPoolExecutor::isInExecutorThread() const {
    return isWorkerThread_ && currentExecutor_ == this;
}

void ThreadPoolExecutor::shutdown(bool wait_for_completion) {
     {
       {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (shutdown_) return;
        shutdown_ = true;
    }

    // ⭐ 必须：叫醒所有 worker
    queueCondition_.notify_all();

    // ⭐ 必须：等待线程真正退出
    if (wait_for_completion) {
        for (auto& worker : workers_) {
            if (worker && worker->thread.joinable()) {
                worker->thread.join();
            }
        }
        workers_.clear();
        terminated_ = true;
    }
    }

    queueCondition_.notify_all();

    if (wait_for_completion) {
        for (auto& worker : workers_) {
            if (worker && worker->thread.joinable()) {
                worker->thread.join();
            }
        }
        terminated_.store(true);
    }
}

void ThreadPoolExecutor::shutdownNow() {
    shutdownNow_.store(true);
    shutdown_.store(true);
    
    // 清空队列
    purge();
    
    // 通知所有线程
    queueCondition_.notify_all();
    
    // 标记为终止
    terminated_.store(true);
}

bool ThreadPoolExecutor::isShutdown() const {
    return shutdown_.load();
}

bool ThreadPoolExecutor::isTerminated() const {
    return terminated_.load();
}

bool ThreadPoolExecutor::awaitTermination(std::chrono::milliseconds timeout) {
    auto start = std::chrono::steady_clock::now();
    
    while (!terminated_.load()) {
        if (std::chrono::steady_clock::now() - start > timeout) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return true;
}

size_t ThreadPoolExecutor::getPendingTaskCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return taskQueue_.size();
}

size_t ThreadPoolExecutor::getWorkerCount() const {
    return poolSize_.load();
}

size_t ThreadPoolExecutor::getActiveCount() const {
    return activeCount_.load();
}

size_t ThreadPoolExecutor::getCompletedTaskCount() const {
    return completedTasks_.load();
}

void ThreadPoolExecutor::setRejectionPolicy(RejectionPolicy policy) {
    rejectionPolicy_ = policy;
}

RejectionPolicy ThreadPoolExecutor::getRejectionPolicy() const {
    return rejectionPolicy_;
}

void ThreadPoolExecutor::setName(const std::string& name) {
    name_ = name;
}

std::string ThreadPoolExecutor::getName() const {
    return name_;
}

ExecutorMetrics ThreadPoolExecutor::getMetrics() const {
    ExecutorMetrics metrics;
    metrics.submittedTasks = submittedTasks_.load();
    metrics.completedTasks = completedTasks_.load();
    metrics.failedTasks = failedTasks_.load();
    metrics.pendingTasks = getPendingTaskCount();
    metrics.activeThreads = getActiveCount();
    metrics.idleThreads = getWorkerCount() - getActiveCount();
    metrics.avgTaskTime = calculateAverageTaskTime();
    metrics.maxTaskTime = std::chrono::milliseconds(maxTaskTime_.load());
    metrics.minTaskTime = std::chrono::milliseconds(minTaskTime_.load());
    metrics.totalExecutionTime = std::chrono::milliseconds(totalTaskTime_.load());
    return metrics;
}

void ThreadPoolExecutor::resetMetrics() {
    submittedTasks_.store(0);
    completedTasks_.store(0);
    failedTasks_.store(0);
    rejectedTasks_.store(0);
    totalTaskTime_.store(0);
    maxTaskTime_.store(0);
    minTaskTime_.store(std::numeric_limits<std::chrono::milliseconds::rep>::max());
}

std::string ThreadPoolExecutor::getStatusReport() const {
    std::stringstream ss;
    ss << "ThreadPoolExecutor [" << name_ << "] Status:\n"
       << "  State: " << (isShutdown() ? "Shutdown" : "Running") 
       << (isTerminated() ? "/Terminated" : "") << "\n"
       << "  Workers: " << getWorkerCount() 
       << " (active: " << getActiveCount() 
       << ", idle: " << (getWorkerCount() - getActiveCount()) << ")\n"
       << "  Tasks: " << getPendingTaskCount() << " pending, "
       << getCompletedTaskCount() << " completed, "
       << failedTasks_.load() << " failed\n"
       << "  Queue: " << taskQueue_.size() << "/" << queueCapacity_ << "\n"
       << "  Rejection Policy: " << static_cast<int>(rejectionPolicy_) << "\n";
    return ss.str();
}

} // namespace thread
} // namespace foundation