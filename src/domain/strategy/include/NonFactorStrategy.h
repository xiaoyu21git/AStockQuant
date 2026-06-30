#pragma once
// ═════════════════════════════════════════════════════════════════════════
// NonFactorStrategy — 非因子策略基类 + 具体策略子类 (纯 C++，零 Qt)
// 使用 TA-Lib 计算技术指标, 从 historicalView 获取 OHLCV 数据
// 子类: TrendFollowing(MA交叉) / MeanReversion(RSI) / Momentum(MACD)
// ═════════════════════════════════════════════════════════════════════════

#include "IStrategyService.h"
#include "StrategyServiceTypes.h"
#include "../../strategies/include/StrategyDefinitionTypes.h"
#include "../../factor/include/factor_compute/IMarketDataView.h"
#include "../../factor/include/factor_compute/CachedMarketDataView.h"

#include <ta_libc.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace domain::strategy {

// ═══ 基类 ═══
class NonFactorStrategy : public IRuntimeStrategy {
public:
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
        if (rows < 50 || cols == 0) return;

        int rowStride = closeMat.rowStride;
        // 回测时用 context 中的当前行号，实盘（-1）回退到最后一行
        int evalRow = context.currentEvaluationRow();
        int lastRow = (evalRow >= 0) ? evalRow : (rows - 1);
        int lookback = std::min(50, lastRow + 1);
        const int kMaxDailySignals = std::max(1, m_commonCfg.maxPositions);

        // 收集所有信号，按评分排序后限流
        std::vector<StrategySignal> allSignals;
        allSignals.reserve(cols);
        for (int c = 0; c < cols; ++c) {
            std::vector<double> closePrices(lookback);
            for (int i = 0; i < lookback; ++i)
                closePrices[i] = static_cast<double>(
                    closeMat.data[(lastRow - lookback + 1 + i) * rowStride + c]);

            SignalResult sig = evaluateSymbol(closePrices, instruments[c].value,
                                               context, m_commonCfg);
            if (sig.valid) {
                std::string realSymbol;
                if (auto* cv = dynamic_cast<const factor::compute::CachedMarketDataView*>(view)) {
                    const auto& syms = cv->symbolStrings();
                    if (c < static_cast<int>(syms.size()))
                        realSymbol = syms[static_cast<size_t>(c)];
                }
                allSignals.emplace_back(
                    context.strategyInstanceId(),
                    InstrumentId{sig.instrumentId},
                    sig.isBuy ? RuntimeOrderSide::Buy : RuntimeOrderSide::Sell,
                    sig.score, sig.weight,
                    realSymbol);
            }
        }

        // 卖单优先（平仓降低风险），买单按评分排序取 Top-N
        std::sort(allSignals.begin(), allSignals.end(),
            [](const StrategySignal& a, const StrategySignal& b) {
                // 卖单排前面
                if (a.side() == RuntimeOrderSide::Sell && b.side() == RuntimeOrderSide::Buy) return true;
                if (a.side() == RuntimeOrderSide::Buy && b.side() == RuntimeOrderSide::Sell) return false;
                // 同为买/卖，按评分降序
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

protected:
    struct SignalResult {
        bool valid = false;
        bool isBuy = true;
        std::uint32_t instrumentId = 0;
        double score = 0.7;
        double weight = 0.0;
    };

    /// @brief 子类实现：对单个标的判断买卖信号
    virtual SignalResult evaluateSymbol(const std::vector<double>&,
                                         std::uint32_t,
                                         const RuntimeStrategyContext&,
                                         const ::domain::strategies::StrategyCommonConfig&)
    { return {}; }

    StrategyInstanceId m_instanceId;
    ::domain::strategies::StrategyBehaviorKind m_kind;
    ::domain::strategies::StrategyCommonConfig m_commonCfg;
};

// ═══ 趋势跟踪 — MA 交叉 ═══
class TrendFollowingStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;

protected:
    SignalResult evaluateSymbol(const std::vector<double>& closePrices,
                                 std::uint32_t instrumentId,
                                 const RuntimeStrategyContext& ctx,
                                 const ::domain::strategies::StrategyCommonConfig& cfg) override
    {
        SignalResult sig;
        sig.instrumentId = instrumentId;
        sig.weight = std::min(1.0 / std::max(1, cfg.maxPositions), cfg.maxWeightPerStock);

        int N = static_cast<int>(closePrices.size());
        if (N < cfg.slowPeriod + 1) return sig;

        std::vector<double> fma(N,0), sma(N,0);
        int b=0, n=0;
        if (TA_SMA(0,N-1,closePrices.data(),cfg.fastPeriod,&b,&n,fma.data())!=TA_SUCCESS||n<2) return sig;
        if (TA_SMA(0,N-1,closePrices.data(),cfg.slowPeriod,&b,&n,sma.data())!=TA_SUCCESS||n<2) return sig;

        int p=n-2, c=n-1;
        bool golden = (fma[p] <= sma[p] && fma[c] > sma[c]);
        bool dead   = (fma[p] >= sma[p] && fma[c] < sma[c]);

        // 以快慢线价差作为信号强度，使排序限流时有区分度
        double strength = sma[c] > 0.0 ? (fma[c] - sma[c]) / sma[c] : 0.0;
        if (golden)  { sig.valid = true; sig.isBuy = true;  sig.score = 0.5 + std::min(0.45, std::abs(strength) * 10.0); }
        if (dead)    { sig.valid = true; sig.isBuy = false; sig.score = 0.3 + std::min(0.35, std::abs(strength) * 10.0); }
        return sig;
    }
};

// ═══ 均值回归 — RSI ═══
class MeanReversionStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;

protected:
    SignalResult evaluateSymbol(const std::vector<double>& closePrices,
                                 std::uint32_t instrumentId,
                                 const RuntimeStrategyContext& ctx,
                                 const ::domain::strategies::StrategyCommonConfig& cfg) override
    {
        SignalResult sig;
        sig.instrumentId = instrumentId;
        sig.weight = std::min(1.0 / std::max(1, cfg.maxPositions), cfg.maxWeightPerStock);

        double rsi = computeRSI(closePrices, cfg.signalPeriod);
        if (rsi < 30.0) { sig.valid = true; sig.isBuy = true;  sig.score = 0.70; }
        if (rsi > 70.0) { sig.valid = true; sig.isBuy = false; sig.score = 0.65; }
        return sig;
    }

private:
    static double computeRSI(const std::vector<double>& prices, int period) {
        int N = static_cast<int>(prices.size());
        if (N < period + 1) return -1.0;
        std::vector<double> out(N,0);
        int b = 0, n = 0;
        if (TA_RSI(0,N-1,prices.data(),period,&b,&n,out.data())!=TA_SUCCESS||n==0) return -1.0;
        return out[n-1];
    }
};

// ═══ 动量 — MACD ═══
class MomentumStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;

protected:
    SignalResult evaluateSymbol(const std::vector<double>& closePrices,
                                 std::uint32_t instrumentId,
                                 const RuntimeStrategyContext& ctx,
                                 const ::domain::strategies::StrategyCommonConfig& cfg) override
    {
        SignalResult sig;
        sig.instrumentId = instrumentId;
        sig.weight = std::min(1.0 / std::max(1, cfg.maxPositions), cfg.maxWeightPerStock);

        bool golden = checkMACD(closePrices, true, cfg.macdFast, cfg.macdSlow, cfg.macdSignal);
        bool dead   = checkMACD(closePrices, false, cfg.macdFast, cfg.macdSlow, cfg.macdSignal);
        if (golden) { sig.valid = true; sig.isBuy = true;  sig.score = 0.80; }
        if (dead)   { sig.valid = true; sig.isBuy = false; sig.score = 0.60; }
        return sig;
    }

private:
    static bool checkMACD(const std::vector<double>& prices, bool up,
                           int fast, int slow, int sigPeriod) {
        int N = static_cast<int>(prices.size());
        if (N < slow + 1) return false;
        std::vector<double> macd(N,0), sig(N,0), hist(N,0);
        int b = 0, n = 0;
        if (TA_MACD(0,N-1,prices.data(),fast,slow,sigPeriod,&b,&n,
                    macd.data(),sig.data(),hist.data())!=TA_SUCCESS||n<2) return false;
        int p = n-2, c = n-1;
        return up ? (macd[p]<=sig[p]&&macd[c]>sig[c]) : (macd[p]>=sig[p]&&macd[c]<sig[c]);
    }
};

// ═══ 套利 — 配对价差均值回归 ═══
class ArbitrageStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;

protected:
    SignalResult evaluateSymbol(const std::vector<double>& closePrices,
                                 std::uint32_t instrumentId,
                                 const RuntimeStrategyContext& ctx,
                                 const ::domain::strategies::StrategyCommonConfig& cfg) override
    {
        SignalResult sig;
        sig.instrumentId = instrumentId;
        sig.weight = std::min(1.0 / std::max(1, cfg.maxPositions), cfg.maxWeightPerStock);

        // 布林带 %B: (price - lower) / (upper - lower), < 0.1 → 超卖买入, > 0.9 → 超买卖出
        auto bb = computeBBands(closePrices, cfg.bbPeriod, cfg.bbStdDev);
        if (!bb.valid) return sig;

        double lastPrice = closePrices.back();
        double pctB = (bb.upper > bb.lower) ? (lastPrice - bb.lower) / (bb.upper - bb.lower) : 0.5;

        if (pctB < 0.1)  { sig.valid = true; sig.isBuy = true;  sig.score = 0.70; }
        if (pctB > 0.9)  { sig.valid = true; sig.isBuy = false; sig.score = 0.65; }
        return sig;
    }

private:
    struct BBResult { bool valid = false; double upper = 0, middle = 0, lower = 0; };

    static BBResult computeBBands(const std::vector<double>& prices, int period, double nbDev) {
        int N = static_cast<int>(prices.size());
        if (N < period + 1) return {};
        std::vector<double> upper(N,0), middle(N,0), lower(N,0);
        int b = 0, n = 0;
        if (TA_BBANDS(0,N-1,prices.data(),period,nbDev,nbDev,TA_MAType_SMA,
                      &b,&n,upper.data(),middle.data(),lower.data())!=TA_SUCCESS||n==0)
            return {};
        BBResult r;
        r.valid = true; r.upper = upper[n-1]; r.middle = middle[n-1]; r.lower = lower[n-1];
        return r;
    }
};

// ═══ 事件驱动 — 量价异动 ═══
class EventDrivenStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;

protected:
    SignalResult evaluateSymbol(const std::vector<double>& closePrices,
                                 std::uint32_t instrumentId,
                                 const RuntimeStrategyContext& ctx,
                                 const ::domain::strategies::StrategyCommonConfig& cfg) override
    {
        SignalResult sig;
        sig.instrumentId = instrumentId;
        sig.weight = std::min(1.0 / std::max(1, cfg.maxPositions), cfg.maxWeightPerStock);

        int N = static_cast<int>(closePrices.size());
        if (N < 22) return sig;

        // 最近 5 日 vs 前 20 日: 价格突破 + 波动率放大 → "事件"
        double recentAvg = 0, priorAvg = 0, recentVol = 0, priorVol = 0;
        for (int i = N-5; i < N; ++i)  recentAvg += closePrices[i];
        for (int i = N-22; i < N-5; ++i) priorAvg += closePrices[i];
        recentAvg /= 5.0; priorAvg /= 17.0;

        double priorMean = priorAvg;
        for (int i = N-22; i < N-5; ++i) priorVol += (closePrices[i]-priorMean)*(closePrices[i]-priorMean);
        priorVol = std::sqrt(priorVol / 17.0);

        double priceChg = (recentAvg - priorAvg) / (priorAvg > 0 ? priorAvg : 1.0);
        double atr = computeATR(closePrices, 14);

        // 价格突破 + 真实波幅放大 → 事件信号
        bool eventUp   = priceChg > 0.03 && atr > priorVol * 1.5;
        bool eventDown = priceChg < -0.03 && atr > priorVol * 1.5;

        if (eventUp)   { sig.valid = true; sig.isBuy = true;  sig.score = 0.75; }
        if (eventDown) { sig.valid = true; sig.isBuy = false; sig.score = 0.60; }
        return sig;
    }

private:
    static double computeATR(const std::vector<double>& prices, int period) {
        int N = static_cast<int>(prices.size());
        if (N < period + 1) return 0.0;
        // ATR 近似: 用收盘价变化幅度替代 (无 High/Low 数据)
        std::vector<double> tr(N-1);
        for (int i = 1; i < N; ++i) tr[i-1] = std::abs(prices[i] - prices[i-1]);
        std::vector<double> atr(N,0);
        int b = 0, n = 0;
        // 用 SMA of true range 近似 ATR
        for (int i = period-1; i < N-1; ++i) {
            double sum = 0;
            for (int j = i-period+1; j <= i; ++j) sum += tr[j];
            atr[i] = sum / period;
        }
        (void)b; (void)n;
        return atr[N-2];
    }
};

// ═══ 高频 — 微观结构 (1min bar 动量) ═══
class HighFrequencyStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;

protected:
    SignalResult evaluateSymbol(const std::vector<double>& closePrices,
                                 std::uint32_t instrumentId,
                                 const RuntimeStrategyContext& ctx,
                                 const ::domain::strategies::StrategyCommonConfig& cfg) override
    {
        SignalResult sig;
        sig.instrumentId = instrumentId;
        sig.weight = std::min(1.0 / std::max(1, cfg.maxPositions), cfg.maxWeightPerStock);

        int N = static_cast<int>(closePrices.size());
        if (N < 12) return sig;

        // 短期动量: 最近 3 根 K 线的价格变化率 + 方向一致性
        double roc3 = (closePrices[N-1] - closePrices[N-4]) / (closePrices[N-4] > 0 ? closePrices[N-4] : 1.0);
        double roc6 = (closePrices[N-1] - closePrices[N-7]) / (closePrices[N-7] > 0 ? closePrices[N-7] : 1.0);

        // 方向一致性: 最近 5 根 K 线有几根是阳线
        int upBars = 0;
        for (int i = N-5; i < N; ++i)
            if (closePrices[i] > closePrices[i-1]) ++upBars;

        // 短期动量 + 方向一致 → 追涨; 短期负向 + 一致 → 杀跌
        if (roc3 > 0.002 && roc6 > 0.001 && upBars >= 4)
            { sig.valid = true; sig.isBuy = true;  sig.score = 0.80; }
        if (roc3 < -0.002 && roc6 < -0.001 && upBars <= 1)
            { sig.valid = true; sig.isBuy = false; sig.score = 0.75; }
        return sig;
    }
};

// ═══ 自定义 — 多指标配置驱动 ═══
class CustomStrategy final : public NonFactorStrategy {
public:
    using NonFactorStrategy::NonFactorStrategy;

protected:
    SignalResult evaluateSymbol(const std::vector<double>& closePrices,
                                 std::uint32_t instrumentId,
                                 const RuntimeStrategyContext& ctx,
                                 const ::domain::strategies::StrategyCommonConfig& cfg) override
    {
        SignalResult sig;
        sig.instrumentId = instrumentId;
        sig.weight = std::min(1.0 / std::max(1, cfg.maxPositions), cfg.maxWeightPerStock);

        int N = static_cast<int>(closePrices.size());
        if (N < 26) return sig;

        // 多指标综合: MA(10)×MA(25) + RSI(14) + MACD — 至少两个同向才发信号
        auto trendUp   = checkMACross(closePrices, cfg.fastPeriod, cfg.slowPeriod, true);
        auto trendDown = checkMACross(closePrices, cfg.fastPeriod, cfg.slowPeriod, false);
        double rsi = computeRSI(closePrices, cfg.signalPeriod);
        auto macdUp   = checkMACDSignal(closePrices, true, cfg.macdFast, cfg.macdSlow, cfg.macdSignal);
        auto macdDown = checkMACDSignal(closePrices, false, cfg.macdFast, cfg.macdSlow, cfg.macdSignal);

        int buyVotes = (trendUp?1:0) + (rsi>0&&rsi<40?1:0) + (macdUp?1:0);
        int sellVotes = (trendDown?1:0) + (rsi>60?1:0) + (macdDown?1:0);

        if (buyVotes >= 2)  { sig.valid = true; sig.isBuy = true;  sig.score = 0.70 + buyVotes*0.05; }
        if (sellVotes >= 2) { sig.valid = true; sig.isBuy = false; sig.score = 0.65 + sellVotes*0.05; }
        return sig;
    }

private:
    static bool checkMACross(const std::vector<double>& prices, int fast, int slow, bool up) {
        int N = static_cast<int>(prices.size());
        if (N < slow + 1) return false;
        std::vector<double> fma(N,0), sma(N,0);
        int b=0, n=0;
        if (TA_SMA(0,N-1,prices.data(),fast,&b,&n,fma.data())!=TA_SUCCESS||n<2) return false;
        if (TA_SMA(0,N-1,prices.data(),slow,&b,&n,sma.data())!=TA_SUCCESS||n<2) return false;
        return up ? (fma[n-2]<=sma[n-2]&&fma[n-1]>sma[n-1])
                  : (fma[n-2]>=sma[n-2]&&fma[n-1]<sma[n-1]);
    }
    static double computeRSI(const std::vector<double>& prices, int period) {
        int N = static_cast<int>(prices.size());
        if (N < period + 1) return -1.0;
        std::vector<double> out(N,0); int b=0, n=0;
        if (TA_RSI(0,N-1,prices.data(),period,&b,&n,out.data())!=TA_SUCCESS||n==0) return -1.0;
        return out[n-1];
    }
    static bool checkMACDSignal(const std::vector<double>& prices, bool up,
                                  int fast, int slow, int sigPeriod) {
        int N = static_cast<int>(prices.size());
        if (N < slow + 1) return false;
        std::vector<double> macd(N,0), sig(N,0), hist(N,0);
        int b=0, n=0;
        if (TA_MACD(0,N-1,prices.data(),fast,slow,sigPeriod,&b,&n,
                    macd.data(),sig.data(),hist.data())!=TA_SUCCESS||n<2) return false;
        return up ? (macd[n-2]<=sig[n-2]&&macd[n-1]>sig[n-1])
                  : (macd[n-2]>=sig[n-2]&&macd[n-1]<sig[n-1]);
    }
};

} // namespace domain::strategy
