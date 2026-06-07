#include "FactorBacktestOrchestrator.h"
#include "factor_compute/BacktestFactorEngine.h"
#include "BacktestScheduler.h"

namespace application::backtest {

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

void FactorBacktestOrchestrator::setFactorEngine(factor::compute::BacktestFactorEngine* engine) {
    m_engine = engine;
}

void FactorBacktestOrchestrator::setReporter(factor::compute::BacktestReporter* reporter) {
    m_reporter = reporter;
}

void FactorBacktestOrchestrator::run(
    const BacktestRunConfig& config,
    FactorOrchestratorProgressCallback onProgress,
    FactorOrchestratorResultCallback onComplete)
{
    if (!m_scheduler) {
        if (onComplete) onComplete("{\"error\":\"scheduler not set\"}");
        return;
    }

    auto plan = m_scheduler->submit(0, 500);
    if (onProgress) onProgress(0.0, "starting");

    factor::compute::BacktestReporterInput reporterInput;

    const auto& factorIdList = config.factorIds.empty()
        ? std::vector<std::string>{"backtest_factor"}
        : config.factorIds;

    m_scheduler->forEachBatch(plan,
        [&](const factor::compute::MarketMatrixBatch& marketMatrix) {
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
        });

    // 所有模式（因子回测/实盘/策略回测）只返回因子值
    // Reporter 分析由上层 FactorUI 中 BacktestResultView 触发
    if (onComplete) {
        std::string jsonResult = "{\"status\":\"SUCCESS\",\"factorValues\":[";
        bool first = true;
        for (const auto& [date, symbolMap] : reporterInput.factorValuesByDate) {
            for (const auto& [symbol, value] : symbolMap) {
                if (!first) jsonResult += ",";
                first = false;
                jsonResult += "{\"date\":\"" + date + "\",\"symbol\":\"" + symbol + "\",\"value\":" + std::to_string(value) + "}";
            }
        }
        jsonResult += "]}";
        onComplete(jsonResult);
    }
    if (onProgress) onProgress(100.0, "completed");
}

} // namespace application::backtest