// thread_pool.cpp - 实现 thread_pool.hpp 中的并行算法
#include "foundation/thread/thread_pool.hpp"

namespace foundation {
namespace thread {

// // submit 函数实现
// // template<typename F, typename... Args>
// // void submit(IExecutorPtr executor, F&& f, Args&&... args) {
// //     auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
// //     executor->post(std::move(task));
// // }

// // async 函数实现
// // template<typename F, typename... Args>
// // inline auto async(IExecutorPtr executor, F&& f, Args&&... args)
// //     -> std::future<std::invoke_result_t<F, Args...>> {
    
// //     return executor->submit(std::forward<F>(f), std::forward<Args>(args)...);
// // }

// // 模板显式实例化（常用类型）
// // template void submit(IExecutorPtr, void(*)(), ...);
// // template std::future<void> async(IExecutorPtr, void(*)(), ...);

// template<typename InputIt, typename OutputIt, typename Transform>
// OutputIt parallel_transform(IExecutorPtr executor,
//                            InputIt first, InputIt last,
//                            OutputIt d_first,
//                            Transform transform) {
    
//     std::vector<std::future<void>> futures;
    
//     for (auto it = first; it != last; ++it) {
//         auto current_it = it;
//         auto current_d_it = d_first + std::distance(first, it);
        
//         futures.push_back(executor->submit([current_it, current_d_it, transform]() {
//             *current_d_it = transform(*current_it);
//         }));
//     }
    
//     for (auto& future : futures) {
//         future.wait();
//     }
    
//     return d_first + std::distance(first, last);
// }

// template<typename InputIt, typename T, typename BinaryOp>
// T parallel_reduce(IExecutorPtr executor,
//                  InputIt first, InputIt last,
//                  T init,
//                  BinaryOp binary_op) {
    
//     size_t distance = std::distance(first, last);
//     if (distance == 0) return init;
    
//     size_t num_threads = 4;
//     if (auto thread_pool = std::dynamic_pointer_cast<ThreadPoolExecutor>(executor)) {
//         num_threads = thread_pool->getWorkerCount();
//     }
    
//     size_t block_size = (distance + num_threads - 1) / num_threads;
//     std::vector<std::future<T>> futures;
    
//     for (size_t i = 0; i < num_threads; ++i) {
//         InputIt block_first = first + i * block_size;
//         InputIt block_last = first + (std::min)((i + 1) * block_size, distance);
        
//         if (block_first >= last) break;
        
//         futures.push_back(executor->submit([block_first, block_last, binary_op]() {
//             T result{};
//             for (auto it = block_first; it != block_last; ++it) {
//                 result = binary_op(result, *it);
//             }
//             return result;
//         }));
//     }
    
//     T final_result = init;
//     for (auto& future : futures) {
//         final_result = binary_op(final_result, future.get());
//     }
    
//     return final_result;
// }

// template<typename InputIt, typename UnaryOp>
// void parallel_for_each(IExecutorPtr executor,
//                       InputIt first, InputIt last,
//                       UnaryOp op) {
    
//     std::vector<std::future<void>> futures;
    
//     for (auto it = first; it != last; ++it) {
//         auto current_it = it;
//         futures.push_back(executor->submit([current_it, op]() {
//             op(*current_it);
//         }));
//     }
    
//     for (auto& future : futures) {
//         future.wait();
//     }
// }

// // 常用类型的显式实例化
// template std::vector<int>::iterator parallel_transform(
//     IExecutorPtr, 
//     std::vector<int>::iterator, 
//     std::vector<int>::iterator,
//     std::vector<int>::iterator,
//     std::function<int(int)>);

// template int parallel_reduce(
//     IExecutorPtr,
//     std::vector<int>::iterator,
//     std::vector<int>::iterator,
//     int,
//     std::function<int(int, int)>);

// template void parallel_for_each(
//     IExecutorPtr,
//     std::vector<int>::iterator,
//     std::vector<int>::iterator,
//     std::function<void(int)>);

} // namespace thread
} // namespace foundation