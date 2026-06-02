#include "BacktestQuickStart.hpp"

namespace application::backtest {

QuickStartRunOutcome BacktestQuickStart::runAndFetchArtifact(
    const ExistingModuleSlots& slots,
    RunSpec spec)
{
    QuickStartRunOutcome outcome;

    BacktestRuntimeBuildError buildError;
    std::optional<BacktestRuntime> runtime = BacktestRuntime::build(slots, &buildError);
    if (!runtime.has_value()) {
        outcome.buildCode = buildError.code;
        outcome.runResult.code = buildError.code;
        outcome.runResult.result.code = buildError.code;
        outcome.runResult.result.failureReason = toRunFailureReason(buildError.code);
        outcome.runResult.result.completedStage = RunStage::Validate;
        outcome.runResult.result.partial = false;
        return outcome;
    }

    outcome.runResult = runtime->runBacktest(std::move(spec));
    if (outcome.runResult.taskId.has_value()) {
        outcome.artifact = runtime->findArtifactByTaskId(*outcome.runResult.taskId);
    }

    return outcome;
}

QuickStartRunOutcome BacktestQuickStart::runAndFetchArtifactWithPolicies(
    const ExistingModuleSlots& slots,
    RunSpec spec,
    std::unique_ptr<ISignalValueProjection> signalValueProjection,
    std::unique_ptr<IRiskLimitsPolicy> riskLimitsPolicy,
    std::unique_ptr<ITranslationSpecPolicy> translationSpecPolicy)
{
    QuickStartRunOutcome outcome;

    BacktestRuntimeBuildError buildError;
    std::optional<BacktestRuntime> runtime = BacktestRuntime::buildWithExecutionPolicies(
        slots,
        std::move(signalValueProjection),
        std::move(riskLimitsPolicy),
        std::move(translationSpecPolicy),
        &buildError);
    if (!runtime.has_value()) {
        outcome.buildCode = buildError.code;
        outcome.runResult.code = buildError.code;
        outcome.runResult.result.code = buildError.code;
        outcome.runResult.result.failureReason = toRunFailureReason(buildError.code);
        outcome.runResult.result.completedStage = RunStage::Validate;
        outcome.runResult.result.partial = false;
        return outcome;
    }

    outcome.runResult = runtime->runBacktest(std::move(spec));
    if (outcome.runResult.taskId.has_value()) {
        outcome.artifact = runtime->findArtifactByTaskId(*outcome.runResult.taskId);
    }

    return outcome;
}

QuickStartRunOutcome BacktestQuickStart::runAndFetchArtifactWithFillSideMode(
    const ExistingModuleSlots& slots,
    RunSpec spec,
    FillOrderSideMode fillSideMode)
{
    spec.fillOrderSideMode = fillSideMode;
    return runAndFetchArtifact(slots, std::move(spec));
}

} // namespace application::backtest