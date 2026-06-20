#include "factor_compute/FactorEngine.h"
#include "factor_compute/SignalCache.h"
#include "factor_compute/MarketDataViewHistoricalAdapter.h"
#include "FactorInstanceManager.h"
#include "BaseFactor.h"
#include "HistoricalView.h"
#include "foundation/json/json_facade.h"
#include "factor_compute/CachedMarketDataView.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace factor::compute {

// computeOneDay 调用计数器（每次 compute() 入口重置，用于限制日志输出）
static int s_computeOneDayCounter = 0;

BacktestDataService::BacktestDataService() = default;
BacktestDataService::~BacktestDataService() = default;

void BacktestDataService::buildViewForFields(const std::vector<std::string>& extraFields)
{
    buildViewForFields(extraFields, nullptr);
}

void BacktestDataService::buildViewForFields(const std::vector<std::string>&,
                                              const std::function<void(double)>& onProgress)
{
    if (m_marketView) {
        if (onProgress) onProgress(100.0);
        return;
    }
    if (onProgress) onProgress(100.0);
}

void BacktestDataService::setMarketView(IMarketDataView* view) {
    m_marketView = view;
}

MarketMatrixBatch BacktestDataService::loadBatch(std::size_t batchIndex)
{
    MarketMatrixBatch batch;
    batch.batchIndex = batchIndex;
    batch.marketView = m_marketView;
    return batch;
}

FactorEngine::FactorEngine(uint64_t maxMemoryBytes)
    : m_signalCache(std::make_unique<SignalCache>(maxMemoryBytes)) {
}

FactorEngine::~FactorEngine() = default;

void FactorEngine::setInstanceManager(factor::FactorInstanceManager* mgr) {
    m_instanceManager = mgr;
}

void FactorEngine::setDataService(BacktestDataService* dataSvc) {
    m_dataSvc = dataSvc;
}

FactorMatrix FactorEngine::compute(const MarketMatrixBatch& marketData,
                                            const FactorCacheKey& cacheKey) {
    FactorMatrix result;
    result.batchIndex = marketData.batchIndex;

    s_computeOneDayCounter = 0;  // 每个因子重置计数器

    fprintf(stderr, "[FE] compute ENTER: factorName=%s m_instanceManager=%p m_dataSvc=%p marketView=%p\n",
            cacheKey.factorName.c_str(),
            static_cast<void*>(m_instanceManager),
            static_cast<void*>(m_dataSvc),
            static_cast<const void*>(marketData.marketView));
    fflush(stderr);

    if (!m_instanceManager) {
        fprintf(stderr, "[FE] compute ABORT: m_instanceManager is null\n");
        fflush(stderr);
        return result;
    }

    auto factor = m_instanceManager->createInstance(cacheKey.factorName);
    fprintf(stderr, "[FE] compute: createInstance(%s) = %p\n",
            cacheKey.factorName.c_str(), static_cast<void*>(factor.get()));
    fflush(stderr);
    if (!factor) {
        fprintf(stderr, "[FE] compute ABORT: createInstance returned null\n");
        fflush(stderr);
        return result;
    }

    // 获取因子需要的字段 → 按需构建 MarketView
    if (m_dataSvc) {
        auto fieldReqs = factor->getDataRequirements();
        fprintf(stderr, "[FE] compute: requiredFields=%zu optionalFields=%zu\n",
                fieldReqs.requiredFields.size(), fieldReqs.optionalFields.size());
        for (const auto& f : fieldReqs.requiredFields)
            fprintf(stderr, "[FE] compute:   required: %s\n", f.c_str());
        for (const auto& f : fieldReqs.optionalFields)
            fprintf(stderr, "[FE] compute:   optional: %s\n", f.c_str());
        fflush(stderr);
        std::vector<std::string> neededFields = fieldReqs.requiredFields;
        for (const auto& f : fieldReqs.optionalFields) {
            neededFields.push_back(f);
        }
        fprintf(stderr, "[FE] compute: calling buildViewForFields (may take a while for large datasets)...\n");
        fflush(stderr);
        m_dataSvc->buildViewForFields(neededFields);
        fprintf(stderr, "[FE] compute: buildViewForFields DONE\n");
        fflush(stderr);
    } else {
        fprintf(stderr, "[FE] compute: no m_dataSvc, skipping buildViewForFields\n");
        fflush(stderr);
    }

    // 从 DataSvc 获取数据视图
    const IMarketDataView* view = marketData.marketView;
    if (!view && m_dataSvc) {
        MarketMatrixBatch batch = m_dataSvc->loadBatch(0);
        view = batch.marketView;
    }
    fprintf(stderr, "[FE] compute: view=%p dates=%zu instruments=%zu\n",
            static_cast<const void*>(view),
            view ? view->dates().size() : 0,
            view ? view->instruments().size() : 0);
    fflush(stderr);

    if (!view) {
        fprintf(stderr, "[FE] compute ABORT: view is null\n");
        fflush(stderr);
        return result;
    }

    CachedMarketDataViewHistoricalAdapter adapter(*view);
    auto symbols = adapter.getAvailableSymbols("");
    fprintf(stderr, "[FE] compute: symbols=%zu\n", symbols.size());
    fflush(stderr);

    int dateCount = 0, valueCount = 0;
    for (const auto& date : view->dates()) {
        // date.value 是 YYYYMMDD int，转为 "YYYY-MM-DD" 以匹配 getValues 的查找格式
        const int dv = date.value;
        char dateBuf[16];
        std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", dv / 10000, (dv / 100) % 100, dv % 100);
        std::string dateStr(dateBuf);
        auto raw = computeOneDay(*factor, dateStr, symbols, *view);
        if (!raw.empty()) {
            std::map<std::string, double> dateValues;
            for (auto& [sym, val] : raw) {
                dateValues[sym] = val;
                ++valueCount;
            }
            result.factorValues[dateStr] = std::move(dateValues);
            ++dateCount;
        }
    }
    fprintf(stderr, "[FE] compute DONE: dates=%zu symbols=%zu validDates=%d values=%d\n",
            view->dates().size(), symbols.size(), dateCount, valueCount);
    fflush(stderr);

    return result;
}

// ── 公共的单日计算入口：所有调用方（compute / computeSingleDate / RuntimeFactorSvc）共享 ──

std::unordered_map<std::string, double> FactorEngine::computeOneDay(
    factor::BaseFactor& factor,
    const std::string& dateStr,
    const std::vector<std::string>& symbols,
    const IMarketDataView& view)
{
    std::unordered_map<std::string, double> result;
    auto historicalView = std::make_shared<CachedMarketDataViewHistoricalAdapter>(view);
    factor::CalculationContext ctx(dateStr, symbols, historicalView);
    auto cr = factor.calculate(ctx);
    int nanCount = 0, infCount = 0, finiteCount = 0;
    double firstFinite = 0.0;
    for (const auto& [sym, val] : cr.values) {
        if (std::isfinite(val)) {
            result[sym] = val;
            if (finiteCount == 0) firstFinite = val;
            ++finiteCount;
        } else if (std::isnan(val)) {
            ++nanCount;
        } else if (std::isinf(val)) {
            ++infCount;
        }
    }
    // 输出前 3 个日期的详细统计（计数器在 compute() 入口重置）
    if (++s_computeOneDayCounter <= 3) {
        fprintf(stderr, "[FE] computeOneDay #%d: date=%s symbols=%zu total=%zu finite=%d nan=%d inf=%d firstFinite=%.6f\n",
                s_computeOneDayCounter, dateStr.c_str(), symbols.size(), cr.values.size(),
                finiteCount, nanCount, infCount, firstFinite);
        if (nanCount > 0 || infCount > 0) {
            int printed = 0;
            for (const auto& [sym, val] : cr.values) {
                if (!std::isfinite(val) && printed < 3) {
                    fprintf(stderr, "[FE]   bad sample: sym=%s val=%.6f\n", sym.c_str(), val);
                    fflush(stderr);
                    ++printed;
                }
            }
        }
        fflush(stderr);
    }
    return result;
}

std::unordered_map<std::string, double> FactorEngine::computeSingleDate(
    const std::string& factorName,
    const std::string& date,
    const std::vector<std::string>& symbols,
    const IMarketDataView* view)
{
    if (!m_instanceManager) {
        fprintf(stderr, "[FactorEngine] computeSingleDate: m_instanceManager is null\n"); fflush(stderr);
        return {};
    }
    if (!view) {
        fprintf(stderr, "[FactorEngine] computeSingleDate: view is null\n"); fflush(stderr);
        return {};
    }
    auto factor = m_instanceManager->createInstance(factorName);
    if (!factor) {
        fprintf(stderr, "[FactorEngine] computeSingleDate: factor not found: %s\n", factorName.c_str());
        fflush(stderr);
        return {};
    }
    return computeOneDay(*factor, date, symbols, *view);
}

BacktestReporter::BacktestReporter() = default;
BacktestReporter::~BacktestReporter() = default;

BacktestReporterOutput BacktestReporter::analyze(const BacktestReporterInput& input) {
    BacktestReporterOutput output;

    for (const auto& [date, symbolMap] : input.factorValuesByDate) {
        output.totalSignalCount += static_cast<uint32_t>(symbolMap.size());
        for (const auto& [symbol, value] : symbolMap) {
            if (std::isfinite(value) && std::abs(value) > 1e-9) {
                output.presentSignalCount++;
            }
        }
    }

    return output;
}

} // namespace factor::compute