#pragma once
#ifndef FOUNDATION_THREAD_THREAD_POOL_HPP
#define FOUNDATION_THREAD_THREAD_POOL_HPP

#include "ThreadPoolExecutor.h"
#include <memory>
#include <functional>
#include <future>
#include <vector>
#include <type_traits>
#include <algorithm>
#include <numeric>

namespace foundation {
namespace thread {
// ============ 线程池工厂 ============
#ifndef THREADPOOLEXECUTOR_PTR_DEFINED
using ThreadPoolExecutorPtr = std::shared_ptr<ThreadPoolExecutor>;
#endif
class ThreadPoolFactory {
public:
    // 创建固定大小的线程池
    static inline ThreadPoolExecutorPtr create_fixed(size_t size) {
        return std::make_shared<ThreadPoolExecutor>(size, size);
    }
    
    // 创建CPU感知线程池
    static inline ThreadPoolExecutorPtr create_cpu_aware() {
        size_t cores = std::thread::hardware_concurrency();
        if (cores == 0) cores = 4;
        return std::make_shared<ThreadPoolExecutor>(cores, cores * 2);
    }
    
    // 创建IO密集型线程池
    static inline ThreadPoolExecutorPtr create_io_intensive() {
        size_t cores = std::thread::hardware_concurrency();
        if (cores == 0) cores = 4;
        return std::make_shared<ThreadPoolExecutor>(cores, cores * 4);
    }
    
    // 创建单线程线程池
    static inline ThreadPoolExecutorPtr create_single_threaded() {
        return std::make_shared<ThreadPoolExecutor>(1, 1);
    }
    
    // 创建动态线程池
    static inline ThreadPoolExecutorPtr create_dynamic(size_t min_threads, size_t max_threads) {
        return std::make_shared<ThreadPoolExecutor>(min_threads, max_threads);
    }
};

// ============ 并行算法 ============
template<typename InputIt, typename OutputIt, typename Transform>
OutputIt parallel_transform(IExecutorPtr executor,
                           InputIt first, InputIt last,
                           OutputIt d_first,
                           Transform transform) {
    
    std::vector<std::future<void>> futures;
    
    for (auto it = first; it != last; ++it) {
        auto current_it = it;
        auto current_d_it = d_first + std::distance(first, it);
        
        futures.push_back(executor->submit([current_it, current_d_it, transform]() {
            *current_d_it = transform(*current_it);
        }));
    }
    
    for (auto& future : futures) {
        future.wait();
    }
    
    return d_first + std::distance(first, last);
}

template<typename InputIt, typename T, typename BinaryOp>
T parallel_reduce(IExecutorPtr executor,
                 InputIt first, InputIt last,
                 T init,
                 BinaryOp binary_op) {
    
    size_t distance = std::distance(first, last);
    if (distance == 0) return init;
    
    size_t num_threads = 4;
    if (auto thread_pool = std::dynamic_pointer_cast<ThreadPoolExecutor>(executor)) {
        num_threads = thread_pool->getWorkerCount();
    }
    
    size_t block_size = (distance + num_threads - 1) / num_threads;
    std::vector<std::future<T>> futures;
    
    for (size_t i = 0; i < num_threads; ++i) {
        InputIt block_first = first + i * block_size;
        InputIt block_last = first + (std::min)((i + 1) * block_size, distance);
        
        if (block_first >= last) break;
        
        futures.push_back(executor->submit([block_first, block_last, binary_op]() {
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

template<typename InputIt, typename UnaryOp>
void parallel_for_each(IExecutorPtr executor,
                      InputIt first, InputIt last,
                      UnaryOp op) {
    
    std::vector<std::future<void>> futures;
    
    for (auto it = first; it != last; ++it) {
        auto current_it = it;
        futures.push_back(executor->submit([current_it, op]() {
            op(*current_it);
        }));
    }
    
    for (auto& future : futures) {
        future.wait();
    }
}
// 基础任务提交（无返回值）
template<typename F, typename... Args>
void submit(IExecutorPtr executor, F&& f, Args&&... args) {
    auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    executor->post(std::move(task));
}
// 异步任务提交（返回 future）
template<typename F, typename... Args>
inline auto async(IExecutorPtr executor, F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {
    
    return executor->submit(std::forward<F>(f), std::forward<Args>(args)...);
}
} // namespace thread
} // namespace foundation

#endif // FOUNDATION_THREAD_THREAD_POOL_HPP