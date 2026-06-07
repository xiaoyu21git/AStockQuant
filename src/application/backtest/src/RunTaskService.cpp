#include "RunTaskService.hpp"

namespace application::backtest {

RunTaskService::RunTaskService(const IRunOrchestrator& orchestrator)
    : orchestrator_(&orchestrator)
{
}

void RunTaskService::rebindOrchestrator(const IRunOrchestrator& orchestrator)
{
    orchestrator_ = &orchestrator;
}

IngestTaskResult RunTaskService::ingestBacktestTask(RunSpec spec)
{
    IngestTaskResult result;

    if (!spec.request || !spec.runtimeBudget.isValid()) {
        result.code = RunTaskServiceErrorCode::InvalidInput;
        return result;
    }

    if (nextTaskId_ == RunTaskId::kInvalidValue || nextTaskId_ > kMaximumTaskId) {
        result.code = RunTaskServiceErrorCode::InvalidState;
        return result;
    }

    spec.taskId.value = nextTaskId_;

    for (const RunSpec& existingSpec : queuedSpecs_) {
        if (existingSpec.taskId.value == spec.taskId.value) {
            result.code = RunTaskServiceErrorCode::DuplicateTask;
            return result;
        }
    }

    queuedSpecs_.push_back(spec);

    RunTaskId taskId;
    taskId.value = nextTaskId_;
    result.taskId = taskId;
    result.code = RunTaskServiceErrorCode::None;

    if (nextTaskId_ == kMaximumTaskId) {
        nextTaskId_ = RunTaskId::kInvalidValue;
    } else {
        ++nextTaskId_;
    }
    return result;
}

ExecuteTaskResult RunTaskService::execute(RunTaskId taskId) const
{
    ExecuteTaskResult result;

    const std::optional<RunSpec> spec = findTaskSpec(taskId);
    if (!spec.has_value()) {
        result.code = RunTaskServiceErrorCode::TaskNotFound;
        result.result.code = RunErrorCode::InvalidRunSpec;
        result.result.failureReason = toRunFailureReason(result.result.code);
        result.result.completedStage = RunStage::Validate;
        result.result.partial = false;
        return result;
    }

    result.code = RunTaskServiceErrorCode::None;
    result.result = orchestrator_->run(*spec);
    return result;
}

std::optional<RunSpec> RunTaskService::findTaskSpec(RunTaskId taskId) const
{
    for (const RunSpec& spec : queuedSpecs_) {
        if (spec.taskId.value == taskId.value) {
            return spec;
        }
    }
    return std::nullopt;
}

} // namespace application::backtest