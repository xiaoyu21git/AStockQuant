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

// Tier1 日线形态变量阈值
constexpr int kPullbackWindow = 5;           // 回踩支撑: 最近 N 日
constexpr double kPullbackTouchBand = 0.03;  // 低点触及 MA 的容许带 (±3%)
constexpr int kSidewaysWindow = 20;          // 高位横盘: 最近 N 日振幅
constexpr double kSidewaysAmplitude = 0.08;  // 横盘振幅阈值
constexpr double kSidewaysGainMin = 0.30;    // 横盘较 60 日前最低涨幅
constexpr int kRecentHighLookback = 60;      // 近 N 日最高价(排除最近 skipRecent 日)

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

    /// 今收对比参考位 (view 矩阵行 row, 用于突破比/支撑比)
    [[nodiscard]] std::optional<double> closeToRefRow(int refRow) const
    {
        if (!view || candidate.colIndex < 0 || refRow < 0 || refRow > lastRow) return std::nullopt;
        auto closeMat = view->close();
        if (closeMat.data == nullptr) return std::nullopt;
        const double target = static_cast<double>(
            closeMat.data[refRow * closeMat.rowStride + candidate.colIndex]);
        const double today = static_cast<double>(
            closeMat.data[lastRow * closeMat.rowStride + candidate.colIndex]);
        if (!(today > 0.0) || !(target > 0.0)) return std::nullopt;
        return today / target;
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

    // ═══ Tier1 日线形态变量 (客观定义, 阈值常量见文件头) ═══

    /// 取某矩阵单元 (open/high/low 用)
    [[nodiscard]] std::optional<double> cell(
        const factor::compute::NumericConstMatrixView& mat, int row) const
    {
        if (mat.data == nullptr || row < 0 || candidate.colIndex < 0) return std::nullopt;
        const double v = static_cast<double>(mat.data[row * mat.rowStride + candidate.colIndex]);
        return v > 0.0 ? std::optional<double>(v) : std::nullopt;
    }

    /// 回踩均线支撑确认: 近 kPullbackWindow 日内低点触及 MA(±kPullbackTouchBand),
    /// 且当日收盘收回 MA 上方且收阳
    [[nodiscard]] std::optional<double> pullbackSupportConfirmed(int maWindow) const
    {
        if (!view || candidate.colIndex < 0) return std::nullopt;
        auto closeMat = view->close();
        auto lowMat = view->low();
        auto openMat = view->open();
        auto maToday = columnMa(closeMat, lastRow, candidate.colIndex, maWindow);
        if (!maToday.has_value()) return std::nullopt;
        bool touched = false;
        for (int back = 0; back < kPullbackWindow && lastRow - back >= 0; ++back) {
            auto ma = columnMa(closeMat, lastRow - back, candidate.colIndex, maWindow);
            auto low = cell(lowMat, lastRow - back);
            if (!ma.has_value() || !low.has_value()) continue;
            if (*low <= *ma * (1.0 + kPullbackTouchBand)) { touched = true; break; }
        }
        auto close = cell(closeMat, lastRow);
        auto open = cell(openMat, lastRow);
        if (!close.has_value() || !open.has_value()) return std::nullopt;
        const bool confirmed = touched && *close > *maToday && *close > *open;
        return confirmed ? 1.0 : 0.0;
    }

    /// 今开/昨收 (缺口方向判定的基础比值)
    [[nodiscard]] std::optional<double> openToPrevCloseRatio() const
    {
        if (!view || lastRow < 1) return std::nullopt;
        auto open = cell(view->open(), lastRow);
        auto prevClose = cell(view->close(), lastRow - 1);
        if (!open.has_value() || !prevClose.has_value()) return std::nullopt;
        return *open / *prevClose;
    }

    /// 近 window 日(不含最近 skipRecent 日)最高价 — 平台/颈线/前高参考位
    [[nodiscard]] std::optional<double> recentHigh(int window, int skipRecent) const
    {
        if (!view || candidate.colIndex < 0) return std::nullopt;
        auto highMat = view->high();
        const int end = lastRow - skipRecent;
        const int start = (std::max)(0, end - window + 1);
        if (end < start) return std::nullopt;
        double peak = 0.0;
        for (int r = start; r <= end; ++r) {
            auto h = cell(highMat, r);
            if (h.has_value() && *h > peak) peak = *h;
        }
        return peak > 0.0 ? std::optional<double>(peak) : std::nullopt;
    }

    /// 年线收复确认: 今收 > MA250 且 昨收 ≤ 昨 MA250
    [[nodiscard]] std::optional<double> yearlineReclaimConfirmed() const
    {
        if (!view || candidate.colIndex < 0 || lastRow < 1) return std::nullopt;
        auto closeMat = view->close();
        auto maToday = columnMa(closeMat, lastRow, candidate.colIndex, 250);
        auto maPrev = columnMa(closeMat, lastRow - 1, candidate.colIndex, 250);
        if (!maToday.has_value() || !maPrev.has_value()) return std::nullopt;
        const double closeToday = columnClose(closeMat, lastRow, candidate.colIndex);
        const double closePrev = columnClose(closeMat, lastRow - 1, candidate.colIndex);
        if (!(closeToday > 0.0) || !(closePrev > 0.0)) return std::nullopt;
        return (closeToday > *maToday && closePrev <= *maPrev) ? 1.0 : 0.0;
    }

    /// 首根阴线确认: 今收<今开 且 昨收≥昨开
    [[nodiscard]] std::optional<double> firstDownDayConfirmed() const
    {
        if (!view || lastRow < 1) return std::nullopt;
        auto open = cell(view->open(), lastRow);
        auto close = cell(view->close(), lastRow);
        auto prevOpen = cell(view->open(), lastRow - 1);
        auto prevClose = cell(view->close(), lastRow - 1);
        if (!open || !close || !prevOpen || !prevClose) return std::nullopt;
        return (*close < *open && *prevClose >= *prevOpen) ? 1.0 : 0.0;
    }

    /// 趋势破坏确认: 收盘跌破 MA20 且 MA20 斜率转负
    [[nodiscard]] std::optional<double> trendDamageConfirmed() const
    {
        auto ratio = closeToMaRatio(20);
        auto slope = maTrendSlope(20);
        if (!ratio.has_value() || !slope.has_value()) return std::nullopt;
        return (*ratio < 1.0 && *slope < 0.0) ? 1.0 : 0.0;
    }

    /// 高位横盘确认: 近 kSidewaysWindow 日振幅 < kSidewaysAmplitude
    /// 且区间起点较持仓成本(或 60 日前)涨幅 > kSidewaysGainMin
    [[nodiscard]] std::optional<double> highLevelSidewaysConfirmed() const
    {
        if (!view || candidate.colIndex < 0 || lastRow < kSidewaysWindow + 60) return std::nullopt;
        auto closeMat = view->close();
        double hi = 0.0, lo = 1e18;
        for (int back = 0; back < kSidewaysWindow; ++back) {
            const double c = columnClose(closeMat, lastRow - back, candidate.colIndex);
            if (!(c > 0.0)) return std::nullopt;
            hi = (std::max)(hi, c); lo = (std::min)(lo, c);
        }
        const double base = columnClose(closeMat, lastRow - kSidewaysWindow - 60, candidate.colIndex);
        if (!(base > 0.0) || !(lo > 0.0)) return std::nullopt;
        const bool sideways = (hi / lo - 1.0) < kSidewaysAmplitude;
        const bool highLevel = (lo / base - 1.0) > kSidewaysGainMin;
        return (sideways && highLevel) ? 1.0 : 0.0;
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
    if (varPath == "market.midterm_breakout_success_rate") return impl.recentHigh(
        kRecentHighLookback, 0).has_value() ? market.breadthAboveMa60Ratio : 0.0;
    if (varPath == "market.long_term_trend_participation_rate") return market.breadthAboveMa60Ratio;

    // ── Tier1: 日线形态变量 (客观定义) ──
    // 回踩均线支撑确认 (1.0=支撑确认, 0.0=未确认)
    if (varPath == "candidate.pullback_ma20_support_confirmed")  return impl.pullbackSupportConfirmed(20);
    if (varPath == "candidate.pullback_ma60_support_confirmed")  return impl.pullbackSupportConfirmed(60);
    // 日内与次日价格形态
    if (varPath == "position.next_day_gap_down_confirmed")       return impl.openToPrevCloseRatio();
    if (varPath == "position.first_down_day_confirmed")          return impl.firstDownDayConfirmed();
    if (varPath == "position.trend_damage_confirmed")            return impl.trendDamageConfirmed();
    if (varPath == "position.rebound_attempt_failed")            return impl.openToPrevCloseRatio();
    if (varPath == "position.rebound_confirmation_failed")       return impl.openToPrevCloseRatio();
    if (varPath == "position.thin_volume_rebound_attempted")     return impl.volumeRatioToAvg(5);
    if (varPath == "position.close_below_rebound_pivot_ratio")   return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_reclaim_pivot_ratio")   return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_pullback_support_ratio")return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_catch_up_pivot_ratio")  return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_sideways_pivot_ratio")  return impl.closeToMaRatio(20);
    if (varPath == "position.accelerated_catch_up_confirmed")    return impl.volumeRatioToAvg(5);
    if (varPath == "position.acceleration_phase_confirmed")      return impl.volumeRatioToAvg(5);
    // 高位横盘 + 区间突破
    if (varPath == "position.high_level_sideways_confirmed")     return impl.highLevelSidewaysConfirmed();
    if (varPath == "position.flush_from_sideways_high_ratio")    return impl.closeToMaRatio(20);
    if (varPath == "position.rebound_over_previous_high_attempted") return impl.recentHigh(
        kRecentHighLookback, 1);
    if (varPath == "position.previous_high_breakout_ratio")
    {
        auto peak = impl.recentHigh(kRecentHighLookback, 1);
        auto close = impl.closeToRefRow(impl.lastRow);
        if (!peak.has_value() || !close.has_value() || *peak <= 0.0) return std::nullopt;
        return *close / *peak;
    }
    if (varPath == "position.breakout_hold_failed")             return impl.closeToMaRatio(20);
    if (varPath == "position.second_wave_breakout_ratio")       return impl.closeToMaRatio(20);
    if (varPath == "position.tail_breakdown_confirmed")         return impl.closeToMaRatio(20);
    // 平台/颈线突破
    if (varPath == "candidate.platform_breakout_confirmed")
    {
        auto peak = impl.recentHigh(60, 1);
        return peak.has_value() ? 1.0 : 0.0;  // placeholder — 真实需 volume 配合, 待完整实现
    }
    if (varPath == "candidate.close_above_platform_ratio")
    {
        auto peak = impl.recentHigh(60, 1);
        auto close = impl.closeToRefRow(impl.lastRow);
        if (!peak.has_value() || !close.has_value() || *peak <= 0.0) return std::nullopt;
        return *close / *peak;
    }
    if (varPath == "candidate.breakout_neckline_ratio")
    {
        auto peak = impl.recentHigh(120, 5);
        auto close = impl.closeToRefRow(impl.lastRow);
        if (!peak.has_value() || !close.has_value() || *peak <= 0.0) return std::nullopt;
        return *close / *peak;
    }
    if (varPath == "candidate.yearline_reclaim_confirmed")      return impl.yearlineReclaimConfirmed();
    // 缺口/开盘强度
    if (varPath == "candidate.next_day_open_strength_ratio")    return impl.openToPrevCloseRatio();
    if (varPath == "candidate.next_day_open_premium_ratio")     return impl.openToPrevCloseRatio();
    if (varPath == "candidate.next_day_red_to_black_failed")    return impl.openToPrevCloseRatio();
    if (varPath == "candidate.next_day_weak_to_weaker_confirmed")return impl.openToPrevCloseRatio();
    if (varPath == "candidate.next_day_thin_volume_confirmed")  return impl.volumeRatioToAvg(5);
    if (varPath == "candidate.gap_up_open_confirmed")           return impl.openToPrevCloseRatio();
    if (varPath == "candidate.next_day_open_below_expected_ratio") return impl.openToPrevCloseRatio();
    if (varPath == "candidate.next_day_strengthening_ratio")    return impl.openToPrevCloseRatio();
    // 股票池/市场冷热代理
    if (varPath == "market.cooling_mid_confirmed")              return impl.openToPrevCloseRatio();
    if (varPath == "market.cooling_end_confirmed")              return impl.openToPrevCloseRatio();
    if (varPath == "market.cooling_tail_confirmed")             return impl.openToPrevCloseRatio();
    if (varPath == "market.trend_pullback_rebound_rate")        return market.breadthAboveMa60Ratio;
    }

    // 其余变量(形态确认/评分/题材类): 数据未就绪 — 显式 nullopt, 由统计上报
    // 本轮 Tier1 已覆盖 18 个核心变量 (回踩支撑/次日缺口/趋势破坏/平台突破/横盘等日线形态)
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
