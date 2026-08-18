// test_threadpool_join.cpp
// ThreadPoolExecutor 停止链回归测试 — terminated_ 三路径统一置位 + shutdown 幂等 join + 析构必 join
// 用法: 直接运行, 失败计数非零时返回非零退出码

#include "foundation/thread/ThreadPoolExecutor.h"

#include <atomic>
#include <chrono>
#include <cstdio>

int main() {
    int failures = 0;
    const auto expect = [&failures](bool cond, const char* msg) {
        std::printf("%s: %s\n", cond ? "  PASS" : "  FAIL", msg);
        if (!cond) ++failures;
    };

    // ── 1. shutdown(true): 诚实等待任务完成并 join (此前 awaitTermination 必然假超时) ──
    {
        foundation::thread::ThreadPoolExecutor pool(2);
        std::atomic<int> done{0};
        pool.post([&done] { done.store(1, std::memory_order_release); });

        pool.shutdown(true);
        expect(done.load(std::memory_order_acquire) == 1, "shutdown(true) 等待已提交任务完成");
        expect(pool.isTerminated(), "shutdown(true) 后 isTerminated() == true");

        // 二次 shutdown 幂等 (此前 if(shutdown_) 早退 → 析构永不 join)
        pool.shutdown(true);
        expect(pool.isTerminated(), "二次 shutdown(true) 幂等");
    }
    // 作用域正常退出 = 析构 join 不挂起 (Worker::~Worker join 发生在 mutex 存活期)

    // ── 2. shutdown(false): worker 自行退出置 terminated_, awaitTermination 不再假超时 ──
    {
        foundation::thread::ThreadPoolExecutor pool(2);
        pool.shutdown(false);
        expect(pool.isShutdown(), "shutdown(false) 后 isShutdown() == true");
        expect(pool.awaitTermination(std::chrono::seconds(3)),
               "shutdown(false) 后 awaitTermination 在超时内返回 true");
        expect(pool.isTerminated(), "worker 退出后 isTerminated() == true");

        // 组合路径: shutdown(false) + shutdown(true) 幂等 join
        pool.shutdown(true);
        expect(pool.isTerminated(), "shutdown(false)+shutdown(true) 组合后 isTerminated() == true");
    }

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
