#pragma once

#include "ModuleRegistryAssembler.h"
#include "RunArtifactRepository.h"
#include "RunTaskService.hpp"
#include "../../../domain/scheduler/include/ResourceGovernor.h"

#include <optional>
#include <vector>

namespace application::backtest {

struct BacktestRuntimeBuildError final {
    RunErrorCode code{RunErrorCode::None};

    [[nodiscard]] bool ok() const noexcept
    {
        return code == RunErrorCode::None;
    }
};

struct RunBacktestIngressResult final {
    RunErrorCode code{RunErrorCode::None};
    std::optional<RunTaskId> taskId;
    RunResult result;

    [[nodiscard]] bool ok() const noexcept
    {
        return code == RunErrorCode::None && result.ok();
    }
};

class BacktestRuntime final {
public:
    static std::optional<BacktestRuntime>
    build(const ExistingModuleSlots& slots, BacktestRuntimeBuildError* error = nullptr);

    static std::optional<BacktestRuntime>
    buildWithExecutionPolicies(
        const ExistingModuleSlots& slots,
        std::unique_ptr<ISignalValueProjection> signalValueProjection,
        std::unique_ptr<IRiskLimitsPolicy> riskLimitsPolicy,
        std::unique_ptr<ITranslationSpecPolicy> translationSpecPolicy,
        BacktestRuntimeBuildError* error = nullptr);

    BacktestRuntime(OwnedModules ownedModules,
                           RunOrchestratorSkeleton orchestrator,
                           RunTaskService runTaskService);

    [[nodiscard]] RunBacktestIngressResult runBacktest(RunSpec spec);

    [[nodiscard]] std::optional<PersistedRunArtifact> findArtifactByTaskId(RunTaskId taskId) const;

    void copyAllArtifacts(std::vector<PersistedRunArtifact>& output) const;

    [[nodiscard]] IRunTaskService& runTaskService() noexcept;
    [[nodiscard]] const IRunTaskService& runTaskService() const noexcept;
    [[nodiscard]] const OwnedModules& ownedModules() const noexcept;

private:
    OwnedModules ownedModules_;
    RunOrchestratorSkeleton orchestrator_;
    RunTaskService runTaskService_;
};

} // namespace application::backtest