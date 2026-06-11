#include "BacktestScheduler.h"
#include "ResourceGovernor.h"
#include "factor_compute/FactorEngine.h"

#include <algorithm>

namespace domain::scheduler {

BacktestScheduler::BacktestScheduler(std::uint64_t memoryLimitBytes) {
    m_governor = new ResourceGovernor(memoryLimitBytes);
}

BacktestScheduler::~BacktestScheduler() {
    delete m_governor;
}

void BacktestScheduler::setDataService(factor::compute::BacktestDataService* dataService) {
    m_dataService = dataService;
}

BatchPlan BacktestScheduler::submit(std::size_t totalStockCount, std::size_t batchSize) {
    ResourceGovernor governor(0U);
    std::size_t numBatches = governor.computeBatchCount(totalStockCount, batchSize);

    BatchPlan plan;
    plan.totalItems   = totalStockCount;
    plan.batchSize    = batchSize;
    plan.totalBatches = numBatches;
    return plan;
}

void BacktestScheduler::forEachBatch(const BatchPlan& plan,
                                      BatchDataCallback callback) {
    for (std::size_t i = 0; i < plan.totalBatches; ++i) {
        std::size_t start = i * plan.batchSize;
        std::size_t count = std::min(plan.batchSize, plan.totalItems - start);
        (void)count;

        factor::compute::MarketMatrixBatch marketMatrix;
        if (m_dataService) {
            marketMatrix = m_dataService->loadBatch(i);
        }
        callback(marketMatrix);
    }
}

} // namespace domain::scheduler