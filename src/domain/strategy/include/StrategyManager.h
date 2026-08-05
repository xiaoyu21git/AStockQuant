#pragma once

#include "IStrategyService.h"
#include "IFactorSvc.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace factor {
class FactorInstanceManager;
}

namespace domain::strategy {

class StrategyManager final {
public:
    StrategyManager(const StrategyManager&) = delete;
    StrategyManager& operator=(const StrategyManager&) = delete;

    static StrategyManager& instance();

    [[nodiscard]] StrategyEngine* createEngine(const std::string& strategyId,
                                               std::unique_ptr<IRuntimeFactorService> factorSvc = nullptr);
    [[nodiscard]] StrategyEngine* get(const std::string& id) const;
    void remove(const std::string& id);

    void startAll();
    void pauseAll();
    void resumeAll();
    void stopAll();

    /// @brief 回测（同步）路径 — 串行处理所有引擎
    [[nodiscard]] std::vector<OrderRequest> stepAll(const MarketDataPoint& mdp);

    /// @brief 为所有引擎注册订单回调监听器（实盘模式下使用）
    void setOrderListener(IOrderListener* listener);

    [[nodiscard]] std::size_t count() const;
    [[nodiscard]] bool empty() const;

    // ── 桥接层注入依赖（解耦 domain ↔ bridge）──

    /// @brief 设置因子实例管理器（由桥接层在初始化时注入一次）
    void setFactorInstanceManager(factor::FactorInstanceManager* mgr) {
        m_factorInstanceManager = mgr;
    }

    /// @brief 设置默认订单监听器（由桥接层在初始化时注入一次，所有引擎共享）
    void setDefaultOrderListener(IOrderListener* listener) {
        m_defaultOrderListener = listener;
    }

    /// @brief 设置实盘数据持久化目录（由桥接层在初始化时注入一次）
    void setLiveDataPath(const std::string& path) { m_liveDataPath = path; }

    // ── 统一生命周期管理 ──

    /// @brief 完整启动一个策略：创建引擎 → 加载历史数据 → 启动实盘循环
    /// 同步执行（耗时操作，由调用方负责放入工作线程）。
    /// @param strategyId 策略 UUID
    /// @throws std::runtime_error 任何阶段失败时抛出
    void startStrategy(const std::string& strategyId);

    /// @brief 停止一个策略：停实盘循环 → 停服务 → 从管理器移除
    void stopStrategy(const std::string& strategyId);

    /// @brief 获取或创建引擎（内部处理因子服务装配）
    [[nodiscard]] StrategyEngine* getOrCreateEngine(const std::string& strategyId);

private:
    StrategyManager() = default;

    /// @brief 根据策略配置创建因子服务（需 m_factorInstanceManager 已注入）
    [[nodiscard]] std::unique_ptr<IRuntimeFactorService> createFactorService();

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::unique_ptr<StrategyEngine>> m_engines;
    factor::FactorInstanceManager* m_factorInstanceManager{nullptr};
    IOrderListener* m_defaultOrderListener{nullptr};
    std::string m_liveDataPath;  // 实盘数据持久化目录
};

} // namespace domain::strategy
