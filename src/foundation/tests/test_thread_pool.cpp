#include <gtest/gtest.h>
#include "foundation/thread/thread_pool.hpp"
#include "foundation/thread/ThreadPoolExecutor.h"
#include <atomic>
#include <future>
#include <vector>

// TEST(ThreadPoolTest, BasicCreation) {
//     // 测试创建线程池
//     auto pool = foundation::thread::ThreadPoolFactory::create_fixed(2);
//     ASSERT_NE(pool, nullptr);
    
//     // 测试线程池基本功能
//     std::atomic<int> counter{0};
    
//     auto task = [&counter]() {
//         counter.fetch_add(1, std::memory_order_relaxed);
//     };
    
//     // 直接使用 post 方法
//     pool->post(task);
//     pool->post(task);
    
//     // 等待任务完成
//     std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
//     EXPECT_GE(counter.load(), 0);
    
//     // 清理
//     pool->shutdown(true);
// }
TEST(ThreadPoolTest, BasicCreation) {
   
        auto pool = foundation::thread::ThreadPoolFactory::create_fixed(2);
        ASSERT_NE(pool, nullptr);
        auto future = pool->submit([]() {
            return 42;
        });
        EXPECT_EQ(future.get(), 42);
        pool->shutdown(true);
}
TEST(ThreadPoolTest, SubmitAndAsync) {
    auto pool = foundation::thread::ThreadPoolFactory::create_fixed(2);
    
    // 使用 submit 函数
    std::atomic<int> value{0};
    foundation::thread::submit(pool, [&value]() {
        value.store(42);
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(value.load(), 42);
    
    // 使用 async 函数
    auto future = foundation::thread::async(pool, []() -> int {
        return 100;
    });
    
    EXPECT_EQ(future.get(), 100);
    
    pool->shutdown(true);
}

TEST(ThreadPoolTest, MultipleTasks) {
    auto pool = foundation::thread::ThreadPoolFactory::create_fixed(4);
    
    constexpr int NUM_TASKS = 100;
    std::atomic<int> completed{0};
    std::vector<std::future<void>> futures;
    
    for (int i = 0; i < NUM_TASKS; ++i) {
        futures.push_back(foundation::thread::async(pool, [&completed]() {
            completed.fetch_add(1);
        }));
    }
    
    // 等待所有任务完成
    for (auto& future : futures) {
        future.wait();
    }
    
    EXPECT_EQ(completed.load(), NUM_TASKS);
    pool->shutdown(true);
}

TEST(ThreadPoolTest, ParallelTransform) {
    auto pool = foundation::thread::ThreadPoolFactory::create_fixed(4);
    
    std::vector<int> input = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> output(input.size());
    
    // 并行计算平方
    foundation::thread::parallel_transform(pool,
        input.begin(), input.end(),
        output.begin(),
        [](int x) { return x * x; }
    );
    
    // 验证结果
    for (size_t i = 0; i < input.size(); ++i) {
        EXPECT_EQ(output[i], input[i] * input[i]);
    }
    
    pool->shutdown(true);
}

TEST(ThreadPoolTest, ParallelReduce) {
    auto pool = foundation::thread::ThreadPoolFactory::create_fixed(4);
    
    std::vector<int> numbers(100);
    for (int i = 0; i < 100; ++i) {
        numbers[i] = i + 1;
    }
    
    // 并行求和
    int sum = foundation::thread::parallel_reduce(pool,
        numbers.begin(), numbers.end(),
        0,
        std::plus<int>()
    );
    
    // 验证结果（前100个自然数的和）
    int expected = 100 * 101 / 2;
    EXPECT_EQ(sum, expected);
    
    pool->shutdown(true);
}

TEST(ThreadPoolTest, DifferentPoolTypes) {
    // 测试不同类型的线程池
    
    // 1. 单线程池
    auto single = foundation::thread::ThreadPoolFactory::create_single_threaded();
    EXPECT_NE(single, nullptr);
    
    // 2. CPU感知池
    auto cpu_aware = foundation::thread::ThreadPoolFactory::create_cpu_aware();
    EXPECT_NE(cpu_aware, nullptr);
    
    // 3. IO密集型池
    auto io_intensive = foundation::thread::ThreadPoolFactory::create_io_intensive();
    EXPECT_NE(io_intensive, nullptr);
    
    // 测试单线程池的顺序执行
    std::vector<int> execution_order;
    std::mutex mutex;
    
    for (int i = 0; i < 5; ++i) {
        foundation::thread::async(single, [i, &execution_order, &mutex]() {
            std::lock_guard<std::mutex> lock(mutex);
            execution_order.push_back(i);
        });
    }
    
    single->shutdown(true);
    
    // 单线程池应该按提交顺序执行
    for (size_t i = 0; i < execution_order.size(); ++i) {
        EXPECT_EQ(execution_order[i], static_cast<int>(i));
    }
    
    // 清理其他线程池
    cpu_aware->shutdown(true);
    io_intensive->shutdown(true);
}

TEST(ThreadPoolTest, ExceptionSafety) {
    auto pool = foundation::thread::ThreadPoolFactory::create_fixed(2);
    
    // 测试异常不会崩溃线程池
    auto future = foundation::thread::async(pool, []() -> int {
        throw std::runtime_error("Test exception");
        return 0;
    });
    
    EXPECT_THROW(future.get(), std::runtime_error);
    
    // 线程池应该还能正常工作
    auto future2 = foundation::thread::async(pool, []() -> int {
        return 42;
    });
    
    EXPECT_EQ(future2.get(), 42);
    
    pool->shutdown(true);
}

TEST(ThreadPoolTest, ThreadPoolFactory) {
    // 测试线程池管理器
    auto default_pool = foundation::thread::ThreadPoolFactory::create_cpu_aware();
    EXPECT_NE(default_pool, nullptr);
    
    // 测试默认线程池能工作
    auto future = foundation::thread::async(default_pool, []() -> std::string {
        return "Hello from default pool";
    });
    
    EXPECT_EQ(future.get(), "Hello from default pool");
    
    // 注意：不要关闭默认线程池，它可能被其他测试使用
}

