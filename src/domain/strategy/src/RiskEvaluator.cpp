#include "../include/RiskEvaluator.h"
#include "../include/RiskManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace domain::strategy {

// ═════════════════════════════════════════════════════════════════════════
// RiskResult — 工厂方法
// ═════════════════════════════════════════════════════════════════════════
RiskResult RiskResult::accept(RiskRejectCode c, double s, std::string d) {
    return RiskResult(true, c, s, std::move(d));
}
RiskResult RiskResult::rejected(RiskRejectCode c, double s, std::string d) {
    return RiskResult(false, c, s, std::move(d));
}
RiskResult::RiskResult(bool a, RiskRejectCode c, double s, std::string d)
    : m_approved(a), m_code(c), m_riskScore(s), m_description(std::move(d)) {}

// ═════════════════════════════════════════════════════════════════════════
// RiskEvaluator — 主评估方法（6 步验证管道）
// ═════════════════════════════════════════════════════════════════════════
RiskResult RiskEvaluator::evaluateOrder(const RiskInput& input) {
    // ── 步骤 1：基础字段验证 ──
    // 策略 ID 和标的代码必须非空
    if (input.strategyId().empty() || input.symbol().empty()) {
        return RiskResult::rejected(
            RiskRejectCode::MissingRequiredFields, 1.0,
            "缺少必填字段: strategyId 或 symbol 为空");
    }

    // 价格有效性（现金还款/股份归还可能不需要价格，由调用方控制）
    if (input.price() < 0.0) {
        return RiskResult::rejected(
            RiskRejectCode::PriceInvalid, 1.0,
            "价格无效: 价格为负值");
    }

    // 策略必须已绑定
    if (!input.strategyBound()) {
        return RiskResult::rejected(
            RiskRejectCode::StrategyNotBound, 1.0,
            "策略未绑定到交易账户");
    }

    // 策略必须处于激活状态
    if (!input.strategyActive()) {
        return RiskResult::rejected(
            RiskRejectCode::StrategyNotActive, 1.0,
            "策略未激活");
    }

    // ── 大盘指数回撤保护（仅拦截增加敞口的买入，不拦减仓/卖出）──
    if (input.indexDrawdownPct() > 0.0 && input.indexDrawdownLimitPercent() > 0.0
        && input.isBuyOrder() && increasesExposure(OrderDirection::Buy, PositionEffect::Open)) {
        if (input.indexDrawdownPct() >= input.indexDrawdownLimitPercent()) {
            return RiskResult::rejected(
                RiskRejectCode::IndexDrawdownExceeded, 0.85,
                "大盘回撤保护: " + std::to_string(static_cast<int>(input.indexDrawdownPct()))
                + "% >= " + std::to_string(static_cast<int>(input.indexDrawdownLimitPercent())) + "%");
        }
    }

    // 信号强度检查（阈值 0.1 为最低可接受信号）
    if (input.signalStrength() < 0.1) {
        return RiskResult::rejected(
            RiskRejectCode::SignalStrengthTooWeak, 0.9,
            "信号强度过弱 (阈值: 0.1)");
    }

    // 自动策略必须提供数量
    if (input.isAutoStrategySignal() && input.quantity() <= 0 && input.cashAmount() <= 0.0) {
        return RiskResult::rejected(
            RiskRejectCode::AutoStrategyWithoutQuantity, 1.0,
            "自动策略信号缺少委托数量/金额");
    }

    // ── 步骤 2：三级熔断停止 ──
    if (input.level3TradingHaltActive()) {
        return RiskResult::rejected(
            RiskRejectCode::Level3TradingHaltActive, 1.0,
            "三级熔断生效中，暂停所有交易");
    }

    // ── 步骤 2.5：涨跌停检查（买卖通用，referencePrice 由 buildRiskInput 从 preClose 填充）──
    if (input.referencePrice() > 0.0) {
        bool atLimit = RiskManager::isPriceAtLimit(
            input.price(), input.referencePrice(), input.isBuyOrder());
        if (atLimit) {
            return RiskResult::rejected(
                RiskRejectCode::PriceInvalid, 1.0,
                input.isBuyOrder() ? "涨停板，无法买入" : "跌停板，无法卖出");
        }
    }

    // ── 步骤 3：买入/卖出侧定向检查 ──
    if (input.isBuyOrder()) {
        // 3a. 持仓快照必须就绪
        if (!input.positionSnapshotReady()) {
            return RiskResult::rejected(
                RiskRejectCode::PositionSnapshotNotReady, 0.96,
                "持仓快照未就绪，无法计算风险敞口");
        }

        // 3b. 交易时段检查
        if (!input.tradingSessionOpen()) {
            return RiskResult::rejected(
                RiskRejectCode::TradingSessionClosed, 0.98,
                "当前非交易时段");
        }

        // 3c. 止损检查：当有持仓且浮动亏损超过止损线
        if (input.stopLossPercent() > 0.0
            && input.symbolPositionReturnPercent() < 0.0
            && std::abs(input.symbolPositionReturnPercent()) >= input.stopLossPercent()) {
            return RiskResult::rejected(
                RiskRejectCode::StopLossTriggered, 0.95,
                "止损触发: 当前回撤已超过止损线");
        }

        // 3d. 止盈检查
        if (input.takeProfitPercent() > 0.0
            && input.symbolPositionReturnPercent() > 0.0
            && input.symbolPositionReturnPercent() >= input.takeProfitPercent()) {
            return RiskResult::rejected(
                RiskRejectCode::TakeProfitTriggered, 0.72,
                "止盈触发: 当前盈利已达到止盈线");
        }

        const double totalAsset = input.currentTotalAsset();
        const double proposedNotional = input.requestedNotional() > 0.0
            ? input.requestedNotional()
            : input.price() * static_cast<double>(std::max<int64_t>(1, input.quantity()));

        // 3e. 最大回撤检查（账户级别）
        if (input.maxDrawdownLimitPercent() > 0.0
            && input.currentDrawdownPercent() < 0.0
            && std::abs(input.currentDrawdownPercent()) >= input.maxDrawdownLimitPercent()) {
            return RiskResult::rejected(
                RiskRejectCode::MaxDrawdownExceeded, 1.0,
                "最大回撤超限");
        }

        // 3f. 三级浮动熔断：breaker3 > breaker2 > breaker1
        if (input.breakerLevel3Percent() > 0.0
            && input.currentDrawdownPercent() < 0.0
            && std::abs(input.currentDrawdownPercent()) >= input.breakerLevel3Percent()) {
            return RiskResult::rejected(
                RiskRejectCode::BreakerLevel3, 1.0,
                "三级熔断触发 (Level 3)");
        }
        if (input.breakerLevel1Percent() > 0.0
            && input.currentDrawdownPercent() < 0.0
            && std::abs(input.currentDrawdownPercent()) >= input.breakerLevel1Percent()) {
            return RiskResult::rejected(
                RiskRejectCode::BreakerLevel1, 0.95,
                "一级熔断触发 (Level 1)");
        }
        if (input.breakerLevel2Percent() > 0.0
            && input.currentDrawdownPercent() < 0.0
            && std::abs(input.currentDrawdownPercent()) >= input.breakerLevel2Percent()) {
            return RiskResult::rejected(
                RiskRejectCode::BreakerLevel2, 0.98,
                "二级熔断触发 (Level 2)");
        }

        // 3g. 持仓集中度检查
        if (totalAsset > 0.0 && input.maxPositionPercent() > 0.0) {
            const double existingExposure = input.symbolMarketValue();
            const double combinedExposure = existingExposure + proposedNotional;
            const double concentrationPct = (combinedExposure / totalAsset) * 100.0;
            if (concentrationPct > input.maxPositionPercent()) {
                return RiskResult::rejected(
                    RiskRejectCode::PositionConcentrationExceeded, 0.88,
                    "单标的持仓集中度超限");
            }
        }

        // 3h. 总敞口检查
        if (totalAsset > 0.0 && input.maxTotalExposurePercent() > 0.0) {
            const double newTotalExposure = input.currentMarketValue() + proposedNotional;
            const double newExposurePct = (newTotalExposure / totalAsset) * 100.0;
            const double clampedLimit = clampMaxTotalExposure(input.maxTotalExposurePercent());
            if (newExposurePct > clampedLimit) {
                return RiskResult::rejected(
                    RiskRejectCode::TotalExposureExceeded, 0.84,
                    "总敞口超限");
            }
        }
    } else {
        // ── 卖出侧检查 ──

        // 4a. 必须有可卖持仓
        if (input.closeableQuantity() <= 0) {
            return RiskResult::rejected(
                RiskRejectCode::NoSellablePosition, 1.0,
                "无可卖持仓");
        }

        // 4b. 卖出数量不能超过可卖数量
        if (input.quantity() > input.closeableQuantity()) {
            return RiskResult::rejected(
                RiskRejectCode::SellQuantityExceedsHolding, 1.0,
                "卖出数量超过可卖持仓");
        }
    }

    // ── 步骤 5：金额/滑点检查 ──
    double notional = input.requestedNotional() > 0.0
        ? input.requestedNotional()
        : input.price() * static_cast<double>(std::max<int64_t>(1, input.quantity()));

    // 5a. 订单金额上限
    if (input.orderSizeLimitWan() > 0.0
        && notional > input.orderSizeLimitWan() * 10000.0) {
        return RiskResult::rejected(
            RiskRejectCode::OrderSizeExceeded, 0.92,
            "单笔订单金额超限");
    }

    // 5b. 滑点容忍度
    if (input.referencePrice() > 0.0 && input.slippageLimitPercent() > 0.0) {
        const double slip = adverseSlippagePct(
            input.isBuyOrder() ? OrderDirection::Buy : OrderDirection::Sell,
            input.price(), input.referencePrice());
        if (slip > input.slippageLimitPercent()) {
            return RiskResult::rejected(
                RiskRejectCode::SlippageExceeded, 0.81,
                "滑点超限");
        }
    }

    // ── 步骤 6：日成交额检查 ──
    if (input.turnoverLimitWan() > 0.0) {
        const double projected = input.currentDailyTurnoverNotional() + notional;
        if (projected > input.turnoverLimitWan() * 10000.0) {
            return RiskResult::rejected(
                RiskRejectCode::DailyTurnoverExceeded, 0.83,
                "当日累计成交额超限");
        }
    }

    // ── 全部通过 ──
    return RiskResult::accept(RiskRejectCode::None, 0.15, "风控校验通过");
}

// ═════════════════════════════════════════════════════════════════════════
// RiskEvaluator — 枚举转换和辅助方法（桥接层边界用）
// ═════════════════════════════════════════════════════════════════════════

std::string RiskEvaluator::descriptionForCode(RiskRejectCode code) {
    switch (code) {
    case RiskRejectCode::None:
        return "风控校验通过";
    case RiskRejectCode::MissingRequiredFields:
        return "缺少必填字段";
    case RiskRejectCode::StrategyNotBound:
        return "策略未绑定";
    case RiskRejectCode::StrategyNotActive:
        return "策略未激活";
    case RiskRejectCode::PriceInvalid:
        return "价格无效";
    case RiskRejectCode::SignalStrengthTooWeak:
        return "信号强度过弱";
    case RiskRejectCode::PositionSnapshotNotReady:
        return "持仓快照未就绪";
    case RiskRejectCode::TradingSessionClosed:
        return "交易时段已关闭";
    case RiskRejectCode::NoSellablePosition:
        return "无可卖持仓";
    case RiskRejectCode::SellQuantityExceedsHolding:
        return "卖出数量超过持仓";
    case RiskRejectCode::OrderSizeExceeded:
        return "订单金额超限";
    case RiskRejectCode::SlippageExceeded:
        return "滑点超限";
    case RiskRejectCode::DailyTurnoverExceeded:
        return "日成交额超限";
    case RiskRejectCode::StopLossTriggered:
        return "止损触发";
    case RiskRejectCode::TakeProfitTriggered:
        return "止盈触发";
    case RiskRejectCode::BreakerLevel1:
        return "一级熔断";
    case RiskRejectCode::BreakerLevel2:
        return "二级熔断";
    case RiskRejectCode::BreakerLevel3:
        return "三级熔断";
    case RiskRejectCode::MaxDrawdownExceeded:
        return "最大回撤超限";
    case RiskRejectCode::PositionConcentrationExceeded:
        return "持仓集中度超限";
    case RiskRejectCode::TotalExposureExceeded:
        return "总敞口超限";
    case RiskRejectCode::IndexDrawdownExceeded:
        return "大盘回撤保护：暂停开仓";
    case RiskRejectCode::Level3TradingHaltActive:
        return "三级交易暂停生效";
    case RiskRejectCode::AutoStrategyWithoutQuantity:
        return "自动策略缺少数量";
    }
    return "未知风控拒绝原因";
}

OrderDirection RiskEvaluator::directionFromString(const std::string& raw) {
    if (raw.empty()) {
        return OrderDirection::Buy; // 安全默认值
    }
    // 精确匹配：避免字符串比较的歧义
    if (raw == "BUY" || raw == "Buy" || raw == "buy") return OrderDirection::Buy;
    if (raw == "SELL" || raw == "Sell" || raw == "sell") return OrderDirection::Sell;
    if (raw == "LONG" || raw == "Long" || raw == "long") return OrderDirection::Buy;
    if (raw == "SHORT" || raw == "Short" || raw == "short") return OrderDirection::Sell;
    // 数字编码
    if (raw == "1") return OrderDirection::Buy;
    if (raw == "2") return OrderDirection::Sell;
    return OrderDirection::Buy;
}

PositionEffect RiskEvaluator::positionEffectFromString(const std::string& raw) {
    if (raw.empty()) {
        return PositionEffect::Unspecified;
    }
    if (raw == "OPEN" || raw == "Open" || raw == "open") return PositionEffect::Open;
    if (raw == "CLOSE" || raw == "Close" || raw == "close") return PositionEffect::Close;
    if (raw == "1") return PositionEffect::Open;
    if (raw == "2") return PositionEffect::Close;
    return PositionEffect::Unspecified;
}

SpecialAction RiskEvaluator::specialActionFromString(const std::string& raw) {
    if (raw.empty()) {
        return SpecialAction::None;
    }
    // 现金还款变体
    if (raw == "CashRepay" || raw == "cashRepay" || raw == "cashrepay"
        || raw == "CASH_REPAY" || raw == "repay" || raw == "Repay") {
        return SpecialAction::CashRepay;
    }
    // 股份归还变体
    if (raw == "ShareReturn" || raw == "shareReturn" || raw == "sharereturn"
        || raw == "SHARE_RETURN" || raw == "returnStock" || raw == "returnstock"
        || raw == "ReturnStock") {
        return SpecialAction::ShareReturn;
    }
    return SpecialAction::None;
}

bool RiskEvaluator::increasesExposure(OrderDirection dir, PositionEffect effect) {
    // 买入或开仓操作增加风险敞口
    if (dir == OrderDirection::Buy) return true;
    if (effect == PositionEffect::Open) return true;
    return false;
}

double RiskEvaluator::adverseSlippagePct(OrderDirection dir,
                                          double orderPrice,
                                          double referencePrice) {
    if (referencePrice <= 0.0) {
        return 0.0;
    }
    const double pct = (orderPrice - referencePrice) / referencePrice * 100.0;
    // 买入：买入价高于参考价 = 不利滑点（正值）
    // 卖出：卖出价低于参考价 = 不利滑点（转为正值）
    return (dir == OrderDirection::Buy) ? pct : -pct;
}

bool RiskEvaluator::isShortSide(PositionEffect effect,
                                 const std::string& rawPositionSide) {
    // 优先使用枚举
    if (effect == PositionEffect::Open) {
        // 开仓操作本身不能判断多空，依赖原始持仓方向字符串
        return (rawPositionSide == "SHORT" || rawPositionSide == "Short"
                || rawPositionSide == "short" || rawPositionSide == "2");
    }
    // 平仓操作：平多 = 卖出（不是空方），平空 = 买入（是空方操作）
    return false;
}

RiskLiveMetrics RiskEvaluator::computeLiveMetrics(
    double totalAsset,
    double marketValue,
    double maxTotalExposurePct,
    double peakTotalAsset) {

    RiskLiveMetrics metrics;

    // 总敞口百分比
    metrics.currentTotalExposurePercent =
        totalAsset > 0.0 ? (marketValue / totalAsset) * 100.0 : 0.0;

    // VaR 使用率 = 敞口 / 最大敞口限制 * 100
    const double clampedExposure = clampMaxTotalExposure(maxTotalExposurePct);
    metrics.varUsagePercent =
        clampedExposure > 0.0
            ? (metrics.currentTotalExposurePercent / clampedExposure) * 100.0
            : 0.0;

    // VaR 预算 = 总资产 * 最大敞口比例
    metrics.varBudgetAmount = totalAsset * (clampedExposure / 100.0);

    // 估算 VaR = 持仓市值 * 2%（简化估算，实际应使用历史波动率模型）
    metrics.estimatedVarAmount = marketValue * 0.02;

    // 当前回撤
    if (peakTotalAsset > 0.0 && totalAsset < peakTotalAsset) {
        metrics.currentDrawdownPercent =
            ((totalAsset - peakTotalAsset) / peakTotalAsset) * 100.0;
    } else {
        metrics.currentDrawdownPercent = 0.0;
    }

    return metrics;
}

double RiskEvaluator::clampMaxTotalExposure(double rawPct) {
    // 敞口比例必须在 [0, 100] 区间内
    return std::clamp(rawPct, 0.0, 100.0);
}

void RiskEvaluator::applyConfig(RiskInput& input, const RiskConfig& config) {
    input.setOrderSizeLimitWan(config.orderSizeLimitWan);
    input.setSlippageLimitPercent(config.slippageLimitPercent);
    input.setTurnoverLimitWan(config.turnoverLimitWan);
    input.setStopLossPercent(config.stopLossPercent);
    input.setTakeProfitPercent(config.takeProfitPercent);
    input.setMaxDrawdownLimitPercent(config.maxDrawdownLimitPercent);
    input.setBreakerLevel1Percent(config.breakerLevel1Percent);
    input.setBreakerLevel2Percent(config.breakerLevel2Percent);
    input.setBreakerLevel3Percent(config.breakerLevel3Percent);
    input.setMaxPositionPercent(config.maxPositionPercent);
    input.setMaxTotalExposurePercent(config.maxTotalExposurePercent);
}

RiskConfig RiskConfig::defaults() noexcept {
    RiskConfig cfg;
    cfg.orderSizeLimitWan      = 500.0;
    cfg.slippageLimitPercent   = 2.0;
    cfg.turnoverLimitWan       = 5000.0;
    cfg.stopLossPercent        = 10.0;
    cfg.takeProfitPercent      = 20.0;
    cfg.maxDrawdownLimitPercent = 12.0;
    cfg.maxDailyLossPercent    = 5.0;
    cfg.breakerLevel1Percent   = 5.0;
    cfg.breakerLevel2Percent   = 8.0;
    cfg.breakerLevel3Percent   = 12.0;
    cfg.maxPositionPercent     = 15.0;
    cfg.maxTotalExposurePercent = 67.0;
    cfg.commissionRate         = 0.0003;  // 万三
    cfg.minCommission          = 5.0;     // 最低 5 元
    cfg.stampTaxRate           = 0.001;   // 千一（仅卖出）
    cfg.indexDrawdownLimitPercent = 3.0;
    cfg.indexDrawdownLookbackDays = 5;
    cfg.indexSymbol               = "000001.SH";
    return cfg;
}

} // namespace domain::strategy
