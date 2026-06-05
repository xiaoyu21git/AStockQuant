#include "../include/BacktestPipelineOrchestrator.h"

#include "../../../domain/backtest/include/BacktestRequest.h"

#include <algorithm>

namespace application::backtest {

namespace {

struct StageEntry {
    RunStage stage;
    RunErrorCode missingError;
};

constexpr StageEntry kPipelineStages[] = {
    {RunStage::GenerateSignal, RunErrorCode::MissingSignalProducer},
    {RunStage::ConstructTargetPosition, RunErrorCode::MissingExecutionModule},
    {RunStage::RiskApprove, RunErrorCode::MissingExecutionModule},
    {RunStage::GenerateOrders, RunErrorCode::MissingExecutionModule},
    {RunStage::ExecuteFill, RunErrorCode::MissingExecutionModule},
    {RunStage::UpdatePositionState, RunErrorCode::MissingExecutionModule},
    {RunStage::AggregateMetrics, RunErrorCode::MissingExecutionModule},
    {RunStage::BuildDiagnostics, RunErrorCode::MissingExecutionModule},
    {RunStage::Finalize, RunErrorCode::MissingExecutionModule},
};

constexpr std::size_t kPipelineStageCount = sizeof(kPipelineStages) / sizeof(kPipelineStages[0]);

} // anonymous namespace

StagePipeline::StagePipeline() = default;

void StagePipeline::setSignalProducer(std::shared_ptr<ISignalProducer> producer)
{
    signalProducer_ = std::move(producer);
}

void StagePipeline::setRiskApprovalEngine(std::shared_ptr<IRiskApprovalStageEngine> engine)
{
    riskApprovalEngine_ = std::move(engine);
}

void StagePipeline::setOrderGenerationEngine(std::shared_ptr<IOrderGenerationEngine> engine)
{
    orderGenerationEngine_ = std::move(engine);
}

void StagePipeline::setFillEngine(std::shared_ptr<IFillEngine> engine)
{
    fillEngine_ = std::move(engine);
}

StageResult StagePipeline::executeStage(RunStage stage, RunContext& context) const
{
    switch (stage) {
    case RunStage::GenerateSignal:
        if (signalProducer_) {
            return signalProducer_->generateSignal(context);
        }
        break;
    case RunStage::ConstructTargetPosition: {
        StageResult ok;
        ok.stage = stage;
        ok.code = RunErrorCode::None;
        return ok;
    }
    case RunStage::RiskApprove:
        if (riskApprovalEngine_) {
            return riskApprovalEngine_->approve(context);
        }
        break;
    case RunStage::GenerateOrders:
        if (orderGenerationEngine_) {
            return orderGenerationEngine_->generateOrders(context);
        }
        break;
    case RunStage::ExecuteFill:
        if (fillEngine_) {
            return fillEngine_->executeFill(context);
        }
        break;
    case RunStage::UpdatePositionState:
    case RunStage::AggregateMetrics:
    case RunStage::BuildDiagnostics:
    case RunStage::Finalize: {
        StageResult ok;
        ok.stage = stage;
        ok.code = RunErrorCode::None;
        return ok;
    }
    default:
        break;
    }

    StageResult fail;
    fail.stage = stage;
    fail.code = RunErrorCode::MissingExecutionModule;
    return fail;
}

RunResult StagePipeline::execute(RunContext& context)
{
    aggregator_.reset();
    if (context.spec.request) {
        aggregator_.setWindowDates(
            context.spec.request->window.startDate,
            context.spec.request->window.endDate);
    }

    for (std::size_t i = 0; i < kPipelineStageCount; ++i) {
        const RunStage stage = kPipelineStages[i].stage;
        aggregator_.recordStageStart(stage);
        StageResult result = executeStage(stage, context);
        aggregator_.recordStageEnd(stage, result);
        if (stage == RunStage::ExecuteFill) {
            aggregator_.updateFromFillResult(context);
        }
        if (!result.ok()) {
            return buildFailureResult(stage, result.code);
        }
    }

    RunResult ok;
    ok.code = RunErrorCode::None;
    ok.completedStage = RunStage::Finalize;
    return ok;
}

AggregatedPerformanceMetrics StagePipeline::metrics() const
{
    return aggregator_.build(
        *const_cast<domain::backtest::BacktestRequest*>(nullptr)); // placeholder
}

RunResult StagePipeline::buildFailureResult(RunStage failedStage,
                                             RunErrorCode code) const
{
    RunResult result;
    result.code = code;
    result.failureReason = toRunFailureReason(code);
    result.completedStage = failedStage;
    return result;
}

DefaultPipelineFactory::DefaultPipelineFactory(
    std::shared_ptr<ISignalProducer> signalProducer,
    const astock::domain::trading::risk_approval::IRiskApprovalEngine& riskApprovalEngine,
    const astock::domain::trading::signal_orders::ISignalOrderTranslator& signalOrderTranslator)
    : signalProducer_(std::move(signalProducer))
    , riskApprovalEngine_(riskApprovalEngine)
    , signalOrderTranslator_(signalOrderTranslator)
{
}

std::unique_ptr<StagePipeline> DefaultPipelineFactory::create() const
{
    auto pipeline = std::make_unique<StagePipeline>();
    pipeline->setSignalProducer(signalProducer_);

    auto riskStage = std::make_shared<SignalDrivenRiskApprovalStageAdapter>(riskApprovalEngine_);
    pipeline->setRiskApprovalEngine(riskStage);

    auto orderStage = std::make_shared<SignalDrivenOrderGenerationAdapter>(signalOrderTranslator_);
    pipeline->setOrderGenerationEngine(orderStage);

    auto fillStage = std::make_shared<BacktestVenueFillEngineAdapter>();
    pipeline->setFillEngine(fillStage);

    return pipeline;
}

BacktestOrchestrator::BacktestOrchestrator(std::unique_ptr<StagePipeline> pipeline)
    : pipeline_(std::move(pipeline))
{
}

RunResult BacktestOrchestrator::run(const RunSpec& spec)
{
    RunContext context;
    context.spec = spec;

    return pipeline_->execute(context);
}

AggregatedPerformanceMetrics BacktestOrchestrator::metrics() const
{
    return pipeline_->metrics();
}

} // namespace application::backtest