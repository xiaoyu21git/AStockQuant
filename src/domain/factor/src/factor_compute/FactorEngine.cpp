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
#include "foundation/thread/CancellationGuard.h"
#include <algorithm>
#include <atomic>
#include <cassert>
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
                                            size_t skipDates,
                                            const CachedMarketDataViewHistoricalAdapter::DbFallbackFn& dbFallback,
                                            const std::atomic<bool>* cancelFlag) {
    FactorMatrix result;
    result.batchIndex = marketData.batchIndex;

    INTERNAL_INFO_STREAM << "[FE] compute 进入: factorName=" << cacheKey.factorName
        << " m_instanceManager=" << static_cast<void*>(m_instanceManager)
        << " m_dataSvc=" << static_cast<void*>(m_dataSvc)
        << " marketView=" << static_cast<const void*>(marketData.marketView);

    if (!m_instanceManager) {
        INTERNAL_ERROR_STREAM << "[FE] compute 中止: m_instanceManager 为空";
        return result;
    }

    auto factor = m_instanceManager->createInstance(cacheKey.factorName);
    INTERNAL_INFO_STREAM << "[FE] compute: createInstance(" << cacheKey.factorName << ") = " << static_cast<void*>(factor.get());
    if (!factor) {
        INTERNAL_ERROR_STREAM << "[FE] compute 中止: createInstance 返回 null";
        return result;
    }
    return computeWithInstance(*factor, marketData, cacheKey, skipDates,
                               dbFallback, cancelFlag);
}

FactorMatrix FactorEngine::computeWithInstance(
    factor::BaseFactor& factor,
    const MarketMatrixBatch& marketData,
    const FactorCacheKey& cacheKey,
    size_t skipDates,
    const CachedMarketDataViewHistoricalAdapter::DbFallbackFn& dbFallback,
    const std::atomic<bool>* cancelFlag)
{
    // [P2-1] 防回退 ASSERT: 计算路径不得触碰 SignalCache — 并行多 worker 共享引擎
    // 的无锁前提。已核实引擎写入口仅 setInstanceManager/setDataService/
    // clearSignalCache 三个 setter, 计算期间不被调用; SignalCache 的写方为
    // FactorComputeEngine 链路 (回测路径不经过), 故入口计数不变 = 未污染。
    // (仅在 Debug 生效 — ASSERT 语义)
    const size_t cacheCountBefore = m_signalCache ? m_signalCache->entryCount() : 0;
    (void)cacheCountBefore;

    FactorMatrix result = computeImpl(factor, marketData, cacheKey, skipDates,
                                      dbFallback, cancelFlag, this);

    assert(!m_signalCache || m_signalCache->entryCount() == cacheCountBefore
           && "compute 路径不得读写 SignalCache");
    return result;
}

// ── 核心计算循环: compute / computeWithInstance 共用的唯一实现 (静态纯函数) ──
// 并行多 worker 共享同一引擎不加锁 — engine 仅以 const 指针进入 (只读 m_dataSvc)

FactorMatrix FactorEngine::computeImpl(
    factor::BaseFactor& factor,
    const MarketMatrixBatch& marketData,
    const FactorCacheKey& cacheKey,
    size_t skipDates,
    const CachedMarketDataViewHistoricalAdapter::DbFallbackFn& dbFallback,
    const std::atomic<bool>* cancelFlag,
    const FactorEngine* engine)
{
    FactorMatrix result;
    result.batchIndex = marketData.batchIndex;

    // 获取因子需要的字段 → 按需构建 MarketView
    if (engine->m_dataSvc) {
        auto fieldReqs = factor.getDataRequirements();
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
        INTERNAL_INFO_STREAM << "[FE] compute: calling buildViewForFields (大数据集可能需要一段时间)...";
        engine->m_dataSvc->buildViewForFields(neededFields);
        INTERNAL_INFO_STREAM << "[FE] compute: buildViewForFields DONE";
    } else {
        INTERNAL_INFO_STREAM << "[FE] compute: 无 m_dataSvc, 跳过 buildViewForFields";
    }

    // 从 DataSvc 获取数据视图
    const IMarketDataView* view = marketData.marketView;
    if (!view && engine->m_dataSvc) {
        MarketMatrixBatch batch = engine->m_dataSvc->loadBatch(0);
        view = batch.marketView;
    }
    INTERNAL_INFO_STREAM << "[FE] compute: view=" << static_cast<const void*>(view)
        << " dates=" << (view ? view->dates().size() : 0)
        << " instruments=" << (view ? view->instruments().size() : 0);

    if (!view) {
        INTERNAL_ERROR_STREAM << "[FE] compute 中止: view 为空";
        return result;
    }

    // dbFallback 参数优先 (并行块任务经此传入), 空则回退读 m_dataSvc
    const CachedMarketDataViewHistoricalAdapter::DbFallbackFn dbFn =
        dbFallback ? dbFallback
                   : (engine->m_dataSvc ? engine->m_dataSvc->dbFallback()
                                        : CachedMarketDataViewHistoricalAdapter::DbFallbackFn{});
    // 单个 adapter 全日期循环复用 — 每(因子×日期)重建两个 ~5700 项哈希索引是纯开销
    auto adapter = std::make_shared<CachedMarketDataViewHistoricalAdapter>(*view);
    if (dbFn) adapter->setDbFallback(dbFn);
    auto symbols = adapter->getAvailableSymbols("");
    int dateCount = 0, valueCount = 0;
    INTERNAL_INFO_STREAM << "[FE] compute: symbols=" << symbols.size()
        << " skipDates=" << skipDates;
    // 软取消检查点: 每日期循环间 (取消延迟 = 单日 calculate)
    foundation::thread::CancellationGuard cancelGuard(cancelFlag);
    const auto& allDates = view->dates();
    for (size_t di = skipDates; di < allDates.size(); ++di) {
        cancelGuard.throwIfCancelled();
        const auto& date = allDates[di];
        // date.value 是 YYYYMMDD int，转为 "YYYY-MM-DD" 以匹配 getValues 的查找格式
        const int dv = date.value;
        char dateBuf[16];
        std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", dv / 10000, (dv / 100) % 100, dv % 100);
        std::string dateStr(dateBuf);
        auto raw = computeOneDay(factor, dateStr, symbols, adapter);
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
    INTERNAL_INFO_STREAM << "[FE] compute 完成: " << cacheKey.factorName
        << " dates=" << view->dates().size()
        << " symbols=" << symbols.size() << " validDates=" << dateCount << " values=" << valueCount;

    return result;
}

// ── 公共的单日计算入口：所有调用方（compute / computeSingleDate / RuntimeFactorSvc）共享 ──

std::unordered_map<std::string, double> FactorEngine::computeOneDay(
    factor::BaseFactor& factor,
    const std::string& dateStr,
    const std::vector<std::string>& symbols,
    const std::shared_ptr<CachedMarketDataViewHistoricalAdapter>& historicalView)
{
    std::unordered_map<std::string, double> result;
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
        INTERNAL_ERROR_STREAM << "[FE] computeSingleDate: m_instanceManager 为空";
        return {};
    }
    if (!view) {
        INTERNAL_ERROR_STREAM << "[FE] computeSingleDate: view 为空";
        return {};
    }
    auto factor = m_instanceManager->createInstance(factorName);
    if (!factor) {
        INTERNAL_ERROR_STREAM << "[FE] computeSingleDate: factor 未找到: " << factorName;
        return {};
    }
    // 单日独立路径: 每次独立构造 adapter (与 compute 的全循环复用互不影响)
    auto historicalView = std::make_shared<CachedMarketDataViewHistoricalAdapter>(*view);
    return computeOneDay(*factor, date, symbols, historicalView);
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