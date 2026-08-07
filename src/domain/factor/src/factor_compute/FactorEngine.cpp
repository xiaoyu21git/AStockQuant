#include "factor_compute/FactorEngine.h"
#include "factor_compute/SignalCache.h"
#include "factor_compute/MarketDataViewHistoricalAdapter.h"
#include "factor_compute/CachedMarketDataView.h"
#include "FactorInstanceManager.h"
#include "BaseFactor.h"
#include "HistoricalView.h"
#include "foundation/json/json_facade.h"
#include "foundation/Utils/DateUtils.h"
#include "foundation/log/logging.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
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

void FactorEngine::clearSignalCache() {
    if (m_signalCache) {
        m_signalCache->clear();
    }
}

FactorMatrix FactorEngine::compute(const MarketMatrixBatch& marketData,
                                            const FactorCacheKey& cacheKey,
                                            size_t skipDates) {
    FactorMatrix result;
    result.batchIndex = marketData.batchIndex;

    s_computeOneDayCounter = 0;  // 每个因子重置计数器

    INTERNAL_INFO_STREAM << "[FE] compute ENTER: factorName=" << cacheKey.factorName
        << " m_instanceManager=" << static_cast<void*>(m_instanceManager)
        << " m_dataSvc=" << static_cast<void*>(m_dataSvc)
        << " marketView=" << static_cast<const void*>(marketData.marketView);

    if (!m_instanceManager) {
        INTERNAL_ERROR_STREAM << "[FE] compute ABORT: m_instanceManager is null";
        return result;
    }

    auto factor = m_instanceManager->createInstance(cacheKey.factorName);
    INTERNAL_INFO_STREAM << "[FE] compute: createInstance(" << cacheKey.factorName << ") = " << static_cast<void*>(factor.get());
    if (!factor) {
        INTERNAL_ERROR_STREAM << "[FE] compute ABORT: createInstance returned null";
        return result;
    }

    // 获取因子需要的字段 → 按需构建 MarketView
    if (m_dataSvc) {
        auto fieldReqs = factor->getDataRequirements();
        {
            std::ostringstream oss;
            oss << "[FE] compute: requiredFields=" << fieldReqs.requiredFields.size() << " optionalFields=" << fieldReqs.optionalFields.size();
            for (const auto& f : fieldReqs.requiredFields)
                oss << "\n[FE] compute:   required: " << f;
            for (const auto& f : fieldReqs.optionalFields)
                oss << "\n[FE] compute:   optional: " << f;
            INTERNAL_DEBUG_STREAM << oss.str();
        }
        std::vector<std::string> neededFields = fieldReqs.requiredFields;
        for (const auto& f : fieldReqs.optionalFields) {
            neededFields.push_back(f);
        }
        INTERNAL_INFO_STREAM << "[FE] compute: calling buildViewForFields (may take a while for large datasets)...";
        m_dataSvc->buildViewForFields(neededFields);
        INTERNAL_INFO_STREAM << "[FE] compute: buildViewForFields DONE";
    } else {
        INTERNAL_INFO_STREAM << "[FE] compute: no m_dataSvc, skipping buildViewForFields";
    }

    // 从 DataSvc 获取数据视图
    const IMarketDataView* view = marketData.marketView;
    if (!view && m_dataSvc) {
        MarketMatrixBatch batch = m_dataSvc->loadBatch(0);
        view = batch.marketView;
    }
    INTERNAL_INFO_STREAM << "[FE] compute: view=" << static_cast<const void*>(view)
        << " dates=" << (view ? view->dates().size() : 0)
        << " instruments=" << (view ? view->instruments().size() : 0);

    if (!view) {
        INTERNAL_ERROR_STREAM << "[FE] compute ABORT: view is null";
        return result;
    }

    CachedMarketDataViewHistoricalAdapter adapter(*view);
    if (m_dataSvc && m_dataSvc->dbFallback()) {
        adapter.setDbFallback(m_dataSvc->dbFallback());
    }
    auto symbols = adapter.getAvailableSymbols("");
    int dateCount = 0, valueCount = 0;
    INTERNAL_INFO_STREAM << "[FE] compute: symbols=" << symbols.size()
        << " skipDates=" << skipDates;
    const auto& allDates = view->dates();
    for (size_t di = skipDates; di < allDates.size(); ++di) {
        const auto& date = allDates[di];
        // date.value 是 YYYYMMDD int，转为 "YYYY-MM-DD" 以匹配 getValues 的查找格式
        const int dv = date.value;
        char dateBuf[16];
        std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", dv / 10000, (dv / 100) % 100, dv % 100);
        std::string dateStr(dateBuf);
        auto raw = computeOneDay(*factor, dateStr, symbols, *view,
            m_dataSvc ? m_dataSvc->dbFallback() : CachedMarketDataViewHistoricalAdapter::DbFallbackFn{});
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
    INTERNAL_INFO_STREAM << "[FE] compute DONE: " << cacheKey.factorName
        << " dates=" << view->dates().size()
        << " symbols=" << symbols.size() << " validDates=" << dateCount << " values=" << valueCount;

    return result;
}

// ── 公共的单日计算入口：所有调用方（compute / computeSingleDate / RuntimeFactorSvc）共享 ──

std::unordered_map<std::string, double> FactorEngine::computeOneDay(
    factor::BaseFactor& factor,
    const std::string& dateStr,
    const std::vector<std::string>& symbols,
    const IMarketDataView& view,
    const CachedMarketDataViewHistoricalAdapter::DbFallbackFn& dbFallback)
{
    std::unordered_map<std::string, double> result;
    auto historicalView = std::make_shared<CachedMarketDataViewHistoricalAdapter>(view);
    if (dbFallback) historicalView->setDbFallback(dbFallback);
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
    if (cr.values.empty()) {
        std::string firstSym = symbols.empty() ? "(none)" : symbols[0];
        std::string diag;
        if (cr.metadata.has("emptyReason")) {
            diag = cr.metadata.get("emptyReason").asString();
        } else if (cr.metadata.has("error")) {
            diag = cr.metadata.get("error").asString();
        }
        INTERNAL_WARN_STREAM << "[FE] 因子计算空: date=" << dateStr << " sym=" << firstSym
                             << " nan=" << nanCount
                             << (diag.empty() ? "" : " reason=" + diag);
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
        INTERNAL_ERROR_STREAM << "[FE] computeSingleDate: m_instanceManager is null";
        return {};
    }
    if (!view) {
        INTERNAL_ERROR_STREAM << "[FE] computeSingleDate: view is null";
        return {};
    }
    auto factor = m_instanceManager->createInstance(factorName);
    if (!factor) {
        INTERNAL_ERROR_STREAM << "[FE] computeSingleDate: factor not found: " << factorName;
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