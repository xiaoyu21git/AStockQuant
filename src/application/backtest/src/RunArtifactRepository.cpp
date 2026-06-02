#include "RunArtifactRepository.h"

namespace application::backtest {

StageResult InMemoryRunArtifactRepository::persistArtifacts(RunContext& context) const
{
    StageResult result;
    result.stage = RunStage::PersistArtifacts;
    result.code = RunErrorCode::None;

    if (!context.spec.taskId.isValid() || context.workingSet.diagnosticsCount == 0U) {
        result.code = RunErrorCode::PersistFailed;
        return result;
    }

    PersistedRunArtifact artifact;
    artifact.taskId = context.spec.taskId;
    artifact.mode = context.spec.mode;
    artifact.code = RunErrorCode::None;
    artifact.failureReason = RunFailureReason::None;
    artifact.completedStage = RunStage::PersistArtifacts;
    artifact.partial = false;
    artifact.rebalancePointCount = context.workingSet.rebalancePointCount;
    artifact.targetPositionCount = context.workingSet.targetPositionCount;
    artifact.approvedOrderCount = context.workingSet.approvedOrderCount;
    artifact.generatedOrderCount = context.workingSet.generatedOrderCount;
    artifact.filledOrderCount = context.workingSet.filledOrderCount;
    artifact.metricCount = context.workingSet.metricCount;
    artifact.diagnosticsCount = context.workingSet.diagnosticsCount;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        storage_.push_back(artifact);
    }

    context.workingSet.persistedArtifactCount = 1U;
    return result;
}

void InMemoryRunArtifactRepository::upsertRunResult(const RunSpec& spec, const RunResult& result) const
{
    if (!spec.taskId.isValid()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (PersistedRunArtifact& artifact : storage_) {
        if (artifact.taskId.value != spec.taskId.value) {
            continue;
        }

        artifact.mode = spec.mode;
        artifact.code = result.code;
        artifact.failureReason = result.failureReason;
        artifact.completedStage = result.completedStage;
        artifact.partial = result.partial;
        if (result.diagnosticsCount > 0U) {
            artifact.diagnosticsCount = result.diagnosticsCount;
        }
        return;
    }

    PersistedRunArtifact artifact;
    artifact.taskId = spec.taskId;
    artifact.mode = spec.mode;
    artifact.code = result.code;
    artifact.failureReason = result.failureReason;
    artifact.completedStage = result.completedStage;
    artifact.partial = result.partial;
    artifact.diagnosticsCount = result.diagnosticsCount;
    storage_.push_back(artifact);
}

std::optional<PersistedRunArtifact> InMemoryRunArtifactRepository::findByTaskId(RunTaskId taskId) const
{
    if (!taskId.isValid()) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const PersistedRunArtifact& artifact : storage_) {
        if (artifact.taskId.value == taskId.value) {
            return artifact;
        }
    }
    return std::nullopt;
}

void InMemoryRunArtifactRepository::copyAll(std::vector<PersistedRunArtifact>& output) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    output = storage_;
}

} // namespace application::backtest