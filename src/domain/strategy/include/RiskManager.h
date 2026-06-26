// RiskManager.h — 风控管理器（domain/strategy, 纯 C++，零 Qt）
// 无状态，按 accountId 查询 AccountEngine 后判断。
// 调用方负责把 RiskResult 投递到 EventBus "trading.risk.rejected"
#pragma once

#include "../../../engine/include/GmSessionEngine.h"
#include "RiskEvaluator.h"

namespace domain::strategy {

class RiskManager {
public:
    static RiskManager& instance();

    /// @brief 手动下单风控 — TradeExecutionBridge 显式调用
    RiskResult checkManualOrder(const std::string& accountId,
                                const engine::OrderRequest& req);

    /// @brief 策略信号风控 — StrategyEngine 发单前调用
    RiskResult checkAutoSignal(const std::string& accountId,
                               const engine::OrderRequest& req,
                               double signalStrength);

    void setRiskConfig(const RiskConfig& config);
    const RiskConfig& riskConfig() const;

private:
    RiskManager() = default;
    RiskConfig m_config;

    bool priceAtLimit(const std::string& symbol, double price, bool isBuy) const;
};

} // namespace domain::strategy
