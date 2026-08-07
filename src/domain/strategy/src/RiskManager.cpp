// RiskManager.cpp — 风控实现 (被动+主动巡检)
#include "../include/RiskManager.h"
#include "../../market/include/MarketDataService.h"
#include "../../market/include/LiveData.h"
#include "../../../engine/include/AccountEngine.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>

namespace domain::strategy {

RiskManager& RiskManager::instance() {
    static RiskManager mgr;
    return mgr;
}

void RiskManager::setRiskConfig(const RiskConfig& config) { m_config = config; }
const RiskConfig& RiskManager::riskConfig() const { return m_config; }

// ── 纯函数: 涨跌停检查 ──
bool RiskManager::isPriceAtLimit(double currentPrice, double preClose, bool isBuy,
                                  const std::string& symbol) {
    if (preClose <= 0) return false;
    double limitPct = 9.9;  // 主板默认 ±10%, 留 0.1% 容差
    if (!symbol.empty()) {
        // 科创板 688xxx/689xxx: ±20%
        if (symbol.size() >= 3 && (symbol.substr(0, 3) == "688" || symbol.substr(0, 3) == "689"))
            limitPct = 19.9;
        // 创业板 300xxx/301xxx: ±20%
        else if (symbol.size() >= 3 && (symbol.substr(0, 3) == "300" || symbol.substr(0, 3) == "301"))
            limitPct = 19.9;
        // 北交所 8xxxxx: ±30%
        else if (!symbol.empty() && symbol[0] == '8')
            limitPct = 29.9;
    }
    double changePct = (currentPrice - preClose) / preClose * 100.0;
    if (isBuy)  return changePct >= limitPct;
    else        return changePct <= -limitPct;
}

// ── 公共: 找持仓 ──
namespace {
    const engine::Position* findPosition(const std::string& symbol,
                                         const std::vector<engine::Position>& positions) {
        for (auto& p : positions) {
            if (p.symbol == symbol) return &p;
        }
        return nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════════
// 手动下单风控
// ═══════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════
// 主动巡检 — 止损止盈退出（委托 OrderBuilder 构建订单）
// ═══════════════════════════════════════════════════════════════════

std::vector<engine::OrderRequest> RiskManager::patrolPositions(
    domain::trading::OrderBuilder& builder,
    const std::string& strategyId,
    const std::string& accountId) {
    std::vector<engine::OrderRequest> orders;
    if (m_config.stopLossPercent <= 0 && m_config.takeProfitPercent <= 0)
        return orders;

    auto& accEng = engine::AccountEngine::instance();
    auto positions = accEng.positions();
    if (positions.empty()) return orders;

    static std::atomic<int> patrolRound{0};
    if (patrolRound.fetch_add(1, std::memory_order_relaxed) == 0) {
        INTERNAL_INFO_STREAM << "[RiskManager] 巡检启动, 持仓数=" << positions.size();
    }
    for (const auto& pos : positions) {
        if (pos.availableQty <= 0 || pos.costPrice <= 0) continue;

        auto& d = domain::market::MarketDataService::instance().liveData(pos.symbol);
        if (!d.valid()) continue;
        double price = d.dailyBar().close();
        if (price <= 0) continue;

        double pnlPct = (price - pos.costPrice) / pos.costPrice * 100.0;

        // 止损
        if (m_config.stopLossPercent > 0 && pnlPct < 0
            && std::abs(pnlPct) >= m_config.stopLossPercent) {
            INTERNAL_WARN_STREAM << "[RiskManager] 止损触发: " << pos.symbol
                                 << " 成本=" << pos.costPrice << " 现价=" << price
                                 << " 浮亏=" << static_cast<int>(std::abs(pnlPct)) << "%";
            orders.push_back(builder.buildStopOrder(pos.symbol, price, pos.availableQty, strategyId, accountId));
            continue;
        }

        // 止盈
        if (m_config.takeProfitPercent > 0 && pnlPct > 0
            && pnlPct >= m_config.takeProfitPercent) {
            INTERNAL_INFO_STREAM << "[RiskManager] 止盈触发: " << pos.symbol
                                 << " 成本=" << pos.costPrice << " 现价=" << price
                                 << " 浮盈=" << static_cast<int>(pnlPct) << "%";
            orders.push_back(builder.buildStopOrder(pos.symbol, price, pos.availableQty, strategyId, accountId));
        }
    }

    return orders;
}

} // namespace domain::strategy
