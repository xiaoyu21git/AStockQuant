// RiskManager.h — 风控管理器 (纯 C++, 零外部依赖)
// 纯函数: 接收订单+账户快照 → 返回通过/不通过.
// 不调 AccountEngine, 不调 GmSessionEngine, 不查行情.
#pragma once

#include "../../../engine/include/GmSessionEngine.h"  // engine::AccountInfo, engine::Position, engine::OrderRequest
#include "RiskEvaluator.h"

#include <vector>

namespace domain::strategy {

class RiskManager {
public:
    static RiskManager& instance();

    /// @brief 手动下单风控 — 调用方负责提供账户快照+持仓+参考价
    RiskResult checkManualOrder(const engine::OrderRequest& req,
                                const engine::AccountInfo& account,
                                const std::vector<engine::Position>& positions,
                                double currentPrice);

    /// @brief 策略信号风控 — 调用方负责提供账户快照+持仓+参考价
    RiskResult checkAutoSignal(const engine::OrderRequest& req,
                               const engine::AccountInfo& account,
                               const std::vector<engine::Position>& positions,
                               double currentPrice,
                               double signalStrength);

    void setRiskConfig(const RiskConfig& config);
    const RiskConfig& riskConfig() const;

    /// @brief 纯函数: 检查是否涨跌停 (供调用方决定是否跳过风控)
    static bool isPriceAtLimit(double currentPrice, double preClose, bool isBuy);

private:
    RiskManager() = default;
    RiskConfig m_config;
};

} // namespace domain::strategy
