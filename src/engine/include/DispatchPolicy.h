#pragma once
#include <chrono>
#include <memory>
#include <atomic>

namespace engine {
enum class DispatchMode {
    Immediate,   // 立即分发
    Batch,       // 批量分发
    TimeBased,   // 定时分发
    Hybrid       // 混合模式
};
enum class ExecutionMode {
        Sync,  // 同步模式
        Async  // 异步模式
    };
// 抽象分发策略
class DispatchPolicy {
public:
    virtual ~DispatchPolicy() = default;

    virtual bool should_dispatch(size_t queue_size,
                                 std::chrono::steady_clock::time_point last_dispatch) const = 0;

    virtual size_t batch_size() const = 0;
    virtual std::chrono::milliseconds interval() const = 0;
    virtual DispatchMode mode() const = 0;  // 新增：获取策略模式
};

// 立即分发策略
class ImmediatePolicy : public DispatchPolicy {
public:
    bool should_dispatch(size_t, std::chrono::steady_clock::time_point) const override { return true; }
    size_t batch_size() const override { return 1; }
    std::chrono::milliseconds interval() const override { return std::chrono::milliseconds(0); }
    DispatchMode mode() const override { return DispatchMode::Immediate; }
};

// 批量策略
class BatchPolicy : public DispatchPolicy {
private:
    size_t batch_;
public:
    explicit BatchPolicy(size_t batch_size) : batch_(batch_size) {}
    bool should_dispatch(size_t queue_size, std::chrono::steady_clock::time_point) const override {
        return queue_size >= batch_;
    }
    size_t batch_size() const override { return batch_; }
    std::chrono::milliseconds interval() const override { return std::chrono::milliseconds(0); }
    DispatchMode mode() const override { return DispatchMode::Batch; }
};

// 时间间隔策略
class TimePolicy : public DispatchPolicy {
private:
    std::chrono::milliseconds interval_;
public:
    explicit TimePolicy(std::chrono::milliseconds interval) : interval_(interval) {}
    bool should_dispatch(size_t, std::chrono::steady_clock::time_point last_dispatch) const override {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_dispatch);
        return elapsed >= interval_;
    }
    size_t batch_size() const override { return 0; }
    std::chrono::milliseconds interval() const override { return interval_; }
    DispatchMode mode() const override { return DispatchMode::TimeBased; }
};
// 混合策略：批量 + 时间
class HybridPolicy : public DispatchPolicy {
private:
    size_t batch_;
    std::chrono::milliseconds interval_;
    
public:
    HybridPolicy(size_t batch_size, std::chrono::milliseconds interval)
        : batch_(batch_size), interval_(interval) {}
    
    bool should_dispatch(size_t queue_size, 
                        std::chrono::steady_clock::time_point last_dispatch) const override {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_dispatch);
        return queue_size >= batch_ || elapsed >= interval_;
    }
    
    size_t batch_size() const override { return batch_; }
    std::chrono::milliseconds interval() const override { return interval_; }
    DispatchMode mode() const override { return DispatchMode::Hybrid; }
};
// DispatchStrategy 组合策略
class DispatchStrategy {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    DispatchStrategy()
        : last_dispatch_(Clock::now())
    {}

    explicit DispatchStrategy(std::shared_ptr<DispatchPolicy> policy)
        : policy_(std::move(policy))
        , last_dispatch_(Clock::now())
    {}

    // ================== policy ==================

    void set_policy(std::shared_ptr<DispatchPolicy> policy) {
        policy_ = std::move(policy);
        reset();
    }

    std::shared_ptr<DispatchPolicy> get_policy() const {
        return policy_;
    }

    // ================== decision ==================

    bool should_dispatch(std::size_t queue_size,
                         TimePoint now = Clock::now()) const
    {
        if (!policy_) return false;
        return policy_->should_dispatch(queue_size, last_dispatch_.load());
    }

    // ================== state ==================

    void update_last_dispatch(TimePoint now = Clock::now()) {
        last_dispatch_.store(now, std::memory_order_relaxed);
    }

    void reset() {
        update_last_dispatch();
    }

private:
    std::shared_ptr<DispatchPolicy> policy_;
    std::atomic<TimePoint> last_dispatch_;
};

} // namespace engine
