// include/thread/thread_pool.hpp
#pragma once

#include <memory>
#include <functional>
#include <future>
#include <vector>
#include <type_traits>
#include <stdio.h>
#include <iostream>
#include "IExecutor.h"
namespace foundation {
namespace thread {

// ============ 前置声明 ============
class IExecutor;
class ThreadPoolExecutor;
class InlineExecutor;

// ============ 线程池配置 ============
struct ThreadPoolConfig {
    size_t min_threads = 1;
    size_t max_threads = 0;  // 0表示使用硬件并发数
    size_t queue_capacity = 1000;
    std::chrono::milliseconds keep_alive_time = std::chrono::seconds(60);
    bool enable_work_stealing = false;
    bool enable_metrics = false;
    
    ThreadPoolConfig() {
        if (max_threads == 0) {
            max_threads = std::thread::hardware_concurrency();
            if (max_threads == 0) {
                max_threads = 4;
            }
        }
        if (min_threads > max_threads) {
            min_threads = max_threads;
        }
    }
};

// ============ 线程池状态 ============
enum class ThreadPoolState {
    CREATED,    // 已创建
    RUNNING,    // 运行中
    STOPPING,   // 正在停止
    STOPPED,    // 已停止
    DESTROYED   // 已销毁
};

struct ThreadPoolMetrics {
    size_t active_threads = 0;
    size_t idle_threads = 0;
    size_t pending_tasks = 0;
    size_t completed_tasks = 0;
    size_t failed_tasks = 0;
    std::chrono::milliseconds avg_task_time{0};
};

// ============ 智能指针别名 ============
using IExecutorPtr = std::shared_ptr<IExecutor>;
using ThreadPoolPtr = std::shared_ptr<ThreadPoolExecutor>;
using InlineExecutorPtr = std::shared_ptr<InlineExecutor>;

// ============ 线程池工厂 ============
class ThreadPoolFactory {
public:
    // 创建固定大小的线程池
    static ThreadPoolPtr create_fixed(size_t size);
    
    // 创建CPU感知线程池（根据CPU核心数）
    static ThreadPoolPtr create_cpu_aware();
    
    // 创建IO密集型线程池（2倍CPU核心数）
    static ThreadPoolPtr create_io_intensive();
    
    // 创建计算密集型线程池（CPU核心数）
    static ThreadPoolPtr create_compute_intensive();
    
    // 创建单线程线程池
    static ThreadPoolPtr create_single_threaded();
    
    // 创建动态线程池（根据负载调整）
    static ThreadPoolPtr create_dynamic(const ThreadPoolConfig& config);
    
    // 创建内联执行器（直接在当前线程执行）
    static InlineExecutorPtr create_inline();
};

// ============ 任务提交接口 ============

// 基础任务提交（无返回值）
template<typename F, typename... Args>
void submit(IExecutorPtr executor, F&& f, Args&&... args) {
    auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    executor->post(std::move(task));
}

// 异步任务提交（返回future）
template<typename F, typename... Args>
auto async(IExecutorPtr executor, F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {
    
    using ResultType = std::invoke_result_t<F, Args...>;
    
    auto task = std::make_shared<std::packaged_task<ResultType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<ResultType> future = task->get_future();
    
    executor->post([task]() {
        try {
            (*task)();
        } catch (...) {
            task->set_exception(std::current_exception());
        }
    });
    
    return future;
}

// 批量提交任务
template<typename F>
void submit_batch(IExecutorPtr executor, const std::vector<F>& tasks) {
    for (const auto& task : tasks) {
        executor->post(task);
    }
}

// ============ 并行算法 ============

// 并行映射（Parallel Map）
template<typename InputIt, typename OutputIt, typename Transform>
OutputIt parallel_transform(IExecutorPtr executor,
                           InputIt first, InputIt last,
                           OutputIt d_first,
                           Transform transform) {
    
    using ValueType = typename std::iterator_traits<InputIt>::value_type;
    std::vector<std::future<void>> futures;
    
    auto it = first;
    auto d_it = d_first;
    
    while (it != last) {
        auto current_it = it;
        auto current_d_it = d_it;
        
        futures.push_back(async(executor, [current_it, current_d_it, transform]() {
            *current_d_it = transform(*current_it);
        }));
        
        ++it;
        ++d_it;
    }
    
    // 等待所有任务完成
    for (auto& future : futures) {
        future.get();
    }
    
    return d_it;
}

// 并行归约（Parallel Reduce）
template<typename InputIt, typename T, typename BinaryOp>
T parallel_reduce(IExecutorPtr executor,
                 InputIt first, InputIt last,
                 T init,
                 BinaryOp binary_op) {
    
    using ValueType = typename std::iterator_traits<InputIt>::value_type;
    
    size_t distance = std::distance(first, last);
    if (distance == 0) return init;
    
    size_t num_threads = std::dynamic_pointer_cast<ThreadPoolExecutor>(executor)
                        ->getWorkerCount();
    
    size_t block_size = (distance + num_threads - 1) / num_threads;
    
    std::vector<std::future<T>> futures;
    
    for (size_t i = 0; i < num_threads; ++i) {
        InputIt block_first = first + i * block_size;
        InputIt block_last = first + (std::min)((i + 1) * block_size, distance);
        
        if (block_first >= last) break;
        
        futures.push_back(async(executor, [block_first, block_last, binary_op]() {
            T result{};
            for (auto it = block_first; it != block_last; ++it) {
                result = binary_op(result, *it);
            }
            return result;
        }));
    }
    
    T final_result = init;
    for (auto& future : futures) {
        final_result = binary_op(final_result, future.get());
    }
    
    return final_result;
}

// 并行遍历（Parallel For Each）
template<typename InputIt, typename UnaryOp>
void parallel_for_each(IExecutorPtr executor,
                      InputIt first, InputIt last,
                      UnaryOp op) {
    
    std::vector<std::future<void>> futures;
    
    for (auto it = first; it != last; ++it) {
        auto current_it = it;
        futures.push_back(async(executor, [current_it, op]() {
            op(*current_it);
        }));
    }
    
    for (auto& future : futures) {
        future.get();
    }
}

// ============ 线程池管理工具 ============
class ThreadPoolManager {
public:
    // 全局默认线程池
    static ThreadPoolPtr default_pool();
    
    // 设置全局线程池
    static void set_default_pool(ThreadPoolPtr pool);
    
    // 获取所有活跃的线程池
    static std::vector<ThreadPoolPtr> get_active_pools();
    
    // 关闭所有线程池
    static void shutdown_all(bool wait_for_completion = true);
    
    // 监控线程池状态
    static ThreadPoolMetrics get_metrics(ThreadPoolPtr pool);
    
private:
    static std::vector<ThreadPoolPtr> active_pools_;
    static std::mutex pools_mutex_;
};

// ============ 任务包装器 ============
class TaskWrapper {
public:
    template<typename F, typename... Args>
    TaskWrapper(F&& f, Args&&... args) {
        task_ = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    }
    
    void operator()() {
        try {
            task_();
        } catch (const std::exception& e) {
            // 记录异常，但不抛出
            std::cerr << "Task exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown task exception" << std::endl;
        }
    }
    
private:
    std::function<void()> task_;
};

// ============ 便捷宏 ============

// 提交任务到默认线程池
#define THREAD_SUBMIT(task) \
    foundation::thread::submit(foundation::thread::ThreadPoolManager::default_pool(), task)

// 异步执行任务
#define THREAD_ASYNC(func, ...) \
    foundation::thread::async(foundation::thread::ThreadPoolManager::default_pool(), func, ##__VA_ARGS__)

// 并行循环
#define PARALLEL_FOR_EACH(container, lambda) \
    foundation::thread::parallel_for_each( \
        foundation::thread::ThreadPoolManager::default_pool(), \
        (container).begin(), (container).end(), \
        lambda)

// 检查是否在线程池线程中
#define IN_THREAD_POOL() \
    foundation::thread::ThreadPoolManager::default_pool()->isInExecutorThread()

} // namespace thread
} // namespace foundation

// ============ 实现文件 ============
// 注意：这个文件是头文件，但包含模板实现
// 具体实现需要在thread_pool.cpp中

// 内联函数定义
#include "thread_pool.inl"