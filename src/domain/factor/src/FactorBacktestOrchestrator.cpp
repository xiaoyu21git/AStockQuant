#include "FactorBacktestOrchestrator.h"
#include "factor_compute/FactorEngine.h"
#include "factor_compute/MarketDataViewHistoricalAdapter.h"
#include "BacktestScheduler.h"
#include "CompositeFactorConfig.h"
#include "FactorMetricsCalculator.h"
#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace Factor::backtest {

FactorBacktestOrchestrator::FactorBacktestOrchestrator() = default;
FactorBacktestOrchestrator::~FactorBacktestOrchestrator() = default;

void FactorBacktestOrchestrator::setScheduler(domain::scheduler::BacktestScheduler* scheduler) {
    m_scheduler = scheduler;
}

void FactorBacktestOrchestrator::setDataService(factor::compute::BacktestDataService* dataService) {
    m_dataService = dataService;
    if (m_scheduler) {
        m_scheduler->setDataService(dataService);
    }
}

void FactorBacktestOrchestrator::setFactorEngine(factor::compute::FactorEngine* engine) {
    m_engine = engine;
}

void FactorBacktestOrchestrator::setReporter(factor::compute::BacktestReporter* reporter) {
    m_reporter = reporter;
}

std::vector<std::string> FactorBacktestOrchestrator::sortedDatesFrom(
    const std::map<std::string, std::map<std::string, double>>& fvByDate)
{
    std::vector<std::string> dates;
    dates.reserve(fvByDate.size());
    for (const auto& [date, _] : fvByDate) {
        dates.push_back(date);
    }
    std::sort(dates.begin(), dates.end());
    return dates;
}

void FactorBacktestOrchestrator::run(
    const BacktestRunConfig& config,
    FactorOrchestratorProgressCallback onProgress,
    FactorOrchestratorResultCallback onComplete)
{
    auto emitError = [&](const char* msg) {
        auto err = foundation::json::JsonFacade::createObject();
        err.set("error", foundation::json::JsonFacade::createString(msg));
        err.set("metrics", foundation::json::JsonFacade::createObject());
        if (onComplete) onComplete(err.toString());
    };

    if (!m_scheduler) { emitError("scheduler not set"); return; }
    if (!m_engine)   { emitError("factor engine not set"); return; }
    if (!m_engine->hasInstanceManager()) {
        emitError("FactorService not initialized — factor instances unavailable");
        return;
    }

    // 从 DataSvc 获取实际标的数量
    size_t instrumentCount = 0;
    if (m_dataService) {
        auto firstBatch = m_dataService->loadBatch(0);
        if (firstBatch.marketView && !firstBatch.marketView->instruments().empty()) {
            instrumentCount = firstBatch.marketView->instruments().size();
        }
    }
    size_t batchSize = instrumentCount > 0 ? instrumentCount : 1;
    auto plan = m_scheduler->submit(instrumentCount > 0 ? instrumentCount : 1, batchSize);
    if (onProgress) onProgress(5.0, "data loaded");

    factor::compute::BacktestReporterInput reporterInput;
    const factor::compute::IMarketDataView* lastMarketView = nullptr;

    const bool isComposite = (config.factorMode == FactorMode::Composite && !config.compositeChildren.empty());
    const auto& factorIdList = config.factorIds.empty()
        ? std::vector<std::string>{"backtest_factor"}
        : config.factorIds;

    const size_t totalFactors = isComposite ? config.compositeChildren.size() : factorIdList.size();
    size_t factorIndex = 0;

    m_scheduler->forEachBatch(plan,
        [&](const factor::compute::MarketMatrixBatch& marketMatrix) {
            lastMarketView = marketMatrix.marketView;

            if (isComposite) {
                // ── 组合因子路径：逐个子因子计算 → 加权合并 ──
                // 收集所有子因子的原始结果
                std::vector<std::map<std::string, std::map<std::string, double>>> childResults;
                childResults.reserve(config.compositeChildren.size());
                double totalWeight = 0.0;

                for (const auto& child : config.compositeChildren) {
                    factor::compute::FactorCacheKey cacheKey;
                    cacheKey.factorName = child.instanceId;
                    auto factorResult = m_engine->compute(marketMatrix, cacheKey);
                    childResults.push_back(std::move(factorResult.factorValues));
                    totalWeight += child.weight;
                }

                if (totalWeight <= 0.0) {
                    return;  // 总权重为0，跳过
                }

                // 收集所有日期和标号的并集
                std::map<std::string, std::map<std::string, double>> combinedValues;
                for (const auto& childFV : childResults) {
                    for (const auto& [date, symMap] : childFV) {
                        for (const auto& [symbol, _] : symMap) {
                            combinedValues[date][symbol] = 0.0;  // 占位
                        }
                    }
                }

                // 加权合并
                for (const auto& [date, symMap] : combinedValues) {
                    for (const auto& [symbol, _] : symMap) {
                        double weightedSum = 0.0;
                        double presentWeight = 0.0;
                        for (size_t ci = 0; ci < childResults.size(); ++ci) {
                            const auto& childFV = childResults[ci];
                            auto dateIt = childFV.find(date);
                            if (dateIt == childFV.end()) continue;
                            auto symIt = dateIt->second.find(symbol);
                            if (symIt == dateIt->second.end()) continue;
                            const double value = symIt->second;
                            if (!std::isfinite(value)) continue;
                            const double directedValue = config.compositeChildren[ci].ascending ? value : -value;
                            weightedSum += config.compositeChildren[ci].weight * directedValue;
                            presentWeight += config.compositeChildren[ci].weight;
                        }
                        if (presentWeight / totalWeight >= config.compositeMinCoverageRatio) {
                            reporterInput.factorValuesByDate[date][symbol] =
                                presentWeight > 0.0 ? weightedSum / presentWeight : 0.0;
                        }
                    }
                }
            } else {
                for (const auto& factorId : factorIdList) {
                    if (m_engine) {
                        factor::compute::FactorCacheKey cacheKey;
                        cacheKey.factorName = factorId;
                        auto factorResult = m_engine->compute(marketMatrix, cacheKey);

                        // 汇总因子值到 Reporter
                        for (const auto& [date, symbolValues] : factorResult.factorValues) {
                            for (const auto& [symbol, value] : symbolValues) {
                                reporterInput.factorValuesByDate[date][symbol] = value;
                            }
                        }
                    }
                }
            }
        });

    if (onProgress) onProgress(40.0, "factors computed");

    // compute() 内部通过 buildViewForFields 构建了 MarketView
    // 需要重新获取 view 指针，确保模拟交易有正确的价格矩阵
    if (m_dataService) {
        auto updatedBatch = m_dataService->loadBatch(0);
        if (updatedBatch.marketView) {
            lastMarketView = updatedBatch.marketView;
        }
    }

    // ── 模拟成交 ──
    factor::compute::SimulatedTradingResult tradingResult;
    bool hasFactorValues = !reporterInput.factorValuesByDate.empty();
    if (!hasFactorValues) {
        emitError("factor computation produced no values — check factor IDs and dataset");
        return;
    }

    factor::compute::SimulatedTradingParams params;
        params.numGroups       = config.numGroups;
        params.forwardDays     = std::max(1, config.forwardDays);
        params.rebalanceDays   = std::max(1, config.rebalanceDays);
        params.commissionRate  = config.commissionRate;
        params.slippageRate    = config.slippageRate;
        params.riskFreeRate    = config.riskFreeRate;
        params.initialCapital  = config.initialCapital;
        params.onProgress      = [&](double pct) {
            if (onProgress) onProgress(40.0 + pct * 20.0, "simulating trades");
        };

        m_executor = std::make_unique<factor::compute::SimulatedTradingExecutor>(params);

        auto sortedDates = sortedDatesFrom(reporterInput.factorValuesByDate);

        // 从因子值中提取标的符号集合 → 构建 instrumentIds + idToSymbol
        std::unordered_map<uint32_t, std::string> instrumentIdToSymbol;
        std::vector<factor::compute::InstrumentId> instrumentIds;
        {
            uint32_t nextId = 0;
            std::unordered_map<std::string, uint32_t> symbolToId;
            for (const auto& [date, symMap] : reporterInput.factorValuesByDate) {
                for (const auto& [sym, _] : symMap) {
                    if (symbolToId.find(sym) == symbolToId.end()) {
                        symbolToId[sym] = nextId;
                        instrumentIdToSymbol[nextId] = sym;
                        instrumentIds.push_back(factor::compute::InstrumentId{nextId});
                        ++nextId;
                    }
                }
            }
        }

        // 构建价格矩阵（close prices）
        // 若 marketView 可用则使用真实数据，否则用全1矩阵（不产生收益影响）
        factor::compute::NumericConstMatrixView priceView{};
        const int32_t numDates = static_cast<int32_t>(sortedDates.size());
        const int32_t numInsts = static_cast<int32_t>(instrumentIds.size());

        if (lastMarketView && numDates > 0 && numInsts > 0) {
            priceView = lastMarketView->close();
        } else {
            // 无真实价格时，构建全 1.0 的临时矩阵（避免空指针）
            // 警告：价格为 1.0 意味着成交模拟完全失真，应在上游校验数据完整性
            INTERNAL_WARN_STREAM << "[FactorBacktestOrchestrator] No real price data available, "
                                 << "using default price 1.0 — fill simulation will be inaccurate";
            static constexpr float kDefaultPrice = 1.0f;
            static std::vector<factor::compute::signal_value_t> s_defaultPrices;
            const size_t needed = static_cast<size_t>(std::max(1, numDates))
                                * static_cast<size_t>(std::max(1, numInsts));
            if (s_defaultPrices.size() < needed) {
                s_defaultPrices.assign(needed, kDefaultPrice);
            }
            priceView.data       = s_defaultPrices.data();
            priceView.rowCount    = std::max(1, numDates);
            priceView.columnCount = std::max(1, numInsts);
            priceView.rowStride   = std::max(1, numInsts);
        }

        // 转换为 SimulatedTradingExecutor 定义的 unordered_map 类型
        factor::compute::SimulatedTradingExecutor::FactorValuesByDate fvByDate;
        for (const auto& [date, symMap] : reporterInput.factorValuesByDate) {
            std::unordered_map<std::string, double> innerMap;
            for (const auto& [sym, val] : symMap) {
                innerMap[sym] = val;
            }
            fvByDate[date] = std::move(innerMap);
        }

        tradingResult = m_executor->execute(
            fvByDate,
            sortedDates,
            priceView,
            instrumentIds,
            instrumentIdToSymbol);

    INTERNAL_DEBUG_STREAM << "[Orchestrator] groups=" << tradingResult.groups.size()
        << " sharpe=" << tradingResult.sharpeRatio
        << " annualRet=" << tradingResult.annualizedReturn
        << " maxDD=" << tradingResult.maxDrawdown;

    if (onProgress) onProgress(60.0, "trading simulated");

    // ── Reporter 分析 ──
    factor::compute::BacktestReporterOutput reporterOutput;
    if (m_reporter && !reporterInput.factorValuesByDate.empty()) {
        reporterInput.numGroups      = config.numGroups;
        reporterInput.forwardDays    = config.forwardDays;
        reporterInput.commissionRate = config.commissionRate;
        reporterInput.slippageRate   = config.slippageRate;
        reporterInput.riskFreeRate   = config.riskFreeRate;
        reporterOutput = m_reporter->analyze(reporterInput);
    }

    if (onProgress) onProgress(80.0, "building result");

    // ── 产出 JSON 结果 ──
    if (onComplete) {
        // ── 构建 Calculator 输入 ──
        ::factor::BacktestConfig btConfig;
        btConfig.forwardDays    = std::max(1, config.forwardDays);
        btConfig.rebalanceDays  = std::max(1, config.rebalanceDays);
        btConfig.numGroups      = config.numGroups;
        btConfig.commissionRate = config.commissionRate;
        btConfig.slippageRate   = config.slippageRate;
        btConfig.riskFreeRate   = config.riskFreeRate;

        if (onProgress) onProgress(50.0, "computing IC");

        // ── 预先排序日期（IC 计算 + JSON 输出都需要）──
        auto allSortedDates = sortedDatesFrom(reporterInput.factorValuesByDate);

        // ── 计算 Rank IC（因子值与真实前向收益的 Spearman 秩相关）──
        ::factor::ICIRResult icir;
        {
            std::vector<double> icSeries;
            const auto& sortedDates = allSortedDates;
            const int fwdDays = std::max(1, config.forwardDays);

            // 构建 symbol → price-matrix-column 映射
            std::unordered_map<std::string, int32_t> symToCol;
            if (lastMarketView) {
                // 用 CachedMarketDataViewHistoricalAdapter 获取标的符号列表
                factor::compute::CachedMarketDataViewHistoricalAdapter adapter(*lastMarketView);
                auto symbols = adapter.getAvailableSymbols("");
                for (int32_t ci = 0; ci < static_cast<int32_t>(symbols.size()); ++ci) {
                    symToCol[symbols[ci]] = ci;
                }
            }

            auto priceView = lastMarketView ? lastMarketView->close()
                : factor::compute::NumericConstMatrixView{};
            const int32_t rowStride = priceView.rowStride;

            const size_t icTotalSteps = sortedDates.size() > static_cast<size_t>(fwdDays)
                ? sortedDates.size() - fwdDays : 0;
            size_t icStep = 0;
            for (size_t di = 0; di + static_cast<size_t>(fwdDays) < sortedDates.size(); ++di) {
                if (onProgress && icTotalSteps > 0 && icStep % 10 == 0) {
                    onProgress(50.0 + (static_cast<double>(icStep) / icTotalSteps) * 15.0, "computing IC");
                }
                ++icStep;
                const std::string& dateNow = sortedDates[di];

                auto itNow = reporterInput.factorValuesByDate.find(dateNow);
                if (itNow == reporterInput.factorValuesByDate.end()) continue;

                // 收集因子值 + 前向收益
                std::vector<std::pair<double, double>> pairs; // {factorValue, fwdReturn}
                for (const auto& [sym, fv] : itNow->second) {
                    if (!std::isfinite(fv)) continue;
                    auto colIt = symToCol.find(sym);
                    if (colIt == symToCol.end()) continue;

                    const int32_t col = colIt->second;
                    double priceNow  = priceView.data[static_cast<int32_t>(di) * rowStride + col];
                    double priceFwd  = priceView.data[static_cast<int32_t>(di + fwdDays) * rowStride + col];

                    if (priceNow > 1e-9 && std::isfinite(priceNow)
                        && priceFwd > 1e-9 && std::isfinite(priceFwd)) {
                        double fwdRet = (priceFwd / priceNow) - 1.0;
                        if (std::isfinite(fwdRet) && std::abs(fwdRet) < 0.5) {
                            pairs.emplace_back(fv, fwdRet);
                        }
                    }
                }
                if (pairs.size() < 30) continue;

                // 因子值 → 秩
                std::sort(pairs.begin(), pairs.end(),
                    [](auto& a, auto& b) { return a.first < b.first; });
                std::vector<double> fRanks(pairs.size()), rRanks(pairs.size());
                for (size_t i = 0; i < pairs.size(); ++i) {
                    fRanks[i] = static_cast<double>(i);
                    rRanks[i] = pairs[i].second;
                }

                // 前向收益 → 秩
                std::vector<size_t> idx(rRanks.size());
                for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
                std::sort(idx.begin(), idx.end(),
                    [&](size_t a, size_t b) { return rRanks[a] < rRanks[b]; });
                for (size_t i = 0; i < idx.size(); ++i) rRanks[idx[i]] = static_cast<double>(i);

                // Spearman
                double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
                double n = static_cast<double>(pairs.size());
                for (size_t i = 0; i < pairs.size(); ++i) {
                    sumX += fRanks[i]; sumY += rRanks[i];
                    sumXY += fRanks[i] * rRanks[i];
                    sumX2 += fRanks[i] * fRanks[i];
                    sumY2 += rRanks[i] * rRanks[i];
                }
                double num = n * sumXY - sumX * sumY;
                double den = std::sqrt((n * sumX2 - sumX * sumX) * (n * sumY2 - sumY * sumY));
                if (den > 1e-12) icSeries.push_back(num / den);
            }

            if (!icSeries.empty()) {
                double sum = 0, sumSq = 0;
                int posCount = 0;
                for (double v : icSeries) {
                    sum += v; sumSq += v * v;
                    if (v > 0) ++posCount;
                }
                icir.icMean  = sum / static_cast<double>(icSeries.size());
                icir.icStd   = std::sqrt(std::max(0.0,
                    sumSq / static_cast<double>(icSeries.size()) - icir.icMean * icir.icMean));
                icir.ir      = icir.icStd > 1e-12 ? icir.icMean / icir.icStd : 0.0;
                icir.icPositiveRatio = static_cast<double>(posCount) / static_cast<double>(icSeries.size());
                icir.icSeries = std::move(icSeries);
            }
        }

        ::factor::GroupBacktestResult groupRes;
        for (const auto& gm : tradingResult.groups) {
            groupRes.groupReturns.push_back(gm.returnRate);
            groupRes.groupStockCounts.push_back(static_cast<int>(gm.stockCount));
            groupRes.minFactorValues.push_back(gm.minFactorValue);
            groupRes.maxFactorValues.push_back(gm.maxFactorValue);
        }
        if (!groupRes.groupReturns.empty()) {
            groupRes.topGroupReturn    = groupRes.groupReturns.front();
            groupRes.bottomGroupReturn = groupRes.groupReturns.back();
            groupRes.longShortReturn   = groupRes.topGroupReturn - groupRes.bottomGroupReturn;
        }

        if (onProgress) onProgress(70.0, "computing metrics");

        // ── 委托 FactorBacktestMetricsCalculator（真实序列，不再传空）──
        ::factor::BacktestResult btResult;
        btResult.config      = btConfig;
        btResult.icirResult  = icir;
        btResult.groupResult = groupRes;

        std::vector<std::vector<double>> groupReturnSeriesByGroup;
        ::factor::FactorBacktestMetricsCalculator::Inputs inputs{
            btConfig,
            btResult.icirResult,
            btResult.groupResult,
            tradingResult.strategyDailyReturns,
            tradingResult.strategyDailyReturns,
            tradingResult.strategyDailyReturns,
            tradingResult.periodTurnovers,            // turnoverSeries
            {},                                       // longShortDates (暂无)
            groupReturnSeriesByGroup
        };
        ::factor::FactorBacktestMetricsCalculator::populateResultMetrics(btResult, inputs);

        // ── 用 JsonFacade 序列化 BacktestResult → JSON ──
        using J = foundation::json::JsonFacade;
        auto root = J::createObject();
        root.set("status",       J::createString("SUCCESS"));
        root.set("factorValues", J::createArray());

        // 回测时间区间（供 QML 显示）
        if (!allSortedDates.empty()) {
            root.set("startDate", J::createString(allSortedDates.front()));
            root.set("endDate",   J::createString(allSortedDates.back()));
        }

        auto metrics = J::createObject();

        // groups
        auto groupsArr = J::createArray();
        for (const auto& gm : tradingResult.groups) {
            auto gObj = J::createObject();
            gObj.set("groupName",       J::createString("G" + std::to_string(gm.groupIndex)));
            gObj.set("groupIndex",      J::createDouble(static_cast<double>(gm.groupIndex)));
            gObj.set("returnRate",      J::createDouble(gm.returnRate));
            gObj.set("annualizedReturn",J::createDouble(gm.annualizedReturn));
            gObj.set("cumulativeReturn",J::createDouble(gm.returnRate));
            gObj.set("stockCount",      J::createDouble(static_cast<double>(gm.stockCount)));
            gObj.set("minFactorValue",  J::createDouble(gm.minFactorValue));
            gObj.set("maxFactorValue",  J::createDouble(gm.maxFactorValue));
            groupsArr.push_back(gObj);
        }
        metrics.set("groups", groupsArr);

        // trading — Calculator 输出
        auto t = J::createObject();
        t.set("sharpe",           J::createDouble(btResult.sharpeRatio));
        t.set("annualizedReturn", J::createDouble(btResult.annualReturn));
        t.set("annualStdDev",     J::createDouble(btResult.volatility));
        t.set("maxDrawdown",      J::createDouble(btResult.maxDrawdown));
        t.set("totalReturn",      J::createDouble(tradingResult.totalReturn));
        metrics.set("trading", t);

        // ic — Calculator 输出（对齐 FactorQualityMetrics16View）
        auto ic = J::createObject();
        ic.set("value",      J::createDouble(btResult.factorMetrics.rankIcMean));
        ic.set("ir",         J::createDouble(btResult.factorMetrics.rankIcir));
        ic.set("std",        J::createDouble(btResult.factorMetrics.rankIcStd));
        ic.set("winRate",    J::createDouble(btResult.factorMetrics.icWinRate));
        ic.set("pValue",     J::createDouble(btResult.factorMetrics.icPValue));
        ic.set("tStat",      J::createDouble(btResult.factorMetrics.icTStat));
        ic.set("halfLife",   J::createDouble(static_cast<double>(btResult.factorMetrics.icHalfLife)));
        metrics.set("ic", ic);

        // factorMetrics — Calculator 输出的全部因子质量指标
        auto fm = J::createObject();
        fm.set("monotonicityScore",    J::createDouble(btResult.factorMetrics.monotonicityScore));
        fm.set("longShortSharpe",      J::createDouble(btResult.factorMetrics.longShortSharpe));
        fm.set("longShortAnnualReturn",J::createDouble(btResult.factorMetrics.longShortAnnualReturn));
        fm.set("costAdjustedSharpe",   J::createDouble(btResult.factorMetrics.costAdjustedSharpe));
        fm.set("annualTurnover",       J::createDouble(btResult.factorMetrics.annualTurnover));
        fm.set("alpha",                J::createDouble(btResult.factorMetrics.alpha));
        fm.set("monthlyWinRate",       J::createDouble(btResult.factorMetrics.monthlyWinRate));
        fm.set("numGroups",            J::createDouble(static_cast<double>(btResult.factorMetrics.numGroups)));
        metrics.set("factorMetrics", fm);

        // factorQuality — 综合评级
        auto fq = J::createObject();
        fq.set("rating", J::createDouble(static_cast<double>(btResult.factorMetrics.coreRating)));
        fq.set("label",  J::createString(
            btResult.factorMetrics.coreRating == ::factor::FactorBacktestMetrics::Rating::EXCELLENT ? "优秀" :
            btResult.factorMetrics.coreRating == ::factor::FactorBacktestMetrics::Rating::GOOD      ? "良好" :
            btResult.factorMetrics.coreRating == ::factor::FactorBacktestMetrics::Rating::PASS       ? "合格" : "不合格"));
        metrics.set("factorQuality", fq);

        // execution — Calculator 输出 + 基础信号计数
        auto ex = J::createObject();
        ex.set("totalSignals",     J::createDouble(static_cast<double>(reporterOutput.totalSignalCount)));
        ex.set("presentSignals",   J::createDouble(static_cast<double>(reporterOutput.presentSignalCount)));
        ex.set("turnoverRatio",    J::createDouble(tradingResult.turnoverRate));
        ex.set("sharpeRatio",      J::createDouble(btResult.sharpeRatio));
        ex.set("annualizedReturn", J::createDouble(btResult.annualReturn));
        ex.set("maxDrawdown",      J::createDouble(btResult.maxDrawdown));
        ex.set("totalReturn",      J::createDouble(tradingResult.totalReturn));
        ex.set("winRate",          J::createDouble(btResult.winRate));
        ex.set("profitFactor",     J::createDouble(btResult.profitFactor));
        ex.set("volatility",       J::createDouble(btResult.volatility));
        ex.set("sortinoRatio",     J::createDouble(btResult.sortinoRatio));
        ex.set("calmarRatio",      J::createDouble(btResult.calmarRatio));
        ex.set("valueAtRisk",      J::createDouble(btResult.valueAtRisk));
        ex.set("conditionalVaR",   J::createDouble(btResult.conditionalVaR));
        ex.set("finalEquity",      J::createDouble(tradingResult.finalEquity));
        ex.set("validSampleCount", J::createDouble(static_cast<double>(tradingResult.validSampleCount)));
        metrics.set("execution", ex);

        // returnSeries — 收益率序列（供 QML 净值曲线渲染）
        auto retSeries = J::createArray();
        for (double r : tradingResult.strategyDailyReturns) {
            retSeries.push_back(J::createDouble(r));
        }
        metrics.set("returnSeries", retSeries);

        root.set("metrics", metrics);
        onComplete(root.toString());
    }
    if (onProgress) onProgress(100.0, "completed");
}

} // namespace Factor::backtest
