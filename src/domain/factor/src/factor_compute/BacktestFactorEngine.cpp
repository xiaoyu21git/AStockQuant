#include "factor_compute/BacktestFactorEngine.h"
#include "factor_compute/SignalCache.h"
#include "factor_compute/MarketDataViewHistoricalAdapter.h"
#include "FactorInstanceManager.h"
#include "BaseFactor.h"
#include "HistoricalView.h"

#include <algorithm>
#include <thread>
#include <cmath>

namespace factor::compute {

BacktestDataService::BacktestDataService() = default;
BacktestDataService::~BacktestDataService() = default;

void BacktestDataService::initialize(const std::string& parquetPath, std::size_t batchSize) {
    (void)parquetPath;
    (void)batchSize;
}

MarketMatrixBatch BacktestDataService::loadBatch(std::size_t batchIndex) {
    MarketMatrixBatch batch;
    batch.batchIndex = batchIndex;
    return batch;
}

BacktestFactorEngine::BacktestFactorEngine(uint64_t maxMemoryBytes)
    : m_signalCache(std::make_unique<SignalCache>(maxMemoryBytes)) {
}

BacktestFactorEngine::~BacktestFactorEngine() = default;

void BacktestFactorEngine::setInstanceManager(factor::FactorInstanceManager* mgr) {
    m_instanceManager = mgr;
}

FactorMatrix BacktestFactorEngine::compute(const MarketMatrixBatch& marketData,
                                            const FactorCacheKey& cacheKey) {
    FactorMatrix result;
    result.batchIndex = marketData.batchIndex;

    if (!m_instanceManager) return result;

    auto factor = m_instanceManager->createInstance(cacheKey.factorName);
    if (!factor) return result;

    auto fieldReqs = factor->getDataRequirements();
    auto boundary = factor->getBoundaryRules();

    unsigned int numThreads = std::max(1u,
        std::thread::hardware_concurrency() > 2
            ? std::thread::hardware_concurrency() - 2 : 1u);
    (void)fieldReqs;
    (void)boundary;
    (void)numThreads;

    if (marketData.marketView) {
        CachedMarketDataViewHistoricalAdapter adapter(*marketData.marketView);
        auto symbols = adapter.getAvailableSymbols("");
        for (const auto& date : marketData.marketView->dates()) {
            std::string dateStr = std::to_string(date.value);
            factor::CalculationContext ctx(dateStr, symbols,
                std::make_shared<CachedMarketDataViewHistoricalAdapter>(*marketData.marketView));
            auto cr = factor->calculate(ctx);
            std::map<std::string, double> dateValues;
            for (const auto& [sym, val] : cr.values) {
                dateValues[sym] = val;
            }
            result.factorValues[dateStr] = std::move(dateValues);
        }
    }

    return result;
}

BacktestReporter::BacktestReporter() = default;
BacktestReporter::~BacktestReporter() = default;

BacktestReporterOutput BacktestReporter::analyze(const BacktestReporterInput& input) {
    BacktestReporterOutput output;
    return output;
}

} // namespace factor::compute
