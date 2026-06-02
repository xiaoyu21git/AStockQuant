#include "ExportArtifactBuilderAdapter.h"

#include "../../../domain/factor/include/factor_compute/AnalysisReportTypes.h"

namespace application::backtest {
namespace {

struct ExportArtifactEnvelope final {
    RunTaskId taskId;
    std::uint32_t persistedArtifactCount{0U};
    std::uint32_t rebalancePointCount{0U};
    std::uint32_t targetPositionCount{0U};
    std::uint32_t approvedOrderCount{0U};
    std::uint32_t generatedOrderCount{0U};
    std::uint32_t filledOrderCount{0U};
    std::uint32_t metricCount{0U};
    std::uint32_t diagnosticsCount{0U};

    [[nodiscard]] bool isValid() const noexcept
    {
        return taskId.isValid()
            && persistedArtifactCount > 0U
            && rebalancePointCount > 0U
            && targetPositionCount > 0U
            && approvedOrderCount > 0U
            && generatedOrderCount > 0U
            && filledOrderCount > 0U
            && metricCount > 0U
            && diagnosticsCount > 0U
            && approvedOrderCount >= generatedOrderCount
            && generatedOrderCount >= filledOrderCount
            && diagnosticsCount >= factor::compute::factorQualityMetrics16Count();
    }
};

} // namespace

StageResult StrictExportArtifactBuilderAdapter::buildExportArtifacts(RunContext& context) const
{
    StageResult result;
    result.stage = RunStage::Finalize;
    result.code = RunErrorCode::None;

    ExportArtifactEnvelope envelope;
    envelope.taskId = context.spec.taskId;
    envelope.persistedArtifactCount = context.workingSet.persistedArtifactCount;
    envelope.rebalancePointCount = context.workingSet.rebalancePointCount;
    envelope.targetPositionCount = context.workingSet.targetPositionCount;
    envelope.approvedOrderCount = context.workingSet.approvedOrderCount;
    envelope.generatedOrderCount = context.workingSet.generatedOrderCount;
    envelope.filledOrderCount = context.workingSet.filledOrderCount;
    envelope.metricCount = context.workingSet.metricCount;
    envelope.diagnosticsCount = context.workingSet.diagnosticsCount;

    if (!envelope.isValid()
        || envelope.persistedArtifactCount < kMinimumPersistedArtifactCount
        || envelope.metricCount < kMinimumMetricCount
        || envelope.diagnosticsCount < kMinimumDiagnosticsCount
        || envelope.targetPositionCount < kMinimumTargetPositionCount
        || envelope.rebalancePointCount < kMinimumRebalancePointCount
        || envelope.filledOrderCount < kMinimumOrderFlowCount) {
        result.code = RunErrorCode::StageExecutionFailed;
        return result;
    }

    context.workingSet.persistedArtifactCount = envelope.persistedArtifactCount;
    return result;
}

} // namespace application::backtest