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

    if (sortedDates.size() < static_cast<size_t>(forwardDays + 1) || nGroups <= 0) {
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

    const size_t totalSteps = sortedDates.size() > static_cast<size_t>(forwardDays)
        ? (sortedDates.size() - forwardDays + rebalanceDays - 1) / rebalanceDays : 0;
    size_t stepIndex = 0;

    for (size_t di = 0; di + forwardDays < sortedDates.size(); di += rebalanceDays) {
        if (params_.onProgress && totalSteps > 0)
            params_.onProgress(static_cast<double>(stepIndex) / static_cast<double>(totalSteps));

        size_t sellDayIdx = di + forwardDays;
        const std::string& buyDate  = sortedDates[di];
        const std::string& sellDate = sortedDates[sellDayIdx];

        auto fvIt = factorValues.find(buyDate);
        if (fvIt == factorValues.end()) { ++stepIndex; continue; }

        // ═══ 因子排序 ═══
        std::vector<std::pair<std::string, double>> ranked;
        for (const auto& [sym, fv] : fvIt->second)
            if (std::isfinite(fv)) ranked.emplace_back(sym, fv);
        if (ranked.empty()) { ++stepIndex; continue; }

        std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

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
                if (col < 0 || col >= priceView.columnCount
                    || di >= static_cast<size_t>(priceView.rowCount)
                    || sellDayIdx >= static_cast<size_t>(priceView.rowCount))
                    continue;

                double bp = priceView.data[static_cast<size_t>(di) * rowStride + col];
                double sp = priceView.data[sellDayIdx * rowStride + col];
                if (haveAdjust) {
                    const auto& adjView = usePreAdjust ? preAdjustView : postAdjustView;
                    if (adjView.isValid() && adjView.rowCount > 0 && col < adjView.columnCount) {
                        const int32_t adjStride = adjView.rowStride >= adjView.columnCount
                            ? adjView.rowStride : adjView.columnCount;
                        double ba = adjView.data[static_cast<size_t>(di) * adjStride + col];
                        double sa = adjView.data[sellDayIdx * adjStride + col];
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
        std::unordered_set<std::string> newLong, newShort;
        for (size_t i = 0; i < groupSize; ++i) newLong.insert(ranked[i].first);
        for (size_t i = N - groupSize; i < N; ++i) newShort.insert(ranked[i].first);

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
        double periodTurnover = (periodLongTurnover + periodShortTurnover) / 2.0;

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
                if (di < static_cast<size_t>(priceView.rowCount)
                    && col >= 0 && col < priceView.columnCount)
                    return priceView.data[static_cast<size_t>(di) * rowStride + col];
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

            double longRaw  = dayGroupRawReturns[0];
            double shortRaw = dayGroupRawReturns[nGroups - 1];
            double lc = periodLongTurnover  * costPerTrade;
            double sc = periodShortTurnover * costPerTrade;
            double longNet  = (1.0 + longRaw)  * (1.0 - lc) / (1.0 + lc) - 1.0;
            double shortNet = (1.0 + shortRaw) * (1.0 - sc) / (1.0 + sc) - 1.0;
            result.periodTrackings.push_back({
                buyDate,
                static_cast<int32_t>(longHolding.size()),
                static_cast<int32_t>(shortHolding.size()),
                longBought, longSold, shortBought, shortSold,
                periodLongTurnover, periodShortTurnover,
                longRaw, shortRaw,
                longNet - shortNet
            });
        }

        // ═══ 策略日收益：多空价差 - 换手成本 ═══
        double longRaw  = dayGroupRawReturns[0];
        double shortRaw = dayGroupRawReturns[nGroups - 1];
        double dailyRawRet = longRaw - shortRaw;

        // 只对换手部分扣费
        double longCost  = periodLongTurnover  * costPerTrade;
        double shortCost = periodShortTurnover * costPerTrade;
        double longNet   = (1.0 + longRaw)  * (1.0 - longCost)  / (1.0 + longCost)  - 1.0;
        double shortNet  = (1.0 + shortRaw) * (1.0 - shortCost) / (1.0 + shortCost) - 1.0;
        double dailyCostAdjRet = longNet - shortNet;

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
    result.groups.resize(nGroups);
    for (int32_t g = 0; g < nGroups; ++g) {
        GroupBacktestMetrics& gm = result.groups[g];
        gm.groupIndex = g + 1;
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
            std::sort(allFiniteVals.begin(), allFiniteVals.end(), std::greater<double>());
            const size_t Nvals = allFiniteVals.size();
            const size_t gSize = Nvals / nGroups;
            for (int32_t g = 0; g < nGroups; ++g) {
                size_t startIdx = g * gSize;
                size_t endIdx = (g + 1 == nGroups) ? Nvals - 1 : startIdx + gSize - 1;
                if (startIdx < Nvals) {
                    result.groups[g].maxFactorValue = allFiniteVals[startIdx];        // 降序排列，startIdx 是组内最大
                    result.groups[g].minFactorValue = allFiniteVals[std::min(endIdx, Nvals - 1)]; // endIdx 是组内最小
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

} // namespace factor::compute
