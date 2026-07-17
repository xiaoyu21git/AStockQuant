// 回测变量提供者 — 实现
// 变量分两类:
//   精确可算: 均线比/均线斜率/量比/涨幅/持仓浮盈/市场宽度等 → 本文件实现
//   形态确认类(_confirmed/_score/theme_*): 需要形态识别或分时/题材数据,
//     第一版返回 nullopt(DataMissing), 由统计呈现"数据未就绪", 绝不猜测
// 市场状态 regime_state 编码阈值见 kRegime* 常量 (业务定义, 可调)

#include "RuleVariableProvider.h"
#include "RuleConditionEvaluator.h"

#include "foundation/log/logging.hpp"

#include <algorithm>
#include <cmath>

namespace domain::strategy::rules {

namespace {

constexpr int kMaSlopeWindow = 5;           // 均线斜率取 5 日变化率
constexpr int kRecentHighWindow = 60;       // 市场近期高点回撤窗口
// regime_state 编码阈值 (基于全市场站上 MA60 比例; 业务定义待校准)
constexpr double kRegimeBullBreadth = 0.60;
constexpr double kRegimeBearBreadth = 0.35;

/// 单列收盘均线 (视图列, 截止 lastRow, 窗口 n); 数据不足返回 nullopt
std::optional<double> columnMa(const factor::compute::NumericConstMatrixView& closeMat,
                               int lastRow, int col, int window)
{
    if (closeMat.data == nullptr || lastRow + 1 < window) return std::nullopt;
    double sum = 0.0;
    for (int i = 0; i < window; ++i) {
        const double v = static_cast<double>(
            closeMat.data[(lastRow - i) * closeMat.rowStride + col]);
        if (!(v > 0.0)) return std::nullopt;   // 停牌/缺数据不猜
        sum += v;
    }
    return sum / static_cast<double>(window);
}

double columnClose(const factor::compute::NumericConstMatrixView& closeMat, int row, int col)
{
    return static_cast<double>(closeMat.data[row * closeMat.rowStride + col]);
}

} // namespace

struct BacktestRuleVariableProvider::Impl {
    const factor::compute::IMarketDataView* view{nullptr};
    std::int32_t date{0};
    int lastRow{-1};
    const std::unordered_map<std::string, domain::trading::Position>* positions{nullptr};

    RuleMarketSnapshot market;
    bool marketReady{false};
    RuleCandidateContext candidate;

    // ── 每标的派生量 (按需计算) ──
    [[nodiscard]] std::optional<double> closeToMaRatio(int window) const
    {
        if (!view || candidate.colIndex < 0) return std::nullopt;
        auto closeMat = view->close();
        auto ma = columnMa(closeMat, lastRow, candidate.colIndex, window);
        if (!ma.has_value() || !(*ma > 0.0)) return std::nullopt;
        const double close = columnClose(closeMat, lastRow, candidate.colIndex);
        if (!(close > 0.0)) return std::nullopt;
        return close / *ma;
    }

    [[nodiscard]] std::optional<double> maTrendSlope(int window) const
    {
        if (!view || candidate.colIndex < 0) return std::nullopt;
        auto closeMat = view->close();
        auto maNow = columnMa(closeMat, lastRow, candidate.colIndex, window);
        auto maPrev = columnMa(closeMat, lastRow - kMaSlopeWindow, candidate.colIndex, window);
        if (!maNow.has_value() || !maPrev.has_value() || !(*maPrev > 0.0)) return std::nullopt;
        return (*maNow - *maPrev) / *maPrev;
    }

    [[nodiscard]] std::optional<double> volumeRatioToAvg(int window) const
    {
        if (!view || candidate.colIndex < 0 || lastRow < window) return std::nullopt;
        auto volMat = view->volume();
        if (volMat.data == nullptr) return std::nullopt;
        const double today = static_cast<double>(
            volMat.data[lastRow * volMat.rowStride + candidate.colIndex]);
        double sum = 0.0;
        for (int i = 1; i <= window; ++i)
            sum += static_cast<double>(
                volMat.data[(lastRow - i) * volMat.rowStride + candidate.colIndex]);
        const double avg = sum / static_cast<double>(window);
        if (!(avg > 0.0) || !(today >= 0.0)) return std::nullopt;
        return today / avg;
    }

    [[nodiscard]] std::optional<double> changePercent() const
    {
        if (!view || candidate.colIndex < 0 || lastRow < 1) return std::nullopt;
        auto closeMat = view->close();
        const double today = columnClose(closeMat, lastRow, candidate.colIndex);
        const double prev = columnClose(closeMat, lastRow - 1, candidate.colIndex);
        if (!(today > 0.0) || !(prev > 0.0)) return std::nullopt;
        return (today / prev - 1.0) * 100.0;
    }

    /// 持仓期内高点回撤 (entry 日 ~ 今)
    [[nodiscard]] std::optional<double> trailingDrawdownRatio() const
    {
        if (!view || candidate.colIndex < 0 || !candidate.isHolding) return std::nullopt;
        const int entryRow = lastRow - static_cast<int>(candidate.holdDays);
        if (entryRow < 0) return std::nullopt;
        auto closeMat = view->close();
        double peak = 0.0;
        for (int r = entryRow; r <= lastRow; ++r) {
            const double v = columnClose(closeMat, r, candidate.colIndex);
            if (v > peak) peak = v;
        }
        const double today = columnClose(closeMat, lastRow, candidate.colIndex);
        if (!(peak > 0.0) || !(today > 0.0)) return std::nullopt;
        return 1.0 - today / peak;
    }
};

BacktestRuleVariableProvider::BacktestRuleVariableProvider()
    : m_impl(std::make_shared<Impl>()) {}

void BacktestRuleVariableProvider::setDay(
    const factor::compute::IMarketDataView* view, std::int32_t date,
    const std::unordered_map<std::string, domain::trading::Position>* positions)
{
    m_impl->view = view;
    m_impl->date = date;
    m_impl->positions = positions;
    m_impl->marketReady = false;
    if (view) {
        const auto& dates = view->dates();
        m_impl->lastRow = -1;
        for (int i = static_cast<int>(dates.size()) - 1; i >= 0; --i) {
            if (dates[static_cast<std::size_t>(i)].value <= date) { m_impl->lastRow = i; break; }
        }
        if (m_impl->lastRow >= 0) {
            m_impl->market = computeMarketSnapshot(view, m_impl->lastRow, kRecentHighWindow);
            m_impl->marketReady = true;
        }
    }
}

void BacktestRuleVariableProvider::setCandidate(const RuleCandidateContext& ctx)
{
    m_impl->candidate = ctx;
}

std::optional<double> BacktestRuleVariableProvider::resolve(const std::string& varPath) const
{
    const auto& impl = *m_impl;
    if (!impl.view || impl.lastRow < 0) return std::nullopt;

    // ── candidate.* ──
    if (varPath == "candidate.close_to_ma20_ratio")  return impl.closeToMaRatio(20);
    if (varPath == "candidate.close_to_ma60_ratio")  return impl.closeToMaRatio(60);
    if (varPath == "candidate.close_to_ma250_ratio") return impl.closeToMaRatio(250);
    if (varPath == "candidate.ma20_trend_slope")     return impl.maTrendSlope(20);
    if (varPath == "candidate.ma60_trend_slope")     return impl.maTrendSlope(60);
    if (varPath == "candidate.ma250_trend_slope")    return impl.maTrendSlope(250);
    if (varPath == "candidate.midterm_trend_slope")  return impl.maTrendSlope(60);
    if (varPath == "candidate.volume_ratio_to_5d_avg")  return impl.volumeRatioToAvg(5);
    if (varPath == "candidate.volume_ratio_to_20d_avg") return impl.volumeRatioToAvg(20);
    if (varPath == "candidate.change_percent")       return impl.changePercent();

    // ── position.* (精确可算部分) ──
    if (varPath == "position.pnl_percent")
        return impl.candidate.isHolding ? std::optional<double>(impl.candidate.pnlPercent)
                                        : std::nullopt;
    if (varPath == "position.close_below_ma120_ratio") return impl.closeToMaRatio(120);
    if (varPath == "position.ma120_trend_slope")       return impl.maTrendSlope(120);
    if (varPath == "position.trailing_drawdown_ratio") return impl.trailingDrawdownRatio();
    if (varPath == "position.volume_recovery_ratio")   return impl.volumeRatioToAvg(5);
    if (varPath == "position.volume_expansion_ratio")  return impl.volumeRatioToAvg(20);

    // ── market.* (每日快照) ──
    if (impl.marketReady) {
        const auto& market = impl.market;
        if (varPath == "market.breadth_above_ma60_ratio")  return market.breadthAboveMa60Ratio;
        if (varPath == "market.drawdown_from_recent_high") return market.indexDrawdownFromRecentHigh;
        if (varPath == "market.index_above_ma120_ratio")   return market.indexClose > 0.0 && market.indexMa120 > 0.0
            ? std::optional<double>(market.indexClose / market.indexMa120) : std::nullopt;
        if (varPath == "market.regime_state")              return market.regimeState;
        if (varPath == "market.trend_strength_score")      return market.trendStrengthScore;
    }

    // 其余变量(形态确认/评分/题材类): 数据未就绪 — 显式 nullopt, 由统计上报
    return std::nullopt;
}

RuleMarketSnapshot computeMarketSnapshot(
    const factor::compute::IMarketDataView* view, int lastRow, int lookback)
{
    RuleMarketSnapshot snapshot;
    if (!view || lastRow < 1) return snapshot;
    auto closeMat = view->close();
    if (closeMat.data == nullptr) return snapshot;
    const int cols = static_cast<int>(view->instruments().size());
    if (cols == 0) return snapshot;

    // ── 宽度: 全市场站上 MA60 的比例 ──
    int above = 0, counted = 0;
    for (int c = 0; c < cols; ++c) {
        auto ma60 = columnMa(closeMat, lastRow, c, 60);
        if (!ma60.has_value()) continue;
        const double close = columnClose(closeMat, lastRow, c);
        if (!(close > 0.0)) continue;
        ++counted;
        if (close > *ma60) ++above;
    }
    if (counted > 0)
        snapshot.breadthAboveMa60Ratio = static_cast<double>(above) / counted;

    // ── 等权市场指数: 每日全市场平均收益累积, 近 lookback 高点回撤 ──
    // (从 lastRow-lookback 起点归一为 1.0)
    const int startRow = (std::max)(1, lastRow - lookback);
    double index = 1.0, peak = 1.0;
    for (int r = startRow; r <= lastRow; ++r) {
        double sumRet = 0.0; int n = 0;
        for (int c = 0; c < cols; ++c) {
            const double today = columnClose(closeMat, r, c);
            const double prev = columnClose(closeMat, r - 1, c);
            if (today > 0.0 && prev > 0.0) { sumRet += today / prev - 1.0; ++n; }
        }
        if (n > 0) index *= (1.0 + sumRet / n);
        if (index > peak) peak = index;
    }
    snapshot.indexClose = index;
    snapshot.indexDrawdownFromRecentHigh = peak > 0.0 ? 1.0 - index / peak : 0.0;

    // ── 市场状态编码 (阈值见 kRegime*) ──
    const char* regime = "sideways";
    if (snapshot.breadthAboveMa60Ratio >= kRegimeBullBreadth) regime = "bull";
    else if (snapshot.breadthAboveMa60Ratio <= kRegimeBearBreadth) regime = "bear";
    snapshot.regimeState = ruleStringValueCode(regime);
    snapshot.trendStrengthScore = snapshot.breadthAboveMa60Ratio;  // 第一版以宽度为趋势强度代理

    return snapshot;
}

} // namespace domain::strategy::rules
