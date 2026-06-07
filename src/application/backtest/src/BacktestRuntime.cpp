#include "BacktestRuntime.hpp"

namespace application::backtest {

namespace {

[[nodiscard]] RunErrorCode toRunErrorCode(RunTaskServiceErrorCode code) noexcept
{
    switch (code) {
    case RunTaskServiceErrorCode::None:
        return RunErrorCode::None;
    case RunTaskServiceErrorCode::InvalidInput:
        return RunErrorCode::InvalidRunSpec;
    case RunTaskServiceErrorCode::InvalidState:
        return RunErrorCode::StageExecutionFailed;
    case RunTaskServiceErrorCode::TaskNotFound:
        return RunErrorCode::InvalidRunSpec;
    case RunTaskServiceErrorCode::DuplicateTask:
        return RunErrorCode::StageExecutionFailed;
    default:
        return RunErrorCode::StageExecutionFailed;
    }
}

[[nodiscard]] const InMemoryRunArtifactRepository* resolveArtifactRepository(
    const OwnedModules& ownedModules) noexcept
{
    return dynamic_cast<const InMemoryRunArtifactRepository*>(ownedModules.resultRepository.get());
}

[[nodiscard]] InMemoryRunArtifactRepository* resolveMutableArtifactRepository(
    OwnedModules& ownedModules) noexcept
{
    return dynamic_cast<InMemoryRunArtifactRepository*>(ownedModules.resultRepository.get());
}

} // namespace

std::optional<BacktestRuntime>
BacktestRuntime::build(const ExistingModuleSlots& slots, BacktestRuntimeBuildError* error)
{
    return buildWithExecutionPolicies(
        slots,
        nullptr,
        nullptr,
        nullptr,
        error);
}

std::optional<BacktestRuntime>
BacktestRuntime::buildWithExecutionPolicies(
    const ExistingModuleSlots& slots,
    std::unique_ptr<ISignalValueProjection> signalValueProjection,
    std::unique_ptr<IRiskLimitsPolicy> riskLimitsPolicy,
    std::unique_ptr<ITranslationSpecPolicy> translationSpecPolicy,
    BacktestRuntimeBuildError* error)
{
    OwnedModules ownedModules = ModuleRegistryAssembler::assemble(
        slots,
        std::move(signalValueProjection),
        std::move(riskLimitsPolicy),
        std::move(translationSpecPolicy));
    RunModuleRegistry registry = ownedModules.registry();

    if (!registry.hasSharedPipelineModules()) {
        if (error != nullptr) {
            error->code = RunErrorCode::MissingExecutionModule;
        }
        return std::nullopt;
    }

    if (!registry.hasModeModules(RunMode::FactorBacktest)
        && !registry.hasModeModules(RunMode::StrategyBacktest)) {
        if (error != nullptr) {
            error->code = RunErrorCode::MissingModeRoute;
        }
        return std::nullopt;
    }

    if (error != nullptr) {
        error->code = RunErrorCode::None;
    }

    RunOrchestratorSkeleton orchestrator(registry);
    RunTaskService runTaskService(orchestrator);
    return BacktestRuntime(
        std::move(ownedModules),
        std::move(orchestrator),
        std::move(runTaskService));
}

BacktestRuntime::BacktestRuntime(OwnedModules ownedModules,
                                               RunOrchestratorSkeleton orchestrator,
                                               RunTaskService runTaskService)
    : ownedModules_(std::move(ownedModules))
    , orchestrator_(std::move(orchestrator))
    , runTaskService_(std::move(runTaskService))
{
    // RunTaskService 持有 orchestrator_ 的裸指针，move 后需要重新绑定
    runTaskService_.rebindOrchestrator(orchestrator_);
}

RunBacktestIngressResult BacktestRuntime::runBacktest(RunSpec spec)
{
    RunBacktestIngressResult ingressResult;
    const RunSpec submittedSpec = spec;

    IngestTaskResult ingestResult = runTaskService_.ingestBacktestTask(std::move(spec));
    ingressResult.taskId = ingestResult.taskId;
    if (!ingestResult.ok() || !ingestResult.taskId.has_value()) {
        ingressResult.code = toRunErrorCode(ingestResult.code);
        ingressResult.result.code = ingressResult.code;
        ingressResult.result.failureReason = toRunFailureReason(ingressResult.result.code);
        ingressResult.result.completedStage = RunStage::Validate;
        ingressResult.result.partial = false;
        return ingressResult;
    }

    ExecuteTaskResult executeResult = runTaskService_.execute(*ingestResult.taskId);
    ingressResult.code = toRunErrorCode(executeResult.code);
    ingressResult.result = executeResult.result;

    if (ingressResult.code == RunErrorCode::None && !ingressResult.result.ok()) {
        ingressResult.code = ingressResult.result.code;
        ingressResult.result.failureReason = toRunFailureReason(ingressResult.result.code);
    }

    if (ingressResult.code != RunErrorCode::None && ingressResult.result.ok()) {
        ingressResult.result.code = ingressResult.code;
        ingressResult.result.failureReason = toRunFailureReason(ingressResult.result.code);
        ingressResult.result.completedStage = RunStage::Validate;
        ingressResult.result.partial = false;
    }

    InMemoryRunArtifactRepository* repository = resolveMutableArtifactRepository(ownedModules_);
    if (repository != nullptr) {
        repository->upsertRunResult(submittedSpec, ingressResult.result);
    }

    return ingressResult;
}

std::optional<PersistedRunArtifact>
BacktestRuntime::findArtifactByTaskId(RunTaskId taskId) const
{
    const InMemoryRunArtifactRepository* repository = resolveArtifactRepository(ownedModules_);
    if (repository == nullptr) {
        return std::nullopt;
    }
    return repository->findByTaskId(taskId);
}

void BacktestRuntime::copyAllArtifacts(std::vector<PersistedRunArtifact>& output) const
{
    const InMemoryRunArtifactRepository* repository = resolveArtifactRepository(ownedModules_);
    if (repository == nullptr) {
        output.clear();
        return;
    }
    repository->copyAll(output);
}

IRunTaskService& BacktestRuntime::runTaskService() noexcept
{
    return runTaskService_;
}

const IRunTaskService& BacktestRuntime::runTaskService() const noexcept
{
    return runTaskService_;
}

const OwnedModules& BacktestRuntime::ownedModules() const noexcept
{
    return ownedModules_;
}

} // namespace application::backtest