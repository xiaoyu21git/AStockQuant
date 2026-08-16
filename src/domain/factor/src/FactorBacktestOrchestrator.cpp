#include "FactorBacktestOrchestrator.h"
#include "factor_compute/FactorEngine.h"
#include "factor_compute/ArrowMarketDataView.h"
#include "factor_compute/FactorValuePipeline.h"
#include "../../../infrastructure/include/database/MarketDataRepository.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "BacktestScheduler.h"
#include "CompositeFactorConfig.h"
#include "FactorMetricsCalculator.h"
#include "FactorIcUtils.h"
#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

    if (!m_scheduler) { emitError("scheduler 未设置"); return; }
    if (!m_engine)   { emitError("factor engine 未设置"); return; }
    if (!m_engine->hasInstanceManager()) {
        emitError("FactorService 未初始化 — factor instances unavailable");
        return;
    }
    if (!m_dataService || !m_dataService->getView()) {
        emitError("无行情数据视图 — cache dataset not loaded");
        return;
    }

    auto* arrowView = static_cast<factor::compute::ArrowMarketDataView*>(m_dataService->getView());
    if (arrowView->dates().empty() || arrowView->instruments().empty()) {
        emitError("空数据集 — no dates or instruments");
        return;
    }

    const bool isComposite = (config.factorMode == FactorMode::Composite && !config.compositeChildren.empty());
    const auto& factorIdList = config.factorIds.empty()
        ? std::vector<std::string>{"backtest_factor"}
        : config.factorIds;

    INTERNAL_INFO_STREAM << "[回测流程] 开始"
        << " mode=" << (isComposite ? "composite" : "single")
        << " factors=" << factorIdList.size()
        << " groups=" << config.numGroups
        << " forward=" << config.forwardDays << "d"
        << " rebalance=" << config.rebalanceDays << "d";

    if (onProgress) onProgress(5.0, "data indexed");

    // ── 按 config 日期范围过滤 ──
    const auto& arrowDates = arrowView->dates();

    std::vector<factor::compute::DateKey> filteredDatesStorage;
    const std::vector<factor::compute::DateKey>* effectiveDatesPtr = &arrowDates;
    if (config.cacheStartDate.isValid() || config.cacheEndDate.isValid()) {
        for (const auto& d : arrowDates) {
            if (config.cacheStartDate.isValid() && d.value < config.cacheStartDate.value) continue;
            if (config.cacheEndDate.isValid() && d.value > config.cacheEndDate.value) continue;
            filteredDatesStorage.push_back(d);
        }
        if (filteredDatesStorage.empty()) {
            emitError("指定日期范围内无交易日");
            return;
        }
        effectiveDatesPtr = &filteredDatesStorage;
    }
    // ── 用交易日历过滤非交易日 ──
    {
        auto db = astock::database::NativePgConnectionPool::instance().getConnection();
        if (db && db->isOpen()) {
            astock::infrastructure::database::MarketDataRepository repo(db);
            char ds[16], de[16];
            int sv = effectiveDatesPtr->front().value, ev = effectiveDatesPtr->back().value;
            std::snprintf(ds, sizeof(ds), "%04d-%02d-%02d", sv/10000, (sv/100)%100, sv%100);
            std::snprintf(de, sizeof(de), "%04d-%02d-%02d", ev/10000, (ev/100)%100, ev%100);
            auto tradingDays = repo.queryTradeCalendar(ds, de);
            if (!tradingDays.empty()) {
                std::unordered_set<int32_t> tradingSet;
                for (const auto& td : tradingDays) {
                    std::string clean; for (char c : td) if (c != '-') clean += c;
                    tradingSet.insert(static_cast<int32_t>(std::stoi(clean)));
                }
                std::vector<factor::compute::DateKey> filtered;
                for (const auto& dk : *effectiveDatesPtr)
                    if (tradingSet.count(dk.value)) filtered.push_back(dk);
                if (filtered.size() < effectiveDatesPtr->size()) {
                    INTERNAL_INFO_STREAM << "[回测流程] 交易日历过滤: skipped="
                        << (effectiveDatesPtr->size() - filtered.size())
                        << " remaining=" << filtered.size();
                    filteredDatesStorage = std::move(filtered);
                    effectiveDatesPtr = &filteredDatesStorage;
                }
            }
        }
    }
    const auto& allDates = *effectiveDatesPtr;

    const size_t totalDates = allDates.size();
    const int fwdDays = std::max(1, config.forwardDays);
    const int rbDays = std::max(1, config.rebalanceDays);

    // ── 日期链路诊断 ──
    {
        auto fmtDate = [](int32_t v) -> std::string {
            if (v <= 0) return "N/A";
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", v / 10000, (v / 100) % 100, v % 100);
            return buf;
        };
        int rebalanceCount = 0;
        std::string firstRb, lastRb;
        for (size_t di = 0; di + static_cast<size_t>(fwdDays) < totalDates; di += rbDays) {
            if (rebalanceCount == 0)
                firstRb = fmtDate(allDates[di].value);
            lastRb = fmtDate(allDates[di].value);
            ++rebalanceCount;
        }
        INTERNAL_INFO_STREAM << "[MEM] 日期链路诊断:"
            << " arrowRawDates=" << arrowDates.size()
            << " effectiveDates=" << totalDates
            << " first=" << fmtDate(allDates.empty() ? 0 : allDates.front().value)
            << " last=" << fmtDate(allDates.empty() ? 0 : allDates.back().value)
            << " dateFilterStart=" << fmtDate(config.cacheStartDate.isValid() ? config.cacheStartDate.value : 0)
            << " dateFilterEnd=" << fmtDate(config.cacheEndDate.isValid() ? config.cacheEndDate.value : 0)
            << " fwdDays=" << fwdDays
            << " rbDays=" << rbDays
            << " rebalanceDates=" << rebalanceCount
            << " firstRb=" << firstRb
            << " lastRb=" << lastRb
            << " expectedTradingPeriods~=" << rebalanceCount;
    }

    factor::compute::BacktestReporterInput reporterInput;
    std::map<std::string, std::vector<std::pair<double, double>>> icByDate; // date→{(fv, fwdRet)}

    // ── 预建 symbol → column index 映射 (IC 查找 O(1)) ──
    std::unordered_map<std::string, int32_t> symToCol;
    {
        const auto& syms = arrowView->symbolStrings();
        for (size_t si = 0; si < syms.size(); ++si)
            symToCol[syms[si]] = static_cast<int32_t>(si);
    }

    // ── 预建 rebalance 日期集合（仅这些日期需要保留 factorValues 供 SimulatedTrading）──
    std::unordered_set<std::string> rebalanceDates;
    {
        for (size_t di = 0; di + static_cast<size_t>(fwdDays) < allDates.size(); di += rbDays) {
            int dv = allDates[di].value;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", dv / 10000, (dv / 100) % 100, dv % 100);
            rebalanceDates.insert(std::string(buf));
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    // 统一因子值管线 — 因子回测/策略回测共用的唯一因子计算实现
    // 分块/回看/尾部扩展/DB补齐全部内聚在 FactorValuePipeline, 此处只做统计消费:
    // 组合合并/缩尾/IC对/非调仓日清理
    // ══════════════════════════════════════════════════════════════════════
    std::vector<std::string> pipelineFactorIds;
    if (isComposite) {
        pipelineFactorIds.reserve(config.compositeChildren.size());
        for (const auto& child : config.compositeChildren)
            pipelineFactorIds.push_back(child.instanceId);
    } else {
        pipelineFactorIds = factorIdList;
    }

    factor::compute::FactorValuePipeline pipeline(*m_engine, *m_dataService);
    pipeline.run(*arrowView, allDates, pipelineFactorIds, fwdDays,
        [&](const factor::compute::FactorValuePipeline::ChunkOutput& out) {
            const size_t ownSize = out.chunkDates->size();
            const size_t tailSize = out.tailDates->size();
            // 块视图行布局: [回看]+[本块]+[尾部扩展] — IC 取价行偏移 = 回看行数
            const size_t rowBase = out.chunkView->dates().size() - ownSize - tailSize;

            if (isComposite) {
                // ── 组合因子：子因子值加权合并 ──
                double totalWeight = 0.0;
                for (const auto& child : config.compositeChildren)
                    totalWeight += child.weight;
                if (totalWeight > 0.0) {
                    std::map<std::string, std::map<std::string, double>> combinedValues;
                    for (const auto& child : config.compositeChildren) {
                        auto it = out.perFactorValues->find(child.instanceId);
                        if (it == out.perFactorValues->end()) continue;
                        for (const auto& [date, symMap] : it->second)
                            for (const auto& [symbol, _] : symMap)
                                combinedValues[date][symbol] = 0.0;
                    }
                    for (const auto& [date, symMap] : combinedValues) {
                        for (const auto& [symbol, _] : symMap) {
                            double weightedSum = 0.0;
                            double presentWeight = 0.0;
                            for (size_t ci2 = 0; ci2 < config.compositeChildren.size(); ++ci2) {
                                auto it = out.perFactorValues->find(config.compositeChildren[ci2].instanceId);
                                if (it == out.perFactorValues->end()) continue;
                                auto dateIt = it->second.find(date);
                                if (dateIt == it->second.end()) continue;
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
                // ── 单/多因子：逐因子累加, 多因子取均值 ──
                std::map<std::string, std::map<std::string, int>> factorValueCounts;
                for (const auto& factorId : factorIdList) {
                    auto it = out.perFactorValues->find(factorId);
                    if (it == out.perFactorValues->end()) continue;
                    for (const auto& [date, symbolValues] : it->second) {
                        for (const auto& [symbol, value] : symbolValues) {
                            if (!std::isfinite(value)) continue;
                            reporterInput.factorValuesByDate[date][symbol] += value;
                            factorValueCounts[date][symbol]++;
                        }
                    }
                }
                // 多因子均值归一化 (管线保证每个日期恰好算一次, 计数即参与因子数)
                if (factorIdList.size() > 1) {
                    for (auto& [date, symMap] : reporterInput.factorValuesByDate) {
                        auto countIt = factorValueCounts.find(date);
                        if (countIt == factorValueCounts.end()) continue;
                        for (auto& [symbol, val] : symMap) {
                            auto symCountIt = countIt->second.find(symbol);
                            if (symCountIt != countIt->second.end() && symCountIt->second > 1)
                                val /= static_cast<double>(symCountIt->second);
                        }
                    }
                }
            }

            // ── 交叉截面缩尾：IC 和策略共享同一份因子值 (仅本块交易日) ──
            if (config.winsorizeQuantile > 0.0) {
                const double q = config.winsorizeQuantile;
                for (const auto& dk : *out.chunkDates) {
                    char wdbuf[16];
                    int wdv = dk.value;
                    std::snprintf(wdbuf, sizeof(wdbuf), "%04d-%02d-%02d",
                                  wdv / 10000, (wdv / 100) % 100, wdv % 100);
                    std::string wdate(wdbuf);
                    auto wit = reporterInput.factorValuesByDate.find(wdate);
                    if (wit == reporterInput.factorValuesByDate.end()) continue;
                    auto& fvMap = wit->second;
                    if (fvMap.size() < 10) continue;

                    std::vector<double> allVals;
                    allVals.reserve(fvMap.size());
                    for (const auto& [sym, fv] : fvMap)
                        if (std::isfinite(fv)) allVals.push_back(fv);
                    if (allVals.size() < 10) continue;

                    const size_t nv = allVals.size();
                    const size_t loIdx = static_cast<size_t>(q * static_cast<double>(nv));
                    const size_t hiIdx = nv - 1 - loIdx;
                    if (loIdx >= hiIdx) continue;

                    std::nth_element(allVals.begin(), allVals.begin() + static_cast<long long>(loIdx), allVals.end());
                    const double loBound = allVals[loIdx];
                    std::nth_element(allVals.begin(), allVals.begin() + static_cast<long long>(hiIdx), allVals.end());
                    const double hiBound = allVals[hiIdx];

                    size_t clipCount = 0;
                    double origMin = std::numeric_limits<double>::max();
                    double origMax = std::numeric_limits<double>::lowest();
                    for (auto& [sym, fv] : fvMap) {
                        if (!std::isfinite(fv)) continue;
                        if (fv < origMin) origMin = fv;
                        if (fv > origMax) origMax = fv;
                        if (fv < loBound) { fv = loBound; ++clipCount; }
                        else if (fv > hiBound) { fv = hiBound; ++clipCount; }
                    }

                    if (clipCount > 0 && static_cast<double>(clipCount) / static_cast<double>(nv) > 0.01) {
                        INTERNAL_WARN_STREAM << "[Orchestrator] winsorize date=" << wdate
                            << " clipped " << clipCount << "/" << nv << " values"
                            << " origRange=[" << origMin << "," << origMax << "]"
                            << " clipRange=[" << loBound << "," << hiBound << "]";
                    }
                }
            }

            // ── 增量累积 IC 对 ──
            // 只对不超出前向窗口的日期计算; 取价行 = rowBase + di (跳过回看行)
            const size_t computeEnd = (ownSize + tailSize > static_cast<size_t>(fwdDays))
                ? std::min(ownSize, ownSize + tailSize - static_cast<size_t>(fwdDays))
                : 0;
            const auto closeView = out.chunkView->close();

            for (size_t di = 0; di < computeEnd; ++di) {
                char dateBuf[16];
                int dv = (*out.chunkDates)[di].value;
                std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
                              dv / 10000, (dv / 100) % 100, dv % 100);
                std::string dateNow(dateBuf);

                auto itNow = reporterInput.factorValuesByDate.find(dateNow);
                if (itNow == reporterInput.factorValuesByDate.end()) continue;

                for (const auto& [sym, fv] : itNow->second) {
                    if (!std::isfinite(fv)) continue;

                    auto itSym = symToCol.find(sym);
                    if (itSym == symToCol.end()) continue;
                    int32_t symCol = itSym->second;

                    double priceNow  = static_cast<double>(closeView.data[(rowBase + di) * closeView.rowStride + symCol]);
                    double priceFwd  = static_cast<double>(closeView.data[(rowBase + di + static_cast<size_t>(fwdDays)) * closeView.rowStride + symCol]);

                    if (priceNow > 1e-9 && std::isfinite(priceNow)
                        && priceFwd > 1e-9 && std::isfinite(priceFwd)) {
                        double fwdRet = (priceFwd / priceNow) - 1.0;
                        if (std::isfinite(fwdRet) && std::abs(fwdRet) < 0.5) {
                            icByDate[dateNow].emplace_back(fv, fwdRet);
                        }
                    }
                }
            }

            // ── 释放非 rebalance 日的因子值 ──
            for (const auto& dk : *out.chunkDates) {
                int dv = dk.value;
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", dv / 10000, (dv / 100) % 100, dv % 100);
                std::string dateStr(buf);
                if (!rebalanceDates.count(dateStr))
                    reporterInput.factorValuesByDate.erase(dateStr);
            }
        },
        [&](double frac, const std::string& status) {
            if (onProgress) onProgress(5.0 + 55.0 * frac, status);
        });

        // ── 从累积的 IC 对计算 Rank IC ──
        ::factor::ICIRResult icir;
        // ── 逐日 Rank IC ──
        std::vector<double> icSeries;
        for (const auto& [date, pairs] : icByDate) {
            if (pairs.size() < 2) continue;
            std::vector<double> fv, ret;
            fv.reserve(pairs.size()); ret.reserve(pairs.size());
            for (const auto& [f, r] : pairs) { fv.push_back(f); ret.push_back(r); }
            auto spearman = [](std::vector<double>& x, std::vector<double>& y) -> double {
                size_t n = x.size();
                std::vector<size_t> idx(n);
                for (size_t i=0;i<n;++i) idx[i]=i;
                std::sort(idx.begin(),idx.end(),[&](size_t a,size_t b){return x[a]<x[b];});
                std::vector<double> rx(n); for(size_t i=0;i<n;++i) rx[idx[i]]=static_cast<double>(i+1);
                std::sort(idx.begin(),idx.end(),[&](size_t a,size_t b){return y[a]<y[b];});
                std::vector<double> ry(n); for(size_t i=0;i<n;++i) ry[idx[i]]=static_cast<double>(i+1);
                double sx=0,sy=0,sx2=0,sy2=0,sxy=0,N=static_cast<double>(n);
                for(size_t i=0;i<n;++i){sx+=rx[i];sy+=ry[i];sx2+=rx[i]*rx[i];sy2+=ry[i]*ry[i];sxy+=rx[i]*ry[i];}
                double num=N*sxy-sx*sy,den=std::sqrt((N*sx2-sx*sx)*(N*sy2-sy*sy));
                return den>1e-12 ? num/den : 0.0;
            };
            icSeries.push_back(spearman(fv, ret));
        }
        icir.icSeries = icSeries;
        icir.icMean   = factor::icir::calculateMean(icSeries);
        icir.icStd    = factor::icir::calculateStdDev(icSeries, icir.icMean);
        icir.ir       = icir.icStd > 0.0 ? icir.icMean / icir.icStd : 0.0;
        if (!icSeries.empty()) {
            auto pos = std::count_if(icSeries.begin(), icSeries.end(), [](double v){return v>0.0;});
            icir.icPositiveRatio = static_cast<double>(pos) / icSeries.size();
        }
        if (onProgress) onProgress(60.0, "factors computed (per-date IC)");

        // ── 提前采样 scatterData 并释放 icByDate ──
        foundation::json::JsonFacade scatterArr = foundation::json::JsonFacade::createArray();
        for (const auto& [date, pairs] : icByDate) {
            if (pairs.size() < 10) continue;
            size_t step = std::max(size_t(1), pairs.size() / 200);
            for (size_t si = 0; si < pairs.size(); si += step) {
                auto pt = foundation::json::JsonFacade::createObject();
                pt.set("date", foundation::json::JsonFacade::createString(date));
                pt.set("factorValue", foundation::json::JsonFacade::createDouble(pairs[si].first));
                pt.set("forwardRet", foundation::json::JsonFacade::createDouble(pairs[si].second));
                scatterArr.push_back(pt);
            }
        }
        icByDate.clear();
        INTERNAL_INFO_STREAM << "[MEM] icByDate 已清除";

        INTERNAL_INFO_STREAM << "[回测流程] IC计算完成: icSeries.size=" << icir.icSeries.size()
            << " icMean=" << icir.icMean
            << " icWinRate=" << icir.icPositiveRatio;

        // ── 模拟成交 ──
        factor::compute::SimulatedTradingResult tradingResult;
        if (reporterInput.factorValuesByDate.empty()) {
            emitError("因子计算未产生任何值");
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
        params.adjustPriceType = config.adjustPriceType;
        params.winsorizeQuantile = config.winsorizeQuantile;
        params.ascending       = config.ascending;
        params.onProgress      = [&](double pct) {
            if (onProgress) onProgress(60.0 + pct * 15.0, "simulating trades");
        };

        m_executor = std::make_unique<factor::compute::SimulatedTradingExecutor>(params);
        auto sortedDates = sortedDatesFrom(reporterInput.factorValuesByDate);

        INTERNAL_INFO_STREAM << "[MEM] 交易输入: sortedDates=" << sortedDates.size()
            << " firstDate=" << (sortedDates.empty() ? "N/A" : sortedDates.front())
            << " lastDate=" << (sortedDates.empty() ? "N/A" : sortedDates.back());

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
            arrowView->ensureColumns({"close", "pre_adjust_factor", "post_adjust_factor"});
        }

        factor::compute::NumericConstMatrixView priceView{};
        factor::compute::NumericConstMatrixView preAdjustView{};
        factor::compute::NumericConstMatrixView postAdjustView{};
        if (arrowView && numDates > 0 && numInsts > 0) {
            priceView = arrowView->close();
            if (auto preAdj = arrowView->getField("pre_adjust_factor"))
                preAdjustView = preAdj.value();
            if (auto postAdj = arrowView->getField("post_adjust_factor"))
                postAdjustView = postAdj.value();
        } else {
            INTERNAL_ERROR_STREAM << "[FactorBacktestOrchestrator] No real price data, aborting";
            if (onProgress) onProgress(-1.0, "price data unavailable");
            return;
        }

        // 转换为 unordered_map 格式
        factor::compute::SimulatedTradingExecutor::FactorValuesByDate fvByDate;
        for (const auto& [date, symMap] : reporterInput.factorValuesByDate) {
            std::unordered_map<std::string, double> innerMap;
            for (const auto& [sym, val] : symMap)
                innerMap[sym] = val;
            fvByDate[date] = std::move(innerMap);
        }

        INTERNAL_INFO_STREAM << "[回测流程] 模拟交易开始: dates=" << sortedDates.size()
            << " instruments=" << instrumentIds.size();
        tradingResult = m_executor->execute(fvByDate, sortedDates, priceView,
                                             preAdjustView, postAdjustView,
                                             instrumentIds, instrumentIdToSymbol);
        INTERNAL_INFO_STREAM << "[回测流程] 模拟交易结束: periods=" << tradingResult.validSampleCount
            << " totalReturn=" << tradingResult.totalReturn;

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

        // 释放交易模拟输入数据
        reporterInput.factorValuesByDate.clear();
        fvByDate.clear();
        INTERNAL_INFO_STREAM << "[MEM] factorValuesByDate + fvByDate 已清除";

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

            // ── 基准收益序列（若配置了 benchmarkSymbol 则从 Arrow 加载）──
            std::vector<double> benchmarkDailyReturns;
            ::factor::FactorBacktestMetricsCalculator::BenchmarkComparisonSummary benchmarkSummary;
            if (!config.benchmarkSymbol.empty() && arrowView) {
                auto benchSyms = arrowView->symbolStrings();
                auto benchDates = arrowView->dates();
                auto benchClose = arrowView->getField("close");
                if (benchClose.has_value() && !benchSyms.empty()) {
                    // 查找基准标的在 symbols 中的索引
                    int32_t benchCol = -1;
                    for (size_t si = 0; si < benchSyms.size(); ++si) {
                        if (benchSyms[si] == config.benchmarkSymbol) { benchCol = static_cast<int32_t>(si); break; }
                    }
                    if (benchCol >= 0 && benchClose->isValid()) {
                        const auto& bcv = benchClose.value();
                        const int32_t bStride = bcv.rowStride >= bcv.columnCount ? bcv.rowStride : bcv.columnCount;
                        // 对齐到策略交易日的基准日收益
                        std::string prevDate;
                        double prevClose = 0.0;
                        for (size_t di = 0; di < benchDates.size(); ++di) {
                            double closePx = bcv.data[static_cast<int32_t>(di) * bStride + benchCol];
                            if (std::isfinite(closePx) && closePx > 1e-9) {
                                std::string dateStr = std::to_string(benchDates[di].value);
                                // 格式化为 YYYY-MM-DD
                                int dv = benchDates[di].value;
                                char dbuf[16]; snprintf(dbuf, sizeof(dbuf), "%04d-%02d-%02d",
                                    dv / 10000, (dv / 100) % 100, dv % 100);
                                if (!prevDate.empty() && prevClose > 1e-9) {
                                    benchmarkDailyReturns.push_back(closePx / prevClose - 1.0);
                                }
                                prevDate = dbuf;
                                prevClose = closePx;
                            }
                        }
                        if (!benchmarkDailyReturns.empty() && !tradingResult.costAdjustedLongShortReturns.empty()) {
                            benchmarkSummary = ::factor::FactorBacktestMetricsCalculator::calculateBenchmarkMetrics(
                                tradingResult.costAdjustedLongShortReturns, benchmarkDailyReturns);
                        }
                    }
                }
            }

            ::factor::FactorBacktestMetricsCalculator::Inputs inputs{
                btConfig, btResult.icirResult, btResult.groupResult,
                tradingResult.rawLongShortReturns,
                tradingResult.costAdjustedLongShortReturns,
                tradingResult.riskAdjustedLongShortReturns,
                tradingResult.periodTurnovers,
                {},    // longShortDates
                {},    // groupReturnSeriesByGroup
                nullptr, nullptr, 0.0,
                benchmarkSummary.hasValidAlignment ? &benchmarkSummary : nullptr
            };
            ::factor::FactorBacktestMetricsCalculator::populateResultMetrics(btResult, inputs);

            // ── 一致性诊断：spread 方向与 IC 一致但策略仍亏损 ──
            if (btResult.factorMetrics.spreadSignMatchIc && tradingResult.totalReturn < -0.05) {
                if (btResult.factorMetrics.rankIcMean > 0.0) {
                    INTERNAL_WARN_STREAM << "[Orchestrator] spreadSignMatchIc=true (IC>0) 但 totalReturn="
                        << tradingResult.totalReturn
                        << " — factor direction correct, losses may be from costs/slippage/risk controls";
                } else {
                    INTERNAL_WARN_STREAM << "[Orchestrator] spreadSignMatchIc=true (IC<0) 但 totalReturn="
                        << tradingResult.totalReturn
                        << " — factor is inverted (negative IC), consider reversing long/short baskets";
                }
            }

            // JSON 序列化
            using J = foundation::json::JsonFacade;
            auto root = J::createObject();
            root.set("status", J::createString("SUCCESS"));
            if (!config.compositeName.empty())
                root.set("factorName", J::createString(config.compositeName));

            // factorValues — 结果仅需指标，原始数据不输出
            // scatterData — 已在 IC 计算后提前采样并释放 icByDate
            root.set("scatterData", scatterArr);

            if (!allSortedDates.empty()) {
                root.set("startDate", J::createString(allSortedDates.front()));
                root.set("endDate",   J::createString(allSortedDates.back()));
                auto dateList = J::createArray();
                for (const auto& d : allSortedDates)
                    dateList.push_back(J::createString(d));
                root.set("dateList", dateList);
            }

            auto metrics = J::createObject();
            auto groupsArr = J::createArray();
            for (const auto& gm : tradingResult.groups) {
                auto gObj = J::createObject();
                gObj.set("groupName",        J::createString("G" + std::to_string(gm.groupIndex)));
                gObj.set("groupIndex",       J::createDouble(static_cast<double>(gm.groupIndex)));
                gObj.set("returnRate",       J::createDouble(gm.returnRate));
                gObj.set("annualizedReturn", J::createDouble(gm.annualizedReturn));
                gObj.set("cumulativeReturn", J::createDouble(gm.cumulativeReturn));
                gObj.set("stockCount",       J::createDouble(static_cast<double>(gm.stockCount)));
                gObj.set("minFactorValue",   J::createDouble(gm.minFactorValue));
                gObj.set("maxFactorValue",   J::createDouble(gm.maxFactorValue));
                groupsArr.push_back(gObj);
            }
            metrics.set("groups", groupsArr);

            auto ic = J::createObject();
            ic.set("value",      J::createDouble(btResult.factorMetrics.rankIcMean));
            ic.set("ir",         J::createDouble(btResult.factorMetrics.rankIcir));
            ic.set("std",        J::createDouble(btResult.factorMetrics.rankIcStd));
            ic.set("winRate",      J::createDouble(btResult.factorMetrics.icWinRate));
            ic.set("positiveRate", J::createDouble(btResult.factorMetrics.icWinRate));  // QML 读取此名
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
            fm.set("topBottomSpreadReturn", J::createDouble(btResult.factorMetrics.topBottomSpreadReturn));
            fm.set("spreadSignMatchIc",     J::createBool(btResult.factorMetrics.spreadSignMatchIc));
            metrics.set("factorMetrics", fm);

            auto fq = J::createObject();
            fq.set("rating", J::createDouble(static_cast<double>(btResult.factorMetrics.coreRating)));
            fq.set("label",  J::createString(
                btResult.factorMetrics.coreRating == ::factor::FactorBacktestMetrics::Rating::EXCELLENT ? "优秀" :
                btResult.factorMetrics.coreRating == ::factor::FactorBacktestMetrics::Rating::GOOD      ? "良好" :
                btResult.factorMetrics.coreRating == ::factor::FactorBacktestMetrics::Rating::PASS       ? "合格" : "不合格"));
            metrics.set("factorQuality", fq);

            // 单一数据源: tradingResult (SimulatedTradingExecutor)
            // 规则: tradingResult 不产出的字段, 直接不输出
            auto ex = J::createObject();
            ex.set("totalSignals",     J::createDouble(static_cast<double>(reporterOutput.totalSignalCount)));
            ex.set("presentSignals",   J::createDouble(static_cast<double>(reporterOutput.presentSignalCount)));
            ex.set("totalReturn",      J::createDouble(tradingResult.totalReturn));
            ex.set("annualizedReturn", J::createDouble(tradingResult.annualizedReturn));
            ex.set("annualReturn",     J::createDouble(tradingResult.annualizedReturn));  // QML 读取此名
            ex.set("sharpeRatio",      J::createDouble(tradingResult.sharpeRatio));
            ex.set("volatility",       J::createDouble(tradingResult.annualStdDev));
            ex.set("maxDrawdown",      J::createDouble(tradingResult.maxDrawdown));
            ex.set("turnoverRatio",    J::createDouble(tradingResult.turnoverRate));
            ex.set("turnoverRate",     J::createDouble(tradingResult.turnoverRate));      // QML/FactorWorkbench 读取此名
            ex.set("winRate",          J::createDouble(btResult.winRate));                // QML 读取此名
            ex.set("alpha",            J::createDouble(btResult.factorMetrics.alpha));    // QML 读取此名
            ex.set("finalEquity",      J::createDouble(tradingResult.finalEquity));
            ex.set("validSampleCount", J::createDouble(static_cast<double>(tradingResult.validSampleCount)));
            // ── 多空价差诊断（策略实际交易的两端）──
            if (!tradingResult.groups.empty()) {
                const auto& topGrp = tradingResult.groups.front();
                const auto& botGrp = tradingResult.groups.back();
                ex.set("topGroupReturn",       J::createDouble(topGrp.returnRate));
                ex.set("bottomGroupReturn",    J::createDouble(botGrp.returnRate));
                ex.set("longShortSpreadReturn", J::createDouble(topGrp.returnRate - botGrp.returnRate));
                ex.set("spreadSignMatchIc",    J::createBool(btResult.factorMetrics.spreadSignMatchIc));
            }
            if (benchmarkSummary.hasValidAlignment) {
                ex.set("benchmarkAnnualReturn", J::createDouble(benchmarkSummary.benchmarkAnnualReturn));
                ex.set("excessAnnualReturn",   J::createDouble(benchmarkSummary.excessAnnualReturn));
                ex.set("trackingError",        J::createDouble(benchmarkSummary.trackingError));
                ex.set("informationRatio",     J::createDouble(benchmarkSummary.informationRatio));
                ex.set("beta",                 J::createDouble(benchmarkSummary.beta));
            }
            metrics.set("execution", ex);

            // ── 三条收益序列 ──
            auto rawRetSeries = J::createArray();
            for (double r : tradingResult.rawLongShortReturns)
                rawRetSeries.push_back(J::createDouble(r));

            auto costAdjRetSeries = J::createArray();
            for (double r : tradingResult.costAdjustedLongShortReturns)
                costAdjRetSeries.push_back(J::createDouble(r));

            auto riskAdjRetSeries = J::createArray();
            for (double r : tradingResult.riskAdjustedLongShortReturns)
                riskAdjRetSeries.push_back(J::createDouble(r));

            auto retSeries = J::createObject();
            retSeries.set("raw",          rawRetSeries);
            retSeries.set("costAdjusted", costAdjRetSeries);
            retSeries.set("riskAdjusted", riskAdjRetSeries);
            metrics.set("returnSeries", retSeries);

            // ── 分组收益序列 ──
            auto groupRetSeries = J::createArray();
            for (size_t gi = 0; gi < tradingResult.groupDailyReturns.size(); ++gi) {
                auto gArray = J::createArray();
                for (double r : tradingResult.groupDailyReturns[gi])
                    gArray.push_back(J::createDouble(r));
                auto gObj = J::createObject();
                gObj.set("groupIndex", J::createDouble(static_cast<double>(gi)));
                gObj.set("groupName",  J::createString("G" + std::to_string(gi + 1)));
                gObj.set("data", gArray);
                groupRetSeries.push_back(gObj);
            }
            metrics.set("groupReturnSeries", groupRetSeries);

            // ── IC 日序列 ──
            auto icDailyArr = J::createArray();
            for (double v : icSeries)
                icDailyArr.push_back(J::createDouble(v));
            metrics.set("icSeries", icDailyArr);

            // ── 交易记录 ──
            auto tradeLogArr = J::createArray();
            for (const auto& t : tradingResult.tradeLog) {
                auto tj = J::createObject();
                tj.set("symbol",  J::createString(t.symbol));
                tj.set("date",    J::createString(t.date));
                tj.set("side",    J::createString(t.side));
                tj.set("basket",  J::createString(t.basket));
                tj.set("price",   J::createDouble(t.price));
                tj.set("costRate",J::createDouble(t.cost));
                tradeLogArr.push_back(tj);
            }
            metrics.set("tradeLog", tradeLogArr);

            // ── 每期追踪 ──
            auto periodArr = J::createArray();
            for (const auto& p : tradingResult.periodTrackings) {
                auto pj = J::createObject();
                pj.set("date",             J::createString(p.date));
                pj.set("longHeld",         J::createDouble(static_cast<double>(p.longHeld)));
                pj.set("shortHeld",        J::createDouble(static_cast<double>(p.shortHeld)));
                pj.set("longBought",       J::createDouble(static_cast<double>(p.longBought)));
                pj.set("longSold",         J::createDouble(static_cast<double>(p.longSold)));
                pj.set("shortBought",      J::createDouble(static_cast<double>(p.shortBought)));
                pj.set("shortSold",        J::createDouble(static_cast<double>(p.shortSold)));
                pj.set("longTurnover",     J::createDouble(p.longTurnover));
                pj.set("shortTurnover",    J::createDouble(p.shortTurnover));
                pj.set("longRawReturn",    J::createDouble(p.longRawReturn));
                pj.set("shortRawReturn",   J::createDouble(p.shortRawReturn));
                pj.set("strategyNetReturn",J::createDouble(p.strategyNetReturn));
                periodArr.push_back(pj);
            }
            metrics.set("periodTrackings", periodArr);

            root.set("metrics", metrics);
            INTERNAL_INFO_STREAM << "[回测流程] 完成"
                << " totalReturn=" << tradingResult.totalReturn
                << " sharpe=" << btResult.sharpeRatio
                << " icMean=" << btResult.factorMetrics.rankIcMean
                << " spreadSignMatch=" << (btResult.factorMetrics.spreadSignMatchIc ? "Y" : "N");
            onComplete(root.toString());
        }
        if (onProgress) onProgress(100.0, "completed");

    // (dbFallback 由 FactorValuePipeline 内部 guard 在 run 返回时自动清理)
    INTERNAL_INFO_STREAM << "[MEM] Orchestrator::run() 退出 — 管线 dbFallback/回看缓存已由管线内部释放";
}

} // namespace Factor::backtest
