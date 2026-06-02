#pragma once

#include "BacktestInterfaces.hpp"

#include <mutex>
#include <optional>
#include <vector>

namespace application::backtest {

struct PersistedRunArtifact final {
    RunTaskId taskId;
    RunMode mode{RunMode::FactorBacktest};
    RunErrorCode code{RunErrorCode::None};
    RunFailureReason failureReason{RunFailureReason::None};
    RunStage completedStage{RunStage::Validate};
    bool partial{false};
    std::uint32_t rebalancePointCount{0U};
    std::uint32_t targetPositionCount{0U};
    std::uint32_t approvedOrderCount{0U};
    std::uint32_t generatedOrderCount{0U};
    std::uint32_t filledOrderCount{0U};
    std::uint32_t metricCount{0U};
    std::uint32_t diagnosticsCount{0U};
};

class InMemoryRunArtifactRepository final : public IResultRepository {
public:
    [[nodiscard]] StageResult persistArtifacts(RunContext& context) const override;

    void upsertRunResult(const RunSpec& spec, const RunResult& result) const;

    [[nodiscard]] std::optional<PersistedRunArtifact> findByTaskId(RunTaskId taskId) const;

    void copyAll(std::vector<PersistedRunArtifact>& output) const;

private:
    mutable std::mutex mutex_;
    mutable std::vector<PersistedRunArtifact> storage_;
};

} // namespace application::backtest