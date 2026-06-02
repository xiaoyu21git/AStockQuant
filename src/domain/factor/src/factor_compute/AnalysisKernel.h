#pragma once

#include "factor_compute/FactorSignalTypes.h"
#include "factor_compute/IFactorOperatorLibrary.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace factor::compute::detail {

struct SignalReturnPair final {
    int32_t instrumentIndex{0};
    double signalValue{0.0};
    double forwardReturn{0.0};
};

struct IcSeriesSummary final {
    std::vector<double> values;
    double mean{0.0};
    double stdDev{0.0};
    bool valid{false};
    uint32_t attemptedWindowCount{0U};
    uint32_t validWindowCount{0U};
    uint32_t skippedInsufficientSampleCount{0U};
    uint32_t skippedInvalidValueCount{0U};
};

struct TurnoverSummary final {
    std::optional<double> value;
    uint32_t attemptedWindowCount{0U};
    uint32_t validWindowCount{0U};
    uint32_t skippedIncompatibleBucketCount{0U};
    uint32_t skippedOutOfRangeCount{0U};
};

struct LongShortSeriesSummary final {
    std::vector<double> spreadSeries;
    std::optional<double> meanSpread;
    std::optional<double> stdDevSpread;
    uint32_t attemptedWindowCount{0U};
    uint32_t validWindowCount{0U};
    uint32_t skippedInsufficientSampleCount{0U};
    uint32_t skippedInvalidValueCount{0U};
};

class AnalysisKernel final {
public:
    static constexpr uint8_t kPresentMaskValue = 0U;
    static constexpr int32_t kPrimaryFactorIndex = 0;
    static constexpr int32_t kPrimaryTimeIndex = 0;
    static constexpr int32_t kMinimumCorrelationSampleCount = 2;
    static constexpr int32_t kMinimumInformationRatioSampleCount = 2;
    static constexpr int32_t kMinimumTurnoverTimeCount = 2;
    static constexpr int32_t kLayerBucketDivisor = 5;
    static constexpr double kVarianceEpsilon = 1e-12;
    static constexpr double kMetricBoundEpsilon = 1e-9;
    static constexpr double kNormalCdfSqrtHalf = 1.4142135623730951;
    static constexpr double kPerfectCorrelationTStatistic = 1e9;

    [[nodiscard]] std::optional<double>
    calculatePearsonCorrelation(const std::vector<double>& lhs, const std::vector<double>& rhs) const;

    [[nodiscard]] std::optional<double>
    calculateSpearmanCorrelation(const std::vector<double>& lhs, const std::vector<double>& rhs) const;

    [[nodiscard]] std::optional<AnalysisSignificanceMetric>
    calculateSignificance(double correlation, int32_t sampleCount) const;

    [[nodiscard]] std::vector<SignalReturnPair>
    buildCrossSectionPairs(const SignalSet& signalSet, NumericConstMatrixView closeView) const;

    [[nodiscard]] std::vector<SignalReturnPair>
    buildCrossSectionPairsAt(
        const SignalSet& signalSet,
        NumericConstMatrixView closeView,
        int32_t signalTimeIndex,
        int32_t fromRow,
        int32_t toRow) const;

    [[nodiscard]] IcSeriesSummary
    buildIcSeriesSummary(const SignalSet& signalSet, NumericConstMatrixView closeView) const;

    [[nodiscard]] std::optional<double>
    calculateLayeredSpread(const std::vector<SignalReturnPair>& pairs) const;

    [[nodiscard]] std::optional<double>
    calculateTurnover(const SignalSet& signalSet) const;

    [[nodiscard]] TurnoverSummary
    calculateTurnoverSummary(const SignalSet& signalSet) const;

    [[nodiscard]] LongShortSeriesSummary
    buildLongShortSeriesSummary(const SignalSet& signalSet, NumericConstMatrixView closeView) const;
};

} // namespace factor::compute::detail
