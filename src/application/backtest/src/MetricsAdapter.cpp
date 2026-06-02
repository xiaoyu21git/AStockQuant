#include "MetricsAdapter.h"

#include "../../../domain/backtest/include/FactorAnalyticsTypes.h"

#include <algorithm>

namespace application::backtest {
namespace {

[[nodiscard]] std::uint32_t resolveUniverseCount(const RunWorkingSet& workingSet) noexcept
{
    return (std::max)(workingSet.positionSnapshotCount, workingSet.targetPositionCount);
}

[[nodiscard]] std::uint32_t resolveGroupCount(std::uint32_t universeCount) noexcept
{
    if (universeCount == 0U) {
        return 0U;
    }
    if (universeCount == 1U) {
        return 1U;
    }
    return 2U;
}

[[nodiscard]] std::size_t distributeGroupSize(
    std::uint32_t universeCount,
    std::uint32_t groupCount,
    std::uint32_t groupIndex) noexcept
{
    if (groupCount == 0U) {
        return 0U;
    }

    const std::uint32_t baseSize = universeCount / groupCount;
    const std::uint32_t remainder = universeCount % groupCount;
    const std::uint32_t bonus = groupIndex < remainder ? 1U : 0U;
    return static_cast<std::size_t>(baseSize + bonus);
}

} // namespace

StageResult DomainFactorMetricsEngineAdapter::aggregateMetrics(RunContext& context) const
{
    StageResult result;
    result.stage = RunStage::AggregateMetrics;
    result.code = RunErrorCode::None;

    if (context.workingSet.positionSnapshotCount < kMinimumSnapshotCount) {
        result.code = RunErrorCode::StageExecutionFailed;
        return result;
    }

    const std::uint32_t universeCount = resolveUniverseCount(context.workingSet);
    const std::uint32_t groupCount = resolveGroupCount(universeCount);
    if (groupCount < kMinimumGroupCount) {
        result.code = RunErrorCode::StageExecutionFailed;
        return result;
    }

    domain::backtest::FactorBacktestResult analyticsResult;
    analyticsResult.groups.reserve(groupCount);

    for (std::uint32_t index = 0U; index < groupCount; ++index) {
        domain::backtest::FactorGroup group;
        group.groupId = static_cast<int>(index + 1U);
        group.minFactorValue = static_cast<double>(index) * kFactorBucketWidth;
        group.maxFactorValue = static_cast<double>(index + 1U) * kFactorBucketWidth;
        group.stockCount = distributeGroupSize(universeCount, groupCount, index);
        analyticsResult.groups.push_back(group);
    }

    analyticsResult.calculateSummaryStats();

    const std::uint32_t derivedMetricCount = static_cast<std::uint32_t>(analyticsResult.groups.size())
        + kSummaryMetricCount;
    context.workingSet.metricCount = derivedMetricCount;

    if (context.workingSet.metricCount == 0U) {
        result.code = RunErrorCode::StageExecutionFailed;
    }

    return result;
}

} // namespace application::backtest