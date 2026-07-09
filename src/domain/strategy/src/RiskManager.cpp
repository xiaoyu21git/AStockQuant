// RiskManager.cpp — 风控实现 (被动+主动巡检)
#include "../include/RiskManager.h"
#include "../../market/include/MarketDataService.h"
#include "../../market/include/LiveData.h"
#include "../../../engine/include/AccountEngine.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
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
bool RiskManager::isPriceAtLimit(double currentPrice, double preClose, bool isBuy) {
    if (preClose <= 0) return false;
    // A股涨跌停 10%, 科创板 20%, 北交所 30% — 由上层根据 symbol 判断并传入正确的 limitPct
    // 这里只做最基础的 10% 判断, 调用方可根据品种调整
    constexpr double kDefaultLimitPct = 9.9; // 留 0.1% 容差
    double changePct = (currentPrice - preClose) / preClose * 100.0;
    if (isBuy)  return changePct >= kDefaultLimitPct;
    else        return changePct <= -kDefaultLimitPct;
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

std::vector<engine::OrderRequest> RiskManager::patrolPositions(domain::trading::OrderBuilder& builder) {
    std::vector<engine::OrderRequest> orders;
    if (m_config.stopLossPercent <= 0 && m_config.takeProfitPercent <= 0)
        return orders;

    auto& accEng = engine::AccountEngine::instance();
    auto positions = accEng.positions();
    if (positions.empty()) return orders;

    static int patrolRound = 0;
    if (++patrolRound == 1) {
        INTERNAL_INFO_STREAM << "[RiskManager] 巡检启动, 持仓数=" << positions.size();
        for (const auto& pos : positions) {
            INTERNAL_INFO_STREAM << "[RiskManager]   持仓: " << pos.symbol
                                 << " qty=" << pos.availableQty << " cost=" << pos.costPrice
                                 << " last=" << pos.lastPrice;
        }
    }
    static int skipLogCount = 0;
    for (const auto& pos : positions) {
        if (pos.availableQty <= 0 || pos.costPrice <= 0) {
            if (++skipLogCount <= 5)
                INTERNAL_INFO_STREAM << "[RiskManager] 跳过持仓(无成本): " << pos.symbol
                                     << " qty=" << pos.availableQty << " cost=" << pos.costPrice;
            continue;
        }

        auto& d = domain::market::MarketDataService::instance().liveData(pos.symbol);
        if (!d.valid()) {
            if (skipLogCount <= 5)
                INTERNAL_INFO_STREAM << "[RiskManager] 跳过持仓(无行情): " << pos.symbol;
            continue;
        }
        double price = d.dailyBar().close();
        if (price <= 0) continue;

        double pnlPct = (price - pos.costPrice) / pos.costPrice * 100.0;

        // 止损
        if (m_config.stopLossPercent > 0 && pnlPct < 0
            && std::abs(pnlPct) >= m_config.stopLossPercent) {
            INTERNAL_WARN_STREAM << "[RiskManager] 止损触发: " << pos.symbol
                                 << " 成本=" << pos.costPrice << " 现价=" << price
                                 << " 浮亏=" << static_cast<int>(std::abs(pnlPct)) << "%";
            orders.push_back(builder.buildStopOrder(pos.symbol, price, pos.availableQty));
            continue;
        }

        // 止盈
        if (m_config.takeProfitPercent > 0 && pnlPct > 0
            && pnlPct >= m_config.takeProfitPercent) {
            INTERNAL_INFO_STREAM << "[RiskManager] 止盈触发: " << pos.symbol
                                 << " 成本=" << pos.costPrice << " 现价=" << price
                                 << " 浮盈=" << static_cast<int>(pnlPct) << "%";
            orders.push_back(builder.buildStopOrder(pos.symbol, price, pos.availableQty));
        }
    }

    return orders;
}

} // namespace domain::strategy
