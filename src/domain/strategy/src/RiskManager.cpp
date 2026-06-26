// RiskManager.cpp — 风控管理器实现
#include "../include/RiskManager.h"
#include "../../../engine/include/AccountEngine.h"

namespace domain::strategy {

// ═══════════════════════════════════════════════════════════════════
// 单例
// ═══════════════════════════════════════════════════════════════════

RiskManager& RiskManager::instance() {
    static RiskManager mgr;
    return mgr;
}

void RiskManager::setRiskConfig(const RiskConfig& config) { m_config = config; }
const RiskConfig& RiskManager::riskConfig() const { return m_config; }

// ═══════════════════════════════════════════════════════════════════
// 涨跌停
// ═══════════════════════════════════════════════════════════════════

bool RiskManager::priceAtLimit(const std::string& symbol, double price, bool isBuy) const {
    auto q = engine::GmSessionEngine::instance().fetchQuote(symbol);
    if (!q || !q->valid) return false;
    if (isBuy)  return q->isLimitUp();
    else        return q->isLimitDown();
}

// ═══════════════════════════════════════════════════════════════════
// 手动下单风控
// ═══════════════════════════════════════════════════════════════════

RiskResult RiskManager::checkManualOrder(const std::string& accountId,
                                         const engine::OrderRequest& req) {
    auto& accEng = engine::AccountEngine::instance();
    auto account = accEng.account();
    auto positions = accEng.positions();

    // 找当前标的持仓
    const engine::Position* existingPos = nullptr;
    for (auto& p : positions) {
        if (p.symbol == req.symbol) { existingPos = &p; break; }
    }

    const double orderAmount = req.price * static_cast<double>(req.quantity);
    const bool isBuy = (req.side == engine::OrderRequest::Buy);

    // ── 1. 资金检查 ──
    if (isBuy && account.availableCash < orderAmount) {
        return RiskResult::rejected(RiskRejectCode::OrderSizeExceeded, 1.0,
            "可用资金不足: 需要 " + std::to_string(orderAmount)
            + " 可用 " + std::to_string(account.availableCash));
    }

    // ── 2. 卖空超量 ──
    if (!isBuy && (!existingPos || existingPos->availableQty < req.quantity)) {
        return RiskResult::rejected(RiskRejectCode::SellQuantityExceedsHolding, 1.0,
            "可卖量不足: 需要 " + std::to_string(req.quantity)
            + " 可卖 " + std::to_string(existingPos ? existingPos->availableQty : 0));
    }

    // ── 3. 涨跌停 ──
    if (priceAtLimit(req.symbol, req.price, isBuy)) {
        return RiskResult::rejected(RiskRejectCode::PriceInvalid, 1.0,
            isBuy ? "当前价在涨停板, 无法买入" : "当前价在跌停板, 无法卖出");
    }

    // ── 4. 买入集中度 ──
    if (isBuy && account.totalAsset > 0 && m_config.maxPositionPercent > 0) {
        double existingValue = existingPos ? existingPos->marketValue : 0;
        double combined = existingValue + orderAmount;
        double pct = (combined / account.totalAsset) * 100.0;
        if (pct > m_config.maxPositionPercent) {
            return RiskResult::rejected(RiskRejectCode::PositionConcentrationExceeded, 0.88,
                "单票集中度超限: " + std::to_string(static_cast<int>(pct)) + "% > "
                + std::to_string(static_cast<int>(m_config.maxPositionPercent)) + "%");
        }
    }

    // ── 5. 总敞口 ──
    if (isBuy && account.totalAsset > 0 && m_config.maxTotalExposurePercent > 0) {
        double newExposure = account.marketValue + orderAmount;
        double pct = (newExposure / account.totalAsset) * 100.0;
        if (pct > m_config.maxTotalExposurePercent) {
            return RiskResult::rejected(RiskRejectCode::BreakerLevel1, 0.95,
                "总敞口超限: " + std::to_string(static_cast<int>(pct)) + "% > "
                + std::to_string(static_cast<int>(m_config.maxTotalExposurePercent)) + "%");
        }
    }

    return RiskResult::accept();
}

// ═══════════════════════════════════════════════════════════════════
// 策略信号风控
// ═══════════════════════════════════════════════════════════════════

RiskResult RiskManager::checkAutoSignal(const std::string& accountId,
                                        const engine::OrderRequest& req,
                                        double signalStrength) {
    auto& accEng = engine::AccountEngine::instance();
    auto account = accEng.account();
    auto positions = accEng.positions();

    const engine::Position* existingPos = nullptr;
    for (auto& p : positions) {
        if (p.symbol == req.symbol) { existingPos = &p; break; }
    }

    const double orderAmount = req.price * static_cast<double>(req.quantity);
    const bool isBuy = (req.side == engine::OrderRequest::Buy);

    // ── 1. 信号强度 ──
    if (signalStrength < 0.1) {
        return RiskResult::rejected(RiskRejectCode::SignalStrengthTooWeak, 0.9,
            "信号强度不足: " + std::to_string(signalStrength));
    }

    // ── 2. 止损冲突 ──
    if (isBuy && existingPos && m_config.stopLossPercent > 0
        && existingPos->costPrice > 0 && existingPos->lastPrice > 0) {
        double pnlPct = (existingPos->lastPrice - existingPos->costPrice)
                      / existingPos->costPrice * 100.0;
        if (pnlPct < 0 && std::abs(pnlPct) >= m_config.stopLossPercent) {
            return RiskResult::rejected(RiskRejectCode::StopLossTriggered, 0.95,
                "止损线已触发: 浮亏 " + std::to_string(static_cast<int>(std::abs(pnlPct))) + "%");
        }
    }

    // ── 3. 止盈冲突 ──
    if (isBuy && existingPos && m_config.takeProfitPercent > 0
        && existingPos->costPrice > 0 && existingPos->lastPrice > 0) {
        double pnlPct = (existingPos->lastPrice - existingPos->costPrice)
                      / existingPos->costPrice * 100.0;
        if (pnlPct > 0 && pnlPct >= m_config.takeProfitPercent) {
            return RiskResult::rejected(RiskRejectCode::TakeProfitTriggered, 0.72,
                "止盈线已触发: 浮盈 " + std::to_string(static_cast<int>(pnlPct)) + "%");
        }
    }

    // ── 4. 买入集中度 ──
    if (isBuy && account.totalAsset > 0 && m_config.maxPositionPercent > 0) {
        double existingValue = existingPos ? existingPos->marketValue : 0;
        double combined = existingValue + orderAmount;
        double pct = (combined / account.totalAsset) * 100.0;
        if (pct > m_config.maxPositionPercent) {
            return RiskResult::rejected(RiskRejectCode::PositionConcentrationExceeded, 0.88,
                "单票集中度超限: " + std::to_string(static_cast<int>(pct)) + "%");
        }
    }

    // ── 5. 总敞口 ──
    if (isBuy && account.totalAsset > 0 && m_config.maxTotalExposurePercent > 0) {
        double newExposure = account.marketValue + orderAmount;
        double pct = (newExposure / account.totalAsset) * 100.0;
        if (pct > m_config.maxTotalExposurePercent) {
            return RiskResult::rejected(RiskRejectCode::BreakerLevel1, 0.95,
                "总敞口超限: " + std::to_string(static_cast<int>(pct)) + "%");
        }
    }

    return RiskResult::accept();
}

} // namespace domain::strategy
