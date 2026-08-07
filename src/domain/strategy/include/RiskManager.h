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

    // ── 主动 ──

    /// @brief 巡检所有持仓，触发止损止盈时委托 OrderBuilder 生成退出订单
    /// @param builder 调用方传入，不持有，避免悬空指针
    /// @param strategyId 策略ID（全局巡检可传空字符串）
    /// @param accountId 账户ID
    /// @return 需要执行的退出订单列表
    std::vector<engine::OrderRequest> patrolPositions(
        domain::trading::OrderBuilder& builder,
        const std::string& strategyId,
        const std::string& accountId);

    void setRiskConfig(const RiskConfig& config);
    const RiskConfig& riskConfig() const;

    static bool isPriceAtLimit(double currentPrice, double preClose, bool isBuy,
                               const std::string& symbol = "");

private:
    RiskManager() : m_config(RiskConfig::defaults()) {}
    RiskConfig m_config;
};

} // namespace domain::strategy
