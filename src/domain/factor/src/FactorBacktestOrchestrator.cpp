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
#include "FactorAttributionCalculator.h"
#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"
#include "foundation/perf/Stopwatch.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
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

double FactorBacktestOrchestrator::rankCorrelation(std::vector<double>& x, std::vector<double>& y)
{
    // Spearman 秩相关: 对 x/y 分别排秩后求 Pearson (无并列均值秩修正, 与既有 IC 口径一致)
    const size_t n = x.size();
    if (n != y.size() || n < 2) return 0.0;
    std::vector<size_t> idx(n);
    for (size_t i = 0; i < n; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return x[a] < x[b]; });
    std::vector<double> rx(n);
    for (size_t i = 0; i < n; ++i) rx[idx[i]] = static_cast<double>(i + 1);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return y[a] < y[b]; });
    std::vector<double> ry(n);
    for (size_t i = 0; i < n; ++i) ry[idx[i]] = static_cast<double>(i + 1);
    double sx = 0, sy = 0, sx2 = 0, sy2 = 0, sxy = 0;
    const double nDbl = static_cast<double>(n);
    for (size_t i = 0; i < n; ++i) {
        sx += rx[i]; sy += ry[i];
        sx2 += rx[i] * rx[i]; sy2 += ry[i] * ry[i];
        sxy += rx[i] * ry[i];
    }
    const double num = nDbl * sxy - sx * sy;
    const double den = std::sqrt((nDbl * sx2 - sx * sx) * (nDbl * sy2 - sy * sy));
    return den > 1e-12 ? num / den : 0.0;
}

void FactorBacktestOrchestrator::run(
    const BacktestRunConfig& config,
    FactorOrchestratorProgressCallback onProgress,
    FactorOrchestratorResultCallback onComplete,
    const std::atomic<bool>* cancelFlag)
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

    // ── 组合模式: per-child 标量预聚合 (因子归因输入, P0 禁止保留全截面) ──
    // childLongShortByDate: instanceId → date → 调仓日多空收益标量
    //   (交易模拟结束后按 Executor 实际期序列对齐 → childLongShortReturns)
    // childIcSeries:        instanceId → 逐期 Rank IC (含非调仓日, 仅统计用)
    // 存储量 ≈ 子因子数 × 期数 × ~100B, 远小于 1MB
    std::map<std::string, std::map<std::string, double>> childLongShortByDate;
    std::map<std::string, std::vector<double>> childLongShortReturns;
    std::map<std::string, std::vector<double>> childIcSeries;

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
    foundation::perf::Stopwatch swFactorIc;   // 因子计算 + IC 累积
    swFactorIc.start();
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

                // ── 组合模式: per-child 标量预聚合 (因子归因, P0 禁止保留全截面) ──
                // 与 merged IC 同一位置、同一 fwd 窗口、同一停牌剔除规则, 口径一致
                // 截面即刻消费即刻丢弃; 调仓日多空收益仅存标量, 交易模拟后按实际期序列对齐
                if (isComposite && !config.compositeChildren.empty()) {
                    const bool lsDay = rebalanceDates.count(dateNow) != 0;
                    for (const auto& child : config.compositeChildren) {
                        auto cIt = out.perFactorValues->find(child.instanceId);
                        if (cIt == out.perFactorValues->end()) continue;
                        auto dIt = cIt->second.find(dateNow);
                        if (dIt == cIt->second.end()) continue;

                        std::vector<std::pair<double, double>> childPairs;  // (directedValue, fwdRet)
                        childPairs.reserve(dIt->second.size());
                        for (const auto& [sym, fvChild] : dIt->second) {
                            if (!std::isfinite(fvChild)) continue;
                            auto itSym = symToCol.find(sym);
                            if (itSym == symToCol.end()) continue;
                            const int32_t symCol = itSym->second;
                            const double priceNow =
                                static_cast<double>(closeView.data[(rowBase + di) * closeView.rowStride + symCol]);
                            const double priceFwd =
                                static_cast<double>(closeView.data[(rowBase + di + static_cast<size_t>(fwdDays)) * closeView.rowStride + symCol]);
                            if (priceNow > 1e-9 && std::isfinite(priceNow)
                                && priceFwd > 1e-9 && std::isfinite(priceFwd)) {
                                const double fwdRet = (priceFwd / priceNow) - 1.0;
                                if (std::isfinite(fwdRet) && std::abs(fwdRet) < 0.5)
                                    childPairs.emplace_back(child.ascending ? fvChild : -fvChild, fwdRet);
                            }
                        }

                        // per-child Rank IC (标量入序列, 截面丢弃)
                        if (childPairs.size() >= 2) {
                            std::vector<double> cFv, cRet;
                            cFv.reserve(childPairs.size());
                            cRet.reserve(childPairs.size());
                            for (const auto& [f, r] : childPairs) { cFv.push_back(f); cRet.push_back(r); }
                            childIcSeries[child.instanceId].push_back(rankCorrelation(cFv, cRet));
                        }

                        // per-child 调仓日多空收益: Top/Bottom = directed 值降序后前/后 1/numGroups 组,
                        // 组内等权 (口径对齐 SimulatedTradingExecutor 但无成本)
                        if (lsDay) {
                            std::sort(childPairs.begin(), childPairs.end(),
                                      [](const auto& a, const auto& b) { return a.first > b.first; });
                            const size_t cN = childPairs.size();
                            const size_t cGroupSize =
                                cN / static_cast<size_t>(std::max(1, config.numGroups));
                            double lsValue = 0.0;
                            if (cGroupSize > 0) {
                                double topSum = 0.0, botSum = 0.0;
                                for (size_t gi = 0; gi < cGroupSize; ++gi)
                                    topSum += childPairs[gi].second;
                                for (size_t gi = cN - cGroupSize; gi < cN; ++gi)
                                    botSum += childPairs[gi].second;
                                lsValue = topSum / static_cast<double>(cGroupSize)
                                        - botSum / static_cast<double>(cGroupSize);
                            }
                            childLongShortByDate[child.instanceId][dateNow] = lsValue;
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
        },
        config.workerThreads,
        cancelFlag);

    // ── 软取消: 管线提前返回 (未 sink 块被丢弃) → 不发布结果 ──
    // (UI 状态由 Bridge::cancelBacktest 即时更新, 此处不回调 onProgress)
    if (cancelFlag && cancelFlag->load(std::memory_order_acquire)) {
        INTERNAL_INFO_STREAM << "[回测流程] 已取消: 未 sink 块已丢弃, 结果不发布";
        return;
    }

        // ── 从累积的 IC 对计算 Rank IC ──
        ::factor::ICIRResult icir;
        // ── 逐日 Rank IC ──
        std::vector<double> icSeries;
        for (const auto& [date, pairs] : icByDate) {
            if (pairs.size() < 2) continue;
            std::vector<double> fv, ret;
            fv.reserve(pairs.size()); ret.reserve(pairs.size());
            for (const auto& [f, r] : pairs) { fv.push_back(f); ret.push_back(r); }
            icSeries.push_back(rankCorrelation(fv, ret));
        }
        icir.icSeries = icSeries;
        icir.icMean   = factor::icir::calculateMean(icSeries);
        icir.icStd    = factor::icir::calculateStdDev(icSeries, icir.icMean);
        icir.ir       = icir.icStd > 0.0 ? icir.icMean / icir.icStd : 0.0;
        if (!icSeries.empty()) {
            auto pos = std::count_if(icSeries.begin(), icSeries.end(), [](double v){return v>0.0;});
            icir.icPositiveRatio = static_cast<double>(pos) / icSeries.size();
        }
        swFactorIc.stop();
        swFactorIc.report("因子计算+IC");
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
        foundation::perf::Stopwatch swPriceLoad;
        swPriceLoad.start();
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

        swPriceLoad.stop();
        swPriceLoad.report("行情全量加载");

        INTERNAL_INFO_STREAM << "[回测流程] 模拟交易开始: dates=" << sortedDates.size()
            << " instruments=" << instrumentIds.size();
        foundation::perf::Stopwatch swTrade;
        swTrade.start();
        tradingResult = m_executor->execute(fvByDate, sortedDates, priceView,
                                             preAdjustView, postAdjustView,
                                             instrumentIds, instrumentIdToSymbol);
        swTrade.stop();
        swTrade.report("模拟成交");
        INTERNAL_INFO_STREAM << "[回测流程] 模拟交易结束: periods=" << tradingResult.validSampleCount
            << " totalReturn=" << tradingResult.totalReturn;

        if (onProgress) onProgress(80.0, "trading simulated");

        // ── 组合模式: 因子归因 (标量预聚合 → 因子收益法计算) ──
        // 期序列对齐: Executor 首期建仓/N<numGroups 被跳过的期不 push 收益,
        // 以 periodTrackings[].date 为权威期序列, per-child 按日期取标量, 缺席期贡献为 0 (权重重归一)
        std::optional<factor::FactorAttributionReport> factorAttribution;
        if (isComposite) {
            for (const auto& pt : tradingResult.periodTrackings) {
                for (const auto& child : config.compositeChildren) {
                    double lsValue = 0.0;
                    auto cIt = childLongShortByDate.find(child.instanceId);
                    if (cIt != childLongShortByDate.end()) {
                        auto dIt = cIt->second.find(pt.date);
                        if (dIt != cIt->second.end()) lsValue = dIt->second;
                    }
                    childLongShortReturns[child.instanceId].push_back(lsValue);
                }
            }
            childLongShortByDate.clear();

            factor::FactorAttributionCalculator attrCalc;
            factor::FactorAttributionCalculator::Inputs attrInputs;
            attrInputs.children.reserve(config.compositeChildren.size());
            for (const auto& child : config.compositeChildren) {
                factor::FactorAttributionCalculator::PerChildSeries series;
                series.instanceId = child.instanceId;
                series.weight = child.weight;
                series.ascending = child.ascending;
                auto lsIt = childLongShortReturns.find(child.instanceId);
                if (lsIt != childLongShortReturns.end()) series.longShortReturns = lsIt->second;
                auto icIt = childIcSeries.find(child.instanceId);
                if (icIt != childIcSeries.end()) series.icSeries = icIt->second;
                attrInputs.children.push_back(std::move(series));
            }
            attrInputs.compositeRawReturns = tradingResult.rawLongShortReturns;
            attrInputs.compositeCostAdjReturns = tradingResult.costAdjustedLongShortReturns;
            factorAttribution = attrCalc.compute(attrInputs);

            // 恒等式裁决 (构造性成立): total = Σ贡献 + 排名交互残差 + 成本残差
            double sumContrib = 0.0;
            for (const auto& row : factorAttribution->rows) sumContrib += row.contribution;
            const double identityError = factorAttribution->totalLongShortReturn
                - sumContrib
                - factorAttribution->residualRankingInteraction
                - factorAttribution->residualCosts;
            INTERNAL_INFO_STREAM << "[回测流程] 因子归因: 子因子=" << factorAttribution->rows.size()
                << " 期数=" << factorAttribution->periodCount
                << " 总多空(扣费后)=" << factorAttribution->totalLongShortReturn
                << " Σ贡献=" << sumContrib
                << " 成本残差=" << factorAttribution->residualCosts
                << " 交互残差=" << factorAttribution->residualRankingInteraction
                << " 恒等式误差=" << identityError;
        }

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
        foundation::perf::Stopwatch swJson;
        swJson.start();
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

            // ── 因子归因 (仅组合模式且有效时产出; 单因子/无期序列不输出该键) ──
            if (factorAttribution.has_value() && factorAttribution->isValid) {
                auto attrObj = J::createObject();
                attrObj.set("isValid", J::createBool(true));
                attrObj.set("notice", J::createString(factorAttribution->notice));
                attrObj.set("periodCount", J::createDouble(static_cast<double>(factorAttribution->periodCount)));
                attrObj.set("totalLongShortReturn", J::createDouble(factorAttribution->totalLongShortReturn));
                attrObj.set("residualRankingInteraction", J::createDouble(factorAttribution->residualRankingInteraction));
                attrObj.set("residualCosts", J::createDouble(factorAttribution->residualCosts));
                auto rowsArr = J::createArray();
                for (const auto& row : factorAttribution->rows) {
                    auto rowObj = J::createObject();
                    rowObj.set("factorId", J::createString(row.factorId));
                    rowObj.set("weight", J::createDouble(row.weight));
                    rowObj.set("rankIcMean", J::createDouble(row.rankIcMean));
                    rowObj.set("rankIcir", J::createDouble(row.rankIcir));
                    rowObj.set("longShortReturn", J::createDouble(row.longShortReturn));
                    rowObj.set("contribution", J::createDouble(row.contribution));
                    rowObj.set("coveredDays", J::createDouble(static_cast<double>(row.coveredDays)));
                    auto cumArr = J::createArray();
                    for (double v : row.cumulativeContribution)
                        cumArr.push_back(J::createDouble(v));
                    rowObj.set("cumulativeContribution", cumArr);
                    rowsArr.push_back(rowObj);
                }
                attrObj.set("rows", rowsArr);
                // 组合实际累计曲线 (扣费后, UI 堆叠面积图上叠加虚线)
                auto compCumArr = J::createArray();
                double compCum = 0.0;
                for (double r : tradingResult.costAdjustedLongShortReturns) {
                    compCum += r;
                    compCumArr.push_back(J::createDouble(compCum));
                }
                attrObj.set("compositeCumulative", compCumArr);
                metrics.set("factorAttribution", attrObj);
            }

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
        swJson.stop();
        swJson.report("JSON 构建");
        if (onProgress) onProgress(100.0, "completed");

    // (dbFallback 由 FactorValuePipeline 内部 guard 在 run 返回时自动清理)
    INTERNAL_INFO_STREAM << "[MEM] Orchestrator::run() 退出 — 管线 dbFallback/回看缓存已由管线内部释放";
}

} // namespace Factor::backtest
