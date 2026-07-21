#pragma once
// ═════════════════════════════════════════════════════════════════════════
// NonFactorStrategy — 非因子策略 (纯 C++，零 Qt)
// 使用 TA-Lib 计算技术指标, 从 historicalView 获取 OHLCV 数据
// 指标逻辑由构造函数注入, behaviorKind 决定使用的指标组合
// ═════════════════════════════════════════════════════════════════════════

#include "IStrategyService.h"
#include "StrategyServiceTypes.h"
#include "../../strategies/include/StrategyDefinitionTypes.h"
#include "MarketDataService.h"
#include "../../factor/include/factor_compute/IMarketDataView.h"
#include "../../factor/include/factor_compute/CachedMarketDataView.h"

#include <ta_libc.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace domain::strategy {

class NonFactorStrategy : public IRuntimeStrategy {
public:
    struct SignalResult {
        bool valid = false;
        bool isBuy = true;
        std::uint32_t instrumentId = 0;
        double score = 0.7;
    };

    /// 指标求值函数签名
    using EvaluateFn = std::function<SignalResult(
        const std::vector<double>& closePrices,
        std::uint32_t instrumentId,
        const RuntimeStrategyContext& ctx,
        const ::domain::strategies::StrategyCommonConfig& cfg)>;

    NonFactorStrategy(StrategyInstanceId instanceId,
                      ::domain::strategies::StrategyBehaviorKind kind,
                      const ::domain::strategies::StrategyCommonConfig& commonCfg,
                      EvaluateFn evaluateFn,
                      int requiredLookback)
        : m_instanceId(instanceId)
        , m_kind(kind)
        , m_commonCfg(commonCfg)
        , m_evaluateFn(std::move(evaluateFn))
        , m_requiredLookback(requiredLookback)
    {
        static bool taInit = false;
        if (!taInit) { TA_Initialize(); taInit = true; }
    }

    [[nodiscard]] StrategyInstanceId instanceId() const noexcept final { return m_instanceId; }
    [[nodiscard]] bool isEnabled() const noexcept final { return true; }
    [[nodiscard]] rules::RuleSetId ruleSetId() const noexcept final { return rules::kRuleSetAllPass; }
    [[nodiscard]] bool usesFactors() const noexcept final { return false; }

    void evaluate(const std::vector<RuntimeFactorSnapshot>&,
                  const RuntimeStrategyContext& context,
                  std::vector<StrategySignal>& outputSignals) final
    {
        const auto* view = static_cast<const factor::compute::IMarketDataView*>(
            context.historicalViewPtr());
        if (!view) return;

        auto closeMat = view->close();
        auto instruments = view->instruments();
        auto dates = view->dates();
        int rows = static_cast<int>(dates.size());
        int cols = static_cast<int>(instruments.size());
        if (rows < m_requiredLookback || cols == 0) return;

        int rowStride = closeMat.rowStride;
        int evalRow = context.currentEvaluationRow();
        int lastRow = (evalRow >= 0) ? evalRow : (rows - 1);
        int lookback = std::min(m_requiredLookback, lastRow + 1);
        const int kMaxDailySignals = std::max(1, m_commonCfg.maxPositions);

        std::vector<StrategySignal> allSignals;
        allSignals.reserve(cols);
        for (int c = 0; c < cols; ++c) {
            std::vector<double> closePrices(lookback);
            for (int i = 0; i < lookback; ++i)
                closePrices[i] = static_cast<double>(
                    closeMat.data[(lastRow - lookback + 1 + i) * rowStride + c]);

            std::string fullSymbol;
            {
                const auto& syms = view->symbolStrings();
                if (c < static_cast<int>(syms.size())) fullSymbol = syms[static_cast<size_t>(c)];
            }
            if (fullSymbol.empty()) continue;

            {
                auto& d = domain::market::MarketDataService::instance().liveData(fullSymbol);
                if (d.valid()) {
                    double liveC = d.dailyBar().close();
                    if (liveC > 0) closePrices.push_back(liveC);
                }
            }

            SignalResult sig = m_evaluateFn(closePrices, instruments[c].value,
                                             context, m_commonCfg);
            if (sig.valid) {
                auto signal = StrategySignal(
                    context.strategyInstanceId(),
                    InstrumentId{sig.instrumentId},
                    sig.isBuy ? RuntimeOrderSide::Buy : RuntimeOrderSide::Sell,
                    sig.score, weightForSignalScore(sig.score),
                    fullSymbol);
                allSignals.push_back(std::move(signal));
            }
        }

        std::sort(allSignals.begin(), allSignals.end(),
            [](const StrategySignal& a, const StrategySignal& b) {
                if (a.side() == RuntimeOrderSide::Sell && b.side() == RuntimeOrderSide::Buy) return true;
                if (a.side() == RuntimeOrderSide::Buy && b.side() == RuntimeOrderSide::Sell) return false;
                return a.score() > b.score();
            });

        int buyCount = 0;
        for (auto& s : allSignals) {
            if (s.side() == RuntimeOrderSide::Buy) {
                if (buyCount >= kMaxDailySignals) continue;
                ++buyCount;
            }
            outputSignals.push_back(std::move(s));
        }
    }

    /// 根据 behaviorKind 创建对应的指标函数和回看窗口
    static std::pair<EvaluateFn, int> makeIndicator(
        ::domain::strategies::StrategyBehaviorKind kind,
        const ::domain::strategies::StrategyCommonConfig& cfg);

private:
    static constexpr int kLookbackSafetyMargin = 5;

    [[nodiscard]] double weightForSignalScore(double score) const noexcept
    {
        const double weightFloor = (std::max)(0.0, m_commonCfg.minWeightPerStock);
        const double weightCap = (std::max)(weightFloor, m_commonCfg.maxWeightPerStock);
        const double normalizedScore = std::clamp(score, 0.0, 1.0);
        return weightFloor + (weightCap - weightFloor) * normalizedScore;
    }

    StrategyInstanceId m_instanceId;
    ::domain::strategies::StrategyBehaviorKind m_kind;
    ::domain::strategies::StrategyCommonConfig m_commonCfg;
    EvaluateFn m_evaluateFn;
    int m_requiredLookback;
};

// ── 策略子类: 薄封装, 构造时通过 makeIndicator 注入自身指标逻辑 ──

class TrendFollowingStrategy final : public NonFactorStrategy {
public:
    TrendFollowingStrategy(StrategyInstanceId instanceId,
                           ::domain::strategies::StrategyBehaviorKind kind,
                           const ::domain::strategies::StrategyCommonConfig& cfg)
        : NonFactorStrategy(instanceId, kind, cfg,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::TrendFollowing, cfg).first,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::TrendFollowing, cfg).second) {}
};

class MeanReversionStrategy final : public NonFactorStrategy {
public:
    MeanReversionStrategy(StrategyInstanceId instanceId,
                          ::domain::strategies::StrategyBehaviorKind kind,
                          const ::domain::strategies::StrategyCommonConfig& cfg)
        : NonFactorStrategy(instanceId, kind, cfg,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::MeanReversion, cfg).first,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::MeanReversion, cfg).second) {}
};

class MomentumStrategy final : public NonFactorStrategy {
public:
    MomentumStrategy(StrategyInstanceId instanceId,
                     ::domain::strategies::StrategyBehaviorKind kind,
                     const ::domain::strategies::StrategyCommonConfig& cfg)
        : NonFactorStrategy(instanceId, kind, cfg,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::Momentum, cfg).first,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::Momentum, cfg).second) {}
};

class ArbitrageStrategy final : public NonFactorStrategy {
public:
    ArbitrageStrategy(StrategyInstanceId instanceId,
                      ::domain::strategies::StrategyBehaviorKind kind,
                      const ::domain::strategies::StrategyCommonConfig& cfg)
        : NonFactorStrategy(instanceId, kind, cfg,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::Arbitrage, cfg).first,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::Arbitrage, cfg).second) {}
};

class EventDrivenStrategy final : public NonFactorStrategy {
public:
    EventDrivenStrategy(StrategyInstanceId instanceId,
                        ::domain::strategies::StrategyBehaviorKind kind,
                        const ::domain::strategies::StrategyCommonConfig& cfg)
        : NonFactorStrategy(instanceId, kind, cfg,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::EventDriven, cfg).first,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::EventDriven, cfg).second) {}
};

class HighFrequencyStrategy final : public NonFactorStrategy {
public:
    HighFrequencyStrategy(StrategyInstanceId instanceId,
                          ::domain::strategies::StrategyBehaviorKind kind,
                          const ::domain::strategies::StrategyCommonConfig& cfg)
        : NonFactorStrategy(instanceId, kind, cfg,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::HighFrequency, cfg).first,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::HighFrequency, cfg).second) {}
};

class CustomStrategy final : public NonFactorStrategy {
public:
    CustomStrategy(StrategyInstanceId instanceId,
                   ::domain::strategies::StrategyBehaviorKind kind,
                   const ::domain::strategies::StrategyCommonConfig& cfg)
        : NonFactorStrategy(instanceId, kind, cfg,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::Custom, cfg).first,
            makeIndicator(::domain::strategies::StrategyBehaviorKind::Custom, cfg).second) {}
};

} // namespace domain::strategy
