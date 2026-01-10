// src/thread/thread_pool.cpp
#include "thread/thread_pool.hpp"
#include "ThreadPoolExecutor.h"
#include "InlineExecutor.h"
#include <algorithm>
#include <iostream>

namespace foundation {
namespace thread {

// 静态成员初始化
std::vector<ThreadPoolPtr> ThreadPoolManager::active_pools_;
std::mutex ThreadPoolManager::pools_mutex_;

// ============ ThreadPoolFactory 实现 ============

ThreadPoolPtr ThreadPoolFactory::create_dynamic(const ThreadPoolConfig& config) {
    // 这里可以创建一个更高级的动态线程池
    // 暂时返回固定大小的线程池
    auto pool = create_fixed(config.max_threads);
    
    // 注册到管理器
    ThreadPoolManager::register_pool(pool);
    
    return pool;
}

InlineExecutorPtr ThreadPoolFactory::create_inline() {
    return std::make_shared<InlineExecutor>();
}

// ============ ThreadPoolManager 实现 ============

void ThreadPoolManager::set_default_pool(ThreadPoolPtr pool) {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    
    // 关闭旧的默认线程池
    if (default_pool()) {
        default_pool()->shutdown(true);
    }
    
    // 注册新的线程池
    register_pool(pool);
}

std::vector<ThreadPoolPtr> ThreadPoolManager::get_active_pools() {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    return active_pools_;
}

void ThreadPoolManager::shutdown_all(bool wait_for_completion) {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    
    for (auto& pool : active_pools_) {
        if (pool) {
            pool->shutdown(wait_for_completion);
        }
    }
    
    active_pools_.clear();
}

ThreadPoolMetrics ThreadPoolManager::get_metrics(ThreadPoolPtr pool) {
    ThreadPoolMetrics metrics;
    
    if (pool) {
        // 这里可以添加具体的指标收集逻辑
        // 比如：metrics.pending_tasks = pool->getTaskCount();
        // metrics.active_threads = pool->getActiveThreadCount();
    }
    
    return metrics;
}

void ThreadPoolManager::register_pool(ThreadPoolPtr pool) {
    if (!pool) return;
    
    std::lock_guard<std::mutex> lock(pools_mutex_);
    
    // 检查是否已注册
    auto it = std::find(active_pools_.begin(), active_pools_.end(), pool);
    if (it == active_pools_.end()) {
        active_pools_.push_back(pool);
    }
}

void ThreadPoolManager::unregister_pool(ThreadPoolPtr pool) {
    if (!pool) return;
    
    std::lock_guard<std::mutex> lock(pools_mutex_);
    
    auto it = std::find(active_pools_.begin(), active_pools_.end(), pool);
    if (it != active_pools_.end()) {
        active_pools_.erase(it);
    }
}

// ============ 便捷函数 ============

// 创建并配置线程池的便捷函数
ThreadPoolPtr make_thread_pool(const std::string& type = "cpu") {
    if (type == "cpu" || type == "compute") {
        return ThreadPoolFactory::create_cpu_aware();
    } else if (type == "io") {
        return ThreadPoolFactory::create_io_intensive();
    } else if (type == "single") {
        return ThreadPoolFactory::create_single_threaded();
    } else if (type == "fixed") {
        return ThreadPoolFactory::create_fixed(4);
    } else {
        // 尝试解析数字
        try {
            size_t size = std::stoul(type);
            return ThreadPoolFactory::create_fixed(size);
        } catch (...) {
            // 默认返回CPU感知线程池
            return ThreadPoolFactory::create_cpu_aware();
        }
    }
}

// 并行排序（示例）
template<typename RandomIt>
void parallel_sort(ThreadPoolPtr executor, RandomIt first, RandomIt last) {
    parallel_sort(executor, first, last, std::less<typename RandomIt::value_type>());
}

template<typename RandomIt, typename Compare>
void parallel_sort(ThreadPoolPtr executor, RandomIt first, RandomIt last, Compare comp) {
    size_t distance = std::distance(first, last);
    if (distance <= 1024) {
        // 小数组直接排序
        std::sort(first, last, comp);
        return;
    }
    
    RandomIt middle = first + distance / 2;
    
    // 并行排序两部分
    auto left_future = async(executor, [executor, first, middle, comp]() {
        parallel_sort(executor, first, middle, comp);
    });
    
    auto right_future = async(executor, [executor, middle, last, comp]() {
        parallel_sort(executor, middle, last, comp);
    });
    
    left_future.get();
    right_future.get();
    
    // 合并结果
    std::inplace_merge(first, middle, last, comp);
}

} // namespace thread
} // namespace foundation