#include "factor_compute/SimulatedTradingExecutor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace factor::compute {

SimulatedTradingExecutor::SimulatedTradingExecutor(const SimulatedTradingParams& params)
    : params_(params)
{
}

SimulatedTradingResult SimulatedTradingExecutor::execute(
    const FactorValuesByDate& factorValues,
    const std::vector<std::string>& sortedDates,
    NumericConstMatrixView priceView,
    const std::vector<InstrumentId>& instrumentIds,
    const std::unordered_map<uint32_t, std::string>& instrumentIdToSymbol) const
{
    SimulatedTradingResult result;
    const int32_t nGroups = params_.numGroups;
    const int32_t forwardDays = std::max(1, params_.forwardDays);
    const int32_t rebalanceDays = std::max(1, params_.rebalanceDays);
    const double commissionRate = params_.commissionRate;
    const double slippageRate = params_.slippageRate;
    const double costPerTrade = commissionRate + slippageRate;
    const double maxFwdRetAbs = params_.maxFwdRetAbsLimit;
    const double riskFreeRate = params_.riskFreeRate;

    if (sortedDates.size() < static_cast<size_t>(forwardDays + 1) || nGroups <= 0) {
        return result;
    }

    // 构建 instrumentId → column 索引映射
    std::unordered_map<uint32_t, int32_t> idToCol;
    for (int32_t ci = 0; ci < static_cast<int32_t>(instrumentIds.size()); ++ci) {
        idToCol[instrumentIds[ci].value] = ci;
    }

    const int32_t rowCount = priceView.rowCount;
    const int32_t rowStride = priceView.rowStride;

    std::vector<double> groupAccumReturns(nGroups, 0.0);
    std::vector<int32_t> groupValidDays(nGroups, 0);
    std::vector<int64_t> groupTotalStocks(nGroups, 0);
    std::vector<int32_t> groupPeriodCount(nGroups, 0);
    std::vector<double> strategyDailyReturns;

    // 换手追踪：记录上期各组股票集合
    std::vector<std::unordered_set<std::string>> prevGroupStocks(nGroups);
    double totalTurnover = 0.0;
    int32_t turnoverPeriods = 0;

    double equity = params_.initialCapital;
    double maxEquity = params_.initialCapital;

    const size_t totalSteps = sortedDates.size() > static_cast<size_t>(forwardDays)
        ? (sortedDates.size() - forwardDays + rebalanceDays - 1) / rebalanceDays : 0;
    size_t stepIndex = 0;

    for (size_t di = 0; di + forwardDays < sortedDates.size(); di += rebalanceDays) {
        if (params_.onProgress && totalSteps > 0) {
            params_.onProgress(static_cast<double>(stepIndex) / static_cast<double>(totalSteps));
        }
        ++stepIndex;

        size_t sellDayIdx = di + forwardDays;
        const std::string& curDate = sortedDates[di];
        const std::string& sellDate = sortedDates[sellDayIdx];

        auto fvIt = factorValues.find(curDate);
        if (fvIt == factorValues.end()) continue;

        // 按因子值排序标的
        std::vector<std::pair<std::string, double>> ranked;
        for (const auto& [sym, fv] : fvIt->second) {
            if (std::isfinite(fv)) {
                ranked.emplace_back(sym, fv);
            }
        }
        if (ranked.empty()) continue;

        std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        const size_t N = ranked.size();
        if (N < static_cast<size_t>(nGroups)) continue;
        const size_t groupSize = N / nGroups;

        // 构建买入日和卖出日价格映射
        std::unordered_map<std::string, double> buyPrice;
        std::unordered_map<std::string, double> sellPrice;
        for (const auto& [sym, _] : fvIt->second) {
            for (const auto& [id, symStr] : instrumentIdToSymbol) {
                if (symStr == sym) {
                    auto colIt = idToCol.find(id);
                    if (colIt != idToCol.end()) {
                        const int32_t col = colIt->second;
                        const double bp = priceView.data[static_cast<int32_t>(di) * rowStride + col];
                        const double sp = priceView.data[static_cast<int32_t>(sellDayIdx) * rowStride + col];
                        if (std::isfinite(bp) && bp > 1e-9) {
                            buyPrice[sym] = bp;
                        }
                        if (std::isfinite(sp) && sp > 1e-9) {
                            sellPrice[sym] = sp;
                        }
                    }
                    break;
                }
            }
        }

        std::vector<double> dayGroupReturns(nGroups, 0.0);
        std::vector<std::unordered_set<std::string>> curGroupStocks(nGroups);
        int32_t validGroups = 0;

        for (int32_t g = 0; g < nGroups; ++g) {
            size_t start = g * groupSize;
            size_t end = (g + 1 == nGroups) ? N : start + groupSize;
            double sumRet = 0.0;
            int32_t cnt = 0;

            for (size_t i = start; i < end; ++i) {
                const auto& sym = ranked[i].first;
                curGroupStocks[g].insert(sym);
                auto buyIt = buyPrice.find(sym);
                auto sellIt = sellPrice.find(sym);
                if (buyIt != buyPrice.end() && sellIt != sellPrice.end()
                    && std::abs(buyIt->second) > 1e-9) {
                    double fwdRet = (sellIt->second / buyIt->second) - 1.0;
                    if (std::isfinite(fwdRet) && std::abs(fwdRet) < maxFwdRetAbs) {
                        sumRet += fwdRet;
                        ++cnt;
                    }
                }
            }

            if (cnt > 0) {
                // 扣除手续费+滑点（每组一次换仓）
                double avgRet = sumRet / cnt;
                avgRet -= costPerTrade;  // 交易成本
                groupAccumReturns[g] += avgRet;
                groupValidDays[g]++;
                groupTotalStocks[g] += cnt;
                groupPeriodCount[g]++;
                dayGroupReturns[g] = avgRet;
                ++validGroups;
            }
        }

        if (validGroups > 0) {
            // 计算本期换手率：对比当期各组股票 vs 上期
            if (turnoverPeriods > 0) {
                double periodTurnover = 0.0;
                for (int32_t g = 0; g < nGroups; ++g) {
                    if (prevGroupStocks[g].empty() || curGroupStocks[g].empty()) continue;
                    int32_t stayed = 0;
                    for (const auto& sym : curGroupStocks[g]) {
                        if (prevGroupStocks[g].count(sym)) ++stayed;
                    }
                    int32_t left = static_cast<int32_t>(prevGroupStocks[g].size()) - stayed;
                    int32_t entered = static_cast<int32_t>(curGroupStocks[g].size()) - stayed;
                    int32_t total = static_cast<int32_t>(curGroupStocks[g].size());
                    if (total > 0) {
                        periodTurnover += static_cast<double>(entered + left) / (2.0 * total);
                    }
                }
                totalTurnover += periodTurnover / nGroups;
                result.periodTurnovers.push_back(periodTurnover / nGroups);
            }
            ++turnoverPeriods;
            prevGroupStocks = std::move(curGroupStocks);

            double dailyStrategyRet = 0.0;
            for (int32_t g = 0; g < nGroups; ++g) {
                dailyStrategyRet += dayGroupReturns[g];
            }
            dailyStrategyRet /= validGroups;

            strategyDailyReturns.push_back(dailyStrategyRet);
            equity *= (1.0 + dailyStrategyRet);
            if (equity > maxEquity) maxEquity = equity;
            double dd = (maxEquity > 1e-9) ? (maxEquity - equity) / maxEquity : 0.0;
            if (dd > result.maxDrawdown) result.maxDrawdown = dd;
        }
    }

    // 计算分组指标
    result.groups.resize(nGroups);
    for (int32_t g = 0; g < nGroups; ++g) {
        GroupBacktestMetrics& gm = result.groups[g];
        gm.groupIndex = g + 1;
        gm.stockCount = groupPeriodCount[g] > 0
            ? static_cast<int32_t>(groupTotalStocks[g] / groupPeriodCount[g])
            : 0;
        if (groupValidDays[g] > 0) {
            gm.returnRate = groupAccumReturns[g] / groupValidDays[g];
            gm.annualizedReturn = gm.returnRate * 252.0;
        }
    }

    // 计算执行指标
    const int32_t nPeriods = static_cast<int32_t>(strategyDailyReturns.size());
    if (nPeriods > 0) {
        result.totalReturn = (equity - params_.initialCapital) / params_.initialCapital;
        result.finalEquity = equity;

        // 年化：每个 period 代表 rebalanceDays 个交易日
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

        // 夏普比率 = (年化收益 - 无风险利率) / 年化波动率
        result.sharpeRatio = (result.annualStdDev > 1e-12)
            ? ((result.annualizedReturn - riskFreeRate) / result.annualStdDev) : 0.0;
        result.validSampleCount = nPeriods;
        result.turnoverRate = turnoverPeriods > 0 ? totalTurnover / turnoverPeriods : 0.0;
    }

    // 计算分组因子值 min/max
    {
        std::vector<double> allFiniteVals;
        for (const auto& [_, symMap] : factorValues) {
            for (const auto& [_, fv] : symMap) {
                if (std::isfinite(fv)) allFiniteVals.push_back(fv);
            }
        }
        if (!allFiniteVals.empty()) {
            std::sort(allFiniteVals.begin(), allFiniteVals.end(), std::greater<double>());
            const size_t Nvals = allFiniteVals.size();
            const size_t gSize = Nvals / nGroups;
            for (int32_t g = 0; g < nGroups; ++g) {
                size_t startIdx = g * gSize;
                size_t endIdx = (g + 1 == nGroups) ? Nvals - 1 : startIdx + gSize - 1;
                if (startIdx < Nvals) {
                    result.groups[g].minFactorValue = allFiniteVals[startIdx];
                    result.groups[g].maxFactorValue = allFiniteVals[std::min(endIdx, Nvals - 1)];
                }
            }
        }
    }

    result.strategyDailyReturns = std::move(strategyDailyReturns);
    return result;
}

} // namespace factor::compute