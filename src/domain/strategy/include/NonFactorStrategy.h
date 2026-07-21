#pragma once
// ═════════════════════════════════════════════════════════════════════════
// NonFactorStrategy — 非因子策略 (纯 C++，零 Qt)
// 基类: 公共参数(StrategyCommonConfig) + evaluateSymbol 接口
// 子类: 实现个性化指标逻辑
// 引擎层: evaluateSignals() 负责行情遍历 + 信号排序限流
// ═════════════════════════════════════════════════════════════════════════

#include "IStrategyService.h"
#include "StrategyServiceTypes.h"
#include "../../strategies/include/StrategyDefinitionTypes.h"
#include "MarketDataService.h"
#include "../../factor/include/factor_compute/IMarketDataView.h"

#include <ta_libc.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
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

    NonFactorStrategy(StrategyInstanceId instanceId,
                      ::domain::strategies::StrategyBehaviorKind kind,
                      const ::domain::strategies::StrategyCommonConfig& commonCfg)
        : m_instanceId(instanceId)
        , m_kind(kind)
        , m_commonCfg(commonCfg)
    {
        static bool taInit = false;
        if (!taInit) { TA_Initialize(); taInit = true; }
    }

    [[nodiscard]] StrategyInstanceId instanceId() const noexcept final { return m_instanceId; }
    [[nodiscard]] bool isEnabled() const noexcept final { return true; }
    [[nodiscard]] rules::RuleSetId ruleSetId() const noexcept final { return rules::kRuleSetAllPass; }
    [[nodiscard]] bool usesFactors() const noexcept final { return false; }

    // ── 引擎层: 行情遍历 + 调 evaluateSymbol + 排序限流 ──
    void evaluate(const std::vector<RuntimeFactorSnapshot>&,
                  const RuntimeStrategyContext& context,
                  std::vector<StrategySignal>& outputSignals) final
    {
        evaluateSignals(*this, context, outputSignals);
    }

    /// 工厂: 根据 behaviorKind 创建对应子类
    static std::unique_ptr<NonFactorStrategy> create(
        StrategyInstanceId instanceId,
        ::domain::strategies::StrategyBehaviorKind kind,
        const ::domain::strategies::StrategyCommonConfig& cfg);

protected:
    static constexpr int kLookbackSafetyMargin = 5;

    /// 策略所需回看窗口 — 子类覆写
    [[nodiscard]] virtual int requiredLookbackBars() const noexcept
    {
        constexpr int kMinIndicatorWindow = 26;
        return kMinIndicatorWindow + kLookbackSafetyMargin;
    }

    /// 子类实现: 对单个标的判断买卖信号
    [[nodiscard]] virtual SignalResult evaluateSymbol(
        const std::vector<double>& closePrices,
        std::uint32_t instrumentId,
        const RuntimeStrategyContext& ctx,
        const ::domain::strategies::StrategyCommonConfig& cfg) = 0;

    [[nodiscard]] double weightForSignalScore(double score) const noexcept
    {
        const double weightFloor = (std::max)(0.0, m_commonCfg.minWeightPerStock);
        const double weightCap = (std::max)(weightFloor, m_commonCfg.maxWeightPerStock);
        const double normalizedScore = std::clamp(score, 0.0, 1.0);
        return weightFloor + (weightCap - weightFloor) * normalizedScore;
    }

    ::domain::strategies::StrategyCommonConfig m_commonCfg;

private:
    /// 引擎层实现: 遍历行情 → evaluateSymbol → 排序限流
    static void evaluateSignals(NonFactorStrategy& self,
                                const RuntimeStrategyContext& context,
                                std::vector<StrategySignal>& outputSignals);

    StrategyInstanceId m_instanceId;
    ::domain::strategies::StrategyBehaviorKind m_kind;
};

// ═══ 趋势跟踪 — MA 交叉 ═══
class TrendFollowingStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;
protected:
    [[nodiscard]] int requiredLookbackBars() const noexcept override
    { return m_commonCfg.slowPeriod + 1 + kLookbackSafetyMargin; }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&, std::uint32_t,
        const RuntimeStrategyContext&, const ::domain::strategies::StrategyCommonConfig&) override;
};

// ═══ 均值回归 — RSI ═══
class MeanReversionStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;
protected:
    [[nodiscard]] int requiredLookbackBars() const noexcept override
    { return m_commonCfg.signalPeriod + 1 + kLookbackSafetyMargin; }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&, std::uint32_t,
        const RuntimeStrategyContext&, const ::domain::strategies::StrategyCommonConfig&) override;
};

// ═══ 动量 — MACD ═══
class MomentumStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;
protected:
    [[nodiscard]] int requiredLookbackBars() const noexcept override
    { return m_commonCfg.macdSlow + m_commonCfg.macdSignal + kLookbackSafetyMargin; }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&, std::uint32_t,
        const RuntimeStrategyContext&, const ::domain::strategies::StrategyCommonConfig&) override;
};

// ═══ 套利 — 布林带 %B ═══
class ArbitrageStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;
protected:
    [[nodiscard]] int requiredLookbackBars() const noexcept override
    { return m_commonCfg.bbPeriod + 1 + kLookbackSafetyMargin; }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&, std::uint32_t,
        const RuntimeStrategyContext&, const ::domain::strategies::StrategyCommonConfig&) override;
};

// ═══ 事件驱动 — 量价异动 ═══
class EventDrivenStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;
protected:
    [[nodiscard]] int requiredLookbackBars() const noexcept override
    { return 22 + kLookbackSafetyMargin; }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&, std::uint32_t,
        const RuntimeStrategyContext&, const ::domain::strategies::StrategyCommonConfig&) override;
};

// ═══ 高频 — 微观结构 ═══
class HighFrequencyStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;
protected:
    [[nodiscard]] int requiredLookbackBars() const noexcept override
    { return 12 + kLookbackSafetyMargin; }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&, std::uint32_t,
        const RuntimeStrategyContext&, const ::domain::strategies::StrategyCommonConfig&) override;
};

// ═══ 自定义 — 多指标复合 ═══
class CustomStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;
protected:
    [[nodiscard]] int requiredLookbackBars() const noexcept override
    {
        int maxWindow = std::max({26, m_commonCfg.slowPeriod + 1,
            m_commonCfg.macdSlow + m_commonCfg.macdSignal, m_commonCfg.signalPeriod + 1});
        return maxWindow + kLookbackSafetyMargin;
    }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&, std::uint32_t,
        const RuntimeStrategyContext&, const ::domain::strategies::StrategyCommonConfig&) override;
};

} // namespace domain::strategy
