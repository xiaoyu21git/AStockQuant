#include "factor_compute/IAnalysisModule.h"

#include "AnalysisKernel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace factor::compute {

namespace {

static constexpr double kCorrelationLowerBound = -1.0;
static constexpr double kCorrelationUpperBound = 1.0;
static constexpr double kMetricBoundEpsilon = 1e-9;
static constexpr double kAnnualizationPeriodsPerYear = 252.0;
static constexpr double kSharpeStdDevEpsilon = 1e-12;
static constexpr double kTurnoverCostPerUnit = 0.001;

struct RunningMoments final {
    uint32_t count{0U};
    double mean{0.0};
    double m2{0.0};

    [[nodiscard]] bool addSample(double value) noexcept
    {
        if (!std::isfinite(value)) {
            return false;
        }
        if (count == std::numeric_limits<uint32_t>::max()) {
            return false;
        }

        ++count;
        const double countAsDouble = static_cast<double>(count);
        const double delta = value - mean;
        mean += delta / countAsDouble;
        const double delta2 = value - mean;
        m2 += delta * delta2;

        return std::isfinite(mean) && std::isfinite(m2);
    }

    [[nodiscard]] std::optional<double> variance() const noexcept
    {
        if (count == 0U) {
            return std::nullopt;
        }

        const double varianceValue = m2 / static_cast<double>(count);
        if (!std::isfinite(varianceValue) || varianceValue < -kMetricBoundEpsilon) {
            return std::nullopt;
        }

        return varianceValue < 0.0 ? 0.0 : varianceValue;
    }
};

struct CrossSectionSeries final {
    std::vector<double> signalSeries;
    std::vector<double> returnSeries;
};

struct AnalysisBuildContext final {
    uint32_t totalSignalCount{0U};
    uint32_t instrumentCount{0U};
    RunningMoments moments;
    double crossSectionVariance{0.0};
    detail::AnalysisKernel analysisKernel;
    detail::IcSeriesSummary icSummary;
    detail::TurnoverSummary turnoverSummary;
    detail::LongShortSeriesSummary longShortSummary;
    std::vector<detail::SignalReturnPair> pairs;
};

class AnalysisHelper final {
public:
    [[nodiscard]] std::optional<int32_t> toInt32SampleCount(size_t sampleCount) const noexcept
    {
        if (sampleCount > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            return std::nullopt;
        }

        return static_cast<int32_t>(sampleCount);
    }

    [[nodiscard]] std::optional<uint32_t> toUint32Count(size_t count) const noexcept
    {
        if (count > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            return std::nullopt;
        }

        return static_cast<uint32_t>(count);
    }

    [[nodiscard]] std::optional<double> buildRatio(size_t numerator, size_t denominator) const noexcept
    {
        if (denominator == 0U) {
            return std::nullopt;
        }

        const double ratio = static_cast<double>(numerator) / static_cast<double>(denominator);
        if (!std::isfinite(ratio)) {
            return std::nullopt;
        }

        return ratio;
    }

    [[nodiscard]] bool isFiniteIcSummary(const detail::IcSeriesSummary& summary) const noexcept
    {
        if (!std::isfinite(summary.mean)
            || !std::isfinite(summary.stdDev)
            || summary.stdDev < 0.0
            || summary.mean < (kCorrelationLowerBound - kMetricBoundEpsilon)
            || summary.mean > (kCorrelationUpperBound + kMetricBoundEpsilon)
            || summary.values.empty()) {
            return false;
        }

        for (const double value : summary.values) {
            if (!std::isfinite(value)
                || value < (kCorrelationLowerBound - kMetricBoundEpsilon)
                || value > (kCorrelationUpperBound + kMetricBoundEpsilon)) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] bool collectPresentSignalMoments(const SignalSet& signalSet, RunningMoments& moments) const noexcept
    {
        for (size_t index = 0; index < signalSet.mask.size(); ++index) {
            if (signalSet.mask[index] != detail::AnalysisKernel::kPresentMaskValue) {
                continue;
            }

            if (!moments.addSample(signalSet.values[index])) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] bool buildCrossSectionSeries(
        const std::vector<detail::SignalReturnPair>& pairs,
        CrossSectionSeries& series) const noexcept
    {
        series.signalSeries.clear();
        series.returnSeries.clear();
        series.signalSeries.reserve(pairs.size());
        series.returnSeries.reserve(pairs.size());

        for (const detail::SignalReturnPair& pair : pairs) {
            if (!std::isfinite(pair.signalValue) || !std::isfinite(pair.forwardReturn)) {
                return false;
            }

            series.signalSeries.emplace_back(pair.signalValue);
            series.returnSeries.emplace_back(pair.forwardReturn);
        }

        return true;
    }

    [[nodiscard]] bool tryAssignScalarMetric(
        AnalysisScalarMetric& metric,
        const std::optional<double>& value) const noexcept
    {
        if (!value.has_value()) {
            return true;
        }

        if (!std::isfinite(value.value())) {
            return false;
        }

        metric.available = true;
        metric.value = value.value();
        return true;
    }

    [[nodiscard]] bool tryAssignBoundedScalarMetric(
        AnalysisScalarMetric& metric,
        const std::optional<double>& value,
        double lowerBound,
        double upperBound) const noexcept
    {
        if (!value.has_value()) {
            return true;
        }

        if (!std::isfinite(value.value())
            || value.value() < (lowerBound - kMetricBoundEpsilon)
            || value.value() > (upperBound + kMetricBoundEpsilon)) {
            return false;
        }

        metric.available = true;
        metric.value = value.value();
        return true;
    }

    [[nodiscard]] bool tryAssignSignificanceMetric(
        AnalysisSignificanceMetric& metric,
        const std::optional<AnalysisSignificanceMetric>& value) const noexcept
    {
        if (!value.has_value()) {
            return true;
        }

        if (!value->isValid()) {
            return false;
        }

        metric = value.value();
        return true;
    }

    [[nodiscard]] bool tryAssignAbsoluteValueMetric(
        AnalysisScalarMetric& metric,
        const AnalysisScalarMetric& source) const noexcept
    {
        if (!source.available) {
            return true;
        }

        const double absoluteValue = std::abs(source.value);
        if (!std::isfinite(absoluteValue)) {
            return false;
        }

        metric.available = true;
        metric.value = absoluteValue;
        return true;
    }
};

class AnalysisContextBuilder final {
public:
    explicit AnalysisContextBuilder(const AnalysisHelper& helper) noexcept
        : helper_(helper)
    {
    }

    [[nodiscard]] bool initialize(
        const SignalSet& signalSet,
        NumericConstMatrixView closeView,
        AnalysisBuildContext& context,
        FactorError& error) const noexcept
    {
        const std::optional<uint32_t> totalCount = helper_.toUint32Count(signalSet.mask.size());
        if (!totalCount.has_value()) {
            error = FactorError::InternalError;
            return false;
        }
        if (totalCount.value() == 0U) {
            error = FactorError::InsufficientData;
            return false;
        }

        context.totalSignalCount = totalCount.value();
        const std::optional<uint32_t> instrumentCount = helper_.toUint32Count(signalSet.instruments.size());
        if (!instrumentCount.has_value() || instrumentCount.value() == 0U) {
            error = FactorError::InternalError;
            return false;
        }
        context.instrumentCount = instrumentCount.value();
        if (!helper_.collectPresentSignalMoments(signalSet, context.moments)) {
            error = FactorError::InternalError;
            return false;
        }
        if (context.moments.count == 0U) {
            error = FactorError::InsufficientData;
            return false;
        }

        const std::optional<double> variance = context.moments.variance();
        if (!variance.has_value() || !std::isfinite(context.moments.mean)) {
            error = FactorError::InternalError;
            return false;
        }

        context.crossSectionVariance = variance.value();
        context.icSummary = context.analysisKernel.buildIcSeriesSummary(signalSet, closeView);
        context.turnoverSummary = context.analysisKernel.calculateTurnoverSummary(signalSet);
        context.longShortSummary = context.analysisKernel.buildLongShortSeriesSummary(signalSet, closeView);
        context.pairs = context.analysisKernel.buildCrossSectionPairs(signalSet, closeView);
        error = FactorError::InternalError;
        return true;
    }

private:
    const AnalysisHelper& helper_;
};

class AnalysisReportAssembler final {
public:
    explicit AnalysisReportAssembler(const AnalysisHelper& helper) noexcept
        : helper_(helper)
    {
    }

    [[nodiscard]] bool initializeBase(
        const AnalysisBuildContext& context,
        AnalysisReport& report) const noexcept
    {
        report.totalSignalCount = context.totalSignalCount;
        report.presentSignalCount = context.moments.count;

        const std::optional<double> coverageRatio = helper_.buildRatio(
            static_cast<size_t>(context.moments.count),
            static_cast<size_t>(context.totalSignalCount));
        if (!coverageRatio.has_value()) {
            return false;
        }

        report.coverageRatio = coverageRatio.value();
        report.crossSectionMean = context.moments.mean;
        report.crossSectionVariance = context.crossSectionVariance;
        return true;
    }

    [[nodiscard]] bool finalize(
        AnalysisReport& report,
        const AnalysisBuildContext& context,
        const SignalSet&) const noexcept
    {
        if (!assignIcSummary(report, context.analysisKernel, context.icSummary)) {
            return false;
        }
        if (!assignCrossSection(report, context.analysisKernel, context.pairs)) {
            return false;
        }

        report.turnoverWindowDiagnostics.attemptedWindowCount = context.turnoverSummary.attemptedWindowCount;
        report.turnoverWindowDiagnostics.validWindowCount = context.turnoverSummary.validWindowCount;
        report.turnoverWindowDiagnostics.skippedIncompatibleBucketCount =
            context.turnoverSummary.skippedIncompatibleBucketCount;
        report.turnoverWindowDiagnostics.skippedOutOfRangeCount =
            context.turnoverSummary.skippedOutOfRangeCount;
        report.turnoverQuality = buildQualityFromWindowDiagnostics(
            report.turnoverWindowDiagnostics.attemptedWindowCount,
            report.turnoverWindowDiagnostics.validWindowCount);
        if (!helper_.tryAssignBoundedScalarMetric(report.turnoverRatio, context.turnoverSummary.value, 0.0, 1.0)) {
            return false;
        }
        if (!helper_.tryAssignAbsoluteValueMetric(report.discrimination, report.layeredReturnSpread)) {
            return false;
        }
        if (!assignExtendedMetrics(report, context.analysisKernel, context)) {
            return false;
        }
        finalizeMetricAvailabilityReasons(report);

        return report.isValid();
    }

private:
    [[nodiscard]] bool assignIcSummary(
        AnalysisReport& report,
        const detail::AnalysisKernel& analysisKernel,
        const detail::IcSeriesSummary& icSummary) const noexcept
    {
        report.icWindowDiagnostics.attemptedWindowCount = icSummary.attemptedWindowCount;
        report.icWindowDiagnostics.validWindowCount = icSummary.validWindowCount;
        report.icWindowDiagnostics.skippedInsufficientSampleCount =
            icSummary.skippedInsufficientSampleCount;
        report.icWindowDiagnostics.skippedInvalidValueCount =
            icSummary.skippedInvalidValueCount;
        report.informationCoefficientQuality = buildQualityFromWindowDiagnostics(
            report.icWindowDiagnostics.attemptedWindowCount,
            report.icWindowDiagnostics.validWindowCount);

        if (!icSummary.valid) {
            return true;
        }

        if (!helper_.isFiniteIcSummary(icSummary)) {
            return false;
        }

        const std::optional<uint32_t> icSampleCount = helper_.toUint32Count(icSummary.values.size());
        if (!icSampleCount.has_value() || icSampleCount.value() == 0U) {
            return false;
        }
        report.informationCoefficientSampleCount = icSampleCount.value();

        if (!helper_.tryAssignBoundedScalarMetric(
            report.informationCoefficient,
            std::optional<double>(icSummary.mean),
            kCorrelationLowerBound,
            kCorrelationUpperBound)) {
            return false;
        }
        if (!helper_.tryAssignScalarMetric(
                report.informationCoefficientStdDev,
                std::optional<double>(icSummary.stdDev))) {
            return false;
        }

        size_t positiveCount = 0U;
        for (const double icValue : icSummary.values) {
            if (icValue > 0.0) {
                ++positiveCount;
            }
        }

        const std::optional<double> positiveRate = helper_.buildRatio(positiveCount, icSummary.values.size());
        if (!helper_.tryAssignBoundedScalarMetric(
                report.informationCoefficientPositiveRate,
                positiveRate,
                0.0,
                1.0)) {
            return false;
        }

        if (icSummary.values.size() >= static_cast<size_t>(detail::AnalysisKernel::kMinimumInformationRatioSampleCount)
            && icSummary.stdDev > detail::AnalysisKernel::kVarianceEpsilon) {
            if (!helper_.tryAssignScalarMetric(
                    report.informationRatio,
                    std::optional<double>(icSummary.mean / icSummary.stdDev))) {
                return false;
            }
        }

        const std::optional<int32_t> significanceSampleCount = helper_.toInt32SampleCount(icSummary.values.size());
        if (!significanceSampleCount.has_value()) {
            return false;
        }

        const std::optional<AnalysisSignificanceMetric> significance = analysisKernel.calculateSignificance(
            icSummary.mean,
            significanceSampleCount.value());
        return helper_.tryAssignSignificanceMetric(report.informationCoefficientSignificance, significance);
    }

    [[nodiscard]] bool assignCrossSection(
        AnalysisReport& report,
        const detail::AnalysisKernel& analysisKernel,
        const std::vector<detail::SignalReturnPair>& pairs) const noexcept
    {
        if (pairs.empty()) {
            return true;
        }

        CrossSectionSeries series;
        if (!helper_.buildCrossSectionSeries(pairs, series)) {
            return false;
        }

        const std::optional<double> spread = analysisKernel.calculateLayeredSpread(pairs);
        if (!helper_.tryAssignScalarMetric(report.layeredReturnSpread, spread)) {
            return false;
        }

        const std::optional<double> monotonicity =
            analysisKernel.calculateSpearmanCorrelation(series.signalSeries, series.returnSeries);
        return helper_.tryAssignBoundedScalarMetric(
            report.monotonicity,
            monotonicity,
            kCorrelationLowerBound,
            kCorrelationUpperBound);
    }

    [[nodiscard]] bool assignExtendedMetrics(
        AnalysisReport& report,
        const detail::AnalysisKernel& analysisKernel,
        const AnalysisBuildContext& context) const noexcept
    {
        const std::optional<double> annualReturn = buildAnnualizedReturn(context.longShortSummary);
        if (!helper_.tryAssignScalarMetric(report.longShortAnnualReturn, annualReturn)) {
            return false;
        }
        if (!helper_.tryAssignScalarMetric(report.alpha, annualReturn)) {
            return false;
        }

        const std::optional<double> longShortSharpe = buildAnnualizedSharpe(context.longShortSummary);
        if (!helper_.tryAssignScalarMetric(report.longShortSharpe, longShortSharpe)) {
            return false;
        }

        const std::optional<double> costAdjustedSharpe = buildCostAdjustedSharpe(
            context.longShortSummary,
            context.turnoverSummary);
        if (!helper_.tryAssignScalarMetric(report.costAdjustedSharpe, costAdjustedSharpe)) {
            return false;
        }

        const std::optional<double> monthlyWinRate = buildMonthlyWinRate(context.longShortSummary);
        if (!helper_.tryAssignBoundedScalarMetric(report.monthlyWinRate, monthlyWinRate, 0.0, 1.0)) {
            return false;
        }

        const std::optional<int32_t> icHalfLife = buildIcHalfLife(analysisKernel, context.icSummary);
        if (icHalfLife.has_value()) {
            report.icHalfLife.available = true;
            report.icHalfLife.value = icHalfLife.value();
        }

        const std::optional<int32_t> numGroups = buildNumGroups(context.instrumentCount);
        if (numGroups.has_value()) {
            report.numGroups.available = true;
            report.numGroups.value = numGroups.value();
        }

        return true;
    }

    [[nodiscard]] static std::optional<double> buildAnnualizedReturn(
        const detail::LongShortSeriesSummary& summary) noexcept
    {
        if (!summary.meanSpread.has_value() || !std::isfinite(summary.meanSpread.value())) {
            return std::nullopt;
        }
        const double annualizedReturn = summary.meanSpread.value() * kAnnualizationPeriodsPerYear;
        if (!std::isfinite(annualizedReturn)) {
            return std::nullopt;
        }
        return annualizedReturn;
    }

    [[nodiscard]] static std::optional<double> buildAnnualizedSharpe(
        const detail::LongShortSeriesSummary& summary) noexcept
    {
        if (!summary.meanSpread.has_value() || !summary.stdDevSpread.has_value()) {
            return std::nullopt;
        }
        if (!std::isfinite(summary.meanSpread.value())
            || !std::isfinite(summary.stdDevSpread.value())
            || summary.stdDevSpread.value() <= kSharpeStdDevEpsilon) {
            return std::nullopt;
        }

        const double annualizedSharpe =
            (summary.meanSpread.value() / summary.stdDevSpread.value()) * std::sqrt(kAnnualizationPeriodsPerYear);
        if (!std::isfinite(annualizedSharpe)) {
            return std::nullopt;
        }
        return annualizedSharpe;
    }

    [[nodiscard]] static std::optional<double> buildCostAdjustedSharpe(
        const detail::LongShortSeriesSummary& longShortSummary,
        const detail::TurnoverSummary& turnoverSummary) noexcept
    {
        if (!longShortSummary.meanSpread.has_value()
            || !longShortSummary.stdDevSpread.has_value()
            || !turnoverSummary.value.has_value()) {
            return std::nullopt;
        }
        if (!std::isfinite(longShortSummary.meanSpread.value())
            || !std::isfinite(longShortSummary.stdDevSpread.value())
            || !std::isfinite(turnoverSummary.value.value())
            || longShortSummary.stdDevSpread.value() <= kSharpeStdDevEpsilon
            || turnoverSummary.value.value() < -kMetricBoundEpsilon
            || turnoverSummary.value.value() > (1.0 + kMetricBoundEpsilon)) {
            return std::nullopt;
        }

        const double netMeanSpread =
            longShortSummary.meanSpread.value() - turnoverSummary.value.value() * kTurnoverCostPerUnit;
        const double annualizedCostAdjustedSharpe =
            (netMeanSpread / longShortSummary.stdDevSpread.value()) * std::sqrt(kAnnualizationPeriodsPerYear);
        if (!std::isfinite(annualizedCostAdjustedSharpe)) {
            return std::nullopt;
        }
        return annualizedCostAdjustedSharpe;
    }

    [[nodiscard]] static std::optional<double> buildMonthlyWinRate(
        const detail::LongShortSeriesSummary& summary) noexcept
    {
        if (summary.spreadSeries.empty()) {
            return std::nullopt;
        }

        size_t positiveCount = 0U;
        for (const double spread : summary.spreadSeries) {
            if (!std::isfinite(spread)) {
                return std::nullopt;
            }
            if (spread > 0.0) {
                ++positiveCount;
            }
        }

        const double sampleCount = static_cast<double>(summary.spreadSeries.size());
        const double winRate = static_cast<double>(positiveCount) / sampleCount;
        if (!std::isfinite(winRate)) {
            return std::nullopt;
        }
        return winRate;
    }

    [[nodiscard]] static std::optional<int32_t> buildIcHalfLife(
        const detail::AnalysisKernel& analysisKernel,
        const detail::IcSeriesSummary& icSummary) noexcept
    {
        if (icSummary.values.size() < 3U) {
            return std::nullopt;
        }

        std::vector<double> laggedCurrent;
        std::vector<double> laggedPrevious;
        laggedCurrent.reserve(icSummary.values.size() - 1U);
        laggedPrevious.reserve(icSummary.values.size() - 1U);
        for (size_t index = 1U; index < icSummary.values.size(); ++index) {
            laggedCurrent.emplace_back(icSummary.values[index]);
            laggedPrevious.emplace_back(icSummary.values[index - 1U]);
        }

        const std::optional<double> lag1Autocorrelation =
            analysisKernel.calculatePearsonCorrelation(laggedCurrent, laggedPrevious);
        if (!lag1Autocorrelation.has_value()
            || !std::isfinite(lag1Autocorrelation.value())
            || lag1Autocorrelation.value() <= kMetricBoundEpsilon
            || lag1Autocorrelation.value() >= (1.0 - kMetricBoundEpsilon)) {
            return std::nullopt;
        }

        const double denominator = -std::log(lag1Autocorrelation.value());
        if (!std::isfinite(denominator) || denominator <= kMetricBoundEpsilon) {
            return std::nullopt;
        }

        const double halfLife = std::log(2.0) / denominator;
        if (!std::isfinite(halfLife) || halfLife <= 0.0) {
            return std::nullopt;
        }

        const double rounded = std::round(halfLife);
        if (!std::isfinite(rounded)
            || rounded < 1.0
            || rounded > static_cast<double>(std::numeric_limits<int32_t>::max())) {
            return std::nullopt;
        }

        return static_cast<int32_t>(rounded);
    }

    [[nodiscard]] static std::optional<int32_t> buildNumGroups(uint32_t instrumentCount) noexcept
    {
        if (instrumentCount == 0U) {
            return std::nullopt;
        }

        const uint32_t cappedGroups = std::min(
            static_cast<uint32_t>(detail::AnalysisKernel::kLayerBucketDivisor),
            instrumentCount);
        if (cappedGroups == 0U
            || cappedGroups > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
            return std::nullopt;
        }

        return static_cast<int32_t>(cappedGroups);
    }

    static void finalizeMetricAvailabilityReasons(AnalysisReport& report) noexcept
    {
        report.rankIcMeanReason = resolveIcWindowReason(report.informationCoefficient, report.icWindowDiagnostics);
        report.rankIcStdReason = resolveIcWindowReason(report.informationCoefficientStdDev, report.icWindowDiagnostics);
        report.icWinRateReason =
            resolveIcWindowReason(report.informationCoefficientPositiveRate, report.icWindowDiagnostics);

        if (report.informationRatio.available) {
            report.rankIcirReason = AnalysisMetricAvailabilityReason::Available;
        } else if (!report.informationCoefficient.available || !report.informationCoefficientStdDev.available) {
            report.rankIcirReason = AnalysisMetricAvailabilityReason::UnavailableMissingDependency;
        } else if (report.informationCoefficientSampleCount
            < static_cast<uint32_t>(detail::AnalysisKernel::kMinimumInformationRatioSampleCount)) {
            report.rankIcirReason = AnalysisMetricAvailabilityReason::UnavailableInsufficientSample;
        } else if (report.informationCoefficientStdDev.value <= detail::AnalysisKernel::kVarianceEpsilon) {
            report.rankIcirReason = AnalysisMetricAvailabilityReason::UnavailableZeroVariance;
        } else {
            report.rankIcirReason = AnalysisMetricAvailabilityReason::UnavailableInvalidNumeric;
        }

        if (report.informationCoefficientSignificance.available) {
            report.icPValueReason = AnalysisMetricAvailabilityReason::Available;
            report.icTStatReason = AnalysisMetricAvailabilityReason::Available;
        } else if (!report.informationCoefficient.available) {
            report.icPValueReason = AnalysisMetricAvailabilityReason::UnavailableMissingDependency;
            report.icTStatReason = AnalysisMetricAvailabilityReason::UnavailableMissingDependency;
        } else if (report.informationCoefficientSampleCount
            <= static_cast<uint32_t>(detail::AnalysisKernel::kMinimumCorrelationSampleCount)) {
            report.icPValueReason = AnalysisMetricAvailabilityReason::UnavailableInsufficientSample;
            report.icTStatReason = AnalysisMetricAvailabilityReason::UnavailableInsufficientSample;
        } else {
            report.icPValueReason = AnalysisMetricAvailabilityReason::UnavailableInvalidNumeric;
            report.icTStatReason = AnalysisMetricAvailabilityReason::UnavailableInvalidNumeric;
        }

        report.monotonicityReason =
            resolveScalarReason(report.monotonicity, AnalysisMetricAvailabilityReason::UnavailableInsufficientSample);
        report.longShortAnnualReturnReason = resolveScalarReason(
            report.longShortAnnualReturn,
            AnalysisMetricAvailabilityReason::UnavailableInsufficientSample);
        report.annualTurnoverReason =
            resolveTurnoverReason(report.turnoverRatio, report.turnoverWindowDiagnostics);

        if (report.longShortSharpe.available) {
            report.longShortSharpeReason = AnalysisMetricAvailabilityReason::Available;
        } else if (!report.longShortAnnualReturn.available) {
            report.longShortSharpeReason = AnalysisMetricAvailabilityReason::UnavailableMissingDependency;
        } else {
            report.longShortSharpeReason = AnalysisMetricAvailabilityReason::UnavailableZeroVariance;
        }

        report.alphaReason = report.alpha.available
            ? AnalysisMetricAvailabilityReason::Available
            : AnalysisMetricAvailabilityReason::UnavailableMissingDependency;

        if (report.costAdjustedSharpe.available) {
            report.costAdjustedSharpeReason = AnalysisMetricAvailabilityReason::Available;
        } else if (!report.longShortSharpe.available || !report.turnoverRatio.available) {
            report.costAdjustedSharpeReason = AnalysisMetricAvailabilityReason::UnavailableMissingDependency;
        } else {
            report.costAdjustedSharpeReason = AnalysisMetricAvailabilityReason::UnavailableInvalidNumeric;
        }

        report.monthlyWinRateReason = report.monthlyWinRate.available
            ? AnalysisMetricAvailabilityReason::Available
            : AnalysisMetricAvailabilityReason::UnavailableInsufficientSample;

        if (report.icHalfLife.available) {
            report.icHalfLifeReason = AnalysisMetricAvailabilityReason::Available;
        } else if (!report.informationCoefficient.available) {
            report.icHalfLifeReason = AnalysisMetricAvailabilityReason::UnavailableMissingDependency;
        } else if (report.informationCoefficientSampleCount < 3U) {
            report.icHalfLifeReason = AnalysisMetricAvailabilityReason::UnavailableInsufficientSample;
        } else {
            report.icHalfLifeReason = AnalysisMetricAvailabilityReason::UnavailableInvalidNumeric;
        }

        report.numGroupsReason = report.numGroups.available
            ? AnalysisMetricAvailabilityReason::Available
            : AnalysisMetricAvailabilityReason::UnavailableInsufficientSample;
    }

    [[nodiscard]] static AnalysisMetricAvailabilityReason resolveScalarReason(
        const AnalysisScalarMetric& metric,
        AnalysisMetricAvailabilityReason unavailableReason) noexcept
    {
        return metric.available ? AnalysisMetricAvailabilityReason::Available : unavailableReason;
    }

    [[nodiscard]] static AnalysisMetricAvailabilityReason resolveIcWindowReason(
        const AnalysisScalarMetric& metric,
        const IcWindowDiagnostics& diagnostics) noexcept
    {
        if (metric.available) {
            return AnalysisMetricAvailabilityReason::Available;
        }
        if (diagnostics.attemptedWindowCount == 0U) {
            return AnalysisMetricAvailabilityReason::UnavailableNoAttempt;
        }
        if (diagnostics.validWindowCount == 0U) {
            return AnalysisMetricAvailabilityReason::UnavailableInsufficientSample;
        }
        return AnalysisMetricAvailabilityReason::UnavailableInvalidNumeric;
    }

    [[nodiscard]] static AnalysisMetricAvailabilityReason resolveTurnoverReason(
        const AnalysisScalarMetric& metric,
        const TurnoverWindowDiagnostics& diagnostics) noexcept
    {
        if (metric.available) {
            return AnalysisMetricAvailabilityReason::Available;
        }
        if (diagnostics.attemptedWindowCount == 0U) {
            return AnalysisMetricAvailabilityReason::UnavailableNoAttempt;
        }
        if (diagnostics.validWindowCount == 0U) {
            return AnalysisMetricAvailabilityReason::UnavailableInsufficientSample;
        }
        return AnalysisMetricAvailabilityReason::UnavailableInvalidNumeric;
    }

private:
    [[nodiscard]] static AnalysisMetricQuality buildQualityFromWindowDiagnostics(
        uint32_t attemptedWindowCount,
        uint32_t validWindowCount) noexcept
    {
        if (attemptedWindowCount == 0U) {
            return AnalysisMetricQuality::Unavailable;
        }
        if (validWindowCount == 0U) {
            return AnalysisMetricQuality::Unqualified;
        }
        if (validWindowCount == attemptedWindowCount) {
            return AnalysisMetricQuality::Qualified;
        }
        return AnalysisMetricQuality::Degraded;
    }

    const AnalysisHelper& helper_;
};

class AnalysisWorkflow final {
public:
    [[nodiscard]] FactorResult<AnalysisReport>
    run(const SignalSet& signalSet, NumericConstMatrixView closeView) const
    {
        if (!signalSet.isValid()) {
            return buildFailure(FactorError::InvalidUniverse);
        }

        AnalysisBuildContext context;
        FactorError contextError = FactorError::InternalError;
        if (!contextBuilder_.initialize(signalSet, closeView, context, contextError)) {
            return buildFailure(contextError);
        }

        AnalysisReport report;
        if (!reportAssembler_.initializeBase(context, report)) {
            return buildFailure(FactorError::InternalError);
        }
        if (!reportAssembler_.finalize(report, context, signalSet)) {
            return buildFailure(FactorError::InternalError);
        }

        return buildSuccess(report);
    }

private:
    [[nodiscard]] static FactorResult<AnalysisReport> buildFailure(FactorError error)
    {
        return FactorResult<AnalysisReport>::failure(error);
    }

    [[nodiscard]] static FactorResult<AnalysisReport> buildSuccess(const AnalysisReport& report)
    {
        return FactorResult<AnalysisReport>::success(report);
    }

private:
    AnalysisHelper helper_;
    AnalysisContextBuilder contextBuilder_{helper_};
    AnalysisReportAssembler reportAssembler_{helper_};
};

} // namespace

FactorResult<AnalysisReport>
AnalysisModule::analyze(const SignalSet& signalSet, const GenerateSpec&, NumericConstMatrixView closeView) const
{
    const AnalysisWorkflow workflow;
    return workflow.run(signalSet, closeView);
}

} // namespace factor::compute
