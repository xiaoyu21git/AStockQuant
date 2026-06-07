#pragma once

#include "RunOrchestratorSkeleton.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace application::backtest {

enum class RunTaskServiceErrorCode : std::uint8_t {
    None = 0,
    InvalidInput = 1,
    InvalidState = 2,
    TaskNotFound = 3,
    DuplicateTask = 4,
};

struct IngestTaskResult final {
    RunTaskServiceErrorCode code{RunTaskServiceErrorCode::None};
    std::optional<RunTaskId> taskId;

    [[nodiscard]] bool ok() const noexcept
    {
        return code == RunTaskServiceErrorCode::None && taskId.has_value() && taskId->isValid();
    }
};

struct ExecuteTaskResult final {
    RunTaskServiceErrorCode code{RunTaskServiceErrorCode::None};
    RunResult result;

    [[nodiscard]] bool ok() const noexcept
    {
        return code == RunTaskServiceErrorCode::None && result.ok();
    }
};

class IRunTaskService {
public:
    virtual ~IRunTaskService() = default;

    [[nodiscard]] virtual IngestTaskResult ingestBacktestTask(RunSpec spec) = 0;
    [[nodiscard]] virtual ExecuteTaskResult execute(RunTaskId taskId) const = 0;
};

class RunTaskService final : public IRunTaskService {
public:
    explicit RunTaskService(const IRunOrchestrator& orchestrator);

    [[nodiscard]] IngestTaskResult ingestBacktestTask(RunSpec spec) override;
    [[nodiscard]] ExecuteTaskResult execute(RunTaskId taskId) const override;

    /// @brief 重新绑定 orchestrator 引用，用于移动构造后修复指针
    void rebindOrchestrator(const IRunOrchestrator& orchestrator);

private:
    static constexpr std::uint64_t kFirstTaskId = 1ULL;
    static constexpr std::uint64_t kMaximumTaskId = std::numeric_limits<std::uint64_t>::max();

    [[nodiscard]] std::optional<RunSpec> findTaskSpec(RunTaskId taskId) const;

    const IRunOrchestrator* orchestrator_{nullptr};
    std::vector<RunSpec> queuedSpecs_;
    std::uint64_t nextTaskId_{kFirstTaskId};
};

} // namespace application::backtest