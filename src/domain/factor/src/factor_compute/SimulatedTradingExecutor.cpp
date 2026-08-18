#include "factor_compute/SimulatedTradingExecutor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

#include "foundation/log/logging.hpp"

namespace factor::compute {

SimulatedTradingExecutor::SimulatedTradingExecutor(const SimulatedTradingParams& params)
    : params_(params)
{
}

SimulatedTradingResult SimulatedTradingExecutor::execute(
    const FactorValuesByDate& factorValues,
    const std::vector<std::string>& sortedDates,
    const std::vector<int32_t>& sortedDateRows,
    NumericConstMatrixView priceView,
    NumericConstMatrixView preAdjustView,
    NumericConstMatrixView postAdjustView,
    const std::vector<InstrumentId>& instrumentIds,
    const std::unordered_map<uint32_t, std::string>& instrumentIdToSymbol) const
{
    SimulatedTradingResult result;
    const int32_t nGroups = params_.numGroups;
    const int32_t forwardDays = std::max(1, params_.forwardDays);
    const int32_t rebalanceDays = std::max(1, params_.rebalanceDays);
    const double costPerTrade = params_.commissionRate + params_.slippageRate;
    const double maxFwdRetAbs = params_.maxFwdRetAbsLimit;
    const double riskFreeRate = params_.riskFreeRate;
    const bool usePreAdjust = params_.adjustPriceType == "pre";
    const bool haveAdjust = (usePreAdjust && preAdjustView.isValid())
                         || (!usePreAdjust && postAdjustView.isValid());

    // ranked 升序排列: g=0=最低值组, g=nGroups-1=最高值组
    // 方向感知: ascending=true(值大=好) → long=最高值组, ascending=false(值小=好) → long=最低值组
    const int32_t longGroupIdx  = params_.ascending ? (nGroups - 1) : 0;
    const int32_t shortGroupIdx = params_.ascending ? 0 : (nGroups - 1);

    if (sortedDates.empty() || sortedDateRows.size() != sortedDates.size()
        || nGroups <= 0 || priceView.rowCount <= 0) {
        return result;
    }

    // symbol → price column index
    std::unordered_map<std::string, int32_t> symToCol;
    for (int32_t ci = 0; ci < static_cast<int32_t>(instrumentIds.size()); ++ci) {
        auto it = instrumentIdToSymbol.find(instrumentIds[ci].value);
        if (it != instrumentIdToSymbol.end()) symToCol[it->second] = ci;
    }

    const int32_t rowStride = priceView.rowStride >= priceView.columnCount
        ? priceView.rowStride : priceView.columnCount;

    // ── 分组累积指标 ──
    std::vector<double> groupAccumReturns(nGroups, 0.0);
    std::vector<int32_t> groupValidDays(nGroups, 0);
    std::vector<int64_t> groupTotalStocks(nGroups, 0);
    std::vector<int32_t> groupPeriodCount(nGroups, 0);
    std::vector<double> groupCumulativeNetValue(nGroups, 1.0); // 逐期复利累积净值，用于精确累计收益

    // ── 收益序列 ──
    std::vector<double> strategyDailyReturns;
    std::vector<double> rawLongShortReturns;
    std::vector<double> costAdjustedLongShortReturns;
    std::vector<std::vector<double>> groupDailyReturns(nGroups);

    // ── 持仓追踪：long=G1, short=GN ──
    std::unordered_set<std::string> longHolding;
    std::unordered_set<std::string> shortHolding;
    std::unordered_map<std::string, size_t> entryStep; // 最近一次建仓的 stepIndex

    // ── 换手 ──
    double totalTurnover = 0.0;
    int32_t turnoverPeriods = 0;

    // ── 净值 ──
    double equity = params_.initialCapital;
    double maxEquity = params_.initialCapital;

    // sortedDates 已是调仓日列表(上游按 rebalanceDays 过滤), 此处按 1 步进逐日调仓
    // sortedDateRows[i] = sortedDates[i] 在 priceView(全交易日矩阵) 中的行号
    // 前向窗口按交易日计: 卖出价取买入行 + forwardDays 个交易日 (与 IC 前向窗口同口径)
    const size_t totalSteps = sortedDates.size() > static_cast<size_t>(forwardDays)
        ? sortedDates.size() - forwardDays : 0;
    size_t stepIndex = 0;

    for (size_t di = 0; di < sortedDates.size(); ++di) {
        if (params_.onProgress && totalSteps > 0)
            params_.onProgress(static_cast<double>(stepIndex) / static_cast<double>(totalSteps));

        const int32_t buyRow  = sortedDateRows[di];
        const int32_t sellRow = buyRow + forwardDays;
        // 行号随 di 单调递增, 越界后后续全部越界 → 直接结束
        if (buyRow < 0 || sellRow >= priceView.rowCount) break;

        const std::string& buyDate = sortedDates[di];

        auto fvIt = factorValues.find(buyDate);
        if (fvIt == factorValues.end()) { ++stepIndex; continue; }

        // ═══ 因子排序 ═══
        std::vector<std::pair<std::string, double>> ranked;
        for (const auto& [sym, fv] : fvIt->second)
            if (std::isfinite(fv)) ranked.emplace_back(sym, fv);
        if (ranked.empty()) { ++stepIndex; continue; }

        std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

        const size_t N = ranked.size();
        if (N < static_cast<size_t>(nGroups)) { ++stepIndex; continue; }
        const size_t groupSize = N / nGroups;

        // ═══ 各组前向收益（无成本）═══
        std::vector<double> dayGroupRawReturns(nGroups, 0.0);
        std::vector<int32_t> groupCnt(nGroups, 0);

        for (int32_t g = 0; g < nGroups; ++g) {
            size_t start = g * groupSize;
            size_t end = (g + 1 == nGroups) ? N : start + groupSize;
            double sumRet = 0.0;
            int32_t cnt = 0;
            for (size_t i = start; i < end; ++i) {
                const auto& sym = ranked[i].first;
                auto ci = symToCol.find(sym);
                if (ci == symToCol.end()) continue;
                int32_t col = ci->second;
                if (col < 0 || col >= priceView.columnCount) continue;

                double bp = priceView.data[static_cast<size_t>(buyRow) * rowStride + col];
                double sp = priceView.data[static_cast<size_t>(sellRow) * rowStride + col];
                if (haveAdjust) {
                    const auto& adjView = usePreAdjust ? preAdjustView : postAdjustView;
                    if (adjView.isValid() && adjView.rowCount > 0 && col < adjView.columnCount
                        && buyRow < adjView.rowCount && sellRow < adjView.rowCount) {
                        const int32_t adjStride = adjView.rowStride >= adjView.columnCount
                            ? adjView.rowStride : adjView.columnCount;
                        double ba = adjView.data[static_cast<size_t>(buyRow) * adjStride + col];
                        double sa = adjView.data[static_cast<size_t>(sellRow) * adjStride + col];
                        if (std::isfinite(ba) && ba > 1e-9) bp *= ba;
                        if (std::isfinite(sa) && sa > 1e-9) sp *= sa;
                    }
                }
                if (bp > 1e-9 && sp > 1e-9 && std::isfinite(bp) && std::isfinite(sp)) {
                    double fwdRet = sp / bp - 1.0;
                    if (std::isfinite(fwdRet) && std::abs(fwdRet) < maxFwdRetAbs) {
                        sumRet += fwdRet;
                        ++cnt;
                    }
                }
            }
            if (cnt > 0) {
                dayGroupRawReturns[g] = sumRet / cnt;
                groupCnt[g] = cnt;
                groupAccumReturns[g] += dayGroupRawReturns[g];
                groupCumulativeNetValue[g] *= (1.0 + dayGroupRawReturns[g]);
                groupValidDays[g]++;
                groupTotalStocks[g] += cnt;
                groupPeriodCount[g]++;
                groupDailyReturns[g].push_back(dayGroupRawReturns[g]);
            }
        }

        // ═══ 本期多空篮子 ═══
        // ranked 按因子值升序排列; ascending=true → 值大=好 → long 尾部(高分) short 头部(低分)
        // ascending=false → 值小=好 → long 头部(低分) short 尾部(高分)
        // longOnly 时空头篮留空 (禁止做空)
        std::unordered_set<std::string> newLong, newShort;
        fillBaskets(ranked, N, groupSize, newLong, newShort);

        // 首期建仓
        if (longHolding.empty() && shortHolding.empty()) {
            for (const auto& s : newLong) { longHolding.insert(s); entryStep[s] = stepIndex; }
            for (const auto& s : newShort) { shortHolding.insert(s); entryStep[s] = stepIndex; }
            ++stepIndex;
            continue;
        }

        // ═══ 调仓：只换有变化的，最少持有期 forwardDays ═══
        int32_t longSold = 0, longBought = 0;
        int32_t shortSold = 0, shortBought = 0;

        // Long 侧卖出：不在新篮子 且 持有 >= forwardDays
        std::vector<std::string> toRemove;
        for (const auto& s : longHolding) {
            if (!newLong.count(s)) {
                auto it = entryStep.find(s);
                if (it != entryStep.end()
                    && (stepIndex - it->second) * static_cast<size_t>(rebalanceDays) >= static_cast<size_t>(forwardDays)) {
                    toRemove.push_back(s);
                    ++longSold;
                }
            }
        }
        for (const auto& s : toRemove) longHolding.erase(s);

        // Long 侧买入：在新篮子 且 不在持仓
        for (const auto& s : newLong) {
            if (!longHolding.count(s)) {
                longHolding.insert(s);
                entryStep[s] = stepIndex;
                ++longBought;
            }
        }

        // Short 侧卖出
        toRemove.clear();
        for (const auto& s : shortHolding) {
            if (!newShort.count(s)) {
                auto it = entryStep.find(s);
                if (it != entryStep.end()
                    && (stepIndex - it->second) * static_cast<size_t>(rebalanceDays) >= static_cast<size_t>(forwardDays)) {
                    toRemove.push_back(s);
                    ++shortSold;
                }
            }
        }
        for (const auto& s : toRemove) shortHolding.erase(s);

        // Short 侧买入
        for (const auto& s : newShort) {
            if (!shortHolding.count(s)) {
                shortHolding.insert(s);
                entryStep[s] = stepIndex;
                ++shortBought;
            }
        }

        // ═══ 换手率 ═══
        int32_t longSz  = std::max(1, static_cast<int32_t>(longHolding.size()));
        int32_t shortSz = std::max(1, static_cast<int32_t>(shortHolding.size()));
        double periodLongTurnover  = static_cast<double>(longSold + longBought) / longSz;
        double periodShortTurnover = static_cast<double>(shortSold + shortBought) / shortSz;
        double periodTurnover = composeTurnover(periodLongTurnover, periodShortTurnover);

        if (turnoverPeriods > 0) {
            totalTurnover += periodTurnover;
            result.periodTurnovers.push_back(periodTurnover);
        }
        ++turnoverPeriods;

        // ═══ 追踪：交易记录 + 每期快照 ═══
        {
            auto getPrice = [&](const std::string& sym) -> double {
                auto ci = symToCol.find(sym);
                if (ci == symToCol.end()) return 0.0;
                int32_t col = ci->second;
                if (buyRow >= 0 && buyRow < priceView.rowCount
                    && col >= 0 && col < priceView.columnCount)
                    return priceView.data[static_cast<size_t>(buyRow) * rowStride + col];
                return 0.0;
            };
            // 只记录实际成交（买入=新进篮子, 卖出=已卖出且持有期满）
            for (const auto& s : newLong)
                if (!longHolding.count(s))
                    result.tradeLog.push_back({s, buyDate, "BUY", "long", getPrice(s), costPerTrade});
            // 收集本次实际卖出的 long 标的
            for (const auto& s : longHolding) {
                if (!newLong.count(s)) {
                    auto it = entryStep.find(s);
                    if (it != entryStep.end()
                        && (stepIndex - it->second) * static_cast<size_t>(rebalanceDays) >= static_cast<size_t>(forwardDays))
                        result.tradeLog.push_back({s, buyDate, "SELL", "long", getPrice(s), costPerTrade});
                }
            }
            for (const auto& s : newShort)
                if (!shortHolding.count(s))
                    result.tradeLog.push_back({s, buyDate, "BUY", "short", getPrice(s), costPerTrade});
            for (const auto& s : shortHolding) {
                if (!newShort.count(s)) {
                    auto it = entryStep.find(s);
                    if (it != entryStep.end()
                        && (stepIndex - it->second) * static_cast<size_t>(rebalanceDays) >= static_cast<size_t>(forwardDays))
                        result.tradeLog.push_back({s, buyDate, "SELL", "short", getPrice(s), costPerTrade});
                }
            }

            double longRaw  = dayGroupRawReturns[longGroupIdx];
            double shortRaw = dayGroupRawReturns[shortGroupIdx];
            double lc = periodLongTurnover  * costPerTrade;
            double sc = periodShortTurnover * costPerTrade;
            result.periodTrackings.push_back({
                buyDate,
                static_cast<int32_t>(longHolding.size()),
                static_cast<int32_t>(shortHolding.size()),
                longBought, longSold, shortBought, shortSold,
                periodLongTurnover, periodShortTurnover,
                longRaw, shortRaw,
                composeNetReturn(longRaw, shortRaw, lc, sc)
            });
        }

        // ═══ 策略日收益：多空价差 - 换手成本 (longOnly 时仅多头腿) ═══
        double longRaw  = dayGroupRawReturns[longGroupIdx];
        double shortRaw = dayGroupRawReturns[shortGroupIdx];
        double dailyRawRet = composeRawReturn(longRaw, shortRaw);

        // 只对换手部分扣费
        double longCost  = periodLongTurnover  * costPerTrade;
        double shortCost = periodShortTurnover * costPerTrade;
        double dailyCostAdjRet = composeNetReturn(longRaw, shortRaw, longCost, shortCost);

        rawLongShortReturns.push_back(dailyRawRet);
        costAdjustedLongShortReturns.push_back(dailyCostAdjRet);
        strategyDailyReturns.push_back(dailyCostAdjRet);

        // 净值：每 forwardDays/rebalanceDays 步复利一次（非重叠）
        equity *= (1.0 + dailyCostAdjRet);
        if (equity > maxEquity) maxEquity = equity;
        double dd = (maxEquity > 1e-9) ? (maxEquity - equity) / maxEquity : 0.0;
        if (dd > result.maxDrawdown) result.maxDrawdown = dd;

        ++stepIndex;
    }

    // ── 分组指标 ──
    // ranked[0..] = 低因子值, ranked[..N-1] = 高因子值; g=0=低值, g=n-1=高值
    // ascending=true: 高值=好 → G1=尾部(g=n-1), G5=头部(g=0)
    // ascending=false: 低值=好 → G1=头部(g=0), G5=尾部(g=n-1)
    result.groups.resize(nGroups);
    for (int32_t g = 0; g < nGroups; ++g) {
        const int32_t displayIdx = params_.ascending ? (nGroups - 1 - g) : g;
        GroupBacktestMetrics& gm = result.groups[static_cast<size_t>(displayIdx)];
        gm.groupIndex = displayIdx + 1;  // G1=最好(displayIdx=0), G5=最差(displayIdx=4)
        gm.stockCount = groupPeriodCount[g] > 0
            ? static_cast<int32_t>(groupTotalStocks[g] / groupPeriodCount[g]) : 0;
        if (groupValidDays[g] > 0) {
            gm.returnRate = groupAccumReturns[g] / groupValidDays[g];
            gm.cumulativeReturn = groupCumulativeNetValue[g] - 1.0;
            gm.validDays = groupValidDays[g];
            // 年化: 基于复利累计收益 (与策略级 totalReturn→annualizedReturn 同公式)
            const double periodsPerYear = 252.0 / static_cast<double>(rebalanceDays);
            if (gm.cumulativeReturn > -1.0) {
                gm.annualizedReturn = std::pow(1.0 + gm.cumulativeReturn,
                    periodsPerYear / static_cast<double>(groupValidDays[g])) - 1.0;
            } else {
                gm.annualizedReturn = -1.0;  // 100% loss
            }
        }
    }

    // ── 执行指标 ──
    const int32_t nPeriods = static_cast<int32_t>(strategyDailyReturns.size());
    if (nPeriods > 0) {
        result.totalReturn = (equity - params_.initialCapital) / params_.initialCapital;
        result.finalEquity = equity;

        const double periodsPerYear = 252.0 / static_cast<double>(rebalanceDays);
        result.annualizedReturn = (nPeriods < 10000)
            ? std::pow(1.0 + result.totalReturn, periodsPerYear / nPeriods) - 1.0
            : result.totalReturn;

        double avgPeriod = 0.0;
        for (double r : strategyDailyReturns) avgPeriod += r;
        avgPeriod /= nPeriods;

        double variance = 0.0;
        for (double r : strategyDailyReturns) {
            double d = r - avgPeriod;
            variance += d * d;
        }
        variance /= nPeriods;
        double periodStd = std::sqrt(std::max(0.0, variance));
        result.annualStdDev = periodStd * std::sqrt(periodsPerYear);

        result.sharpeRatio = (result.annualStdDev > 1e-12)
            ? ((result.annualizedReturn - riskFreeRate) / result.annualStdDev) : 0.0;
        result.validSampleCount = nPeriods;
        result.turnoverRate = turnoverPeriods > 0 ? totalTurnover / turnoverPeriods : 0.0;
    }

    // ── 分组因子值 min/max ──
    {
        std::vector<double> allFiniteVals;
        for (const auto& [_, symMap] : factorValues)
            for (const auto& [_, fv] : symMap)
                if (std::isfinite(fv)) allFiniteVals.push_back(fv);
        if (!allFiniteVals.empty()) {
            std::sort(allFiniteVals.begin(), allFiniteVals.end());  // 升序，与 ranked 对齐
            const size_t Nvals = allFiniteVals.size();
            const size_t gSize = Nvals / nGroups;
            for (int32_t g = 0; g < nGroups; ++g) {
                const int32_t displayIdx = params_.ascending ? (nGroups - 1 - g) : g;
                size_t startIdx = g * gSize;
                size_t endIdx = (g + 1 == nGroups) ? Nvals - 1 : startIdx + gSize - 1;
                if (startIdx < Nvals) {
                    result.groups[static_cast<size_t>(displayIdx)].minFactorValue = allFiniteVals[startIdx];     // 升序，startIdx 是组内最小
                    result.groups[static_cast<size_t>(displayIdx)].maxFactorValue = allFiniteVals[std::min(endIdx, Nvals - 1)]; // endIdx 是组内最大
                }
            }

            // ── 诊断：检查组内因子值范围是否极端（max/min 比值过大说明有离群值集中）──
            for (int32_t g = 0; g < nGroups; ++g) {
                const auto& gm = result.groups[g];
                const double absMax = std::max(std::abs(gm.maxFactorValue), std::abs(gm.minFactorValue));
                const double absMin = std::min(std::abs(gm.maxFactorValue), std::abs(gm.minFactorValue));
                if (absMin > 1e-9) {
                    const double ratio = absMax / absMin;
                    constexpr double kExtremeRangeRatio = 20.0;
                    if (ratio > kExtremeRangeRatio) {
                        INTERNAL_WARN_STREAM << "[SimulatedTrading] G" << (g + 1)
                            << " factor value range extreme: max=" << gm.maxFactorValue
                            << " min=" << gm.minFactorValue
                            << " ratio=" << ratio
                            << " — outliers may distort group mean return";
                    }
                }
            }
        }
    }

    result.strategyDailyReturns = std::move(strategyDailyReturns);
    result.rawLongShortReturns = std::move(rawLongShortReturns);
    result.costAdjustedLongShortReturns = std::move(costAdjustedLongShortReturns);
    result.groupDailyReturns = std::move(groupDailyReturns);

    // 风险调整
    {
        const double riskFreeDailyRate = riskFreeRate / 252.0;
        result.riskAdjustedLongShortReturns.reserve(result.costAdjustedLongShortReturns.size());
        for (double r : result.costAdjustedLongShortReturns)
            result.riskAdjustedLongShortReturns.push_back(r - riskFreeDailyRate);
    }

    return result;
}

void SimulatedTradingExecutor::fillBaskets(
    const std::vector<std::pair<std::string, double>>& ranked,
    size_t n, size_t groupSize,
    std::unordered_set<std::string>& outLong,
    std::unordered_set<std::string>& outShort) const
{
    // ranked 升序; ascending=true → long 尾部(高分) short 头部(低分)
    // ascending=false → long 头部(低分) short 尾部(高分)
    // longOnly → 空头篮留空, 仅多头腿参与策略收益
    if (params_.ascending) {
        for (size_t i = n - groupSize; i < n; ++i) outLong.insert(ranked[i].first);
        if (!params_.longOnly) {
            for (size_t i = 0; i < groupSize; ++i) outShort.insert(ranked[i].first);
        }
    } else {
        for (size_t i = 0; i < groupSize; ++i) outLong.insert(ranked[i].first);
        if (!params_.longOnly) {
            for (size_t i = n - groupSize; i < n; ++i) outShort.insert(ranked[i].first);
        }
    }
}

double SimulatedTradingExecutor::composeRawReturn(double longRaw, double shortRaw) const
{
    return params_.longOnly ? longRaw : longRaw - shortRaw;
}

double SimulatedTradingExecutor::composeNetReturn(double longRaw, double shortRaw,
                                                  double longCost, double shortCost) const
{
    const double longNet = (1.0 + longRaw) * (1.0 - longCost) / (1.0 + longCost) - 1.0;
    if (params_.longOnly) return longNet;
    const double shortNet = (1.0 + shortRaw) * (1.0 - shortCost) / (1.0 + shortCost) - 1.0;
    return longNet - shortNet;
}

double SimulatedTradingExecutor::composeTurnover(double longTurnover, double shortTurnover) const
{
    return params_.longOnly ? longTurnover : (longTurnover + shortTurnover) / 2.0;
}

} // namespace factor::compute
