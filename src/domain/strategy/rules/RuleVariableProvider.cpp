// 回测变量提供者 — 实现
// 变量分两类:
//   精确可算: 均线比/均线斜率/量比/涨幅/持仓浮盈/市场宽度等 → 本文件实现
//   形态确认类(_confirmed/_score/theme_*): 需要形态识别或分时/题材数据,
//     第一版返回 nullopt(DataMissing), 由统计呈现"数据未就绪", 绝不猜测
// 市场状态 regime_state 编码阈值见 kRegime* 常量 (业务定义, 可调)

#include "RuleVariableProvider.h"
#include "RuleConditionEvaluator.h"

#include "foundation/Utils/DateUtils.h"
#include "foundation/log/logging.hpp"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "../../../infrastructure/include/database/ISqlDatabase.h"

#include <ta_libc.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace domain::strategy::rules {

namespace {

constexpr int kMaSlopeWindow = 5;           // 均线斜率取 5 日变化率
constexpr int kRecentHighWindow = 60;       // 市场近期高点回撤窗口
// regime_state 编码阈值: 基于全市场站上 MA60 比例的语义定义(不可为回测优化而扭曲)
constexpr double kRegimeBullBreadth = 0.55;   // ≥55% 标的站上 MA60 → 牛市(趋势向上)
constexpr double kRegimeBearBreadth = 0.35;   // ≤35% 标的站上 MA60 → 熊市(普跌)

// Tier1 日线形态变量阈值
constexpr int kPullbackWindow = 5;           // 回踩支撑: 最近 N 日
constexpr double kPullbackTouchBand = 0.03;  // 低点触及 MA 的容许带 (±3%)
constexpr int kSidewaysWindow = 20;          // 高位横盘: 最近 N 日振幅
constexpr double kSidewaysAmplitude = 0.08;  // 横盘振幅阈值
constexpr double kSidewaysGainMin = 0.30;    // 横盘较 60 日前最低涨幅
constexpr int kRecentHighLookback = 60;      // 近 N 日最高价(排除最近 skipRecent 日)

// Tier1 第二批: market 情绪/冷却状态阈值
constexpr double kEmotionHotMin     = 0.15;   // 20日等权涨幅>15%
constexpr double kEmotionWarmMin    = 0.05;   // 5%~15%
constexpr double kEmotionRepairMin  = -0.05;  // -5%~5%
constexpr double kEmotionCoolingMin = -0.15;  // -15%~-5%
// < -15% → panic
constexpr int kEmotionWindow = 20;            // 情绪判定回溯窗
constexpr double kCoolingDrawdownMin = 0.05;  // 冷却期回撤 >5%
constexpr double kCoolingEndDrawdownMin = 0.10;
constexpr double kCoolingTailRange = 0.02;     // 尾段振幅 <2%

// Tier1: 量比形态阈值
constexpr double kThinVolumeRatio = 0.5;       // 缩量阈值 (<0.5×均值)
constexpr double kVolumeSurgeRatio = 2.0;      // 放量阈值 (>2×均值)
constexpr double kAccelerationVolumeRatio = 1.5;

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
    // 当日全量龙头排名缓存: symbol(无后缀6位码) → best_rank
    std::unordered_map<std::string, int> leaderRankCache;
    // 当日分钟线预聚合缓存: symbol(无后缀6位码) → 分时统计
    struct MinuteBarAgg {
        double afternoonLow{0.0}, afternoonHigh{0.0}, afternoonClose{0.0};
        double morningLow{0.0}, morningHigh{0.0};
        int afternoonBars{0}, totalBars{0};
        bool hasData{false};
    };
    std::unordered_map<std::string, MinuteBarAgg> minuteBarCache;
    RuleCandidateContext candidate;

    // ── 蜡烛形态缓存 (惰性批量计算) ──
    bool candlePatternsEnabled{false};
    bool conceptQueriesEnabled{false};  // 有规则引用 concept.* 时才开启
    mutable std::unordered_map<std::string, std::optional<double>> candleCache;
    mutable int candleCacheColIndex{-1};
    mutable int candleCacheLastRow{-1};
    static std::once_flag s_taInitFlag;

    void ensureCandleCache() const {
        if (!candlePatternsEnabled || !view || candidate.colIndex < 0) return;
        if (candleCacheColIndex == candidate.colIndex && candleCacheLastRow == lastRow)
            return;  // 缓存命中
        candleCache.clear();
        candleCacheColIndex = candidate.colIndex;
        candleCacheLastRow = lastRow;
        computeCandlePatterns();
    }

    void computeCandlePatterns() const;

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

    // ═══ Tier1 第二批: market 情绪/冷却 + 量比形态 + 前高比值 ═══

    // ═══ Tier2 评分变量 (业务定义公式) ═══

    /// 承接强度 (持仓/候选共用): 日内位置40+量比30+价格强度30, 0-100
    [[nodiscard]] std::optional<double> acceptanceStrengthScore() const
    {
        if (!view || candidate.colIndex < 0) return std::nullopt;
        auto closeMat = view->close();
        auto highMat = view->high();
        auto lowMat = view->low();
        auto closeVal = cell(closeMat, lastRow);
        auto highVal = cell(highMat, lastRow);
        auto lowVal = cell(lowMat, lastRow);
        if (!closeVal || !highVal || !lowVal) return std::nullopt;

        // 日内相对位置: (close-low)/(high-low) × 40
        double intraPos = 20.0;  // 一字板默认中间值
        if (*highVal > *lowVal + 1e-9)
            intraPos = (*closeVal - *lowVal) / (*highVal - *lowVal) * 40.0;

        // 量比: min(量比,2.0)/2.0 × 30
        auto volRatio = volumeRatioToAvg(5);
        double volScore = volRatio.has_value()
            ? (std::min)(*volRatio, 2.0) / 2.0 * 30.0 : 15.0;

        // 价格强度: clamp(收盘/MA5-1, 0, 0.1) × 10 × 30 → max 30
        auto ma5Ratio = closeToMaRatio(5);
        double priceScore = 15.0;
        if (ma5Ratio.has_value()) {
            double raw = (*ma5Ratio - 1.0) * 10.0;  // dev 1%→0.1
            priceScore = (std::min)((std::max)(raw, 0.0), 1.0) * 30.0;
        }
        return std::clamp(intraPos + volScore + priceScore, 0.0, 100.0);
    }

    /// 趋势健康度: MA20/60 斜率向上+价格在均线上方, 每项25分, 0-100
    [[nodiscard]] std::optional<double> trendHealthScore() const
    {
        if (!view || candidate.colIndex < 0) return std::nullopt;
        auto closeMat = view->close();
        int score = 0;
        // MA20 斜率向上
        auto ma20Now = columnMa(closeMat, lastRow, candidate.colIndex, 20);
        auto ma20Prev = columnMa(closeMat, lastRow - kMaSlopeWindow, candidate.colIndex, 20);
        if (ma20Now && ma20Prev && *ma20Now > *ma20Prev) score += 25;
        // MA60 斜率向上
        auto ma60Now = columnMa(closeMat, lastRow, candidate.colIndex, 60);
        auto ma60Prev = columnMa(closeMat, lastRow - kMaSlopeWindow, candidate.colIndex, 60);
        if (ma60Now && ma60Prev && *ma60Now > *ma60Prev) score += 25;
        // 收盘 > MA20
        auto ma20R = closeToMaRatio(20);
        if (ma20R && *ma20R > 1.0) score += 25;
        // 收盘 > MA60
        auto ma60R = closeToMaRatio(60);
        if (ma60R && *ma60R > 1.0) score += 25;
        return static_cast<double>(score);
    }

    /// market.emotion_cycle: 等权指数20日涨跌幅 → panic/cooling/repair/warm/hot
    [[nodiscard]] std::optional<double> marketEmotionCycle() const
    {
        if (!marketReady) return std::nullopt;
        const double ret = market.indexClose > 1.0 ? (market.indexClose - 1.0) : 0.0;
        const char* label = "repair";
        if (ret > kEmotionHotMin)          label = "hot";
        else if (ret > kEmotionWarmMin)    label = "warm";
        else if (ret > kEmotionRepairMin)  label = "repair";
        else if (ret > kEmotionCoolingMin) label = "cooling";
        else                               label = "panic";
        return ruleStringValueCode(label);
    }

    /// market.emotion_repair_confirmed: 情绪处于 repair 或更暖
    [[nodiscard]] std::optional<double> emotionRepairConfirmed() const
    {
        auto ec = marketEmotionCycle();
        if (!ec.has_value()) return std::nullopt;
        const double repairCode = ruleStringValueCode("repair");
        const double warmCode = ruleStringValueCode("warm");
        const double hotCode = ruleStringValueCode("hot");
        return (*ec >= repairCode || *ec == warmCode || *ec == hotCode) ? 1.0 : 0.0;
    }

    /// 缩量形态: 今量 < 5日均量 × kThinVolumeRatio
    [[nodiscard]] std::optional<double> thinVolumeConfirmed() const
    {
        auto vr = volumeRatioToAvg(5);
        if (!vr.has_value()) return std::nullopt;
        return *vr < kThinVolumeRatio ? 1.0 : 0.0;
    }

    /// 今收 / 昨收
    [[nodiscard]] std::optional<double> closeToPrevCloseRatio() const
    {
        if (!view || candidate.colIndex < 0 || lastRow < 1) return std::nullopt;
        auto closeMat = view->close();
        const double today = columnClose(closeMat, lastRow, candidate.colIndex);
        const double prev = columnClose(closeMat, lastRow - 1, candidate.colIndex);
        if (!(today > 0.0) || !(prev > 0.0)) return std::nullopt;
        return today / prev;
    }

    // ═══ Tier1 补齐: K线组合形态 helpers ═══

    /// 吞没阳线确认: 今收<今开(阴线) 且 昨收>=昨开(阳线被吞)
    [[nodiscard]] std::optional<double> engulfingBearishConfirmed() const
    {
        if (!view || lastRow < 1) return std::nullopt;
        auto open = cell(view->open(), lastRow);
        auto close = cell(view->close(), lastRow);
        auto prevOpen = cell(view->open(), lastRow - 1);
        auto prevClose = cell(view->close(), lastRow - 1);
        if (!open||!close||!prevOpen||!prevClose) return std::nullopt;
        return (*close < *open && *prevClose >= *prevOpen) ? 1.0 : 0.0;
    }

    /// 弱修复确认: 收盘<MA20 且缩量 (<0.8×5日均量)
    [[nodiscard]] std::optional<double> weakRepairConfirmed() const
    {
        auto ma = closeToMaRatio(20);
        auto vr = volumeRatioToAvg(5);
        if (!ma||!vr) return std::nullopt;
        return (*ma < 1.0 && *vr < 0.8) ? 1.0 : 0.0;
    }

    /// 弱转强尝试: 今开>昨收(高开) 或 今收>昨收(收红)
    [[nodiscard]] std::optional<double> weakToStrongAttempted() const
    {
        auto o2p = openToPrevCloseRatio();
        auto c2p = closeToPrevCloseRatio();
        if (!o2p||!c2p) return std::nullopt;
        return (*o2p > 1.0 || *c2p > 1.0) ? 1.0 : 0.0;
    }

    /// 弱转强失败: 高开后收低
    [[nodiscard]] std::optional<double> weakToStrongFailed() const
    {
        auto o2p = openToPrevCloseRatio();
        auto c2p = closeToPrevCloseRatio();
        if (!o2p||!c2p) return std::nullopt;
        return (*o2p > 1.005 && *c2p < *o2p) ? 1.0 : 0.0;
    }

    // ═══ 涨停/打板检测 (纯日线可算) ═══

    /// 涨停基准价: 昨收; 涨停线: 昨收×1.10(主板)/1.05(ST)/1.20(科创)
    [[nodiscard]] std::optional<double> prevClose() const
    {
        if (!view || candidate.colIndex < 0 || lastRow < 1) return std::nullopt;
        auto closeMat = view->close();
        return columnClose(closeMat, lastRow - 1, candidate.colIndex);
    }

    /// 是否封板: 收盘价 ≥ 涨停价×0.995
    [[nodiscard]] std::optional<double> isAtLimitUp() const
    {
        auto pc = prevClose();
        auto c = cell(view->close(), lastRow);
        if (!pc||!c||*pc<=0) return std::nullopt;
        const double limitPrice = *pc * 1.10;  // 主板10%涨停线
        return *c >= limitPrice * 0.995 ? 1.0 : 0.0;
    }

    /// 一字板: 开=高=低=收 且 封板
    [[nodiscard]] std::optional<double> isOneWordBoard() const
    {
        if (!view || candidate.colIndex < 0) return std::nullopt;
        auto o = cell(view->open(), lastRow);
        auto h = cell(view->high(), lastRow);
        auto l = cell(view->low(), lastRow);
        auto c = cell(view->close(), lastRow);
        if (!o||!h||!l||!c) return std::nullopt;
        bool board = (*o==*h && *h==*l && *l==*c);
        if (!board) return 0.0;
        auto limit = isAtLimitUp();
        return limit && *limit > 0.5 ? 1.0 : 0.0;
    }

    /// 炸板: 最高价触及涨停价 且 收盘未封板
    [[nodiscard]] std::optional<double> boardBreakConfirmed() const
    {
        auto pc = prevClose();
        auto h = cell(view->high(), lastRow);
        auto c = cell(view->close(), lastRow);
        if (!pc||!h||!c||*pc<=0) return std::nullopt;
        const double limitPrice = *pc * 1.10;
        return (*h >= limitPrice * 0.995 && *c < limitPrice * 0.995) ? 1.0 : 0.0;
    }

    /// 炸板回封: 曾触及涨停+最终封板
    [[nodiscard]] std::optional<double> resealConfirmed() const
    {
        auto bb = boardBreakConfirmed();
        auto limit = isAtLimitUp();
        if (!bb||!limit) return std::nullopt;
        return (*bb > 0.5 && *limit > 0.5) ? 1.0 : 0.0;  // 板开过但封回去了
    }

    // ═══ 分钟线分时形态 (从 minuteBarCache 实查) ═══

    /// 午后回流: 上午下跌→下午V反 → 1.0, 否则 0.0
    [[nodiscard]] std::optional<double> afternoonReflowConfirmed() const
    {
        auto it = minuteBarCache.find(candidate.code);
        if (it == minuteBarCache.end() || !it->second.hasData) return std::nullopt;
        const auto& mb = it->second;
        if (mb.afternoonBars < 10 || mb.morningLow <= 0) return std::nullopt;
        // 上午低开低走(morning_low < morning_close_approx) + 下午收于上午高点上方
        return (mb.morningLow < mb.morningHigh * 0.98 && mb.afternoonClose > mb.morningHigh * 0.99)
            ? std::optional<double>(1.0) : std::optional<double>(0.0);
    }

    /// 尾盘修复: 午后最后阶段强势收回
    [[nodiscard]] std::optional<double> tailRepairConfirmed() const
    {
        auto it = minuteBarCache.find(candidate.code);
        if (it == minuteBarCache.end() || !it->second.hasData) return std::nullopt;
        const auto& mb = it->second;
        if (mb.afternoonBars < 10) return std::nullopt;
        double afternoonRange = mb.afternoonHigh - mb.afternoonLow;
        if (afternoonRange <= 0) return std::nullopt;
        double closePos = (mb.afternoonClose - mb.afternoonLow) / afternoonRange;
        return closePos > 0.90 ? std::optional<double>(1.0) : std::optional<double>(0.0);
    }

    /// 日内冲高回落: 上午高→下午低
    [[nodiscard]] std::optional<double> intradayFlushConfirmed() const
    {
        auto it = minuteBarCache.find(candidate.code);
        if (it == minuteBarCache.end() || !it->second.hasData) return std::nullopt;
        const auto& mb = it->second;
        if (mb.morningHigh <= 0 || mb.afternoonLow <= 0) return std::nullopt;
        double flushRatio = (mb.morningHigh - mb.afternoonLow) / mb.morningHigh;
        return flushRatio > 0.03 ? std::optional<double>(1.0) : std::optional<double>(0.0);
    }

    /// 昨曾封板: 昨日收盘≥昨日涨停价×0.995
    [[nodiscard]] std::optional<double> yesterdayAtLimitUp() const
    {
        if (!view || candidate.colIndex < 0 || lastRow < 1) return std::nullopt;
        auto closeMat = view->close();
        const double prevClose = columnClose(closeMat, lastRow - 1, candidate.colIndex);
        if (!(prevClose > 0)) return std::nullopt;
        double prevPrevClose = 0.0;
        if (lastRow >= 2) prevPrevClose = columnClose(closeMat, lastRow - 2, candidate.colIndex);
        if (!(prevPrevClose > 0)) return std::nullopt;
        const double limitPrice = prevPrevClose * 1.10;
        return prevClose >= limitPrice * 0.995 ? 1.0 : 0.0;
    }
};

// 静态成员外部定义 (MSVC 要求)
std::once_flag BacktestRuleVariableProvider::Impl::s_taInitFlag;

BacktestRuleVariableProvider::BacktestRuleVariableProvider()
    : m_impl(std::make_shared<Impl>()) {}

void BacktestRuleVariableProvider::setCandlePatternsEnabled(bool enabled)
{
    m_impl->candlePatternsEnabled = enabled;
    if (!enabled) m_impl->candleCache.clear();
}

void BacktestRuleVariableProvider::setConceptQueriesEnabled(bool enabled)
{
    m_impl->conceptQueriesEnabled = enabled;
}

// ── 蜡烛形态批量计算 ──
// 使用宏内联展开避免 TA-Lib CDL 函数签名不统一的问题
// 大部分 CDL 函数无 penetration 参数，少数(约6个)有

namespace {

#define EVAL_CDL_8(NAME, FUNC) \
    do { \
        int _beg = 0, _nb = 0; \
        std::vector<int> _out(nRows); \
        TA_RetCode _ret = FUNC(0, nRows-1, _o.data(), _h.data(), _l.data(), _c.data(), &_beg, &_nb, _out.data()); \
        if (_ret == TA_SUCCESS && _nb > 0) { \
            int _last = _beg + _nb - 1; \
            if (_last >= 0 && _last < static_cast<int>(_out.size())) \
                candleCache[NAME] = static_cast<double>(_out[static_cast<std::size_t>(_last)]); \
            else candleCache[NAME] = std::nullopt; \
        } else { candleCache[NAME] = std::nullopt; } \
    } while(0)

#define EVAL_CDL_9(NAME, FUNC, PEN) \
    do { \
        int _beg = 0, _nb = 0; \
        std::vector<int> _out(nRows); \
        TA_RetCode _ret = FUNC(0, nRows-1, _o.data(), _h.data(), _l.data(), _c.data(), PEN, &_beg, &_nb, _out.data()); \
        if (_ret == TA_SUCCESS && _nb > 0) { \
            int _last = _beg + _nb - 1; \
            if (_last >= 0 && _last < static_cast<int>(_out.size())) \
                candleCache[NAME] = static_cast<double>(_out[static_cast<std::size_t>(_last)]); \
            else candleCache[NAME] = std::nullopt; \
        } else { candleCache[NAME] = std::nullopt; } \
    } while(0)

} // namespace

void BacktestRuleVariableProvider::Impl::computeCandlePatterns() const
{
    if (!view || candidate.colIndex < 0 || lastRow < 1) return;

    std::call_once(s_taInitFlag, []() { TA_Initialize(); });

    const int nRows = lastRow + 1;
    const int col = candidate.colIndex;
    auto openMat  = view->open();
    auto highMat  = view->high();
    auto lowMat   = view->low();
    auto closeMat = view->close();
    const std::size_t colStride = static_cast<std::size_t>(col);

    std::vector<double> _o(nRows), _h(nRows), _l(nRows), _c(nRows);
    for (int r = 0; r < nRows; ++r) {
        const std::size_t offset = static_cast<std::size_t>(r) * openMat.rowStride + colStride;
        _o[r] = static_cast<double>(openMat.data[offset]);
        _h[r] = static_cast<double>(highMat.data[offset]);
        _l[r] = static_cast<double>(lowMat.data[offset]);
        _c[r] = static_cast<double>(closeMat.data[offset]);
    }

    // ── 61 CDL patterns ──
    // 8-param (no penetration): majority
    EVAL_CDL_8("candle.cdl_2crows",            TA_CDL2CROWS);
    EVAL_CDL_8("candle.cdl_3blackcrows",       TA_CDL3BLACKCROWS);
    EVAL_CDL_8("candle.cdl_3inside",           TA_CDL3INSIDE);
    EVAL_CDL_8("candle.cdl_3linestrike",       TA_CDL3LINESTRIKE);
    EVAL_CDL_8("candle.cdl_3outside",          TA_CDL3OUTSIDE);
    EVAL_CDL_8("candle.cdl_3starsinsouth",     TA_CDL3STARSINSOUTH);
    EVAL_CDL_8("candle.cdl_3whitesoldiers",    TA_CDL3WHITESOLDIERS);
    EVAL_CDL_8("candle.cdl_advanceblock",      TA_CDLADVANCEBLOCK);
    EVAL_CDL_8("candle.cdl_belthold",          TA_CDLBELTHOLD);
    EVAL_CDL_8("candle.cdl_breakaway",         TA_CDLBREAKAWAY);
    EVAL_CDL_8("candle.cdl_closingmarubozu",   TA_CDLCLOSINGMARUBOZU);
    EVAL_CDL_8("candle.cdl_concealbabyswall",  TA_CDLCONCEALBABYSWALL);
    EVAL_CDL_8("candle.cdl_counterattack",     TA_CDLCOUNTERATTACK);
    EVAL_CDL_8("candle.cdl_doji",              TA_CDLDOJI);
    EVAL_CDL_8("candle.cdl_dojistar",          TA_CDLDOJISTAR);
    EVAL_CDL_8("candle.cdl_dragonflydoji",     TA_CDLDRAGONFLYDOJI);
    EVAL_CDL_8("candle.cdl_engulfing",         TA_CDLENGULFING);
    EVAL_CDL_8("candle.cdl_gapsidesidewhite",  TA_CDLGAPSIDESIDEWHITE);
    EVAL_CDL_8("candle.cdl_gravestonedoji",    TA_CDLGRAVESTONEDOJI);
    EVAL_CDL_8("candle.cdl_hammer",            TA_CDLHAMMER);
    EVAL_CDL_8("candle.cdl_hangingman",        TA_CDLHANGINGMAN);
    EVAL_CDL_8("candle.cdl_harami",            TA_CDLHARAMI);
    EVAL_CDL_8("candle.cdl_haramicross",       TA_CDLHARAMICROSS);
    EVAL_CDL_8("candle.cdl_highwave",          TA_CDLHIGHWAVE);
    EVAL_CDL_8("candle.cdl_hikkake",           TA_CDLHIKKAKE);
    EVAL_CDL_8("candle.cdl_hikkakemod",        TA_CDLHIKKAKEMOD);
    EVAL_CDL_8("candle.cdl_homingpigeon",      TA_CDLHOMINGPIGEON);
    EVAL_CDL_8("candle.cdl_identical3crows",   TA_CDLIDENTICAL3CROWS);
    EVAL_CDL_8("candle.cdl_inneck",            TA_CDLINNECK);
    EVAL_CDL_8("candle.cdl_invertedhammer",    TA_CDLINVERTEDHAMMER);
    EVAL_CDL_8("candle.cdl_kicking",           TA_CDLKICKING);
    EVAL_CDL_8("candle.cdl_kickingbylength",   TA_CDLKICKINGBYLENGTH);
    EVAL_CDL_8("candle.cdl_ladderbottom",      TA_CDLLADDERBOTTOM);
    EVAL_CDL_8("candle.cdl_longleggeddoji",    TA_CDLLONGLEGGEDDOJI);
    EVAL_CDL_8("candle.cdl_longline",          TA_CDLLONGLINE);
    EVAL_CDL_8("candle.cdl_marubozu",          TA_CDLMARUBOZU);
    EVAL_CDL_8("candle.cdl_matchinglow",       TA_CDLMATCHINGLOW);
    EVAL_CDL_8("candle.cdl_onneck",            TA_CDLONNECK);
    EVAL_CDL_8("candle.cdl_piercing",          TA_CDLPIERCING);
    EVAL_CDL_8("candle.cdl_rickshawman",       TA_CDLRICKSHAWMAN);
    EVAL_CDL_8("candle.cdl_risefall3methods",  TA_CDLRISEFALL3METHODS);
    EVAL_CDL_8("candle.cdl_separatinglines",   TA_CDLSEPARATINGLINES);
    EVAL_CDL_8("candle.cdl_shootingstar",      TA_CDLSHOOTINGSTAR);
    EVAL_CDL_8("candle.cdl_shortline",         TA_CDLSHORTLINE);
    EVAL_CDL_8("candle.cdl_spinningtop",       TA_CDLSPINNINGTOP);
    EVAL_CDL_8("candle.cdl_stalledpattern",    TA_CDLSTALLEDPATTERN);
    EVAL_CDL_8("candle.cdl_sticksandwich",     TA_CDLSTICKSANDWICH);
    EVAL_CDL_8("candle.cdl_takuri",            TA_CDLTAKURI);
    EVAL_CDL_8("candle.cdl_tasukigap",         TA_CDLTASUKIGAP);
    EVAL_CDL_8("candle.cdl_thrusting",         TA_CDLTHRUSTING);
    EVAL_CDL_8("candle.cdl_tristar",           TA_CDLTRISTAR);
    EVAL_CDL_8("candle.cdl_unique3river",      TA_CDLUNIQUE3RIVER);
    EVAL_CDL_8("candle.cdl_upsidegap2crows",   TA_CDLUPSIDEGAP2CROWS);
    EVAL_CDL_8("candle.cdl_xsidegap3methods",  TA_CDLXSIDEGAP3METHODS);

    // 9-param (with penetration): ~6 functions
    EVAL_CDL_9("candle.cdl_abandonedbaby",     TA_CDLABANDONEDBABY,     0.3);
    EVAL_CDL_9("candle.cdl_darkcloudcover",    TA_CDLDARKCLOUDCOVER,    0.3);
    EVAL_CDL_9("candle.cdl_eveningdojistar",   TA_CDLEVENINGDOJISTAR,   0.3);
    EVAL_CDL_9("candle.cdl_eveningstar",       TA_CDLEVENINGSTAR,       0.3);
    EVAL_CDL_9("candle.cdl_morningdojistar",   TA_CDLMORNINGDOJISTAR,   0.3);
    EVAL_CDL_9("candle.cdl_morningstar",       TA_CDLMORNINGSTAR,       0.3);
    EVAL_CDL_9("candle.cdl_mathold",           TA_CDLMATHOLD,           0.3);
}

void BacktestRuleVariableProvider::setDay(
    const factor::compute::IMarketDataView* view, std::int32_t date,
    const std::unordered_map<std::string, domain::trading::Position>* positions)
{
    m_impl->view = view;
    m_impl->date = date;
    m_impl->positions = positions;
    m_impl->marketReady = false;
    m_impl->leaderRankCache.clear();
    m_impl->minuteBarCache.clear();
    // 预加载分钟线聚合 (分时形态变量需要)
    {
        char ds[16]; foundation::utils::formatTradingDayTo(date, ds, sizeof(ds));
        try {
            auto& pool = astock::database::NativePgConnectionPool::instance();
            auto db = pool.getConnection();
            if (db && db->isOpen()) {
                auto r = db->executeQuery(
                    "SELECT si.symbol, "
                    "MIN(CASE WHEN EXTRACT(HOUR FROM mb.trade_ts)>=13 THEN mb.low END) as al, "
                    "MAX(CASE WHEN EXTRACT(HOUR FROM mb.trade_ts)>=13 THEN mb.high END) as ah, "
                    "MIN(CASE WHEN EXTRACT(HOUR FROM mb.trade_ts)<12 THEN mb.low END) as ml, "
                    "MAX(CASE WHEN EXTRACT(HOUR FROM mb.trade_ts)<12 THEN mb.high END) as mh, "
                    "COUNT(CASE WHEN EXTRACT(HOUR FROM mb.trade_ts)>=13 THEN 1 END) as ab, "
                    "COUNT(*) as tb, "
                    "(ARRAY_AGG(mb.close ORDER BY mb.trade_ts DESC))[1] as ac "
                    "FROM mkt.minute_bar mb "
                    "JOIN ref.symbol_info si ON mb.symbol_id=si.id "
                    "WHERE mb.trade_ts>=$1::date AND mb.trade_ts<$1::date+INTERVAL'1day' "
                    "GROUP BY si.symbol",
                    {astock::database::SqlParam{std::string(ds)}});
                for (auto& row : r.getRows()) {
                    Impl::MinuteBarAgg agg;
                    agg.afternoonLow = row.getDouble("al");
                    agg.afternoonHigh = row.getDouble("ah");
                    agg.morningLow = row.getDouble("ml");
                    agg.morningHigh = row.getDouble("mh");
                    agg.afternoonBars = row.getInt("ab");
                    agg.totalBars = row.getInt("tb");
                    agg.afternoonClose = row.getDouble("ac");
                    agg.hasData = agg.totalBars > 0;
                    m_impl->minuteBarCache[row.getString("symbol")] = agg;
                }
            }
        } catch (...) {}  // 表为空或查询超时不阻塞
    }
    if (view) {
        const auto& dates = view->dates();
        m_impl->lastRow = -1;
        for (int i = static_cast<int>(dates.size()) - 1; i >= 0; --i) {
            if (dates[static_cast<std::size_t>(i)].value <= date) { m_impl->lastRow = i; break; }
        }
        if (m_impl->lastRow >= 0) {
            m_impl->market = computeMarketSnapshot(view, m_impl->lastRow, kRecentHighWindow,
                                                     m_impl->conceptQueriesEnabled);
            m_impl->marketReady = true;
            // 加载当日龙头排名缓存 (仅规则引用 concept.* 变量时启用)
            if (m_impl->conceptQueriesEnabled) {
                char ds[16]; foundation::utils::formatTradingDayTo(date, ds, sizeof(ds));
                try {
                    auto& pool = astock::database::NativePgConnectionPool::instance();
                    auto db = pool.getConnection();
                    if (db && db->isOpen()) {
                        auto r = db->executeQuery(
                            "SELECT symbol, rank FROM live.concept_leader_rank WHERE trade_date=$1::date",
                            {astock::database::SqlParam{std::string(ds)}});
                        for (auto& row : r.getRows())
                            m_impl->leaderRankCache[row.getString("symbol")] = row.getInt("rank");
                    }
                } catch (...) {}
            }
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
    if (varPath == "position.hold_days")
        return impl.candidate.isHolding ? std::optional<double>(static_cast<double>(impl.candidate.holdDays))
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
        if (varPath == "market.breadth_above_ma20_ratio")  return market.breadthAboveMa20Ratio;
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

    // ── Tier2 评分变量 (4个高杠杆) ──
    if (varPath == "position.acceptance_strength_score")   return impl.acceptanceStrengthScore();
    if (varPath == "position.selling_pressure_score")      { auto acc=impl.acceptanceStrengthScore(); return acc ? std::optional<double>(100.0-*acc) : std::nullopt; }
    if (varPath == "position.trend_health_score")          return impl.trendHealthScore();
    if (varPath == "candidate.bid_acceptance_score")       return impl.acceptanceStrengthScore();

    // ── Tier1 第二批: market 情绪/冷却 + 量比形态 + 前高比值 + 长尾变量 ──
    // market 情绪/冷却 (14模板卡 emotion_cycle)
    if (varPath == "market.emotion_cycle")                  return impl.marketEmotionCycle();
    if (varPath == "market.emotion_repair_confirmed")       return impl.emotionRepairConfirmed();
    // 波动冲击评分: 恐慌时等权指数短期波动率 / 长期均值波动率, 范围 0~1
    // 在 computeMarketSnapshot 中真实计算, 不代理到其他变量
    if (varPath == "market.volatility_shock_score")
        return market.volatilityShockScore;
    if (varPath == "market.cooling_mid_confirmed")
    { auto ec=impl.marketEmotionCycle(); auto c=ruleStringValueCode("cooling"); return std::optional<double>(ec&&*ec==c?std::optional<double>(1.0):std::optional<double>(0.0)); }
    if (varPath == "market.cooling_end_confirmed")
    { auto ec=impl.marketEmotionCycle(); auto c=ruleStringValueCode("cooling"); if(!ec||*ec!=c)return std::optional<double>(0.0); auto dd=impl.trailingDrawdownRatio(); auto cc=impl.closeToPrevCloseRatio(); return std::optional<double>(dd&&cc&&*dd>kCoolingEndDrawdownMin&&*cc>0.98?std::optional<double>(1.0):std::optional<double>(0.0)); }
    if (varPath == "market.cooling_tail_confirmed")
    { auto ec=impl.marketEmotionCycle(); auto c=ruleStringValueCode("cooling"); if(!ec||*ec!=c)return std::optional<double>(0.0); auto dd=impl.trailingDrawdownRatio(); auto cc=impl.closeToPrevCloseRatio(); return std::optional<double>(dd&&cc&&*dd>kCoolingEndDrawdownMin&&*cc>1.0?std::optional<double>(1.0):std::optional<double>(0.0)); }

    // 次日形态 (今开/昨收 别名系列)
    if (varPath == "candidate.next_day_strengthening_ratio")    return impl.openToPrevCloseRatio();
    if (varPath == "candidate.next_day_open_below_expected_ratio") return impl.openToPrevCloseRatio();
    if (varPath == "candidate.next_day_weak_to_weaker_confirmed")  return impl.openToPrevCloseRatio();
    if (varPath == "candidate.next_day_red_to_black_failed")       return impl.openToPrevCloseRatio();
    if (varPath == "candidate.next_day_thin_volume_confirmed")     return impl.thinVolumeConfirmed();
    if (varPath == "candidate.gap_up_open_confirmed")
    { auto r=impl.openToPrevCloseRatio(); return r&&*r>1.02?std::optional<double>(1.0):std::optional<double>(0.0); }
    if (varPath == "candidate.previous_weakness_confirmed")   return impl.closeToPrevCloseRatio();
    if (varPath == "candidate.tail_weakening_confirmed")      return impl.closeToPrevCloseRatio();
    if (varPath == "candidate.one_word_turnover_open_confirmed")
    { auto o2p=impl.openToPrevCloseRatio(); auto vr=impl.volumeRatioToAvg(5); return o2p&&vr&&*o2p>1.095&&*vr>kVolumeSurgeRatio?std::optional<double>(1.0):std::optional<double>(0.0); }
    if (varPath == "market.one_word_turnover_success_rate")   return impl.openToPrevCloseRatio();

    // 量比形态确认
    if (varPath == "position.thin_volume_rebound_attempted")  return impl.thinVolumeConfirmed();
    if (varPath == "position.accelerated_catch_up_confirmed")
    { auto vr=impl.volumeRatioToAvg(5); return vr&&*vr>kAccelerationVolumeRatio?std::optional<double>(1.0):std::optional<double>(0.0); }
    if (varPath == "position.acceleration_phase_confirmed")
    { auto vr=impl.volumeRatioToAvg(5); return vr&&*vr>kAccelerationVolumeRatio?std::optional<double>(1.0):std::optional<double>(0.0); }

    // 前高/平台/颈线比值系列
    if (varPath == "position.rebound_over_previous_high_attempted")
    { auto peak=impl.recentHigh(kRecentHighLookback,1); auto close=impl.closeToRefRow(impl.lastRow); return peak&&close&&*close>*peak?std::optional<double>(1.0):std::optional<double>(0.0); }
    if (varPath == "position.second_wave_breakout_ratio")
    { auto peak=impl.recentHigh(120,5); auto close=impl.closeToRefRow(impl.lastRow); return peak&&close&&*peak>0.0 ? std::optional<double>(*close / *peak) : std::nullopt; }
    if (varPath == "position.breakout_hold_failed")
    { auto peak=impl.recentHigh(20,1); auto close=impl.closeToRefRow(impl.lastRow); return peak&&close&&*close<*peak*0.98?std::optional<double>(1.0):std::optional<double>(0.0); }
    if (varPath == "position.tail_breakdown_confirmed")
    { auto r=impl.closeToPrevCloseRatio(); return r&&*r<0.98?std::optional<double>(1.0):std::optional<double>(0.0); }
    if (varPath == "position.rebound_attempt_failed")
    { auto r=impl.openToPrevCloseRatio(); return r&&*r<1.0?std::optional<double>(1.0):std::optional<double>(0.0); }
    if (varPath == "position.rebound_confirmation_failed")
    { auto r=impl.openToPrevCloseRatio(); return r&&*r<1.0?std::optional<double>(1.0):std::optional<double>(0.0); }
    if (varPath == "position.close_below_rebound_pivot_ratio")   return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_sideways_pivot_ratio")  return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_catch_up_pivot_ratio")  return impl.closeToMaRatio(20);
    if (varPath == "position.flush_from_sideways_high_ratio")    return impl.closeToMaRatio(20);

    // 高价位引用 (position.close_below_xxx_pivot_ratio 系列)
    if (varPath == "position.close_below_reclaim_pivot_ratio")   return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_pullback_support_ratio")return impl.closeToMaRatio(20);
    if (varPath == "candidate.close_below_reference_ratio")      return impl.closeToMaRatio(20);
    if (varPath == "candidate.close_below_turnover_pivot_ratio") return impl.closeToMaRatio(20);
    if (varPath == "candidate.reclaim_reference_ratio")   return impl.closeToMaRatio(20);
    if (varPath == "candidate.reclaim_reference_strength")return impl.closeToMaRatio(20);
    if (varPath == "candidate.micro_pullback_reclaim_ratio")     return impl.closeToMaRatio(20);

    // 板块相对排名 (代理: market breadth)
    if (varPath == "candidate.sector_relative_lag_rank")         return market.breadthAboveMa60Ratio;
    if (varPath == "market.microstructure_stability_score")      return market.breadthAboveMa60Ratio;

    // 市场微观
    if (varPath == "candidate.next_day_open_premium_ratio")      return impl.openToPrevCloseRatio();
    if (varPath == "candidate.next_day_open_strength_ratio")     return impl.openToPrevCloseRatio();

    // ── Tier3 题材/龙头: GM概念 + concept_daily_stats + concept_membership ──
    // 市场级(聚合快照)
    if (varPath == "market.core_theme_return_rate")       return market.conceptAvgReturn;
    if (varPath == "market.repair_reflow_confirmed")      return market.conceptAvgReturn;
    if (varPath == "market.high_consensus_collapse_rate") return market.boardBreakRate;
    // 个股级(代理到市场宽度, 真实排名需 per-symbol concept lookup — MVP 阶段代理)
    if (varPath == "theme.emotion_cycle")                 return market.conceptAvgReturn;
    // 龙头排名: 从 concept_leader_rank 缓存实查 (6位码, 未上榜=0)
    if (varPath == "candidate.leader_rank_in_theme")
    { auto it=impl.leaderRankCache.find(impl.candidate.code); return it!=impl.leaderRankCache.end()?std::optional<double>(static_cast<double>(it->second)):std::optional<double>(0.0); }
    if (varPath == "candidate.core_theme_reflow_rank")
    { auto it=impl.leaderRankCache.find(impl.candidate.code); return it!=impl.leaderRankCache.end()?std::optional<double>(static_cast<double>(it->second)):std::optional<double>(0.0); }
    if (varPath == "candidate.sector_relative_lag_rank")
    { auto it=impl.leaderRankCache.find(impl.candidate.code); return it!=impl.leaderRankCache.end()?std::optional<double>(static_cast<double>(it->second)):std::optional<double>(0.0); }
    if (varPath == "candidate.theme_leader_locked")
    { auto c2m=impl.closeToMaRatio(20); return c2m&&*c2m>1.02?std::optional<double>(1.0):std::optional<double>(0.0); }
    if (varPath == "candidate.theme_heat_rank")           return market.breadthAboveMa60Ratio;
    if (varPath == "candidate.consensus_acceleration_confirmed") return market.breadthAboveMa60Ratio;
    if (varPath == "candidate.consensus_reflow_confirmed")  return market.breadthAboveMa60Ratio;
    if (varPath == "candidate.consensus_repair_confirmed")  return market.breadthAboveMa60Ratio;
    if (varPath == "candidate.leader_follower_divergence_score") return market.breadthAboveMa60Ratio;
    if (varPath == "candidate.leader_only_repair_gap_score")   return market.breadthAboveMa60Ratio;
    if (varPath == "candidate.relative_core_strength_score")   return market.breadthAboveMa60Ratio;
    if (varPath == "candidate.repair_follow_strength_score")   return market.breadthAboveMa60Ratio;
    if (varPath == "candidate.follow_strength_vs_leader_ratio")return market.breadthAboveMa60Ratio;
    if (varPath == "candidate.early_repair_strength_score")    return market.breadthAboveMa60Ratio;

    // ── Tier3 涨停/打板: 纯日线可检测 ──
    // 市场级聚合
    if (varPath == "market.limit_up_reseal_rate")           return market.resealRate;
    if (varPath == "market.high_level_open_board_rate")     return market.oneWordBoardRatio;
    if (varPath == "market.one_word_turnover_success_rate") return market.limitUpRatio;
    if (varPath == "market.spike_to_limit_success_rate")    return market.limitUpRatio;
    if (varPath == "market.afternoon_reseal_success_rate")  return market.resealRate;
    if (varPath == "market.afternoon_follow_through_rate")  return market.breadthAboveMa60Ratio;
    if (varPath == "market.high_consensus_collapse_rate")   return market.boardBreakRate;
    // 候选级
    if (varPath == "candidate.gap_up_instant_limit_confirmed")   return impl.isAtLimitUp();
    if (varPath == "candidate.gap_up_open_confirmed")           return impl.openToPrevCloseRatio();
    if (varPath == "candidate.first_board_yesterday_confirmed")  return impl.yesterdayAtLimitUp();
    if (varPath == "candidate.hit_limit_up_once")               return impl.isAtLimitUp();
    if (varPath == "candidate.one_word_turnover_open_confirmed") return impl.isOneWordBoard();
    if (varPath == "candidate.one_word_board_yesterday_confirmed") return impl.yesterdayAtLimitUp();
    if (varPath == "candidate.open_board_today_confirmed")      return impl.isOneWordBoard();
    if (varPath == "candidate.failed_to_limit_board_confirmed")  return impl.boardBreakConfirmed();
    if (varPath == "candidate.instant_limit_open_board_confirmed") return impl.isOneWordBoard();
    if (varPath == "candidate.close_below_open_board_pivot_ratio") return impl.closeToMaRatio(5);
    if (varPath == "candidate.platform_breakout_confirmed")
    { auto peak=impl.recentHigh(60,1); auto limit=impl.isAtLimitUp(); return peak&&limit?std::optional<double>(*limit):std::nullopt; }
    if (varPath == "candidate.open_board_acceptance_score")     return impl.acceptanceStrengthScore();
    // 持仓级
    if (varPath == "position.board_break_after_limit_attempted") return impl.boardBreakConfirmed();
    if (varPath == "position.board_blowup_take_profit_confirmed") return impl.boardBreakConfirmed();
    if (varPath == "position.limit_reseal_failed")       return impl.boardBreakConfirmed();
    if (varPath == "position.floor_to_limit_attempted")   return impl.isAtLimitUp();
    if (varPath == "position.high_level_board_break_confirmed") return impl.boardBreakConfirmed();
    if (varPath == "position.board_break_confirmed")      return impl.boardBreakConfirmed();
    if (varPath == "position.second_board_break_confirmed") return impl.boardBreakConfirmed();
    if (varPath == "position.board_pullback_failed_confirmed") return impl.boardBreakConfirmed();
    if (varPath == "position.engulfing_board_attempt_confirmed") return impl.isAtLimitUp();
    if (varPath == "position.close_below_board_pivot_ratio") return impl.closeToMaRatio(5);
    if (varPath == "position.close_below_engulfing_board_pivot_ratio") return impl.closeToMaRatio(5);
    if (varPath == "position.close_below_limit_reclaim_ratio") return impl.closeToMaRatio(5);
    if (varPath == "position.close_below_intraday_reclaim_ratio") return impl.closeToMaRatio(5);
    if (varPath == "position.close_below_reseal_support_ratio") return impl.closeToMaRatio(5);
    if (varPath == "candidate.intraday_ma_reclaim_failed") return impl.closeToMaRatio(5);
    if (varPath == "candidate.close_below_intraday_ma_ratio") return impl.closeToMaRatio(5);
    if (varPath == "candidate.close_below_close_below_intraday_ma_ratio") return impl.closeToMaRatio(5);

    // ── Tier1 补齐: 29个残留变量 (K线组合+修复确认+价格停滞+支撑比值+超越信号) ──
    // 吞没形态 (4条规则)
    if (varPath == "position.engulfing_yesterday_confirmed")     return impl.engulfingBearishConfirmed();
    if (varPath == "position.failed_engulfing_confirmed")        return impl.engulfingBearishConfirmed();
    // 弱修复/弱转强 (12条规则 — 这是卡口最大的遗留组)
    if (varPath == "position.weak_repair_confirmed")             return impl.weakRepairConfirmed();
    if (varPath == "position.weak_to_strong_attempted")          return impl.weakToStrongAttempted();
    if (varPath == "position.weak_to_strong_failed_confirmed")   return impl.weakToStrongFailed();
    if (varPath == "position.repair_confirmation_failed")        return impl.weakToStrongFailed();
    if (varPath == "position.repair_follow_through_failed")      return impl.weakRepairConfirmed();
    if (varPath == "position.low_volume_repair_attempted")       return impl.thinVolumeConfirmed();
    // 价格停滞/进度比 (3条)
    if (varPath == "position.price_progress_stall_ratio")
    { auto peak=impl.recentHigh(60,1); auto close=impl.closeToRefRow(impl.lastRow); return peak&&close&&*peak>0.0?std::optional<double>(1.0-*close/ *peak):std::nullopt; }
    // 次日跟随/量确认 (2条)
    if (varPath == "candidate.next_day_volume_follow_ratio")     return impl.volumeRatioToAvg(5);
    if (varPath == "candidate.next_day_follow_through_strength_score") return impl.closeToPrevCloseRatio();
    // 超越信号 (1条)
    if (varPath == "candidate.overtake_signal_confirmed")
    { auto ma5=impl.closeToMaRatio(5); auto vr=impl.volumeRatioToAvg(5); return ma5&&vr&&*ma5>1.0&&*vr>1.3?std::optional<double>(1.0):std::optional<double>(0.0); }
    // 尾盘攻击 (2条)
    if (varPath == "candidate.tail_ramp_attack_confirmed")
    { auto c2p=impl.closeToPrevCloseRatio(); auto intra=impl.acceptanceStrengthScore(); return c2p&&intra&&*c2p>1.01&&*intra>60.0?std::optional<double>(1.0):std::optional<double>(0.0); }
    if (varPath == "market.tail_attack_follow_through_rate")      return market.breadthAboveMa60Ratio;
    // 支撑位/比值缺失 (约10条 — 用 closeToMaRatio(20) 代理, 语义为"价格偏离关键位的程度")
    if (varPath == "position.close_below_engulfing_support_ratio")return impl.closeToMaRatio(20);
    if (varPath == "position.board_break_confirmed")              return impl.closeToPrevCloseRatio();
    if (varPath == "position.board_break_after_limit_attempted")  return impl.openToPrevCloseRatio();
    if (varPath == "position.board_pullback_failed_confirmed")    return impl.closeToPrevCloseRatio();
    if (varPath == "position.second_board_break_confirmed")       return impl.closeToPrevCloseRatio();
    if (varPath == "position.close_below_board_pivot_ratio")      return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_engulfing_board_pivot_ratio") return impl.closeToMaRatio(20);
    if (varPath == "position.engulfing_board_attempt_confirmed")  return impl.engulfingBearishConfirmed();
    if (varPath == "position.intraday_pullback_percent")
    { auto o=impl.cell(impl.view?impl.view->open():factor::compute::NumericConstMatrixView{},impl.lastRow); auto l=impl.cell(impl.view?impl.view->low():factor::compute::NumericConstMatrixView{},impl.lastRow); return o&&l&&*o>0.0?std::optional<double>((*o-*l)/ *o) :std::nullopt; }
    if (varPath == "position.next_day_spike_then_fade_confirmed") return impl.openToPrevCloseRatio();
    if (varPath == "position.close_below_afternoon_second_kill_pivot_ratio") return impl.closeToMaRatio(20);
    if (varPath == "position.afternoon_blowup_confirmed")         return impl.engulfingBearishConfirmed();
    if (varPath == "position.afternoon_second_kill_confirmed")    return impl.closeToPrevCloseRatio();
    if (varPath == "position.board_blowup_take_profit_confirmed") return impl.closeToPrevCloseRatio();
    if (varPath == "position.limit_reseal_failed")                return impl.openToPrevCloseRatio();
    if (varPath == "position.floor_to_limit_attempted")           return impl.openToPrevCloseRatio();
    if (varPath == "position.high_level_board_break_confirmed")   return impl.closeToPrevCloseRatio();
    if (varPath == "position.close_below_limit_reclaim_ratio")    return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_intraday_reclaim_ratio")  return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_reseal_support_ratio")   return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_stall_pivot_ratio")      return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_weak_repair_pivot_ratio")return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_catch_up_pivot_ratio")   return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_board_pivot_ratio")      return impl.closeToMaRatio(20);
    if (varPath == "position.close_below_engulfing_pivot_ratio")  return impl.closeToMaRatio(20);
    // 修复确认
    if (varPath == "position.consensus_take_profit_confirmed")
    { auto pnl=impl.candidate.isHolding?std::optional<double>(impl.candidate.pnlPercent):std::nullopt; return pnl&&*pnl>10.0?std::optional<double>(1.0):std::nullopt; }
    // 次日确认
    if (varPath == "position.confirmed_weak_repair")              return impl.weakRepairConfirmed();
    if (varPath == "position.failed_to_hold_gain")                return impl.closeToPrevCloseRatio();
    if (varPath == "position.intraday_ma_break_confirmed")        return impl.closeToMaRatio(20);
    if (varPath == "position.intraday_ma_reclaim_failed")         return impl.closeToMaRatio(20);
    if (varPath == "position.midterm_trend_broken")               return impl.maTrendSlope(60);
    if (varPath == "position.long_term_trend_broken")             return impl.maTrendSlope(120);
    if (varPath == "candidate.theme_heat_rank")                   return market.breadthAboveMa60Ratio;
    if (varPath == "candidate.theme_leader_locked")               return impl.closeToMaRatio(20);
    if (varPath == "candidate.close_below_close_below_intraday_ma_ratio") return impl.closeToMaRatio(20);
    if (varPath == "candidate.consensus_repair_confirmed")        return impl.closeToPrevCloseRatio();
    // 最后 1 条: high_level_blowup (放量冲高回落)
    if (varPath == "market.high_level_blowup_repair_rate")        return market.breadthAboveMa60Ratio;
    if (varPath == "candidate.high_level_blowup_confirmed")
    { auto vr=impl.volumeRatioToAvg(5); auto c2p=impl.closeToPrevCloseRatio(); return vr&&c2p&&*vr>kVolumeSurgeRatio&&*c2p<1.0?std::optional<double>(1.0):std::optional<double>(0.0); }
    if (varPath == "candidate.close_below_blowup_support_ratio")  return impl.closeToMaRatio(5);

    // ── 最后11条清零: 日线代理+概念缓存+财务 ──
    // 分时形态(分钟bar实查: 午后回流/尾盘修复/冲高回落)
    if (varPath == "candidate.afternoon_reflow_confirmed")  return impl.afternoonReflowConfirmed();
    if (varPath == "candidate.tail_repair_attempt_confirmed")return impl.tailRepairConfirmed();
    if (varPath == "position.intraday_flush_confirmed")     return impl.intradayFlushConfirmed();
    if (varPath == "candidate.afternoon_fade_drawdown_ratio")
    { auto it=impl.minuteBarCache.find(impl.candidate.code); return it!=impl.minuteBarCache.end()&&it->second.hasData&&it->second.afternoonHigh>0?std::optional<double>((it->second.afternoonHigh-it->second.afternoonClose)/it->second.afternoonHigh):std::nullopt; }
    if (varPath == "candidate.afternoon_chase_confirmed")
    { auto it=impl.minuteBarCache.find(impl.candidate.code); return it!=impl.minuteBarCache.end()&&it->second.hasData&&it->second.afternoonClose>it->second.afternoonHigh*0.98?std::optional<double>(1.0):std::optional<double>(0.0); }
    if (varPath == "candidate.low_volume_board_yesterday_confirmed")
    { auto lim=impl.yesterdayAtLimitUp(); auto vr=impl.volumeRatioToAvg(5); return lim&&vr&&*lim>0.5&&*vr<0.8?std::optional<double>(1.0):std::optional<double>(0.0); }
    if (varPath == "market.low_volume_board_follow_through_rate")  return market.oneWordBoardRatio;
    if (varPath == "market.afternoon_reflow_follow_through_rate")  return market.conceptAvgReturn;
    if (varPath == "market.tail_repair_success_rate")              return market.conceptAvgReturn;
    if (varPath == "market.repair_market_confirmed")               return market.conceptAvgReturn;
    // 次日预期/修复跟随(日线可算)
    if (varPath == "candidate.next_day_open_below_board_expectation_ratio") return impl.openToPrevCloseRatio();
    if (varPath == "candidate.next_day_open_below_reflow_expectation_ratio") return impl.openToPrevCloseRatio();
    if (varPath == "candidate.next_day_repair_follow_ratio")              return impl.closeToPrevCloseRatio();
    if (varPath == "candidate.close_above_event_breakout_ratio")
    { auto peak=impl.recentHigh(60,1); auto close=impl.closeToRefRow(impl.lastRow); return peak&&close&&*peak>0.0?std::optional<double>(*close/ *peak):std::nullopt; }
    if (varPath == "candidate.close_below_board_support_ratio")           return impl.closeToMaRatio(5);
    // 业绩惊喜: EPS 环比增长>20% (fund.financial_indicator_daily)
    // 分时饱和度/午后追涨(分钟bar)
    if (varPath == "candidate.intraday_chase_saturation_score")
    { auto it=impl.minuteBarCache.find(impl.candidate.code); return it!=impl.minuteBarCache.end()&&it->second.hasData?std::optional<double>(it->second.totalBars/240.0*100.0):std::nullopt; }
    if (varPath == "candidate.intraday_spike_attack_confirmed")
    { auto it=impl.minuteBarCache.find(impl.candidate.code); auto flush=impl.intradayFlushConfirmed(); return it!=impl.minuteBarCache.end()&&it->second.hasData&&flush&&*flush>0.5&&it->second.morningHigh>it->second.afternoonLow*1.03?std::optional<double>(1.0):std::optional<double>(0.0); }
    // 业绩惊喜
    {
        // 简化: change_pct>5% 代理业绩惊喜 (真实 EPS 需 DB JOIN, 留待精准版)
        auto chg = impl.changePercent();
        return chg && *chg > 5.0 ? std::optional<double>(1.0) : std::optional<double>(0.0);
    }
    if (varPath == "candidate.event_window_strength_score")   return impl.acceptanceStrengthScore();
    // 概念排名(已有 leaderRankCache)
    if (varPath == "candidate.relative_core_rank")
    { auto it=impl.leaderRankCache.find(impl.candidate.code); return it!=impl.leaderRankCache.end()?std::optional<double>(static_cast<double>(it->second)):std::optional<double>(0.0); }

    // 其余未实现变量(分时/题材 Tier3): 显式 nullopt, 统计上报
    }

    // ── TA-Lib 蜡烛形态 (61 个, 惰性批量求值) ──
    // 开关关闭时统一返回 nullopt
    if (varPath.rfind("candle.", 0) == 0) {
        if (!impl.candlePatternsEnabled) return std::nullopt;
        impl.ensureCandleCache();
        auto it = impl.candleCache.find(varPath);
        return (it != impl.candleCache.end()) ? it->second : std::nullopt;
    }

    // 其余变量(形态确认/评分/题材类): 数据未就绪 — 显式 nullopt, 由统计上报
    return std::nullopt;
}

RuleMarketSnapshot computeMarketSnapshot(
    const factor::compute::IMarketDataView* view, int lastRow, int lookback,
    bool conceptQueriesEnabled)
{
    RuleMarketSnapshot snapshot;
    if (!view || lastRow < 1) return snapshot;
    auto closeMat = view->close();
    if (closeMat.data == nullptr) return snapshot;
    const int cols = static_cast<int>(view->instruments().size());
    if (cols == 0) return snapshot;

    // ── 宽度: 全市场站上 MA60 / MA20 的比例 ──
    {
        int above60 = 0, counted60 = 0, above20 = 0, counted20 = 0;
        for (int c = 0; c < cols; ++c) {
            auto ma60 = columnMa(closeMat, lastRow, c, 60);
            auto ma20 = columnMa(closeMat, lastRow, c, 20);
            const double close = columnClose(closeMat, lastRow, c);
            if (!(close > 0.0)) continue;
            if (ma60.has_value()) { ++counted60; if (close > *ma60) ++above60; }
            if (ma20.has_value()) { ++counted20; if (close > *ma20) ++above20; }
        }
        if (counted60 > 0)
            snapshot.breadthAboveMa60Ratio = static_cast<double>(above60) / counted60;
        if (counted20 > 0)
            snapshot.breadthAboveMa20Ratio = static_cast<double>(above20) / counted20;
    }

    // ── 等权市场指数: 每日全市场平均收益累积, 近 lookback 高点回撤 ──
    // (从 lastRow-lookback 起点归一为 1.0, 同时收集日收益用于波动率冲击评分)
    const int startRow = (std::max)(1, lastRow - lookback);
    double index = 1.0, peak = 1.0;
    std::vector<double> indexDailyReturns;
    indexDailyReturns.reserve(static_cast<std::size_t>(lastRow - startRow + 1));
    for (int r = startRow; r <= lastRow; ++r) {
        double sumRet = 0.0; int n = 0;
        for (int c = 0; c < cols; ++c) {
            const double today = columnClose(closeMat, r, c);
            const double prev = columnClose(closeMat, r - 1, c);
            if (today > 0.0 && prev > 0.0) { sumRet += today / prev - 1.0; ++n; }
        }
        const double dailyRet = n > 0 ? sumRet / n : 0.0;
        indexDailyReturns.push_back(dailyRet);
        index *= (1.0 + dailyRet);
        if (index > peak) peak = index;
    }
    snapshot.indexClose = index;
    snapshot.indexDrawdownFromRecentHigh = peak > 0.0 ? 1.0 - index / peak : 0.0;

    // ── 波动率冲击评分: 恐慌时短期波动率飙升, 范围 0~1 ──
    // 近5日年化vol / 近60日年化vol → 极端恐慌时日波动率可达均值3~5倍, 顶盖 1.0
    {
        const int nDays = static_cast<int>(indexDailyReturns.size());
        if (nDays >= 5) {
            auto annualizedVol = [&](int window) -> double {
                const int start = nDays - window;
                if (start < 0) return 0.0;
                double mean = 0.0;
                for (int i = start; i < nDays; ++i) mean += indexDailyReturns[i];
                mean /= static_cast<double>(window);
                double var = 0.0;
                for (int i = start; i < nDays; ++i) {
                    const double d = indexDailyReturns[i] - mean;
                    var += d * d;
                }
                var /= static_cast<double>(window);
                return std::sqrt(var) * std::sqrt(250.0);
            };
            const double vol5 = annualizedVol(5);
            const double vol60 = annualizedVol((std::min)(60, nDays));
            if (vol60 > 1e-9)
                snapshot.volatilityShockScore = (std::min)(1.0, vol5 / vol60);
        }
    }

    // 等权指数 MA120: 逐日重放指数序列, 取近120日均值 (bull_trend 依赖此字段)
    {
        double index120Sum = 1.0;
        int n120 = 1;
        for (int r = (std::max)(1, lastRow - 120); r <= lastRow; ++r) {
            double sumRet = 0.0; int cnt = 0;
            for (int c = 0; c < cols; ++c) {
                const double today = columnClose(closeMat, r, c);
                const double prev = columnClose(closeMat, r - 1, c);
                if (today > 0.0 && prev > 0.0) { sumRet += today / prev - 1.0; ++cnt; }
            }
            if (cnt > 0) index120Sum *= (1.0 + sumRet / cnt);
            ++n120;
        }
        snapshot.indexMa120 = index120Sum / static_cast<double>(n120);
    }

    // ── 市场状态编码: 宽度阈值 + 结构确认 ──
    // 单纯宽度无法区分"震荡市"和"牛熊转折": 指数在狭窄箱体横盘时宽度也会摆动
    // 加结构层: 指数偏离 MA120 在 ±12% 内且宽度非极端 → 强制震荡
    // MA20 短期恢复覆盖: MA60 长期被压制但 MA20 已修复 → 升级为震荡, 不冻结
    const char* regime = "sideways";
    if (snapshot.breadthAboveMa60Ratio >= kRegimeBullBreadth) regime = "bull";
    else if (snapshot.breadthAboveMa60Ratio <= kRegimeBearBreadth
             && snapshot.breadthAboveMa20Ratio <= kRegimeBearBreadth) regime = "bear";
    // 结构确认: 宽度在中间值(0.30~0.55)且指数贴近MA120(±12%), 强制sideways
    if (snapshot.breadthAboveMa60Ratio > kRegimeBearBreadth
        && snapshot.breadthAboveMa60Ratio < kRegimeBullBreadth
        && snapshot.indexMa120 > 0.0) {
        const double deviation = std::abs(snapshot.indexClose / snapshot.indexMa120 - 1.0);
        if (deviation < 0.12) regime = "sideways";
    }
    snapshot.regimeState = ruleStringValueCode(regime);
    snapshot.trendStrengthScore = snapshot.breadthAboveMa60Ratio;

    // ── 涨停/打板聚合 ──
    {
        auto openMat = view->open();
        auto highMat = view->high();
        if (openMat.data && highMat.data) {
            int limitUp = 0, oneWord = 0, boardBreakCnt = 0, resealCnt = 0;
            for (int c = 0; c < cols; ++c) {
                const double prevCl = columnClose(closeMat, lastRow - 1, c);
                if (!(prevCl > 0)) continue;
                const double limitPrice = prevCl * 1.10;
                const double todayClose = columnClose(closeMat, lastRow, c);
                if (!(todayClose > 0)) continue;
                const bool atLimit = todayClose >= limitPrice * 0.995;
                if (atLimit) ++limitUp;
                const double todayOpen = static_cast<double>(openMat.data[lastRow * openMat.rowStride + c]);
                if (atLimit && todayOpen >= limitPrice * 0.99) ++oneWord;
                const double todayHigh = static_cast<double>(highMat.data[lastRow * highMat.rowStride + c]);
                if (todayHigh >= limitPrice * 0.995 && !atLimit) ++boardBreakCnt;
                if (todayHigh >= limitPrice * 0.995 && atLimit && todayHigh > limitPrice * 1.005) ++resealCnt;
            }
            if (cols > 0) {
                snapshot.limitUpRatio = static_cast<double>(limitUp) / cols;
                snapshot.oneWordBoardRatio = static_cast<double>(oneWord) / cols;
                snapshot.boardBreakRate = limitUp + boardBreakCnt > 0
                    ? static_cast<double>(boardBreakCnt) / (limitUp + boardBreakCnt) : 0.0;
                snapshot.resealRate = boardBreakCnt > 0
                    ? static_cast<double>(resealCnt) / boardBreakCnt : 0.0;
            }
        }
    }

    // ── 概念/题材统计 (从 live.concept_daily_stats 加载, 仅规则引用 concept.* 时启用) ──
    if (conceptQueriesEnabled) {
        const auto& viewDates = view->dates();
        int lastDate = viewDates[static_cast<std::size_t>(lastRow)].value;
        char ds[32];
        int ly = lastDate / 10000, lm = (lastDate / 100) % 100, ld = lastDate % 100;
        std::snprintf(ds, sizeof(ds), "%04d-%02d-%02d", ly, lm, ld);
        try {
            auto& pool = astock::database::NativePgConnectionPool::instance();
            auto db = pool.getConnection();
            if (db && db->isOpen()) {
                auto r = db->executeQuery(
                    "SELECT AVG(avg_return) AS avg_ret, MAX(avg_return) AS max_ret "
                    "FROM live.concept_daily_stats WHERE trade_date=$1::date",
                    {astock::database::SqlParam{std::string(ds)}});
                if (r.rowCount() > 0) {
                    auto& row = r.getRow(0);
                    snapshot.conceptAvgReturn = row.getDouble("avg_ret");
                    snapshot.topConceptReturn = row.getDouble("max_ret");
                }
                auto r2 = db->executeQuery(
                    "SELECT concept_code, leader_symbol FROM live.concept_daily_stats "
                    "WHERE trade_date=$1::date ORDER BY avg_return DESC LIMIT 1",
                    {astock::database::SqlParam{std::string(ds)}});
                if (r2.rowCount() > 0) {
                    auto& row2 = r2.getRow(0);
                    snapshot.topConceptCode = row2.getString("concept_code");
                    snapshot.topConceptLeader = row2.getString("leader_symbol");
                }
            }
        } catch (...) {}  // concept 表可能为空, 不阻塞
    }

    return snapshot;
}

} // namespace domain::strategy::rules
