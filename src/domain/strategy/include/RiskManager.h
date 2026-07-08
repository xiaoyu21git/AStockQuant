// RiskManager.h — 风控管理器
// 被动: 订单到达时检查 checkAutoSignal/checkManualOrder
// 主动: 定时巡检持仓，触发止损止盈时委托 OrderBuilder 生成退出订单
#pragma once

#include "../../../engine/include/GmSessionEngine.h"
#include "RiskEvaluator.h"
#include "../../trading/include/OrderBuilder.h"

#include <vector>

namespace domain::strategy {

class RiskManager {
public:
    static RiskManager& instance();

    // ── 被动 ──

    RiskResult checkManualOrder(const engine::OrderRequest& req,
                                const engine::AccountInfo& account,
                                const std::vector<engine::Position>& positions,
                                double currentPrice);

    RiskResult checkAutoSignal(const engine::OrderRequest& req,
                               const engine::AccountInfo& account,
                               const std::vector<engine::Position>& positions,
                               double currentPrice,
                               double signalStrength);

    // ── 主动 ──

    /// @brief 巡检所有持仓，触发止损止盈时委托 OrderBuilder 生成退出订单
    /// @param builder 调用方传入，不持有，避免悬空指针
    /// @return 需要执行的退出订单列表
    std::vector<engine::OrderRequest> patrolPositions(domain::trading::OrderBuilder& builder);

    void setRiskConfig(const RiskConfig& config);
    const RiskConfig& riskConfig() const;

    static bool isPriceAtLimit(double currentPrice, double preClose, bool isBuy);

private:
    RiskManager() : m_config(RiskConfig::defaults()) {}
    RiskConfig m_config;
};

} // namespace domain::strategy
