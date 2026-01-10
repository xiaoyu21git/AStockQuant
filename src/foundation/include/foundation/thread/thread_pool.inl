// include/thread/thread_pool.inl
#pragma once

namespace foundation {
namespace thread {

// 工厂方法内联实现
inline ThreadPoolPtr ThreadPoolFactory::create_fixed(size_t size) {
    return std::make_shared<ThreadPoolExecutor>(size);
}

inline ThreadPoolPtr ThreadPoolFactory::create_cpu_aware() {
    auto cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 4;
    return create_fixed(cores);
}

inline ThreadPoolPtr ThreadPoolFactory::create_io_intensive() {
    auto cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 4;
    return create_fixed(cores * 2);
}

inline ThreadPoolPtr ThreadPoolFactory::create_compute_intensive() {
    auto cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 4;
    return create_fixed(cores);
}

inline ThreadPoolPtr ThreadPoolFactory::create_single_threaded() {
    return create_fixed(1);
}

// 默认线程池实现
inline ThreadPoolPtr ThreadPoolManager::default_pool() {
    static ThreadPoolPtr default_pool = ThreadPoolFactory::create_cpu_aware();
    return default_pool;
}

} // namespace thread
} // namespace foundation