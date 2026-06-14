#include "../include/RiskEvaluator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace domain::strategy {

// ── 工厂方法 ──

RiskResult RiskResult::accept(RiskRejectCode code, double score, std::string description) {
    return RiskResult(true, code, score, std::move(description));
}

RiskResult RiskResult::rejected(RiskRejectCode code, double score, std::string description) {
    return RiskResult(false, code, score, std::move(description));
}

RiskResult::RiskResult(bool approved, RiskRejectCode code, double score, std::string description)
    : m_approved(approved)
    , m_code(code)
    , m_riskScore(score)
    , m_description(std::move(description))
{
}

// ── 代码 → 描述 ──

std::string RiskEvaluator::descriptionForCode(RiskRejectCode code) {
    switch (code) {
    case RiskRejectCode::MissingRequiredFields:       return "策略信号缺少必要字段";
    case RiskRejectCode::StrategyNotBound:            return "策略未绑定到交易配置，拒绝实盘委托";
    case RiskRejectCode::StrategyNotActive:           return "策略未激活，拒绝执行";
    case RiskRejectCode::PriceInvalid:                return "价格无效，拒绝执行";
    case RiskRejectCode::SignalStrengthTooWeak:        return "信号强度不足";
    case RiskRejectCode::PositionSnapshotNotReady:     return "持仓快照尚未同步完成，启动阶段禁止按仓位执行委托";
    case RiskRejectCode::TradingSessionClosed:         return "当前非交易时段，禁止新增买入或加仓委托";
    case RiskRejectCode::NoSellablePosition:           return "当前无可卖持仓，拒绝卖出委托";
    case RiskRejectCode::SellQuantityExceedsHolding:   return "卖出数量超过当前可卖持仓";
    case RiskRejectCode::OrderSizeExceeded:            return "单笔委托金额超过风控上限";
    case RiskRejectCode::SlippageExceeded:             return "委托价偏离超过滑点容忍度";
    case RiskRejectCode::DailyTurnoverExceeded:        return "日累计成交金额超过上限";
    case RiskRejectCode::StopLossTriggered:            return "标的当前收益已触发止损线，禁止继续加仓";
    case RiskRejectCode::TakeProfitTriggered:          return "标的当前收益已触发止盈线，禁止继续加仓";
    case RiskRejectCode::BreakerLevel1:                return "账户当前回撤已触发一级熔断线，禁止继续加仓";
    case RiskRejectCode::BreakerLevel2:                return "账户当前回撤已触发二级熔断线，禁止继续加仓";
    case RiskRejectCode::BreakerLevel3:                return "账户当前回撤已触发三级熔断线，禁止继续加仓";
    case RiskRejectCode::MaxDrawdownExceeded:          return "账户当前回撤超过上限，禁止继续加仓";
    case RiskRejectCode::PositionConcentrationExceeded:return "单票集中度超过上限";
    case RiskRejectCode::TotalExposureExceeded:        return "组合总仓位超过上限";
    case RiskRejectCode::Level3TradingHaltActive:      return "三级熔断已触发，当日停止交易";
    case RiskRejectCode::AutoStrategyWithoutQuantity:  return "自动策略信号未提供明确下单数量或金额";
    case RiskRejectCode::None:
    default:                                           return "基础风控校验通过";
    }
}

// ── 内部评估 ──

namespace {

bool isValidOrder(const RiskInput& input) {
    if (input.strategyId().empty())   return false;
    if (input.symbol().empty())       return false;
    if (input.price() <= 0.0)        return false;
    return true;
}

double adverseSlippagePercent(bool isBuy, double orderPrice, double referencePrice) {
    if (referencePrice <= 0.0 || orderPrice <= 0.0) return 0.0;
    double diff = isBuy ? (orderPrice - referencePrice) : (referencePrice - orderPrice);
    return diff > 0.0 ? (diff / referencePrice) * 100.0 : 0.0;
}

} // anonymous namespace

RiskResult RiskEvaluator::evaluateOrder(const RiskInput& input) {
    // ── 1. 基础字段 ──
    if (!isValidOrder(input)) {
        return RiskResult::rejected(RiskRejectCode::MissingRequiredFields, 1.0, descriptionForCode(RiskRejectCode::MissingRequiredFields));
    }
    if (!input.strategyBound()) {
        return RiskResult::rejected(RiskRejectCode::StrategyNotBound, 0.98, descriptionForCode(RiskRejectCode::StrategyNotBound));
    }
    if (!input.strategyActive()) {
        return RiskResult::rejected(RiskRejectCode::StrategyNotActive, 0.9, descriptionForCode(RiskRejectCode::StrategyNotActive));
    }
    if (input.signalStrength() <= 0.0) {
        return RiskResult::rejected(RiskRejectCode::SignalStrengthTooWeak, 0.75, descriptionForCode(RiskRejectCode::SignalStrengthTooWeak));
    }
    if (input.isAutoStrategySignal() && input.quantity() <= 0 && input.cashAmount() <= 0.0) {
        return RiskResult::rejected(RiskRejectCode::AutoStrategyWithoutQuantity, 0.99, descriptionForCode(RiskRejectCode::AutoStrategyWithoutQuantity));
    }

    // ── 2. 三级熔断中止 ──
    if (input.level3TradingHaltActive()) {
        return RiskResult::rejected(RiskRejectCode::Level3TradingHaltActive, 0.99, descriptionForCode(RiskRejectCode::Level3TradingHaltActive));
    }

    // ── 3. 买入新增风险 ──
    bool increasesExposure = input.isBuyOrder();
    if (increasesExposure) {
        // 持仓快照
        if (!input.positionSnapshotReady()) {
            return RiskResult::rejected(RiskRejectCode::PositionSnapshotNotReady, 0.96, descriptionForCode(RiskRejectCode::PositionSnapshotNotReady));
        }
        // 交易时段
        if (!input.tradingSessionOpen()) {
            return RiskResult::rejected(RiskRejectCode::TradingSessionClosed, 0.98, descriptionForCode(RiskRejectCode::TradingSessionClosed));
        }
        // 止损
        if (input.stopLossPercent() > 0.0 && input.symbolPositionReturnPercent() <= -input.stopLossPercent()) {
            return RiskResult::rejected(RiskRejectCode::StopLossTriggered, 0.91, descriptionForCode(RiskRejectCode::StopLossTriggered));
        }
        // 止盈
        if (input.takeProfitPercent() > 0.0 && input.symbolPositionReturnPercent() >= input.takeProfitPercent()) {
            return RiskResult::rejected(RiskRejectCode::TakeProfitTriggered, 0.72, descriptionForCode(RiskRejectCode::TakeProfitTriggered));
        }
        // 回撤 / 熔断
        double dd = input.currentDrawdownPercent();
        if (input.breakerLevel3Percent() > 0.0 && dd >= input.breakerLevel3Percent()) {
            return RiskResult::rejected(RiskRejectCode::BreakerLevel3, 0.95, descriptionForCode(RiskRejectCode::BreakerLevel3));
        }
        if (input.breakerLevel2Percent() > 0.0 && dd >= input.breakerLevel2Percent()) {
            return RiskResult::rejected(RiskRejectCode::BreakerLevel2, 0.9, descriptionForCode(RiskRejectCode::BreakerLevel2));
        }
        if (input.breakerLevel1Percent() > 0.0 && dd >= input.breakerLevel1Percent()) {
            return RiskResult::rejected(RiskRejectCode::BreakerLevel1, 0.86, descriptionForCode(RiskRejectCode::BreakerLevel1));
        }
        if (input.maxDrawdownLimitPercent() > 0.0 && dd >= input.maxDrawdownLimitPercent()) {
            return RiskResult::rejected(RiskRejectCode::MaxDrawdownExceeded, 0.93, descriptionForCode(RiskRejectCode::MaxDrawdownExceeded));
        }
        // 仓位集中度
        double totalAsset = input.currentTotalAsset();
        if (totalAsset > 0.0 && input.maxPositionPercent() > 0.0) {
            double projected = ((input.symbolMarketValue() + input.requestedNotional()) / totalAsset) * 100.0;
            if (projected > input.maxPositionPercent()) {
                return RiskResult::rejected(RiskRejectCode::PositionConcentrationExceeded, 0.88, descriptionForCode(RiskRejectCode::PositionConcentrationExceeded));
            }
        }
        // 总仓位
        if (totalAsset > 0.0 && input.maxTotalExposurePercent() > 0.0) {
            double projected = ((input.currentMarketValue() + input.requestedNotional()) / totalAsset) * 100.0;
            if (projected > input.maxTotalExposurePercent()) {
                return RiskResult::rejected(RiskRejectCode::TotalExposureExceeded, 0.84, descriptionForCode(RiskRejectCode::TotalExposureExceeded));
            }
        }
    }
    // 卖出持仓检查
    else {
        if (input.closeableQuantity() <= 0) {
            return RiskResult::rejected(RiskRejectCode::NoSellablePosition, 0.89, descriptionForCode(RiskRejectCode::NoSellablePosition));
        }
        if (input.quantity() > input.closeableQuantity()) {
            return RiskResult::rejected(RiskRejectCode::SellQuantityExceedsHolding, 0.9, descriptionForCode(RiskRejectCode::SellQuantityExceedsHolding));
        }
    }

    // ── 4. 金额 / 滑点 ──
    double notional = input.requestedNotional();
    if (notional <= 0.0) notional = input.price() * static_cast<double>(std::max<int64_t>(1, input.quantity()));
    if (input.orderSizeLimitWan() > 0.0 && notional > input.orderSizeLimitWan() * 10000.0) {
        return RiskResult::rejected(RiskRejectCode::OrderSizeExceeded, 0.92, descriptionForCode(RiskRejectCode::OrderSizeExceeded));
    }
    if (input.slippageLimitPercent() > 0.0 && input.price() > 0.0) {
        double slip = adverseSlippagePercent(input.isBuyOrder(), input.price(), input.referencePrice());
        if (input.referencePrice() > 0.0 && slip > input.slippageLimitPercent()) {
            return RiskResult::rejected(RiskRejectCode::SlippageExceeded, 0.81, descriptionForCode(RiskRejectCode::SlippageExceeded));
        }
    }

    // ── 5. 日总额 ──
    if (input.turnoverLimitWan() > 0.0 && (input.currentDailyTurnoverNotional() + notional) > input.turnoverLimitWan() * 10000.0) {
        return RiskResult::rejected(RiskRejectCode::DailyTurnoverExceeded, 0.83, descriptionForCode(RiskRejectCode::DailyTurnoverExceeded));
    }

    return RiskResult::accept();
}

// ── 枚举转换函数 ──

OrderDirection RiskEvaluator::directionFromString(const std::string& raw) {
    // "BUY" / "LONG" / "1" → Buy
    // "SELL" / "SHORT" / "2" → Sell
    if (raw.empty()) return OrderDirection::Buy;
    char first = raw[0];
    if (first == 'B' || first == 'b' || first == 'L' || first == 'l' || first == '1') {
        return OrderDirection::Buy;
    }
    return OrderDirection::Sell;
}

PositionEffect RiskEvaluator::positionEffectFromString(const std::string& raw) {
    if (raw.empty()) return PositionEffect::Unspecified;
    char first = raw[0];
    if (first == 'O' || first == 'o' || first == '1') return PositionEffect::Open;
    if (first == 'C' || first == 'c' || first == '2') return PositionEffect::Close;
    return PositionEffect::Unspecified;
}

SpecialAction RiskEvaluator::specialActionFromString(const std::string& raw) {
    // "repay" / "cashrepay" / "creditrepaycash" → CashRepay
    // "returnstock" / "repayshare" / "creditrepayshare" → ShareReturn
    if (raw.empty()) return SpecialAction::None;
    char first = raw[0];
    if (first == 'r') {
        // "repay" contains "repay"
        if (raw.find("repay") != std::string::npos) {
            if (raw.find("share") != std::string::npos || raw.find("stock") != std::string::npos) {
                return SpecialAction::ShareReturn;
            }
            return SpecialAction::CashRepay;
        }
        if (raw.find("return") != std::string::npos) {
            return SpecialAction::ShareReturn;
        }
    }
    if (first == 'c') {
        // "cashrepay" / "creditrepaycash"
        if (raw.find("repay") != std::string::npos) return SpecialAction::CashRepay;
        // "creditrepayshare"
        if (raw.find("share") != std::string::npos) return SpecialAction::ShareReturn;
    }
    return SpecialAction::None;
}

bool RiskEvaluator::increasesExposure(OrderDirection dir, PositionEffect effect) {
    if (effect == PositionEffect::Close) return false;
    if (effect == PositionEffect::Open) return true;
    return dir == OrderDirection::Buy;
}

double RiskEvaluator::adverseSlippagePct(OrderDirection dir, double orderPrice, double referencePrice) {
    if (referencePrice <= 0.0 || orderPrice <= 0.0) return 0.0;
    double diff = (dir == OrderDirection::Buy)
        ? (orderPrice - referencePrice)
        : (referencePrice - orderPrice);
    return diff > 0.0 ? (diff / referencePrice) * 100.0 : 0.0;
}

bool RiskEvaluator::isShortSide(PositionEffect /*effect*/, const std::string& rawPositionSide) {
    // "SHORT" → true, "LONG" → false
    if (rawPositionSide.empty()) return false;
    char first = rawPositionSide[0];
    return (first == 'S' || first == 's');
}

RiskLiveMetrics RiskEvaluator::computeLiveMetrics(
    double totalAsset, double marketValue, double maxTotalExposurePct, double peakTotalAsset)
{
    RiskLiveMetrics m;
    if (totalAsset <= 0.0) return m;

    // 回撤
    double peak = (std::max)(peakTotalAsset, totalAsset);
    m.currentDrawdownPercent = (peak > 0.0) ? ((peak - totalAsset) / peak) * 100.0 : 0.0;

    // 仓位
    m.currentTotalExposurePercent = (marketValue / totalAsset) * 100.0;

    // VaR
    double clampedExposure = (std::max)(0.0, (std::min)(100.0, maxTotalExposurePct));
    m.varBudgetAmount = totalAsset * (clampedExposure / 100.0);
    m.estimatedVarAmount = marketValue;
    m.varUsagePercent = m.varBudgetAmount > 0.0 ? (m.estimatedVarAmount / m.varBudgetAmount) * 100.0 : 0.0;

    return m;
}

double RiskEvaluator::clampMaxTotalExposure(double rawPct) {
    return (std::max)(0.0, (std::min)(100.0, rawPct));
}

} // namespace domain::strategy
