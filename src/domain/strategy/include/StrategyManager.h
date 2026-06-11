#pragma once

#include "IStrategyService.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain::strategy {

class StrategyManager final {
public:
    StrategyManager(const StrategyManager&) = delete;
    StrategyManager& operator=(const StrategyManager&) = delete;

    static StrategyManager& instance();

    [[nodiscard]] StrategyEngine* createEngine(const std::string& strategyId);
    [[nodiscard]] StrategyEngine* get(const std::string& id) const;
    void remove(const std::string& id);

    void startAll();
    void pauseAll();
    void resumeAll();
    void stopAll();

    /// @brief 回测（同步）路径 — 串行处理所有引擎
    [[nodiscard]] std::vector<OrderRequest> stepAll(const MarketDataPoint& mdp);

    /// @brief 实盘异步路径 — 向所有运行中的引擎推送行情到各自后台线程队列
    void pushMarketData(const MarketDataPoint& mdp);

    /// @brief 为所有引擎注册订单回调监听器（实盘模式下使用）
    void setOrderListener(IOrderListener* listener);

    [[nodiscard]] std::size_t count() const;
    [[nodiscard]] bool empty() const;

private:
    StrategyManager() = default;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::unique_ptr<StrategyEngine>> m_engines;
};

} // namespace domain::strategy
