#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace factor::compute {

struct AnalysisScalarMetric final {
    bool available{false};
    double value{0.0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return !available || std::isfinite(value);
    }
};

struct AnalysisSignificanceMetric final {
    static constexpr double kSignificancePValueThreshold = 0.05;

    bool available{false};
    double tStatistic{0.0};
    double pValue{0.0};
    bool significant{false};

    [[nodiscard]] bool isValid() const noexcept
    {
        return !available
            || (std::isfinite(tStatistic)
                && std::isfinite(pValue)
                && pValue >= 0.0
                && pValue <= 1.0
                && significant == (pValue < kSignificancePValueThreshold));
    }
};

struct IcWindowDiagnostics final {
    uint32_t attemptedWindowCount{0U};
    uint32_t validWindowCount{0U};
    uint32_t skippedInsufficientSampleCount{0U};
    uint32_t skippedInvalidValueCount{0U};

    [[nodiscard]] bool isValid() const noexcept
    {
        return validWindowCount <= attemptedWindowCount
            && skippedInsufficientSampleCount <= attemptedWindowCount
            && skippedInvalidValueCount <= attemptedWindowCount
            && validWindowCount + skippedInsufficientSampleCount + skippedInvalidValueCount == attemptedWindowCount;
    }
};

struct TurnoverWindowDiagnostics final {
    uint32_t attemptedWindowCount{0U};
    uint32_t validWindowCount{0U};
    uint32_t skippedIncompatibleBucketCount{0U};
    uint32_t skippedOutOfRangeCount{0U};

    [[nodiscard]] bool isValid() const noexcept
    {
        return validWindowCount <= attemptedWindowCount
            && skippedIncompatibleBucketCount <= attemptedWindowCount
            && skippedOutOfRangeCount <= attemptedWindowCount
            && validWindowCount + skippedIncompatibleBucketCount + skippedOutOfRangeCount == attemptedWindowCount;
    }
};

enum class AnalysisMetricQuality : uint8_t {
    Unavailable = 0U,
    Qualified = 1U,
    Degraded = 2U,
    Unqualified = 3U,
};

enum class AnalysisMetricAvailabilityReason : uint8_t {
    Available = 0U,
    UnavailableNoAttempt = 1U,
    UnavailableInsufficientSample = 2U,
    UnavailableMissingDependency = 3U,
    UnavailableZeroVariance = 4U,
    UnavailableInvalidNumeric = 5U,
};

struct AnalysisScalarMetricInt final {
    bool available{false};
    int32_t value{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return true;
    }
};

struct AnalysisReport final {
    uint32_t totalSignalCount{0U};
    uint32_t presentSignalCount{0U};
    uint32_t informationCoefficientSampleCount{0U};
    double coverageRatio{0.0};
    double crossSectionMean{0.0};
    double crossSectionVariance{0.0};
    AnalysisScalarMetric informationCoefficient{};
    AnalysisScalarMetric informationCoefficientStdDev{};
    AnalysisScalarMetric informationRatio{};
    AnalysisScalarMetric informationCoefficientPositiveRate{};
    AnalysisSignificanceMetric informationCoefficientSignificance{};
    AnalysisScalarMetric monotonicity{};
    AnalysisScalarMetric discrimination{};
    AnalysisScalarMetric layeredReturnSpread{};
    AnalysisScalarMetric turnoverRatio{};
    AnalysisScalarMetric longShortSharpe{};
    AnalysisScalarMetric longShortAnnualReturn{};
    AnalysisScalarMetricInt icHalfLife{};
    AnalysisScalarMetric costAdjustedSharpe{};
    AnalysisScalarMetric alpha{};
    AnalysisScalarMetric monthlyWinRate{};
    AnalysisScalarMetricInt numGroups{};
    AnalysisMetricAvailabilityReason rankIcMeanReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason rankIcStdReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason rankIcirReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason icWinRateReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason icPValueReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason monotonicityReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason longShortSharpeReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason longShortAnnualReturnReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason icHalfLifeReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason annualTurnoverReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason costAdjustedSharpeReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason alphaReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason icTStatReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason monthlyWinRateReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason numGroupsReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    IcWindowDiagnostics icWindowDiagnostics{};
    TurnoverWindowDiagnostics turnoverWindowDiagnostics{};
    AnalysisMetricQuality informationCoefficientQuality{AnalysisMetricQuality::Unavailable};
    AnalysisMetricQuality turnoverQuality{AnalysisMetricQuality::Unavailable};

    [[nodiscard]] bool isValid() const noexcept
    {
        return totalSignalCount > 0U
            && presentSignalCount > 0U
            && presentSignalCount <= totalSignalCount
            && std::isfinite(coverageRatio)
            && coverageRatio >= 0.0
            && coverageRatio <= 1.0
            && isCoverageRatioConsistent()
            && std::isfinite(crossSectionMean)
            && std::isfinite(crossSectionVariance)
            && crossSectionVariance >= 0.0
            && isCrossSectionVarianceConsistent()
            && informationCoefficient.isValid()
            && informationCoefficientStdDev.isValid()
            && informationRatio.isValid()
            && informationCoefficientPositiveRate.isValid()
            && hasRequiredDependencies()
            && isWithinCorrelationRange(informationCoefficient)
            && isNonNegative(informationCoefficientStdDev)
            && isWithinUnitInterval(informationCoefficientPositiveRate)
            && informationCoefficientSignificance.isValid()
            && isIcSignificanceConsistent()
            && monotonicity.isValid()
            && isWithinCorrelationRange(monotonicity)
            && discrimination.isValid()
            && isNonNegative(discrimination)
            && layeredReturnSpread.isValid()
            && isDiscriminationConsistent()
            && turnoverRatio.isValid()
            && isWithinUnitInterval(turnoverRatio)
            && longShortSharpe.isValid()
            && longShortAnnualReturn.isValid()
            && icHalfLife.isValid()
            && isPositiveInt(icHalfLife)
            && costAdjustedSharpe.isValid()
            && alpha.isValid()
            && monthlyWinRate.isValid()
            && isWithinUnitInterval(monthlyWinRate)
            && numGroups.isValid()
            && isPositiveInt(numGroups)
            && isNumGroupsConsistent()
            && icWindowDiagnostics.isValid()
            && turnoverWindowDiagnostics.isValid()
            && isIcDiagnosticsConsistent()
            && isTurnoverDiagnosticsConsistent();
    }

private:
    static constexpr double kMetricBoundEpsilon = 1e-9;

    [[nodiscard]] static bool isWithinClosedRange(
        const AnalysisScalarMetric& metric,
        double lowerBound,
        double upperBound) noexcept
    {
        return !metric.available
            || (metric.value >= (lowerBound - kMetricBoundEpsilon)
                && metric.value <= (upperBound + kMetricBoundEpsilon));
    }

    [[nodiscard]] static bool isWithinUnitInterval(const AnalysisScalarMetric& metric) noexcept
    {
        return isWithinClosedRange(metric, 0.0, 1.0);
    }

    [[nodiscard]] static bool isWithinCorrelationRange(const AnalysisScalarMetric& metric) noexcept
    {
        return isWithinClosedRange(metric, -1.0, 1.0);
    }

    [[nodiscard]] static bool isNonNegative(const AnalysisScalarMetric& metric) noexcept
    {
        return !metric.available || metric.value >= -kMetricBoundEpsilon;
    }

    [[nodiscard]] static bool isPositiveInt(const AnalysisScalarMetricInt& metric) noexcept
    {
        return !metric.available || metric.value > 0;
    }

    [[nodiscard]] bool hasRequiredDependencies() const noexcept
    {
        return (!informationCoefficient.available || (informationCoefficientStdDev.available && informationCoefficientSampleCount > 0U))
            && (!informationCoefficient.available || informationCoefficientPositiveRate.available)
            && (!informationRatio.available
                || (informationCoefficient.available
                    && informationCoefficientStdDev.available
                    && informationCoefficientSampleCount >= 2U))
            && (!informationCoefficientPositiveRate.available || informationCoefficient.available)
            && (!informationCoefficientSignificance.available || informationCoefficient.available)
            && (!monotonicity.available || layeredReturnSpread.available)
            && (!layeredReturnSpread.available || discrimination.available)
            && (!discrimination.available || layeredReturnSpread.available)
            && (!longShortSharpe.available || longShortAnnualReturn.available)
            && (!costAdjustedSharpe.available || (longShortSharpe.available && turnoverRatio.available))
            && (!alpha.available || longShortAnnualReturn.available)
            && (!monthlyWinRate.available || longShortAnnualReturn.available)
            && (!icHalfLife.available || informationCoefficient.available);
    }

    [[nodiscard]] bool isCoverageRatioConsistent() const noexcept
    {
        const double totalCount = static_cast<double>(totalSignalCount);
        const double presentCount = static_cast<double>(presentSignalCount);
        const double expectedCoverageRatio = presentCount / totalCount;
        return std::abs(coverageRatio - expectedCoverageRatio) <= kMetricBoundEpsilon;
    }

    [[nodiscard]] bool isDiscriminationConsistent() const noexcept
    {
        return !discrimination.available
            || !layeredReturnSpread.available
            || std::abs(discrimination.value - std::abs(layeredReturnSpread.value)) <= kMetricBoundEpsilon;
    }

    [[nodiscard]] bool isIcSignificanceConsistent() const noexcept
    {
        if (!informationCoefficient.available || !informationCoefficientSignificance.available) {
            return true;
        }

        if (std::abs(informationCoefficient.value) <= kMetricBoundEpsilon
            || std::abs(informationCoefficientSignificance.tStatistic) <= kMetricBoundEpsilon) {
            return true;
        }

        return informationCoefficient.value * informationCoefficientSignificance.tStatistic > 0.0;
    }

    [[nodiscard]] bool isCrossSectionVarianceConsistent() const noexcept
    {
        return presentSignalCount > 1U || crossSectionVariance <= kMetricBoundEpsilon;
    }

    [[nodiscard]] bool isIcDiagnosticsConsistent() const noexcept
    {
        return (!informationCoefficient.available || icWindowDiagnostics.validWindowCount > 0U)
            && (informationCoefficientSampleCount == icWindowDiagnostics.validWindowCount);
    }

    [[nodiscard]] bool isTurnoverDiagnosticsConsistent() const noexcept
    {
        return !turnoverRatio.available || turnoverWindowDiagnostics.validWindowCount > 0U;
    }

    [[nodiscard]] bool isNumGroupsConsistent() const noexcept
    {
        return !numGroups.available || static_cast<uint32_t>(numGroups.value) <= presentSignalCount;
    }
};

struct AnalysisCoreMetricsView final {
    uint32_t totalSignalCount{0U};
    uint32_t presentSignalCount{0U};
    double coverageRatio{0.0};
    double crossSectionMean{0.0};
    double crossSectionVariance{0.0};
};

struct InformationCoefficientMetricView final {
    AnalysisScalarMetric informationCoefficient{};
    AnalysisScalarMetric informationCoefficientStdDev{};
    AnalysisScalarMetric informationRatio{};
    AnalysisScalarMetric informationCoefficientPositiveRate{};
    AnalysisSignificanceMetric informationCoefficientSignificance{};
    uint32_t sampleCount{0U};
    IcWindowDiagnostics diagnostics{};
    AnalysisMetricQuality quality{AnalysisMetricQuality::Unavailable};
};

struct CrossSectionMetricView final {
    AnalysisScalarMetric monotonicity{};
    AnalysisScalarMetric discrimination{};
    AnalysisScalarMetric layeredReturnSpread{};
};

struct TurnoverMetricView final {
    AnalysisScalarMetric turnoverRatio{};
    TurnoverWindowDiagnostics diagnostics{};
    AnalysisMetricQuality quality{AnalysisMetricQuality::Unavailable};
};

struct FactorQualityMetrics16View final {
    AnalysisScalarMetric rankIcMean{};
    AnalysisScalarMetric rankIcStd{};
    AnalysisScalarMetric rankIcir{};
    AnalysisScalarMetric icWinRate{};
    AnalysisScalarMetric icPValue{};
    AnalysisScalarMetric monotonicityScore{};
    AnalysisScalarMetric longShortSharpe{};
    AnalysisScalarMetric longShortAnnualReturn{};
    AnalysisScalarMetricInt icHalfLife{};
    AnalysisScalarMetric annualTurnover{};
    AnalysisScalarMetric costAdjustedSharpe{};
    AnalysisScalarMetric alpha{};
    AnalysisScalarMetric icTStat{};
    AnalysisScalarMetric monthlyWinRate{};
    AnalysisScalarMetricInt numGroups{};
    AnalysisScalarMetricInt coreRating{};
};

struct FactorQualityMetrics16DiagnosticsView final {
    AnalysisMetricAvailabilityReason rankIcMeanReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason rankIcStdReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason rankIcirReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason icWinRateReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason icPValueReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason monotonicityScoreReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason longShortSharpeReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason longShortAnnualReturnReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason icHalfLifeReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason annualTurnoverReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason costAdjustedSharpeReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason alphaReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason icTStatReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason monthlyWinRateReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason numGroupsReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
    AnalysisMetricAvailabilityReason coreRatingReason{AnalysisMetricAvailabilityReason::UnavailableNoAttempt};
};

struct FactorQualityMetrics16Snapshot final {
    FactorQualityMetrics16View metrics{};
    FactorQualityMetrics16DiagnosticsView diagnostics{};
};

class IFactorQualityMetricProjector {
public:
    virtual ~IFactorQualityMetricProjector() = default;
    virtual void project(
        const AnalysisReport& report,
        FactorQualityMetrics16View& view) const noexcept = 0;
};

class IcDirectMetricProjector final : public IFactorQualityMetricProjector {
public:
    void project(const AnalysisReport& report, FactorQualityMetrics16View& view) const noexcept override
    {
        view.rankIcMean = report.informationCoefficient;
        view.rankIcStd = report.informationCoefficientStdDev;
        view.rankIcir = report.informationRatio;
        view.icWinRate = report.informationCoefficientPositiveRate;
    }
};

class IcSignificanceMetricProjector final : public IFactorQualityMetricProjector {
public:
    void project(const AnalysisReport& report, FactorQualityMetrics16View& view) const noexcept override
    {
        if (!report.informationCoefficientSignificance.available) {
            return;
        }

        view.icPValue.available = true;
        view.icPValue.value = report.informationCoefficientSignificance.pValue;
        view.icTStat.available = true;
        view.icTStat.value = report.informationCoefficientSignificance.tStatistic;
    }
};

class CrossSectionMetricProjector final : public IFactorQualityMetricProjector {
public:
    void project(const AnalysisReport& report, FactorQualityMetrics16View& view) const noexcept override
    {
        view.monotonicityScore = report.monotonicity;
        view.longShortAnnualReturn = report.longShortAnnualReturn;
    }
};

class TurnoverMetricProjector final : public IFactorQualityMetricProjector {
public:
    void project(const AnalysisReport& report, FactorQualityMetrics16View& view) const noexcept override
    {
        view.annualTurnover = report.turnoverRatio;
    }
};

class NumGroupsMetricProjector final : public IFactorQualityMetricProjector {
public:
    void project(const AnalysisReport& report, FactorQualityMetrics16View& view) const noexcept override
    {
        view.numGroups = report.numGroups;
    }
};

class ExtendedMetricProjector final : public IFactorQualityMetricProjector {
public:
    void project(const AnalysisReport& report, FactorQualityMetrics16View& view) const noexcept override
    {
        view.longShortSharpe = report.longShortSharpe;
        view.icHalfLife = report.icHalfLife;
        view.costAdjustedSharpe = report.costAdjustedSharpe;
        view.alpha = report.alpha;
        view.monthlyWinRate = report.monthlyWinRate;
    }
};

class IFactorQualityMetricPostProcessor {
public:
    virtual ~IFactorQualityMetricPostProcessor() = default;
    virtual void apply(FactorQualityMetrics16View& view) const noexcept = 0;
};

class CoreRatingPostProcessor final : public IFactorQualityMetricPostProcessor {
public:
    static constexpr int32_t kCoreRatingFail = 0;
    static constexpr int32_t kCoreRatingPass = 1;
    static constexpr int32_t kCoreRatingGood = 2;
    static constexpr int32_t kCoreRatingExcellent = 3;

    static constexpr double kExcellentRankIcirThreshold = 0.8;
    static constexpr double kExcellentIcWinRateThreshold = 0.75;
    static constexpr double kExcellentCostAdjustedSharpeThreshold = 1.0;
    static constexpr double kExcellentAnnualTurnoverUpperBound = 1.5;
    static constexpr double kExcellentMonotonicityThreshold = 0.95;

    static constexpr double kGoodRankIcirThreshold = 0.5;
    static constexpr double kGoodIcWinRateThreshold = 0.65;
    static constexpr double kGoodCostAdjustedSharpeThreshold = 0.5;
    static constexpr double kGoodAnnualTurnoverUpperBound = 2.0;
    static constexpr double kGoodMonotonicityThreshold = 0.85;

    static constexpr double kPassRankIcirThreshold = 0.3;
    static constexpr double kPassIcWinRateThreshold = 0.55;
    static constexpr double kPassCostAdjustedSharpeThreshold = 0.0;
    static constexpr double kPassAnnualTurnoverUpperBound = 3.0;
    static constexpr double kPassMonotonicityThreshold = 0.5;

    void apply(FactorQualityMetrics16View& view) const noexcept override
    {
        if (!canEvaluate(view)) {
            return;
        }

        const double monotonicityAbs = std::abs(view.monotonicityScore.value);
        const bool excellent = view.rankIcir.value > kExcellentRankIcirThreshold
            && view.icWinRate.value > kExcellentIcWinRateThreshold
            && view.costAdjustedSharpe.value > kExcellentCostAdjustedSharpeThreshold
            && view.annualTurnover.value <= kExcellentAnnualTurnoverUpperBound
            && monotonicityAbs > kExcellentMonotonicityThreshold;
        const bool good = view.rankIcir.value > kGoodRankIcirThreshold
            && view.icWinRate.value > kGoodIcWinRateThreshold
            && view.costAdjustedSharpe.value > kGoodCostAdjustedSharpeThreshold
            && view.annualTurnover.value <= kGoodAnnualTurnoverUpperBound
            && monotonicityAbs > kGoodMonotonicityThreshold;
        const bool pass = view.rankIcir.value > kPassRankIcirThreshold
            && view.icWinRate.value > kPassIcWinRateThreshold
            && view.costAdjustedSharpe.value > kPassCostAdjustedSharpeThreshold
            && view.annualTurnover.value <= kPassAnnualTurnoverUpperBound
            && monotonicityAbs > kPassMonotonicityThreshold;

        view.coreRating.available = true;
        if (excellent) {
            view.coreRating.value = kCoreRatingExcellent;
            return;
        }
        if (good) {
            view.coreRating.value = kCoreRatingGood;
            return;
        }
        if (pass) {
            view.coreRating.value = kCoreRatingPass;
            return;
        }
        view.coreRating.value = kCoreRatingFail;
    }

private:
    [[nodiscard]] static bool canEvaluate(const FactorQualityMetrics16View& view) noexcept
    {
        return view.rankIcir.available
            && view.icWinRate.available
            && view.costAdjustedSharpe.available
            && view.annualTurnover.available
            && view.monotonicityScore.available;
    }
};

class FactorQualityMetrics16Assembler final {
public:
    [[nodiscard]] FactorQualityMetrics16View build(const AnalysisReport& report) const noexcept
    {
        FactorQualityMetrics16View view;
        const auto& projectorList = projectors();
        for (const IFactorQualityMetricProjector* projector : projectorList) {
            projector->project(report, view);
        }
        const auto& postProcessorList = postProcessors();
        for (const IFactorQualityMetricPostProcessor* postProcessor : postProcessorList) {
            postProcessor->apply(view);
        }
        return view;
    }

private:
    [[nodiscard]] static const std::array<const IFactorQualityMetricProjector*, 6>& projectors() noexcept
    {
        static const IcDirectMetricProjector icDirectProjector;
        static const IcSignificanceMetricProjector icSignificanceProjector;
        static const CrossSectionMetricProjector crossSectionProjector;
        static const TurnoverMetricProjector turnoverProjector;
        static const NumGroupsMetricProjector numGroupsProjector;
        static const ExtendedMetricProjector extendedMetricProjector;
        static const std::array<const IFactorQualityMetricProjector*, 6> projectorList{
            &icDirectProjector,
            &icSignificanceProjector,
            &crossSectionProjector,
            &turnoverProjector,
            &numGroupsProjector,
            &extendedMetricProjector,
        };
        return projectorList;
    }

    [[nodiscard]] static const std::array<const IFactorQualityMetricPostProcessor*, 1>&
    postProcessors() noexcept
    {
        static const CoreRatingPostProcessor coreRatingPostProcessor;
        static const std::array<const IFactorQualityMetricPostProcessor*, 1> postProcessorList{
            &coreRatingPostProcessor,
        };
        return postProcessorList;
    }
};

[[nodiscard]] inline constexpr uint32_t
factorQualityMetrics16Count() noexcept
{
    return 16U;
}

[[nodiscard]] inline AnalysisCoreMetricsView
buildAnalysisCoreMetricsView(const AnalysisReport& report) noexcept
{
    AnalysisCoreMetricsView view;
    view.totalSignalCount = report.totalSignalCount;
    view.presentSignalCount = report.presentSignalCount;
    view.coverageRatio = report.coverageRatio;
    view.crossSectionMean = report.crossSectionMean;
    view.crossSectionVariance = report.crossSectionVariance;
    return view;
}

[[nodiscard]] inline InformationCoefficientMetricView
buildInformationCoefficientMetricView(const AnalysisReport& report) noexcept
{
    InformationCoefficientMetricView view;
    view.informationCoefficient = report.informationCoefficient;
    view.informationCoefficientStdDev = report.informationCoefficientStdDev;
    view.informationRatio = report.informationRatio;
    view.informationCoefficientPositiveRate = report.informationCoefficientPositiveRate;
    view.informationCoefficientSignificance = report.informationCoefficientSignificance;
    view.sampleCount = report.informationCoefficientSampleCount;
    view.diagnostics = report.icWindowDiagnostics;
    view.quality = report.informationCoefficientQuality;
    return view;
}

[[nodiscard]] inline CrossSectionMetricView
buildCrossSectionMetricView(const AnalysisReport& report) noexcept
{
    CrossSectionMetricView view;
    view.monotonicity = report.monotonicity;
    view.discrimination = report.discrimination;
    view.layeredReturnSpread = report.layeredReturnSpread;
    return view;
}

[[nodiscard]] inline TurnoverMetricView
buildTurnoverMetricView(const AnalysisReport& report) noexcept
{
    TurnoverMetricView view;
    view.turnoverRatio = report.turnoverRatio;
    view.diagnostics = report.turnoverWindowDiagnostics;
    view.quality = report.turnoverQuality;
    return view;
}

[[nodiscard]] inline FactorQualityMetrics16View
buildFactorQualityMetrics16View(const AnalysisReport& report) noexcept
{
    const FactorQualityMetrics16Assembler assembler;
    return assembler.build(report);
}

[[nodiscard]] inline FactorQualityMetrics16DiagnosticsView
buildFactorQualityMetrics16DiagnosticsView(const AnalysisReport& report) noexcept
{
    FactorQualityMetrics16DiagnosticsView view;
    view.rankIcMeanReason = report.rankIcMeanReason;
    view.rankIcStdReason = report.rankIcStdReason;
    view.rankIcirReason = report.rankIcirReason;
    view.icWinRateReason = report.icWinRateReason;
    view.icPValueReason = report.icPValueReason;
    view.monotonicityScoreReason = report.monotonicityReason;
    view.longShortSharpeReason = report.longShortSharpeReason;
    view.longShortAnnualReturnReason = report.longShortAnnualReturnReason;
    view.icHalfLifeReason = report.icHalfLifeReason;
    view.annualTurnoverReason = report.annualTurnoverReason;
    view.costAdjustedSharpeReason = report.costAdjustedSharpeReason;
    view.alphaReason = report.alphaReason;
    view.icTStatReason = report.icTStatReason;
    view.monthlyWinRateReason = report.monthlyWinRateReason;
    view.numGroupsReason = report.numGroupsReason;

    const FactorQualityMetrics16View metrics16 = buildFactorQualityMetrics16View(report);
    if (metrics16.coreRating.available) {
        view.coreRatingReason = AnalysisMetricAvailabilityReason::Available;
    } else if (!metrics16.rankIcir.available
        || !metrics16.icWinRate.available
        || !metrics16.costAdjustedSharpe.available
        || !metrics16.annualTurnover.available
        || !metrics16.monotonicityScore.available) {
        view.coreRatingReason = AnalysisMetricAvailabilityReason::UnavailableMissingDependency;
    } else {
        view.coreRatingReason = AnalysisMetricAvailabilityReason::UnavailableInvalidNumeric;
    }
    return view;
}

[[nodiscard]] inline FactorQualityMetrics16Snapshot
buildFactorQualityMetrics16Snapshot(const AnalysisReport& report) noexcept
{
    FactorQualityMetrics16Snapshot snapshot;
    snapshot.metrics = buildFactorQualityMetrics16View(report);
    snapshot.diagnostics = buildFactorQualityMetrics16DiagnosticsView(report);
    return snapshot;
}

} // namespace factor::compute
