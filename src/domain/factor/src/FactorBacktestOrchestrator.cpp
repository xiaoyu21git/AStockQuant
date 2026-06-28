#include "FactorBacktestOrchestrator.h"
#include "factor_compute/FactorEngine.h"
#include "factor_compute/ArrowMarketDataView.h"
#include "factor_compute/MarketDataViewHistoricalAdapter.h"
#include "BacktestScheduler.h"
#include "BaseFactor.h"
#include "FactorInstanceManager.h"
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
    if (!m_dataService || !m_dataService->getView()) {
        emitError("no market data view — cache dataset not loaded");
        return;
    }

    auto* arrowView = static_cast<factor::compute::ArrowMarketDataView*>(m_dataService->getView());
    if (arrowView->dates().empty() || arrowView->instruments().empty()) {
        emitError("empty dataset — no dates or instruments");
        return;
    }

    const bool isComposite = (config.factorMode == FactorMode::Composite && !config.compositeChildren.empty());
    const auto& factorIdList = config.factorIds.empty()
        ? std::vector<std::string>{"backtest_factor"}
        : config.factorIds;

    // ── 收集因子需要的额外字段 ──
    // 优先使用预检阶段预注入的字段，避免重复 createInstance + getDataRequirements
    std::vector<std::string> neededExtraFields;
    if (config.hasPreResolvedFields) {
        neededExtraFields = config.preResolvedExtraFields;
    } else {
        auto collectFields = [&](const std::string& fid) {
            auto factor = m_engine->instanceManager()->createInstance(fid);
            if (factor) {
                for (const auto& f : factor->getDataRequirements().requiredFields)
                    neededExtraFields.push_back(f);
                for (const auto& f : factor->getDataRequirements().optionalFields)
                    neededExtraFields.push_back(f);
            }
        };
        if (isComposite) {
            for (const auto& child : config.compositeChildren)
                collectFields(child.instanceId);
        } else {
            for (const auto& fid : factorIdList)
                collectFields(fid);
        }
        std::sort(neededExtraFields.begin(), neededExtraFields.end());
        neededExtraFields.erase(
            std::unique(neededExtraFields.begin(), neededExtraFields.end()),
            neededExtraFields.end());
        neededExtraFields.erase(
            std::remove_if(neededExtraFields.begin(), neededExtraFields.end(),
                [](const std::string& f) {
                    return f == "open" || f == "high" || f == "low" || f == "close" || f == "volume"
                        || f == "symbol" || f == "trade_date";
                }),
            neededExtraFields.end());
    }

    if (onProgress) onProgress(5.0, "data indexed");

    // ── 分块大小：每块约 60 个交易日 ──
    constexpr int kChunkDates = 60;
    const auto& allDates = arrowView->dates();
    const size_t totalDates = allDates.size();
    const size_t totalChunks = (totalDates + kChunkDates - 1) / kChunkDates;
    const int fwdDays = std::max(1, config.forwardDays);

    // 分块加载列名：核心 5 列 + 因子字段
    std::vector<std::string> chunkColumns = {"open", "high", "low", "close", "volume"};
    for (const auto& f : neededExtraFields)
        chunkColumns.push_back(f);

    factor::compute::BacktestReporterInput reporterInput;
    std::vector<std::pair<double, double>> icPairs; // {factorValue, forwardReturn}

    // ══════════════════════════════════════════════════════════════════════
    // 分块回测主循环
    // ══════════════════════════════════════════════════════════════════════
    for (size_t ci = 0; ci < totalChunks; ++ci) {
        if (onProgress) {
            double pct = 5.0 + (static_cast<double>(ci) / totalChunks) * 55.0;
            onProgress(pct, "chunk " + std::to_string(ci + 1) + "/" + std::to_string(totalChunks));
        }

        size_t start = ci * kChunkDates;
        size_t end = std::min(start + kChunkDates + static_cast<size_t>(fwdDays), totalDates);
        std::vector<factor::compute::DateKey> chunkDates(
            allDates.begin() + start, allDates.begin() + end);

            // 从 Arrow 文件直接加载该块数据（不经过全量缓存）
            auto chunkView = arrowView->makeChunkView(chunkDates, chunkColumns);
            if (!chunkView) continue;

            // 构建 MarketMatrixBatch
            factor::compute::MarketMatrixBatch chunkBatch;
            chunkBatch.batchIndex = ci;
            chunkBatch.marketView = chunkView.get();

            if (isComposite) {
                // ── 组合因子：逐子因子计算 → 加权合并 ──
                std::vector<std::map<std::string, std::map<std::string, double>>> childResults;
                childResults.reserve(config.compositeChildren.size());
                double totalWeight = 0.0;

                for (const auto& child : config.compositeChildren) {
                    factor::compute::FactorCacheKey cacheKey;
                    cacheKey.factorName = child.instanceId;
                    auto factorResult = m_engine->compute(chunkBatch, cacheKey);
                    childResults.push_back(std::move(factorResult.factorValues));
                    totalWeight += child.weight;
                }

                if (totalWeight > 0.0) {
                    std::map<std::string, std::map<std::string, double>> combinedValues;
                    for (const auto& childFV : childResults) {
                        for (const auto& [date, symMap] : childFV) {
                            for (const auto& [symbol, _] : symMap) {
                                combinedValues[date][symbol] = 0.0;
                            }
                        }
                    }
                    for (const auto& [date, symMap] : combinedValues) {
                        for (const auto& [symbol, _] : symMap) {
                            double weightedSum = 0.0;
                            double presentWeight = 0.0;
                            for (size_t ci2 = 0; ci2 < childResults.size(); ++ci2) {
                                const auto& childFV = childResults[ci2];
                                auto dateIt = childFV.find(date);
                                if (dateIt == childFV.end()) continue;
                                auto symIt = dateIt->second.find(symbol);
                                if (symIt == dateIt->second.end()) continue;
                                const double value = symIt->second;
                                if (!std::isfinite(value)) continue;
                                const double directedValue = config.compositeChildren[ci2].ascending ? value : -value;
                                weightedSum += config.compositeChildren[ci2].weight * directedValue;
                                presentWeight += config.compositeChildren[ci2].weight;
                            }
                            if (presentWeight / totalWeight >= config.compositeMinCoverageRatio) {
                                reporterInput.factorValuesByDate[date][symbol] =
                                    presentWeight > 0.0 ? weightedSum / presentWeight : 0.0;
                            }
                        }
                    }
                }
            } else {
                // ── 单/多因子：逐因子计算 ──
                for (const auto& factorId : factorIdList) {
                    factor::compute::FactorCacheKey cacheKey;
                    cacheKey.factorName = factorId;
                    auto factorResult = m_engine->compute(chunkBatch, cacheKey);

                    for (const auto& [date, symbolValues] : factorResult.factorValues) {
                        for (const auto& [symbol, value] : symbolValues) {
                            reporterInput.factorValuesByDate[date][symbol] = value;
                        }
                    }
                }
            }

            // ── 增量累积 IC 对 ──
            // 只对不超出前向窗口的日期计算
            size_t computeEnd = (ci < totalChunks - 1)
                ? chunkDates.size() - static_cast<size_t>(fwdDays)
                : (chunkDates.size() > static_cast<size_t>(fwdDays) ? chunkDates.size() - fwdDays : 0);

            for (size_t di = 0; di < computeEnd; ++di) {
                char dateBuf[16];
                int dv = chunkDates[di].value;
                std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
                              dv / 10000, (dv / 100) % 100, dv % 100);
                std::string dateNow(dateBuf);

                auto itNow = reporterInput.factorValuesByDate.find(dateNow);
                if (itNow == reporterInput.factorValuesByDate.end()) continue;

                for (const auto& [sym, fv] : itNow->second) {
                    if (!std::isfinite(fv)) continue;

                    // 获取当前和前向 close 价格
                    auto closeView = chunkView->close();
                    // 查找 symbol 的列索引
                    int32_t symCol = -1;
                    {
                        auto syms = arrowView->symbolStrings();
                        for (size_t si = 0; si < syms.size(); ++si) {
                            if (syms[si] == sym) { symCol = static_cast<int32_t>(si); break; }
                        }
                    }
                    if (symCol < 0) continue;

                    double priceNow  = static_cast<double>(closeView.data[static_cast<size_t>(di) * closeView.rowStride + symCol]);
                    double priceFwd  = static_cast<double>(closeView.data[static_cast<size_t>(di + fwdDays) * closeView.rowStride + symCol]);

                    if (priceNow > 1e-9 && std::isfinite(priceNow)
                        && priceFwd > 1e-9 && std::isfinite(priceFwd)) {
                        double fwdRet = (priceFwd / priceNow) - 1.0;
                        if (std::isfinite(fwdRet) && std::abs(fwdRet) < 0.5) {
                            icPairs.emplace_back(fv, fwdRet);
                        }
                    }
                }
            }

            // chunkView 在此出作用域 → 该块数据释放
        } // end chunk loop

        // ── 从累积的 IC 对计算 Rank IC ──
        ::factor::ICIRResult icir;
        if (icPairs.size() >= 30) {
            // 按日期分组（每30+对对应一个日期）
            std::vector<double> datesIC;
            // 简化：直接对所有对分组计算 Spearman
            // 此处用简化版 — 取所有对的整体秩相关
            std::vector<double> fvAll, retAll;
            fvAll.reserve(icPairs.size());
            retAll.reserve(icPairs.size());
            for (const auto& [fv, ret] : icPairs) {
                fvAll.push_back(fv);
                retAll.push_back(ret);
            }

            // Spearman rank correlation
            std::vector<size_t> idx(fvAll.size());
            for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;

            // Rank factor values
            std::sort(idx.begin(), idx.end(),
                [&](size_t a, size_t b) { return fvAll[a] < fvAll[b]; });
            std::vector<double> fRanks(fvAll.size());
            for (size_t i = 0; i < idx.size(); ++i) fRanks[idx[i]] = static_cast<double>(i);

            // Rank returns
            std::sort(idx.begin(), idx.end(),
                [&](size_t a, size_t b) { return retAll[a] < retAll[b]; });
            std::vector<double> rRanks(retAll.size());
            for (size_t i = 0; i < idx.size(); ++i) rRanks[idx[i]] = static_cast<double>(i);

            double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
            double nn = static_cast<double>(fRanks.size());
            for (size_t i = 0; i < fRanks.size(); ++i) {
                sumX += fRanks[i]; sumY += rRanks[i];
                sumXY += fRanks[i] * rRanks[i];
                sumX2 += fRanks[i] * fRanks[i];
                sumY2 += rRanks[i] * rRanks[i];
            }
            double num = nn * sumXY - sumX * sumY;
            double den = std::sqrt((nn * sumX2 - sumX * sumX) * (nn * sumY2 - sumY * sumY));
            if (den > 1e-12) {
                icir.icMean = num / den;
                icir.ir = icir.icMean;
                icir.icPositiveRatio = (icir.icMean > 0) ? 1.0 : 0.0;
                icir.icStd = 0.0;
            }
        }

        if (onProgress) onProgress(60.0, "factors computed (chunked)");

        // ── 模拟成交 ──
        factor::compute::SimulatedTradingResult tradingResult;
        if (reporterInput.factorValuesByDate.empty()) {
            emitError("factor computation produced no values");
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
            if (onProgress) onProgress(60.0 + pct * 15.0, "simulating trades");
        };

        m_executor = std::make_unique<factor::compute::SimulatedTradingExecutor>(params);
        auto sortedDates = sortedDatesFrom(reporterInput.factorValuesByDate);

        // 构建 instrumentIds
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

        const int32_t numDates = static_cast<int32_t>(sortedDates.size());
        const int32_t numInsts = static_cast<int32_t>(instrumentIds.size());

        // 确保 close 全量加载（交易模拟需要随机访问）
        if (arrowView) {
            arrowView->ensureColumns({"close"});
        }

        factor::compute::NumericConstMatrixView priceView{};
        if (arrowView && numDates > 0 && numInsts > 0) {
            priceView = arrowView->close();
        } else {
            INTERNAL_WARN_STREAM << "[FactorBacktestOrchestrator] No real price data, using default";
            static constexpr float kDefaultPrice = 1.0f;
            static std::vector<factor::compute::signal_value_t> s_defaultPrices;
            const size_t needed = static_cast<size_t>(std::max(1, numDates))
                                * static_cast<size_t>(std::max(1, numInsts));
            if (s_defaultPrices.size() < needed)
                s_defaultPrices.assign(needed, kDefaultPrice);
            priceView.data       = s_defaultPrices.data();
            priceView.rowCount    = std::max(1, numDates);
            priceView.columnCount = std::max(1, numInsts);
            priceView.rowStride   = std::max(1, numInsts);
        }

        // 转换为 unordered_map 格式
        factor::compute::SimulatedTradingExecutor::FactorValuesByDate fvByDate;
        for (const auto& [date, symMap] : reporterInput.factorValuesByDate) {
            std::unordered_map<std::string, double> innerMap;
            for (const auto& [sym, val] : symMap)
                innerMap[sym] = val;
            fvByDate[date] = std::move(innerMap);
        }

        tradingResult = m_executor->execute(fvByDate, sortedDates, priceView,
                                             instrumentIds, instrumentIdToSymbol);

        if (onProgress) onProgress(80.0, "trading simulated");

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

        // ── 构建 JSON 结果（复用原有逻辑）──
        if (onComplete) {
            ::factor::BacktestConfig btConfig;
            btConfig.forwardDays    = std::max(1, config.forwardDays);
            btConfig.rebalanceDays  = std::max(1, config.rebalanceDays);
            btConfig.numGroups      = config.numGroups;
            btConfig.commissionRate = config.commissionRate;
            btConfig.slippageRate   = config.slippageRate;
            btConfig.riskFreeRate   = config.riskFreeRate;

            auto allSortedDates = sortedDatesFrom(reporterInput.factorValuesByDate);

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

            ::factor::BacktestResult btResult;
            btResult.config      = btConfig;
            btResult.icirResult  = icir;
            btResult.groupResult = groupRes;

            ::factor::FactorBacktestMetricsCalculator::Inputs inputs{
                btConfig, btResult.icirResult, btResult.groupResult,
                tradingResult.strategyDailyReturns,
                tradingResult.strategyDailyReturns,
                tradingResult.strategyDailyReturns,
                tradingResult.periodTurnovers,
                {},
                {}
            };
            ::factor::FactorBacktestMetricsCalculator::populateResultMetrics(btResult, inputs);

            // JSON 序列化
            using J = foundation::json::JsonFacade;
            auto root = J::createObject();
            root.set("status", J::createString("SUCCESS"));
            root.set("factorValues", J::createArray());
            if (!allSortedDates.empty()) {
                root.set("startDate", J::createString(allSortedDates.front()));
                root.set("endDate",   J::createString(allSortedDates.back()));
            }

            auto metrics = J::createObject();
            auto groupsArr = J::createArray();
            for (const auto& gm : tradingResult.groups) {
                auto gObj = J::createObject();
                gObj.set("groupName",        J::createString("G" + std::to_string(gm.groupIndex)));
                gObj.set("groupIndex",       J::createDouble(static_cast<double>(gm.groupIndex)));
                gObj.set("returnRate",       J::createDouble(gm.returnRate));
                gObj.set("annualizedReturn", J::createDouble(gm.annualizedReturn));
                gObj.set("cumulativeReturn", J::createDouble(gm.returnRate));
                gObj.set("stockCount",       J::createDouble(static_cast<double>(gm.stockCount)));
                gObj.set("minFactorValue",   J::createDouble(gm.minFactorValue));
                gObj.set("maxFactorValue",   J::createDouble(gm.maxFactorValue));
                groupsArr.push_back(gObj);
            }
            metrics.set("groups", groupsArr);

            auto t = J::createObject();
            t.set("sharpe",           J::createDouble(btResult.sharpeRatio));
            t.set("annualizedReturn", J::createDouble(btResult.annualReturn));
            t.set("annualStdDev",     J::createDouble(btResult.volatility));
            t.set("maxDrawdown",      J::createDouble(btResult.maxDrawdown));
            t.set("totalReturn",      J::createDouble(tradingResult.totalReturn));
            metrics.set("trading", t);

            auto ic = J::createObject();
            ic.set("value",      J::createDouble(btResult.factorMetrics.rankIcMean));
            ic.set("ir",         J::createDouble(btResult.factorMetrics.rankIcir));
            ic.set("std",        J::createDouble(btResult.factorMetrics.rankIcStd));
            ic.set("winRate",    J::createDouble(btResult.factorMetrics.icWinRate));
            ic.set("pValue",     J::createDouble(btResult.factorMetrics.icPValue));
            ic.set("tStat",      J::createDouble(btResult.factorMetrics.icTStat));
            ic.set("halfLife",   J::createDouble(static_cast<double>(btResult.factorMetrics.icHalfLife)));
            metrics.set("ic", ic);

            auto fm = J::createObject();
            fm.set("monotonicityScore",     J::createDouble(btResult.factorMetrics.monotonicityScore));
            fm.set("longShortSharpe",       J::createDouble(btResult.factorMetrics.longShortSharpe));
            fm.set("longShortAnnualReturn", J::createDouble(btResult.factorMetrics.longShortAnnualReturn));
            fm.set("costAdjustedSharpe",    J::createDouble(btResult.factorMetrics.costAdjustedSharpe));
            fm.set("annualTurnover",        J::createDouble(btResult.factorMetrics.annualTurnover));
            fm.set("alpha",                 J::createDouble(btResult.factorMetrics.alpha));
            fm.set("monthlyWinRate",        J::createDouble(btResult.factorMetrics.monthlyWinRate));
            fm.set("numGroups",             J::createDouble(static_cast<double>(btResult.factorMetrics.numGroups)));
            metrics.set("factorMetrics", fm);

            auto fq = J::createObject();
            fq.set("rating", J::createDouble(static_cast<double>(btResult.factorMetrics.coreRating)));
            fq.set("label",  J::createString(
                btResult.factorMetrics.coreRating == ::factor::FactorBacktestMetrics::Rating::EXCELLENT ? "优秀" :
                btResult.factorMetrics.coreRating == ::factor::FactorBacktestMetrics::Rating::GOOD      ? "良好" :
                btResult.factorMetrics.coreRating == ::factor::FactorBacktestMetrics::Rating::PASS       ? "合格" : "不合格"));
            metrics.set("factorQuality", fq);

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

            auto retSeries = J::createArray();
            for (double r : tradingResult.strategyDailyReturns)
                retSeries.push_back(J::createDouble(r));
            metrics.set("returnSeries", retSeries);

            root.set("metrics", metrics);
            onComplete(root.toString());
        }
        if (onProgress) onProgress(100.0, "completed");
}

} // namespace Factor::backtest
