#pragma once

#include "BacktestContracts.hpp"
#include "BacktestInterfaces.hpp"
#include "ExecutionStageAdapters.h"
#include "PerformanceMetricsAggregator.h"

#include <memory>
#include <string>
#include <vector>

namespace application::backtest {

class StagePipeline final {
public:
    StagePipeline();

    void setSignalProducer(std::shared_ptr<ISignalProducer> producer);
    void setRiskApprovalEngine(std::shared_ptr<IRiskApprovalStageEngine> engine);
    void setOrderGenerationEngine(std::shared_ptr<IOrderGenerationEngine> engine);
    void setFillEngine(std::shared_ptr<IFillEngine> engine);

    [[nodiscard]] RunResult execute(RunContext& context);
    [[nodiscard]] AggregatedPerformanceMetrics metrics() const;

private:
    [[nodiscard]] StageResult executeStage(RunStage stage, RunContext& context) const;
    [[nodiscard]] RunResult buildFailureResult(RunStage failedStage,
                                                RunErrorCode code) const;

    std::shared_ptr<ISignalProducer> signalProducer_;
    std::shared_ptr<IRiskApprovalStageEngine> riskApprovalEngine_;
    std::shared_ptr<IOrderGenerationEngine> orderGenerationEngine_;
    std::shared_ptr<IFillEngine> fillEngine_;
    PerformanceMetricsAggregator aggregator_;
};

class DefaultPipelineFactory final {
public:
    DefaultPipelineFactory(
        std::shared_ptr<ISignalProducer> signalProducer,
        const astock::domain::trading::risk_approval::IRiskApprovalEngine& riskApprovalEngine,
        const astock::domain::trading::signal_orders::ISignalOrderTranslator& signalOrderTranslator);

    [[nodiscard]] std::unique_ptr<StagePipeline> create() const;

private:
    std::shared_ptr<ISignalProducer> signalProducer_;
    const astock::domain::trading::risk_approval::IRiskApprovalEngine& riskApprovalEngine_;
    const astock::domain::trading::signal_orders::ISignalOrderTranslator& signalOrderTranslator_;
};

class BacktestOrchestrator final {
public:
    explicit BacktestOrchestrator(std::unique_ptr<StagePipeline> pipeline);

    [[nodiscard]] RunResult run(const RunSpec& spec);
    [[nodiscard]] AggregatedPerformanceMetrics metrics() const;

private:
    std::unique_ptr<StagePipeline> pipeline_;
    PerformanceMetricsAggregator aggregator_;
};

} // namespace application::backtest