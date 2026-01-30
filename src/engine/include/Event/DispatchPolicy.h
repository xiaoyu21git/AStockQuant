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
class DispatchStrategy {
public:
    // 改为组合模式，而不是中间层
    explicit DispatchStrategy(std::shared_ptr<DispatchPolicy> policy = nullptr)
        : policy_(policy ? std::move(policy) : create_default_policy())
        , last_dispatch_(std::chrono::steady_clock::now()) {}
    
    // 提供到 DispatchPolicy 的转换
    std::shared_ptr<DispatchPolicy> get_policy() const { return policy_; }
    
    // 保持原有接口（为了兼容性）
    bool should_dispatch(size_t queue_size) const {
        return policy_->should_dispatch(queue_size, last_dispatch_);
    }
    
    void update_last_dispatch() {
        last_dispatch_ = std::chrono::steady_clock::now();
    }
    static std::shared_ptr<DispatchPolicy> create_default_policy() {
        return std::make_shared<HybridPolicy>(100, std::chrono::milliseconds(50));
    }
private:
    std::shared_ptr<DispatchPolicy> policy_;
    std::chrono::steady_clock::time_point last_dispatch_;
};
}