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

    /// @brief 为所有引擎注册篮子拦截器（半自动模式下使用, v0.16.0）
    void setBasketInterceptor(IBasketInterceptor* interceptor);

    /// @brief 为所有引擎设置执行模式（v0.16.0）
    void setExecutionMode(EngineExecutionMode mode);

    [[nodiscard]] std::size_t count() const;
    [[nodiscard]] bool empty() const;

    // ── 回测产物快照表 (回测/实盘实例分离: 引擎不进注册表, 只发布不可变结果) ──

    /// @brief 发布回测统计快照 (同 id 覆盖; 仅供回测桥在 worker 线程回测成功后调用)
    /// clearBacktestSnapshots 之后到达的发布被丢弃并打 WARN (防御调用顺序被改动)
    void publishBacktestSnapshot(const std::string& strategyId, BacktestStatsSnapshot snapshot);

    /// @brief 取回测快照 (shared_ptr 持有, 关闭清理时已持有者安全; 无快照返回 nullptr)
    [[nodiscard]] std::shared_ptr<const BacktestStatsSnapshot> getBacktestSnapshot(const std::string& id) const;

    /// @brief 移除指定策略的回测快照
    void removeBacktestSnapshot(const std::string& id);

    /// @brief 清空全部回测快照 (应用关闭时调用; 必须在 QML 引擎销毁、回测 worker 已 join 之后)
    void clearBacktestSnapshots();

    /// @brief 回测快照数量
    [[nodiscard]] std::size_t backtestSnapshotCount() const;

    // ── 桥接层注入依赖（解耦 domain ↔ bridge）──

    /// @brief 设置因子实例管理器（由桥接层在初始化时注入一次）
    void setFactorInstanceManager(factor::FactorInstanceManager* mgr) {
        m_factorInstanceManager = mgr;
    }

    /// @brief 设置默认订单监听器（由桥接层在初始化时注入一次，所有引擎共享）
    void setDefaultOrderListener(IOrderListener* listener) {
        m_defaultOrderListener = listener;
    }

    /// @brief 设置默认篮子拦截器（由桥接层在初始化时注入一次，v0.16.0 SemiAuto）
    void setDefaultBasketInterceptor(IBasketInterceptor* interceptor) {
        m_defaultBasketInterceptor = interceptor;
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

    /// @brief 锁内取出并移除引擎 (调用方必须已持有 m_mutex)
    [[nodiscard]] std::shared_ptr<StrategyEngine> extractEngineLocked(const std::string& id);

    /// @brief 锁外停止引擎统一序列: 停实盘循环 (阻塞 join) → 停服务 (所有停止路径共用,
    /// 必须在锁外执行 — worker 回调可能再取 m_mutex, 持锁 join 有死锁风险)
    /// 引擎以 shared_ptr 持有: 券商回调经 weak_ptr 持锁的在途执行会推迟销毁至回调结束
    void stopEngineOutsideLock(std::shared_ptr<StrategyEngine>& engine);

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<StrategyEngine>> m_engines;
    // 回测产物快照表 (与实盘 m_engines 完全隔离; shared_ptr<const> 供统计页无锁读取)
    std::unordered_map<std::string, std::shared_ptr<const BacktestStatsSnapshot>> m_backtestSnapshots;
    bool m_snapshotsCleared{false};  // 关闭清理标志 (与 m_backtestSnapshots 同锁保护)
    factor::FactorInstanceManager* m_factorInstanceManager{nullptr};
    IOrderListener* m_defaultOrderListener{nullptr};
    IBasketInterceptor* m_defaultBasketInterceptor{nullptr};  // v0.16.0 SemiAuto
    std::string m_liveDataPath;  // 实盘数据持久化目录
};

} // namespace domain::strategy
