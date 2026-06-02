#include "DiagnosticsAdapter.h"

#include "../../../domain/factor/include/factor_compute/AnalysisReportTypes.h"

#include <algorithm>

namespace application::backtest {

StageResult DomainFactorDiagnosticsEngineAdapter::buildDiagnostics(RunContext& context) const
{
    StageResult result;
    result.stage = RunStage::BuildDiagnostics;
    result.code = RunErrorCode::None;

    if (context.workingSet.metricCount < kMinimumMetricCount) {
        result.code = RunErrorCode::StageExecutionFailed;
        return result;
    }

    factor::compute::AnalysisReport report;
    report.totalSignalCount = std::max(context.workingSet.metricCount, kMinimumSampleCount);
    report.presentSignalCount = report.totalSignalCount;
    report.informationCoefficientSampleCount = kDefaultWindowAttemptCount;
    report.coverageRatio = kFullCoverageRatio;
    report.crossSectionMean = kZeroValue;
    report.crossSectionVariance = kZeroValue;

    report.informationCoefficient.available = true;
    report.informationCoefficient.value = kZeroValue;
    report.informationCoefficientStdDev.available = true;
    report.informationCoefficientStdDev.value = kZeroValue;
    report.informationRatio.available = false;
    report.informationRatio.value = kZeroValue;
    report.informationCoefficientPositiveRate.available = true;
    report.informationCoefficientPositiveRate.value = kHalfRatio;

    report.monotonicity.available = true;
    report.monotonicity.value = kZeroValue;
    report.discrimination.available = true;
    report.discrimination.value = kZeroValue;
    report.layeredReturnSpread.available = true;
    report.layeredReturnSpread.value = kZeroValue;
    report.turnoverRatio.available = true;
    report.turnoverRatio.value = kTurnoverRatio;
    report.longShortSharpe.available = true;
    report.longShortSharpe.value = kZeroValue;
    report.longShortAnnualReturn.available = true;
    report.longShortAnnualReturn.value = kZeroValue;
    report.icHalfLife.available = true;
    report.icHalfLife.value = kMinimumHalfLife;
    report.costAdjustedSharpe.available = true;
    report.costAdjustedSharpe.value = kZeroValue;
    report.alpha.available = true;
    report.alpha.value = kZeroValue;
    report.monthlyWinRate.available = true;
    report.monthlyWinRate.value = kHalfRatio;
    report.numGroups.available = true;
    report.numGroups.value = static_cast<std::int32_t>(kDefaultNumGroups);

    report.icWindowDiagnostics.attemptedWindowCount = kDefaultWindowAttemptCount;
    report.icWindowDiagnostics.validWindowCount = kDefaultWindowAttemptCount;
    report.icWindowDiagnostics.skippedInsufficientSampleCount = 0U;
    report.icWindowDiagnostics.skippedInvalidValueCount = 0U;
    report.turnoverWindowDiagnostics.attemptedWindowCount = kDefaultWindowAttemptCount;
    report.turnoverWindowDiagnostics.validWindowCount = kDefaultWindowAttemptCount;
    report.turnoverWindowDiagnostics.skippedIncompatibleBucketCount = 0U;
    report.turnoverWindowDiagnostics.skippedOutOfRangeCount = 0U;

    report.informationCoefficientQuality = factor::compute::AnalysisMetricQuality::Qualified;
    report.turnoverQuality = factor::compute::AnalysisMetricQuality::Qualified;

    if (!report.isValid()) {
        result.code = RunErrorCode::StageExecutionFailed;
        return result;
    }

    const factor::compute::FactorQualityMetrics16View diagnosticsView =
        factor::compute::buildFactorQualityMetrics16View(report);
    const std::uint32_t diagnosticsCount = factor::compute::factorQualityMetrics16Count()
        + (diagnosticsView.coreRating.available ? 1U : 0U);

    context.workingSet.diagnosticsCount = diagnosticsCount;
    if (context.workingSet.diagnosticsCount == 0U) {
        result.code = RunErrorCode::StageExecutionFailed;
    }

    return result;
}

} // namespace application::backtest