#pragma once
// StrategyBase — 技术指标策略基类 (对标 BaseFactor)
//   公共参数: StrategyCommonConfig
//   公共方法: evaluate() = 行情遍历 + 排序限流
//   纯虚接口: evaluateSymbol() — 子类实现个性化指标逻辑
//   纯虚接口: requiredLookbackBars() — 子类声明所需数据窗口

#include "IStrategyService.h"
#include "StrategyServiceTypes.h"
#include "../../strategies/include/StrategyDefinitionTypes.h"
#include "MarketDataService.h"
#include "../../factor/include/factor_compute/IMarketDataView.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace domain::strategy {

class StrategyBase : public IRuntimeStrategy {
public:
    struct SignalResult { bool valid=false; bool isBuy=true; std::uint32_t instrumentId=0; double score=0.7; };

    StrategyBase(StrategyInstanceId instanceId,
                 const ::domain::strategies::StrategyCommonConfig& cfg)
        : m_instanceId(instanceId), m_commonCfg(cfg) {}
    virtual ~StrategyBase() = default;

    [[nodiscard]] StrategyInstanceId instanceId() const noexcept final { return m_instanceId; }
    [[nodiscard]] bool isEnabled() const noexcept final { return true; }
    [[nodiscard]] rules::RuleSetId ruleSetId() const noexcept final { return rules::kRuleSetAllPass; }
    [[nodiscard]] bool usesFactors() const noexcept final { return false; }
    [[nodiscard]] const ::domain::strategies::StrategyCommonConfig& cfg() const noexcept { return m_commonCfg; }

    [[nodiscard]] virtual int requiredLookbackBars() const noexcept = 0;
    [[nodiscard]] virtual SignalResult evaluateSymbol(
        const std::vector<double>& closePrices,
        std::uint32_t instrumentId,
        const RuntimeStrategyContext& ctx) = 0;

    void evaluate(const std::vector<RuntimeFactorSnapshot>&,
                  const RuntimeStrategyContext& ctx,
                  std::vector<StrategySignal>& out) final
    {
        const auto* view = static_cast<const factor::compute::IMarketDataView*>(ctx.historicalViewPtr());
        if (!view) return;
        auto closeMat = view->close();
        auto instruments = view->instruments();
        int rows = static_cast<int>(view->dates().size());
        int cols = static_cast<int>(instruments.size());
        int required = requiredLookbackBars();
        if (rows < required || cols == 0) return;
        int rowStride = closeMat.rowStride;
        int evalRow = ctx.currentEvaluationRow();
        int lastRow = (evalRow >= 0) ? evalRow : (rows - 1);
        int lookback = std::min(required, lastRow + 1);
        int kMax = std::max(1, m_commonCfg.maxPositions);

        // ── 因子候选池: 只评估池内标的 ──
        const bool usePool = ctx.hasCandidatePool();
        const auto& pool = ctx.candidatePool();

        std::vector<StrategySignal> all;
        all.reserve(usePool ? pool.size() : cols);
        for (int c=0; c<cols; ++c) {
            std::string sym;
            { const auto& ss = view->symbolStrings(); if (c<(int)ss.size()) sym=ss[c]; }
            if (sym.empty()) continue;
            if (usePool && !pool.count(sym)) continue;  // 不在候选池 → 跳过

            std::vector<double> prices(lookback);
            for (int i=0; i<lookback; ++i)
                prices[i] = static_cast<double>(closeMat.data[(lastRow-lookback+1+i)*rowStride + c]);
            { auto& d=domain::market::MarketDataService::instance().liveData(sym);
              if (d.valid()) { double lc=d.dailyBar().close(); if (lc>0) prices.push_back(lc); } }
            auto sig = evaluateSymbol(prices, instruments[c].value, ctx);
            if (sig.valid) {
                double lo=(std::max)(0.0,m_commonCfg.minWeightPerStock);
                double hi=(std::max)(lo,m_commonCfg.maxWeightPerStock);
                double w = lo + (hi-lo)*std::clamp(sig.score,0.0,1.0);
                all.push_back(StrategySignal(ctx.strategyInstanceId(),InstrumentId{sig.instrumentId},
                    sig.isBuy?RuntimeOrderSide::Buy:RuntimeOrderSide::Sell, sig.score, w, sym));
            }
        }
        std::sort(all.begin(),all.end(),[](auto& a,auto& b){
            if (a.side()==RuntimeOrderSide::Sell && b.side()==RuntimeOrderSide::Buy) return true;
            if (a.side()==RuntimeOrderSide::Buy && b.side()==RuntimeOrderSide::Sell) return false;
            return a.score()>b.score();
        });
        int buyCnt=0;
        for (auto& s:all) { if (s.side()==RuntimeOrderSide::Buy) { if (buyCnt++>=kMax) continue; } out.push_back(std::move(s)); }
    }

    static std::shared_ptr<IRuntimeStrategy> create(
        StrategyInstanceId instanceId, const StrategyCreationParams& params);

protected:
    StrategyInstanceId m_instanceId;
    ::domain::strategies::StrategyCommonConfig m_commonCfg;
    static constexpr int kSafety = 5;
};

// ═══ 子类 ═══
class TrendFollowingStrategy final : public StrategyBase {
public:
    using StrategyBase::StrategyBase;
    [[nodiscard]] int requiredLookbackBars() const noexcept override
    { return m_commonCfg.slowPeriod+1+kSafety; }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&,std::uint32_t,const RuntimeStrategyContext&) override;
};
class MeanReversionStrategy final : public StrategyBase {
public:
    using StrategyBase::StrategyBase;
    [[nodiscard]] int requiredLookbackBars() const noexcept override
    { return m_commonCfg.signalPeriod+1+kSafety; }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&,std::uint32_t,const RuntimeStrategyContext&) override;
};
class MomentumStrategy final : public StrategyBase {
public:
    using StrategyBase::StrategyBase;
    [[nodiscard]] int requiredLookbackBars() const noexcept override
    { return m_commonCfg.macdSlow+m_commonCfg.macdSignal+kSafety; }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&,std::uint32_t,const RuntimeStrategyContext&) override;
};
class ArbitrageStrategy final : public StrategyBase {
public:
    using StrategyBase::StrategyBase;
    [[nodiscard]] int requiredLookbackBars() const noexcept override
    { return m_commonCfg.bbPeriod+1+kSafety; }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&,std::uint32_t,const RuntimeStrategyContext&) override;
};
class EventDrivenStrategy final : public StrategyBase {
public:
    using StrategyBase::StrategyBase;
    [[nodiscard]] int requiredLookbackBars() const noexcept override { return 22+kSafety; }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&,std::uint32_t,const RuntimeStrategyContext&) override;
};
class HighFrequencyStrategy final : public StrategyBase {
public:
    using StrategyBase::StrategyBase;
    [[nodiscard]] int requiredLookbackBars() const noexcept override { return 12+kSafety; }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&,std::uint32_t,const RuntimeStrategyContext&) override;
};
class CustomStrategy final : public StrategyBase {
public:
    using StrategyBase::StrategyBase;
    [[nodiscard]] int requiredLookbackBars() const noexcept override
    { auto& c=m_commonCfg; return std::max({26,c.slowPeriod+1,c.macdSlow+c.macdSignal,c.signalPeriod+1})+kSafety; }
    [[nodiscard]] SignalResult evaluateSymbol(const std::vector<double>&,std::uint32_t,const RuntimeStrategyContext&) override;
};

} // namespace domain::strategy
