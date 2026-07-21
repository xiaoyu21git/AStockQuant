#include "../include/NonFactorStrategy.h"
#include <ta_libc.h>

namespace domain::strategy {
namespace {

using SignalResult = NonFactorStrategy::SignalResult;
using EvaluateFn = NonFactorStrategy::EvaluateFn;
using StrategyCommonConfig = ::domain::strategies::StrategyCommonConfig;

// ── 共享工具函数 ──

double computeRSI(const std::vector<double>& prices, int period) {
    int N = static_cast<int>(prices.size());
    if (N < period + 1) return -1.0;
    std::vector<double> out(N, 0);
    int b = 0, n = 0;
    if (TA_RSI(0, N-1, prices.data(), period, &b, &n, out.data()) != TA_SUCCESS || n == 0)
        return -1.0;
    return out[n-1];
}

bool checkMACD(const std::vector<double>& prices, bool up,
               int fast, int slow, int sigPeriod) {
    int N = static_cast<int>(prices.size());
    if (N < slow + 1) return false;
    std::vector<double> macd(N, 0), sig(N, 0), hist(N, 0);
    int b = 0, n = 0;
    if (TA_MACD(0, N-1, prices.data(), fast, slow, sigPeriod, &b, &n,
                macd.data(), sig.data(), hist.data()) != TA_SUCCESS || n < 2)
        return false;
    int p = n-2, c = n-1;
    return up ? (macd[p] <= sig[p] && macd[c] > sig[c])
              : (macd[p] >= sig[p] && macd[c] < sig[c]);
}

bool checkMACross(const std::vector<double>& prices, int fast, int slow, bool up) {
    int N = static_cast<int>(prices.size());
    if (N < slow + 1) return false;
    std::vector<double> fma(N, 0), sma(N, 0);
    int b = 0, n = 0;
    if (TA_SMA(0, N-1, prices.data(), fast, &b, &n, fma.data()) != TA_SUCCESS || n < 2) return false;
    if (TA_SMA(0, N-1, prices.data(), slow, &b, &n, sma.data()) != TA_SUCCESS || n < 2) return false;
    return up ? (fma[n-2] <= sma[n-2] && fma[n-1] > sma[n-1])
              : (fma[n-2] >= sma[n-2] && fma[n-1] < sma[n-1]);
}

bool checkMACDSignal(const std::vector<double>& prices, bool up,
                     int fast, int slow, int sigPeriod) {
    return checkMACD(prices, up, fast, slow, sigPeriod);
}

struct BBResult { bool valid = false; double upper = 0, middle = 0, lower = 0; };

BBResult computeBBands(const std::vector<double>& prices, int period, double nbDev) {
    int N = static_cast<int>(prices.size());
    if (N < period + 1) return {};
    std::vector<double> upper(N, 0), middle(N, 0), lower(N, 0);
    int b = 0, n = 0;
    if (TA_BBANDS(0, N-1, prices.data(), period, nbDev, nbDev, TA_MAType_SMA,
                  &b, &n, upper.data(), middle.data(), lower.data()) != TA_SUCCESS || n == 0)
        return {};
    BBResult r;
    r.valid = true; r.upper = upper[n-1]; r.middle = middle[n-1]; r.lower = lower[n-1];
    return r;
}

double computeATR(const std::vector<double>& prices, int period) {
    int N = static_cast<int>(prices.size());
    if (N < period + 1) return 0.0;
    std::vector<double> tr(N-1);
    for (int i = 1; i < N; ++i) tr[i-1] = std::abs(prices[i] - prices[i-1]);
    for (int i = period-1; i < N-1; ++i) {
        double sum = 0;
        for (int j = i-period+1; j <= i; ++j) sum += tr[j];
    }
    double sum = 0;
    for (int j = N-1-period+1; j <= N-2; ++j) sum += tr[j];
    return sum / period;
}

// ── 各策略指标实现 ──

SignalResult trendFollowingEval(const std::vector<double>& closePrices,
                                std::uint32_t instrumentId,
                                const RuntimeStrategyContext&,
                                const StrategyCommonConfig& cfg) {
    SignalResult sig;
    sig.instrumentId = instrumentId;
    int N = static_cast<int>(closePrices.size());
    if (N < cfg.slowPeriod + 1) return sig;

    std::vector<double> fma(N, 0), sma(N, 0);
    int b = 0, n = 0;
    if (TA_SMA(0, N-1, closePrices.data(), cfg.fastPeriod, &b, &n, fma.data()) != TA_SUCCESS || n < 2) return sig;
    if (TA_SMA(0, N-1, closePrices.data(), cfg.slowPeriod, &b, &n, sma.data()) != TA_SUCCESS || n < 2) return sig;

    int p = n-2, c = n-1;
    bool golden = (fma[p] <= sma[p] && fma[c] > sma[c]);
    bool dead   = (fma[p] >= sma[p] && fma[c] < sma[c]);

    double strength = sma[c] > 0.0 ? (fma[c] - sma[c]) / sma[c] : 0.0;
    if (golden) { sig.valid = true; sig.isBuy = true;  sig.score = 0.5 + std::min(0.45, std::abs(strength) * 10.0); }
    if (dead)   { sig.valid = true; sig.isBuy = false; sig.score = 0.3 + std::min(0.35, std::abs(strength) * 10.0); }
    return sig;
}

SignalResult meanReversionEval(const std::vector<double>& closePrices,
                               std::uint32_t instrumentId,
                               const RuntimeStrategyContext&,
                               const StrategyCommonConfig& cfg) {
    SignalResult sig;
    sig.instrumentId = instrumentId;
    double rsi = computeRSI(closePrices, cfg.signalPeriod);
    if (rsi < 30.0) { sig.valid = true; sig.isBuy = true;  sig.score = 0.70; }
    if (rsi > 70.0) { sig.valid = true; sig.isBuy = false; sig.score = 0.65; }
    return sig;
}

SignalResult momentumEval(const std::vector<double>& closePrices,
                          std::uint32_t instrumentId,
                          const RuntimeStrategyContext&,
                          const StrategyCommonConfig& cfg) {
    SignalResult sig;
    sig.instrumentId = instrumentId;
    bool golden = checkMACD(closePrices, true, cfg.macdFast, cfg.macdSlow, cfg.macdSignal);
    bool dead   = checkMACD(closePrices, false, cfg.macdFast, cfg.macdSlow, cfg.macdSignal);
    if (golden) { sig.valid = true; sig.isBuy = true;  sig.score = 0.80; }
    if (dead)   { sig.valid = true; sig.isBuy = false; sig.score = 0.60; }
    return sig;
}

SignalResult arbitrageEval(const std::vector<double>& closePrices,
                           std::uint32_t instrumentId,
                           const RuntimeStrategyContext&,
                           const StrategyCommonConfig& cfg) {
    SignalResult sig;
    sig.instrumentId = instrumentId;
    auto bb = computeBBands(closePrices, cfg.bbPeriod, cfg.bbStdDev);
    if (!bb.valid) return sig;
    double lastPrice = closePrices.back();
    double pctB = (bb.upper > bb.lower) ? (lastPrice - bb.lower) / (bb.upper - bb.lower) : 0.5;
    if (pctB < 0.1) { sig.valid = true; sig.isBuy = true;  sig.score = 0.70; }
    if (pctB > 0.9) { sig.valid = true; sig.isBuy = false; sig.score = 0.65; }
    return sig;
}

SignalResult eventDrivenEval(const std::vector<double>& closePrices,
                             std::uint32_t instrumentId,
                             const RuntimeStrategyContext&,
                             const StrategyCommonConfig&) {
    SignalResult sig;
    sig.instrumentId = instrumentId;
    int N = static_cast<int>(closePrices.size());
    if (N < 22) return sig;

    double recentAvg = 0, priorAvg = 0, priorVol = 0;
    for (int i = N-5; i < N; ++i)  recentAvg += closePrices[i];
    for (int i = N-22; i < N-5; ++i) priorAvg += closePrices[i];
    recentAvg /= 5.0; priorAvg /= 17.0;

    double priorMean = priorAvg;
    for (int i = N-22; i < N-5; ++i)
        priorVol += (closePrices[i] - priorMean) * (closePrices[i] - priorMean);
    priorVol = std::sqrt(priorVol / 17.0);

    double priceChg = (recentAvg - priorAvg) / (priorAvg > 0 ? priorAvg : 1.0);
    double atr = computeATR(closePrices, 14);

    bool eventUp   = priceChg > 0.03 && atr > priorVol * 1.5;
    bool eventDown = priceChg < -0.03 && atr > priorVol * 1.5;

    if (eventUp)   { sig.valid = true; sig.isBuy = true;  sig.score = 0.75; }
    if (eventDown) { sig.valid = true; sig.isBuy = false; sig.score = 0.60; }
    return sig;
}

SignalResult highFrequencyEval(const std::vector<double>& closePrices,
                               std::uint32_t instrumentId,
                               const RuntimeStrategyContext&,
                               const StrategyCommonConfig&) {
    SignalResult sig;
    sig.instrumentId = instrumentId;
    int N = static_cast<int>(closePrices.size());
    if (N < 12) return sig;

    double roc3 = (closePrices[N-1] - closePrices[N-4]) / (closePrices[N-4] > 0 ? closePrices[N-4] : 1.0);
    double roc6 = (closePrices[N-1] - closePrices[N-7]) / (closePrices[N-7] > 0 ? closePrices[N-7] : 1.0);

    int upBars = 0;
    for (int i = N-5; i < N; ++i)
        if (closePrices[i] > closePrices[i-1]) ++upBars;

    if (roc3 > 0.002 && roc6 > 0.001 && upBars >= 4)
        { sig.valid = true; sig.isBuy = true;  sig.score = 0.80; }
    if (roc3 < -0.002 && roc6 < -0.001 && upBars <= 1)
        { sig.valid = true; sig.isBuy = false; sig.score = 0.75; }
    return sig;
}

SignalResult customEval(const std::vector<double>& closePrices,
                        std::uint32_t instrumentId,
                        const RuntimeStrategyContext&,
                        const StrategyCommonConfig& cfg) {
    SignalResult sig;
    sig.instrumentId = instrumentId;
    int N = static_cast<int>(closePrices.size());
    if (N < 26) return sig;

    auto trendUp   = checkMACross(closePrices, cfg.fastPeriod, cfg.slowPeriod, true);
    auto trendDown = checkMACross(closePrices, cfg.fastPeriod, cfg.slowPeriod, false);
    double rsi = computeRSI(closePrices, cfg.signalPeriod);
    auto macdUp   = checkMACDSignal(closePrices, true, cfg.macdFast, cfg.macdSlow, cfg.macdSignal);
    auto macdDown = checkMACDSignal(closePrices, false, cfg.macdFast, cfg.macdSlow, cfg.macdSignal);

    int buyVotes = (trendUp ? 1 : 0) + (rsi > 0 && rsi < 40 ? 1 : 0) + (macdUp ? 1 : 0);
    int sellVotes = (trendDown ? 1 : 0) + (rsi > 60 ? 1 : 0) + (macdDown ? 1 : 0);

    if (buyVotes >= 2)  { sig.valid = true; sig.isBuy = true;  sig.score = 0.70 + buyVotes * 0.05; }
    if (sellVotes >= 2) { sig.valid = true; sig.isBuy = false; sig.score = 0.65 + sellVotes * 0.05; }
    return sig;
}

} // anonymous namespace

std::pair<NonFactorStrategy::EvaluateFn, int> NonFactorStrategy::makeIndicator(
    ::domain::strategies::StrategyBehaviorKind kind,
    const ::domain::strategies::StrategyCommonConfig& cfg)
{
    constexpr int kSafetyMargin = 5;
    switch (kind) {
    case ::domain::strategies::StrategyBehaviorKind::TrendFollowing:
        return {trendFollowingEval, cfg.slowPeriod + 1 + kSafetyMargin};
    case ::domain::strategies::StrategyBehaviorKind::MeanReversion:
        return {meanReversionEval, cfg.signalPeriod + 1 + kSafetyMargin};
    case ::domain::strategies::StrategyBehaviorKind::Momentum:
        return {momentumEval, cfg.macdSlow + cfg.macdSignal + kSafetyMargin};
    case ::domain::strategies::StrategyBehaviorKind::Arbitrage:
        return {arbitrageEval, cfg.bbPeriod + 1 + kSafetyMargin};
    case ::domain::strategies::StrategyBehaviorKind::EventDriven:
        return {eventDrivenEval, 22 + kSafetyMargin};
    case ::domain::strategies::StrategyBehaviorKind::HighFrequency:
        return {highFrequencyEval, 12 + kSafetyMargin};
    case ::domain::strategies::StrategyBehaviorKind::Custom:
        return {customEval,
            std::max({26, cfg.slowPeriod + 1, cfg.macdSlow + cfg.macdSignal, cfg.signalPeriod + 1}) + kSafetyMargin};
    default:
        return {trendFollowingEval, cfg.slowPeriod + 1 + kSafetyMargin};
    }
}

} // namespace domain::strategy
