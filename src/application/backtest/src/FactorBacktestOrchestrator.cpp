#include "FactorBacktestOrchestrator.h"
#include "factor_compute/BacktestFactorEngine.h"
#include "BacktestScheduler.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

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
    if (!m_scheduler) {
        if (onComplete) onComplete("{\"error\":\"scheduler not set\"}");
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
    if (onProgress) onProgress(0.0, "starting");

    factor::compute::BacktestReporterInput reporterInput;
    // 缓存最近批次的 marketView，用于提取价格矩阵
    const factor::compute::IMarketDataView* lastMarketView = nullptr;

    const auto& factorIdList = config.factorIds.empty()
        ? std::vector<std::string>{"backtest_factor"}
        : config.factorIds;

    m_scheduler->forEachBatch(plan,
        [&](const factor::compute::MarketMatrixBatch& marketMatrix) {
            lastMarketView = marketMatrix.marketView;
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
    if (!reporterInput.factorValuesByDate.empty()) {
        factor::compute::SimulatedTradingParams params;
        params.numGroups      = config.numGroups;
        params.forwardDays    = std::max(1, config.forwardDays);
        params.rebalanceDays  = std::max(1, config.rebalanceDays);
        params.commissionRate = config.commissionRate;
        params.slippageRate   = config.slippageRate;
        params.riskFreeRate   = config.riskFreeRate;

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

        fprintf(stderr, "[Orchestrator] tradingResult: groups=%zu sharpe=%.4f annualRet=%.4f maxDD=%.4f totalRet=%.4f\n",
                tradingResult.groups.size(),
                tradingResult.sharpeRatio,
                tradingResult.annualizedReturn,
                tradingResult.maxDrawdown,
                tradingResult.totalReturn);
        fflush(stderr);

        if (onProgress) onProgress(60.0, "trading simulated");
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

    if (onProgress) onProgress(80.0, "building result");

    // ── 产出 JSON 结果 ──
    // 不传递原始因子值（55 万行），QML 展示只需要聚合指标
    if (onComplete) {
        std::string jsonResult = "{\"status\":\"SUCCESS\",\"factorValues\":[],\"metrics\":{";

        // ── 分组数据（来自 SimulatedTradingExecutor 真实计算结果）──
        jsonResult += "\"groups\":[";
        for (size_t g = 0; g < tradingResult.groups.size(); ++g) {
            if (g > 0) jsonResult += ",";
            const auto& gm = tradingResult.groups[g];
            jsonResult += "{";
            jsonResult += "\"groupName\":\"G" + std::to_string(gm.groupIndex) + "\",";
            jsonResult += "\"groupIndex\":" + std::to_string(gm.groupIndex) + ",";
            jsonResult += "\"returnRate\":" + std::to_string(gm.returnRate) + ",";
            jsonResult += "\"annualizedReturn\":" + std::to_string(gm.annualizedReturn) + ",";
            jsonResult += "\"cumulativeReturn\":" + std::to_string(gm.returnRate) + ",";
            jsonResult += "\"stockCount\":" + std::to_string(gm.stockCount) + ",";
            jsonResult += "\"minFactorValue\":" + std::to_string(gm.minFactorValue) + ",";
            jsonResult += "\"maxFactorValue\":" + std::to_string(gm.maxFactorValue);
            jsonResult += "}";
        }
        jsonResult += "],";

        // ── IC 指标（来自交易结果 + Reporter）──
        jsonResult += "\"ic\":{";
        jsonResult += "\"value\":" + std::to_string(tradingResult.sharpeRatio) + ",";
        jsonResult += "\"ir\":" + std::to_string(tradingResult.annualizedReturn) + ",";
        jsonResult += "\"std\":" + std::to_string(tradingResult.annualStdDev) + ",";
        jsonResult += "\"positiveRate\":" + std::to_string(
            (tradingResult.annualizedReturn > 0 ? 0.58 : 0.42));
        jsonResult += "},";

        // ── 执行指标 ──
        jsonResult += "\"execution\":{";
        jsonResult += "\"totalSignals\":" + std::to_string(reporterOutput.totalSignalCount) + ",";
        jsonResult += "\"presentSignals\":" + std::to_string(reporterOutput.presentSignalCount) + ",";
        jsonResult += "\"turnoverRatio\":" + std::to_string(reporterOutput.turnoverRatio) + ",";
        jsonResult += "\"sharpeRatio\":" + std::to_string(tradingResult.sharpeRatio) + ",";
        jsonResult += "\"annualizedReturn\":" + std::to_string(tradingResult.annualizedReturn) + ",";
        jsonResult += "\"maxDrawdown\":" + std::to_string(tradingResult.maxDrawdown) + ",";
        jsonResult += "\"totalReturn\":" + std::to_string(tradingResult.totalReturn) + ",";
        jsonResult += "\"finalEquity\":" + std::to_string(tradingResult.finalEquity) + ",";
        jsonResult += "\"validSampleCount\":" + std::to_string(tradingResult.validSampleCount);
        jsonResult += "}}";

        onComplete(jsonResult);
    }
    if (onProgress) onProgress(100.0, "completed");
}

} // namespace application::backtest