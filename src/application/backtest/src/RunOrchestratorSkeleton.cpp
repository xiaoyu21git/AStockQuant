#include "RunOrchestratorSkeleton.h"

namespace application::backtest {

namespace {

constexpr RunStage kStagePlan = RunStage::Plan;

} // namespace

RunOrchestratorSkeleton::RunOrchestratorSkeleton(RunModuleRegistry moduleRegistry)
    : moduleRegistry_(moduleRegistry)
{
}

RunResult RunOrchestratorSkeleton::run(const RunSpec& spec) const
{
    RunContext context;
    context.spec = spec;

    if (!spec.isValid()) {
        return failure(RunErrorCode::InvalidRunSpec, RunStage::Validate, context);
    }

    if (!hasRequiredModules(spec.mode)) {
        return failure(RunErrorCode::MissingExecutionModule, RunStage::Plan, context);
    }

    const RunSpecValidationResult validationResult = moduleRegistry_.runSpecValidator->validate(spec);
    if (!validationResult.ok()) {
        return failure(validationResult.code, RunStage::Validate, context);
    }

    const ModeRouteResult modeRouteResult = moduleRegistry_.modeRouter->resolve(spec);
    if (!modeRouteResult.ok()) {
        return failure(modeRouteResult.code, kStagePlan, context);
    }

    const ISignalProducer* signalProducer = resolveSignalProducer(spec.mode);
    if (signalProducer == nullptr) {
        return failure(RunErrorCode::MissingSignalProducer, RunStage::GenerateSignal, context);
    }

    const IFillEngine* fillEngine = resolveFillEngine(spec.mode);
    if (fillEngine == nullptr) {
        return failure(RunErrorCode::MissingExecutionModule, RunStage::ExecuteFill, context);
    }

    StageResult stageResult = moduleRegistry_.runtimeGuard->checkStageBudget(
        RunStage::LoadWindowData,
        spec.runtimeBudget.forStage(RunStage::LoadWindowData),
        context);
    if (!stageResult.ok()) {
        return failure(stageResult.code, stageResult.stage, context);
    }

    stageResult = moduleRegistry_.marketDataWindowProvider->loadWindowData(context);
    if (!stageResult.ok()) {
        return failure(stageResult.code, stageResult.stage, context);
    }

    stageResult = moduleRegistry_.runtimeGuard->checkStageBudget(
        RunStage::GenerateSignal,
        spec.runtimeBudget.forStage(RunStage::GenerateSignal),
        context);
    if (!stageResult.ok()) {
        return failure(stageResult.code, stageResult.stage, context);
    }

    stageResult = signalProducer->generateSignal(context);
    if (!stageResult.ok()) {
        return failure(stageResult.code, stageResult.stage, context);
    }

    stageResult = moduleRegistry_.portfolioConstructionEngine->constructTargetPosition(context);
    if (!stageResult.ok()) {
        return failure(stageResult.code, stageResult.stage, context);
    }

    stageResult = moduleRegistry_.riskApprovalEngine->approve(context);
    if (!stageResult.ok()) {
        return failure(stageResult.code, stageResult.stage, context);
    }

    stageResult = moduleRegistry_.orderGenerationEngine->generateOrders(context);
    if (!stageResult.ok()) {
        return failure(stageResult.code, stageResult.stage, context);
    }

    stageResult = fillEngine->executeFill(context);
    if (!stageResult.ok()) {
        return failure(stageResult.code, stageResult.stage, context);
    }

    stageResult = moduleRegistry_.positionStateEngine->updatePositionState(context);
    if (!stageResult.ok()) {
        return failure(stageResult.code, stageResult.stage, context);
    }

    stageResult = moduleRegistry_.metricsEngine->aggregateMetrics(context);
    if (!stageResult.ok()) {
        return failure(stageResult.code, stageResult.stage, context);
    }

    stageResult = moduleRegistry_.diagnosticsEngine->buildDiagnostics(context);
    if (!stageResult.ok()) {
        return failure(stageResult.code, stageResult.stage, context);
    }

    stageResult = moduleRegistry_.resultRepository->persistArtifacts(context);
    if (!stageResult.ok()) {
        return failure(stageResult.code, stageResult.stage, context);
    }

    stageResult = moduleRegistry_.exportArtifactBuilder->buildExportArtifacts(context);
    if (!stageResult.ok()) {
        return failure(stageResult.code, stageResult.stage, context);
    }

    RunResult result;
    result.code = RunErrorCode::None;
    result.failureReason = RunFailureReason::None;
    result.completedStage = RunStage::Finalize;
    result.partial = false;
    result.persistedArtifactCount = context.workingSet.persistedArtifactCount;
    result.diagnosticsCount = context.workingSet.diagnosticsCount;
    return result;
}

RunResult RunOrchestratorSkeleton::failure(
    RunErrorCode code,
    RunStage stage,
    const RunContext& context)
{
    RunResult result;
    result.code = code;
    result.failureReason = toRunFailureReason(code);
    result.completedStage = stage;
    result.partial = stage != RunStage::Validate;
    result.persistedArtifactCount = context.workingSet.persistedArtifactCount;
    result.diagnosticsCount = context.workingSet.diagnosticsCount;
    return result;
}

const ISignalProducer* RunOrchestratorSkeleton::resolveSignalProducer(RunMode mode) const
{
    if (mode == RunMode::FactorBacktest) {
        return moduleRegistry_.factorSignalProducer;
    }
    return moduleRegistry_.strategySignalProducer;
}

const IFillEngine* RunOrchestratorSkeleton::resolveFillEngine(RunMode mode) const
{
    if (mode == RunMode::FactorBacktest || mode == RunMode::StrategyBacktest) {
        return moduleRegistry_.backtestFillEngine;
    }
    return moduleRegistry_.liveFillEngine;
}

bool RunOrchestratorSkeleton::hasRequiredModules(RunMode mode) const
{
    return moduleRegistry_.hasSharedPipelineModules() && moduleRegistry_.hasModeModules(mode);
}

} // namespace application::backtest