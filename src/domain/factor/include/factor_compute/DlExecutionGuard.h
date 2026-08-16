#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// factor::compute::DlExecutionGuard — DL 推理独占槽 (因子级互斥)
//
// ONNX Runtime Session 并发 Run 存在不可靠风险 → 任一时刻至多一个 DL 因子
// compute 在算 (双轨制: 普通因子不经 Guard 直接计算, 不受 DL 拖累 —
// 管线按因子逐个调 compute, 天然因子级)。
// ══════════════════════════════════════════════════════════════════════════════

#include <condition_variable>
#include <mutex>

namespace factor::compute {

/// @brief 进程级 DL 推理互斥槽 (RAII 获取/释放, 公平等待)
class DlExecutionGuard {
public:
    DlExecutionGuard() { acquire(); }
    ~DlExecutionGuard() { release(); }

    DlExecutionGuard(const DlExecutionGuard&) = delete;
    DlExecutionGuard& operator=(const DlExecutionGuard&) = delete;

private:
    // 函数内静态 (Meyers singleton): 跨编译单元单实例 (C++11 起保证), 不引入 C++20
    static std::mutex& gateMutex()
    {
        static std::mutex m;
        return m;
    }

    static std::condition_variable& gateCv()
    {
        static std::condition_variable cv;
        return cv;
    }

    static bool& gateBusy()
    {
        static bool busy = false;
        return busy;
    }

    void acquire()
    {
        std::unique_lock<std::mutex> lock(gateMutex());
        gateCv().wait(lock, [] { return !gateBusy(); });
        gateBusy() = true;
    }

    void release()
    {
        {
            std::lock_guard<std::mutex> lock(gateMutex());
            gateBusy() = false;
        }
        gateCv().notify_one();
    }
};

} // namespace factor::compute
