#include "AnalysisKernel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_set>
#include <utility>

namespace factor::compute::detail {

namespace {

struct RunningMoments final {
    size_t count{0U};
    double mean{0.0};
    double m2{0.0};

    [[nodiscard]] bool addSample(double value) noexcept
    {
        if (!std::isfinite(value)) {
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
        if (!std::isfinite(varianceValue) || varianceValue < 0.0) {
            return std::nullopt;
        }

        return varianceValue;
    }
};

[[nodiscard]] std::optional<size_t> flattenIndexChecked(
    const SignalSet& signalSet,
    int32_t timeIndex,
    int32_t instrumentIndex,
    int32_t factorIndex) noexcept
{
    if (timeIndex < 0 || instrumentIndex < 0 || factorIndex < 0) {
        return std::nullopt;
    }

    const size_t timeValue = static_cast<size_t>(timeIndex);
    const size_t instrumentValue = static_cast<size_t>(instrumentIndex);
    const size_t factorValue = static_cast<size_t>(factorIndex);
    const size_t timeStride = static_cast<size_t>(signalSet.index.timeStride);
    const size_t instrumentStride = static_cast<size_t>(signalSet.index.instrumentStride);
    const size_t factorStride = static_cast<size_t>(signalSet.index.factorStride);
    const size_t maxSize = std::numeric_limits<size_t>::max();

    if (timeStride > 0U && timeValue > maxSize / timeStride) {
        return std::nullopt;
    }
    const size_t timeOffset = timeValue * timeStride;

    if (instrumentStride > 0U && instrumentValue > maxSize / instrumentStride) {
        return std::nullopt;
    }
    const size_t instrumentOffset = instrumentValue * instrumentStride;
    if (timeOffset > maxSize - instrumentOffset) {
        return std::nullopt;
    }
    const size_t timeInstrumentOffset = timeOffset + instrumentOffset;

    if (factorStride > 0U && factorValue > maxSize / factorStride) {
        return std::nullopt;
    }
    const size_t factorOffset = factorValue * factorStride;
    if (timeInstrumentOffset > maxSize - factorOffset) {
        return std::nullopt;
    }

    const size_t flatIndex = timeInstrumentOffset + factorOffset;
    if (flatIndex >= signalSet.mask.size() || flatIndex >= signalSet.values.size()) {
        return std::nullopt;
    }

    return flatIndex;
}

struct CrossSectionBuildSpec final {
    int32_t signalTimeIndex{0};
    int32_t fromRow{0};
    int32_t toRow{0};
};

[[nodiscard]] CrossSectionBuildSpec buildCrossSectionSpec(
    int32_t signalTimeIndex,
    int32_t fromRow,
    int32_t toRow) noexcept
{
    CrossSectionBuildSpec spec;
    spec.signalTimeIndex = signalTimeIndex;
    spec.fromRow = fromRow;
    spec.toRow = toRow;
    return spec;
}

[[nodiscard]] CrossSectionBuildSpec buildNextStepCrossSectionSpec(int32_t signalTimeIndex) noexcept
{
    return buildCrossSectionSpec(signalTimeIndex, signalTimeIndex, signalTimeIndex + 1);
}

class SignalSetAccessor final {
public:
    explicit SignalSetAccessor(const SignalSet& signalSet) noexcept
        : signalSet_(signalSet)
    {
    }

    [[nodiscard]] size_t instrumentCount() const noexcept
    {
        return signalSet_.instruments.size();
    }

    [[nodiscard]] std::optional<size_t> flattenIndex(
        int32_t timeIndex,
        int32_t instrumentIndex,
        int32_t factorIndex) const noexcept
    {
        return flattenIndexChecked(signalSet_, timeIndex, instrumentIndex, factorIndex);
    }

    [[nodiscard]] std::optional<double> presentSignalValue(
        int32_t timeIndex,
        int32_t instrumentIndex,
        int32_t factorIndex) const noexcept
    {
        const std::optional<size_t> index = flattenIndex(timeIndex, instrumentIndex, factorIndex);
        if (!index.has_value()) {
            return std::nullopt;
        }
        if (signalSet_.mask[index.value()] != AnalysisKernel::kPresentMaskValue) {
            return std::nullopt;
        }

        const double signalValue = signalSet_.values[index.value()];
        if (!std::isfinite(signalValue)) {
            return std::nullopt;
        }

        return signalValue;
    }

private:
    const SignalSet& signalSet_;
};

class CrossSectionPairBuilder final {
public:
    CrossSectionPairBuilder(const SignalSet& signalSet, NumericConstMatrixView closeView)
        : accessor_(signalSet)
        , closeView_(closeView)
        , instrumentCount_(static_cast<int32_t>(accessor_.instrumentCount()))
        , hasFactors_(!signalSet.signalIds.empty())
    {
    }

    [[nodiscard]] static bool isValidCloseView(
        NumericConstMatrixView closeView,
        int32_t instrumentCount) noexcept
    {
        return closeView.isValid()
            && closeView.columnCount >= instrumentCount
            && closeView.rowStride >= instrumentCount;
    }

    [[nodiscard]] std::vector<SignalReturnPair> buildFromSpec(const CrossSectionBuildSpec& spec) const
    {
        std::vector<SignalReturnPair> pairs;
        if (!hasFactors_ || instrumentCount_ <= 0) {
            return pairs;
        }
        if (!isValidCloseView(closeView_, instrumentCount_) || !isValidRows(spec.fromRow, spec.toRow)) {
            return pairs;
        }

        pairs.reserve(static_cast<size_t>(instrumentCount_));
        for (int32_t instrumentIndex = 0; instrumentIndex < instrumentCount_; ++instrumentIndex) {
            const std::optional<double> signalValue = accessor_.presentSignalValue(
                spec.signalTimeIndex,
                instrumentIndex,
                AnalysisKernel::kPrimaryFactorIndex);
            if (!signalValue.has_value()) {
                continue;
            }

            const std::optional<std::pair<double, double>> closePair =
                loadClosePair(instrumentIndex, spec.fromRow, spec.toRow);
            if (!closePair.has_value()) {
                continue;
            }

            SignalReturnPair pair;
            pair.instrumentIndex = instrumentIndex;
            pair.signalValue = signalValue.value();
            pair.forwardReturn = (closePair.value().second / closePair.value().first) - 1.0;
            if (!std::isfinite(pair.forwardReturn)) {
                continue;
            }

            pairs.emplace_back(pair);
        }

        return pairs;
    }

private:
    [[nodiscard]] bool isValidRows(int32_t fromRow, int32_t toRow) const noexcept
    {
        return fromRow >= 0
            && toRow >= 0
            && fromRow < closeView_.rowCount
            && toRow < closeView_.rowCount;
    }

    [[nodiscard]] std::optional<std::pair<double, double>> loadClosePair(
        int32_t instrumentIndex,
        int32_t fromRow,
        int32_t toRow) const noexcept
    {
        const size_t fromFlat = static_cast<size_t>(fromRow) * static_cast<size_t>(closeView_.rowStride)
            + static_cast<size_t>(instrumentIndex);
        const size_t toFlat = static_cast<size_t>(toRow) * static_cast<size_t>(closeView_.rowStride)
            + static_cast<size_t>(instrumentIndex);

        const double fromClose = closeView_.data[fromFlat];
        const double toClose = closeView_.data[toFlat];
        if (!std::isfinite(fromClose)
            || !std::isfinite(toClose)
            || std::abs(fromClose) <= AnalysisKernel::kVarianceEpsilon) {
            return std::nullopt;
        }

        return std::make_pair(fromClose, toClose);
    }

    SignalSetAccessor accessor_;
    NumericConstMatrixView closeView_;
    int32_t instrumentCount_{0};
    bool hasFactors_{false};
};

class IcSeriesSummaryBuilder final {
public:
    IcSeriesSummaryBuilder(
        const AnalysisKernel& analysisKernel,
        const SignalSet& signalSet,
        NumericConstMatrixView closeView)
        : analysisKernel_(analysisKernel)
        , signalSet_(signalSet)
        , closeView_(closeView)
        , pairBuilder_(signalSet, closeView)
    {
    }

    [[nodiscard]] IcSeriesSummary build() const
    {
        IcSeriesSummary summary;
        const int32_t timeCount = static_cast<int32_t>(signalSet_.dates.size());
        if (timeCount < 2 || closeView_.rowCount < 2) {
            return summary;
        }

        const int32_t usableWindowCount = std::min(timeCount - 1, closeView_.rowCount - 1);
        summary.attemptedWindowCount = static_cast<uint32_t>(usableWindowCount);
        summary.values.reserve(static_cast<size_t>(usableWindowCount));

        std::vector<double> signalSeries;
        std::vector<double> returnSeries;
        const size_t instrumentCapacity = signalSet_.instruments.size();
        signalSeries.reserve(instrumentCapacity);
        returnSeries.reserve(instrumentCapacity);

        for (int32_t timeIndex = 0; timeIndex < usableWindowCount; ++timeIndex) {
            const IcWindowValueResult icWindowValue =
                computeWindowIcValue(timeIndex, signalSeries, returnSeries);
            if (icWindowValue.skipReason == IcWindowSkipReason::InsufficientSample) {
                ++summary.skippedInsufficientSampleCount;
                continue;
            }
            if (icWindowValue.skipReason == IcWindowSkipReason::InvalidValue) {
                ++summary.skippedInvalidValueCount;
                continue;
            }

            const std::optional<double>& ic = icWindowValue.value;
            if (ic.has_value()) {
                summary.values.emplace_back(ic.value());
                ++summary.validWindowCount;
            }
        }

        if (summary.values.empty()) {
            return summary;
        }

        RunningMoments moments;
        for (const double value : summary.values) {
            if (!moments.addSample(value)) {
                return summary;
            }
        }

        const std::optional<double> variance = moments.variance();
        if (!variance.has_value()) {
            return summary;
        }
        const double stdDev = std::sqrt(variance.value());
        if (!std::isfinite(stdDev)) {
            return summary;
        }

        summary.mean = moments.mean;
        summary.stdDev = stdDev;
        summary.valid = true;
        return summary;
    }

private:
    enum class IcWindowSkipReason : uint8_t {
        None,
        InsufficientSample,
        InvalidValue,
    };

    struct IcWindowValueResult final {
        std::optional<double> value;
        IcWindowSkipReason skipReason{IcWindowSkipReason::None};
    };

    [[nodiscard]] IcWindowValueResult computeWindowIcValue(
        int32_t signalTimeIndex,
        std::vector<double>& signalSeries,
        std::vector<double>& returnSeries) const
    {
        IcWindowValueResult result;
        const CrossSectionBuildSpec spec = buildNextStepCrossSectionSpec(signalTimeIndex);
        const std::vector<SignalReturnPair> pairs = pairBuilder_.buildFromSpec(spec);
        if (pairs.size() < static_cast<size_t>(AnalysisKernel::kMinimumCorrelationSampleCount)) {
            result.skipReason = IcWindowSkipReason::InsufficientSample;
            return result;
        }

        extractSeriesFromPairs(pairs, signalSeries, returnSeries);
        result.value = analysisKernel_.calculatePearsonCorrelation(signalSeries, returnSeries);
        if (!result.value.has_value()) {
            result.skipReason = IcWindowSkipReason::InvalidValue;
        }
        return result;
    }

    static void extractSeriesFromPairs(
        const std::vector<SignalReturnPair>& pairs,
        std::vector<double>& signalSeries,
        std::vector<double>& returnSeries)
    {
        signalSeries.clear();
        returnSeries.clear();
        for (const SignalReturnPair& pair : pairs) {
            signalSeries.emplace_back(pair.signalValue);
            returnSeries.emplace_back(pair.forwardReturn);
        }
    }

    const AnalysisKernel& analysisKernel_;
    const SignalSet& signalSet_;
    NumericConstMatrixView closeView_;
    CrossSectionPairBuilder pairBuilder_;
};

class LongShortSeriesSummaryBuilder final {
public:
    LongShortSeriesSummaryBuilder(
        const AnalysisKernel& analysisKernel,
        const SignalSet& signalSet,
        NumericConstMatrixView closeView)
        : analysisKernel_(analysisKernel)
        , signalSet_(signalSet)
        , closeView_(closeView)
        , pairBuilder_(signalSet, closeView)
    {
    }

    [[nodiscard]] LongShortSeriesSummary build() const
    {
        LongShortSeriesSummary summary;
        const int32_t timeCount = static_cast<int32_t>(signalSet_.dates.size());
        if (timeCount < 2 || closeView_.rowCount < 2) {
            return summary;
        }

        const int32_t usableWindowCount = std::min(timeCount - 1, closeView_.rowCount - 1);
        summary.attemptedWindowCount = static_cast<uint32_t>(usableWindowCount);
        summary.spreadSeries.reserve(static_cast<size_t>(usableWindowCount));

        RunningMoments moments;
        for (int32_t timeIndex = 0; timeIndex < usableWindowCount; ++timeIndex) {
            const CrossSectionBuildSpec spec = buildNextStepCrossSectionSpec(timeIndex);
            const std::vector<SignalReturnPair> pairs = pairBuilder_.buildFromSpec(spec);
            if (pairs.size() < static_cast<size_t>(AnalysisKernel::kMinimumCorrelationSampleCount)) {
                ++summary.skippedInsufficientSampleCount;
                continue;
            }

            const std::optional<double> spread = analysisKernel_.calculateLayeredSpread(pairs);
            if (!spread.has_value() || !std::isfinite(spread.value())) {
                ++summary.skippedInvalidValueCount;
                continue;
            }

            summary.spreadSeries.emplace_back(spread.value());
            if (!moments.addSample(spread.value())) {
                ++summary.skippedInvalidValueCount;
                summary.spreadSeries.pop_back();
                continue;
            }
            ++summary.validWindowCount;
        }

        if (summary.validWindowCount == 0U) {
            return summary;
        }

        summary.meanSpread = moments.mean;
        const std::optional<double> variance = moments.variance();
        if (!variance.has_value()) {
            return summary;
        }

        const double stdDev = std::sqrt(variance.value());
        if (!std::isfinite(stdDev)) {
            return summary;
        }
        summary.stdDevSpread = stdDev;
        return summary;
    }

private:
    const AnalysisKernel& analysisKernel_;
    const SignalSet& signalSet_;
    NumericConstMatrixView closeView_;
    CrossSectionPairBuilder pairBuilder_;
};

class TopLayerSelector final {
public:
    TopLayerSelector(const SignalSet& signalSet, int32_t factorIndex) noexcept
        : accessor_(signalSet)
        , factorIndex_(factorIndex)
    {
    }

    [[nodiscard]] std::vector<int32_t> pickAtTime(int32_t timeIndex) const
    {
        std::vector<std::pair<double, int32_t>> ranked;
        const int32_t instrumentCount = static_cast<int32_t>(accessor_.instrumentCount());
        ranked.reserve(static_cast<size_t>(instrumentCount));

        for (int32_t instrumentIndex = 0; instrumentIndex < instrumentCount; ++instrumentIndex) {
            const std::optional<double> signalValue =
                accessor_.presentSignalValue(timeIndex, instrumentIndex, factorIndex_);
            if (!signalValue.has_value()) {
                continue;
            }

            ranked.emplace_back(signalValue.value(), instrumentIndex);
        }

        if (ranked.empty()) {
            return {};
        }

        const int32_t bucketSize = std::max(
            1,
            static_cast<int32_t>(ranked.size()) / AnalysisKernel::kLayerBucketDivisor);
        const size_t topCount = static_cast<size_t>(bucketSize);
        auto descendingBySignal = [](const auto& lhs, const auto& rhs) {
            if (lhs.first == rhs.first) {
                return lhs.second < rhs.second;
            }
            return lhs.first > rhs.first;
        };

        if (topCount < ranked.size()) {
            std::nth_element(
                ranked.begin(),
                ranked.begin() + static_cast<std::ptrdiff_t>(topCount),
                ranked.end(),
                descendingBySignal);
        }
        std::sort(
            ranked.begin(),
            ranked.begin() + static_cast<std::ptrdiff_t>(topCount),
            descendingBySignal);

        std::vector<int32_t> topLayer;
        topLayer.reserve(static_cast<size_t>(bucketSize));
        for (int32_t index = 0; index < bucketSize; ++index) {
            topLayer.push_back(ranked[static_cast<size_t>(index)].second);
        }

        return topLayer;
    }

private:
    SignalSetAccessor accessor_;
    int32_t factorIndex_{0};
};

class TurnoverCalculator final {
public:
    explicit TurnoverCalculator(const SignalSet& signalSet) noexcept
        : signalSet_(signalSet)
        , selector_(signalSet, AnalysisKernel::kPrimaryFactorIndex)
    {
    }

    [[nodiscard]] TurnoverSummary buildSummary() const
    {
        TurnoverSummary summary;
        const int32_t timeCount = static_cast<int32_t>(signalSet_.dates.size());
        if (timeCount < AnalysisKernel::kMinimumTurnoverTimeCount || signalSet_.signalIds.empty()) {
            return summary;
        }

        summary.attemptedWindowCount = static_cast<uint32_t>(timeCount - 1);
        double turnoverSum = 0.0;
        int32_t turnoverCount = 0;
        for (int32_t timeIndex = 1; timeIndex < timeCount; ++timeIndex) {
            const std::vector<int32_t> previousTop = selector_.pickAtTime(timeIndex - 1);
            const std::vector<int32_t> currentTop = selector_.pickAtTime(timeIndex);

            const WindowTurnoverResult windowTurnoverResult =
                calculateWindowTurnover(previousTop, currentTop);
            if (windowTurnoverResult.skipReason == TurnoverSkipReason::IncompatibleBucket) {
                ++summary.skippedIncompatibleBucketCount;
                continue;
            }
            if (windowTurnoverResult.skipReason == TurnoverSkipReason::OutOfRange) {
                ++summary.skippedOutOfRangeCount;
                continue;
            }

            const std::optional<double>& turnoverValue = windowTurnoverResult.value;
            if (!turnoverValue.has_value()) {
                continue;
            }

            turnoverSum += turnoverValue.value();
            ++turnoverCount;
            ++summary.validWindowCount;
        }

        if (turnoverCount <= 0) {
            return summary;
        }

        const double turnoverMean = turnoverSum / static_cast<double>(turnoverCount);
        if (!std::isfinite(turnoverMean)
            || turnoverMean < -AnalysisKernel::kMetricBoundEpsilon
            || turnoverMean > (1.0 + AnalysisKernel::kMetricBoundEpsilon)) {
            summary.value.reset();
            return summary;
        }

        summary.value = turnoverMean;
        return summary;
    }

private:
    enum class TurnoverSkipReason : uint8_t {
        None,
        IncompatibleBucket,
        OutOfRange,
    };

    struct WindowTurnoverResult final {
        std::optional<double> value;
        TurnoverSkipReason skipReason{TurnoverSkipReason::None};
    };

    [[nodiscard]] static WindowTurnoverResult calculateWindowTurnover(
        const std::vector<int32_t>& previousTop,
        const std::vector<int32_t>& currentTop)
    {
        WindowTurnoverResult result;
        if (previousTop.empty() || currentTop.empty() || previousTop.size() != currentTop.size()) {
            result.skipReason = TurnoverSkipReason::IncompatibleBucket;
            return result;
        }

        std::unordered_set<int32_t> previousTopSet;
        previousTopSet.reserve(previousTop.size());
        for (const int32_t instrument : previousTop) {
            previousTopSet.insert(instrument);
        }

        int32_t overlapCount = 0;
        for (const int32_t currentInstrument : currentTop) {
            if (previousTopSet.find(currentInstrument) != previousTopSet.end()) {
                ++overlapCount;
            }
        }

        const double bucketSize = static_cast<double>(currentTop.size());
        const double turnoverValue = 1.0 - (static_cast<double>(overlapCount) / bucketSize);
        if (!std::isfinite(turnoverValue)
            || turnoverValue < -AnalysisKernel::kMetricBoundEpsilon
            || turnoverValue > (1.0 + AnalysisKernel::kMetricBoundEpsilon)) {
            result.skipReason = TurnoverSkipReason::OutOfRange;
            return result;
        }

        result.value = turnoverValue;
        return result;
    }

    const SignalSet& signalSet_;
    TopLayerSelector selector_;
};

class AverageRankCalculator final {
public:
    [[nodiscard]] std::vector<double> calculate(const std::vector<double>& values) const
    {
        std::vector<std::pair<double, size_t>> sorted;
        sorted.reserve(values.size());
        for (size_t index = 0; index < values.size(); ++index) {
            sorted.emplace_back(values[index], index);
        }

        std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.first == rhs.first) {
                return lhs.second < rhs.second;
            }
            return lhs.first < rhs.first;
        });

        std::vector<double> ranks(values.size(), 0.0);
        size_t cursor = 0;
        while (cursor < sorted.size()) {
            size_t tail = cursor + 1;
            while (tail < sorted.size() && sorted[tail].first == sorted[cursor].first) {
                ++tail;
            }

            const double avgRank = (static_cast<double>(cursor + 1) + static_cast<double>(tail)) / 2.0;
            for (size_t index = cursor; index < tail; ++index) {
                ranks[sorted[index].second] = avgRank;
            }
            cursor = tail;
        }

        return ranks;
    }
};

class PearsonCorrelationCalculator final {
public:
    [[nodiscard]] std::optional<double> calculate(
        const std::vector<double>& lhs,
        const std::vector<double>& rhs) const
    {
        if (lhs.size() != rhs.size()
            || static_cast<int32_t>(lhs.size()) < AnalysisKernel::kMinimumCorrelationSampleCount) {
            return std::nullopt;
        }

        const double count = static_cast<double>(lhs.size());
        const double lhsSum = std::accumulate(lhs.begin(), lhs.end(), 0.0);
        const double rhsSum = std::accumulate(rhs.begin(), rhs.end(), 0.0);
        const double lhsMean = lhsSum / count;
        const double rhsMean = rhsSum / count;
        if (!std::isfinite(lhsMean) || !std::isfinite(rhsMean)) {
            return std::nullopt;
        }

        double covariance = 0.0;
        double lhsVariance = 0.0;
        double rhsVariance = 0.0;
        for (size_t index = 0; index < lhs.size(); ++index) {
            if (!std::isfinite(lhs[index]) || !std::isfinite(rhs[index])) {
                return std::nullopt;
            }

            const double lhsCentered = lhs[index] - lhsMean;
            const double rhsCentered = rhs[index] - rhsMean;
            covariance += lhsCentered * rhsCentered;
            lhsVariance += lhsCentered * lhsCentered;
            rhsVariance += rhsCentered * rhsCentered;
        }

        if (lhsVariance <= AnalysisKernel::kVarianceEpsilon
            || rhsVariance <= AnalysisKernel::kVarianceEpsilon) {
            return std::nullopt;
        }

        const double denominator = std::sqrt(lhsVariance * rhsVariance);
        if (denominator <= AnalysisKernel::kVarianceEpsilon) {
            return std::nullopt;
        }

        const double correlation = covariance / denominator;
        if (!std::isfinite(correlation)
            || std::abs(correlation) > (1.0 + AnalysisKernel::kVarianceEpsilon)) {
            return std::nullopt;
        }

        return correlation;
    }
};

class SpearmanCorrelationCalculator final {
public:
    [[nodiscard]] std::optional<double> calculate(
        const std::vector<double>& lhs,
        const std::vector<double>& rhs) const
    {
        if (lhs.size() != rhs.size()
            || static_cast<int32_t>(lhs.size()) < AnalysisKernel::kMinimumCorrelationSampleCount) {
            return std::nullopt;
        }

        for (size_t index = 0; index < lhs.size(); ++index) {
            if (!std::isfinite(lhs[index]) || !std::isfinite(rhs[index])) {
                return std::nullopt;
            }
        }

        const AverageRankCalculator rankCalculator;
        const std::vector<double> lhsRanks = rankCalculator.calculate(lhs);
        const std::vector<double> rhsRanks = rankCalculator.calculate(rhs);

        const PearsonCorrelationCalculator pearsonCalculator;
        return pearsonCalculator.calculate(lhsRanks, rhsRanks);
    }
};

class SignificanceCalculator final {
public:
    [[nodiscard]] std::optional<AnalysisSignificanceMetric>
    calculate(double correlation, int32_t sampleCount) const
    {
        if (sampleCount <= AnalysisKernel::kMinimumCorrelationSampleCount) {
            return std::nullopt;
        }

        if (!std::isfinite(correlation)
            || std::abs(correlation) > (1.0 + AnalysisKernel::kVarianceEpsilon)) {
            return std::nullopt;
        }

        const double denominatorTerm = 1.0 - (correlation * correlation);
        if (denominatorTerm <= AnalysisKernel::kVarianceEpsilon) {
            AnalysisSignificanceMetric metric;
            metric.available = true;
            metric.tStatistic = correlation >= 0.0
                ? AnalysisKernel::kPerfectCorrelationTStatistic
                : -AnalysisKernel::kPerfectCorrelationTStatistic;
            metric.pValue = 0.0;
            metric.significant = true;
            return metric;
        }

        const double degreesOfFreedom = static_cast<double>(sampleCount - 2);
        const double tStatistic = correlation * std::sqrt(degreesOfFreedom / denominatorTerm);
        const double pValue = std::erfc(std::abs(tStatistic) / AnalysisKernel::kNormalCdfSqrtHalf);
        if (!std::isfinite(tStatistic) || !std::isfinite(pValue) || pValue < 0.0 || pValue > 1.0) {
            return std::nullopt;
        }

        AnalysisSignificanceMetric metric;
        metric.available = true;
        metric.tStatistic = tStatistic;
        metric.pValue = pValue;
        metric.significant = pValue < AnalysisSignificanceMetric::kSignificancePValueThreshold;
        return metric;
    }
};

class LayeredSpreadCalculator final {
public:
    [[nodiscard]] std::optional<double>
    calculate(const std::vector<SignalReturnPair>& pairs) const
    {
        if (pairs.size() < static_cast<size_t>(AnalysisKernel::kMinimumCorrelationSampleCount)) {
            return std::nullopt;
        }

        std::vector<SignalReturnPair> sortedPairs = pairs;
        std::sort(sortedPairs.begin(), sortedPairs.end(), [](const SignalReturnPair& lhs, const SignalReturnPair& rhs) {
            return lhs.signalValue < rhs.signalValue;
        });

        const int32_t pairCount = static_cast<int32_t>(sortedPairs.size());
        const int32_t bucketSize = std::max(1, pairCount / AnalysisKernel::kLayerBucketDivisor);

        double bottomSum = 0.0;
        double topSum = 0.0;
        for (int32_t index = 0; index < bucketSize; ++index) {
            bottomSum += sortedPairs[static_cast<size_t>(index)].forwardReturn;
            topSum += sortedPairs[sortedPairs.size() - static_cast<size_t>(bucketSize) + static_cast<size_t>(index)]
                          .forwardReturn;
        }

        const double bucketCount = static_cast<double>(bucketSize);
        const double bottomMean = bottomSum / bucketCount;
        const double topMean = topSum / bucketCount;
        if (!std::isfinite(bottomMean) || !std::isfinite(topMean)) {
            return std::nullopt;
        }

        const double spread = topMean - bottomMean;
        if (!std::isfinite(spread)) {
            return std::nullopt;
        }

        return spread;
    }
};

[[nodiscard]] std::vector<SignalReturnPair> buildPairsWithSpec(
    const SignalSet& signalSet,
    NumericConstMatrixView closeView,
    const CrossSectionBuildSpec& spec)
{
    const CrossSectionPairBuilder builder(signalSet, closeView);
    return builder.buildFromSpec(spec);
}

[[nodiscard]] bool isValidIcSummaryInput(
    const SignalSet& signalSet,
    NumericConstMatrixView closeView) noexcept
{
    const int32_t timeCount = static_cast<int32_t>(signalSet.dates.size());
    const int32_t instrumentCount = static_cast<int32_t>(signalSet.instruments.size());
    return timeCount >= 2
        && closeView.rowCount >= 2
        && !signalSet.signalIds.empty()
        && CrossSectionPairBuilder::isValidCloseView(closeView, instrumentCount);
}

[[nodiscard]] bool isValidLongShortSummaryInput(
    const SignalSet& signalSet,
    NumericConstMatrixView closeView) noexcept
{
    return isValidIcSummaryInput(signalSet, closeView);
}

template <typename Calculator, typename... Args>
[[nodiscard]] auto invokeCalculator(Args&&... args)
{
    const Calculator calculator;
    return calculator.calculate(std::forward<Args>(args)...);
}

} // namespace

std::optional<double>
AnalysisKernel::calculatePearsonCorrelation(const std::vector<double>& lhs, const std::vector<double>& rhs) const
{
    return invokeCalculator<PearsonCorrelationCalculator>(lhs, rhs);
}

std::optional<double>
AnalysisKernel::calculateSpearmanCorrelation(const std::vector<double>& lhs, const std::vector<double>& rhs) const
{
    return invokeCalculator<SpearmanCorrelationCalculator>(lhs, rhs);
}

std::optional<AnalysisSignificanceMetric>
AnalysisKernel::calculateSignificance(double correlation, int32_t sampleCount) const
{
    return invokeCalculator<SignificanceCalculator>(correlation, sampleCount);
}

std::vector<SignalReturnPair>
AnalysisKernel::buildCrossSectionPairs(const SignalSet& signalSet, NumericConstMatrixView closeView) const
{
    if (closeView.rowCount < kMinimumCorrelationSampleCount) {
        return {};
    }

    const CrossSectionBuildSpec spec =
        buildCrossSectionSpec(kPrimaryTimeIndex, 0, closeView.rowCount - 1);
    return buildPairsWithSpec(signalSet, closeView, spec);
}

std::vector<SignalReturnPair>
AnalysisKernel::buildCrossSectionPairsAt(
    const SignalSet& signalSet,
    NumericConstMatrixView closeView,
    int32_t signalTimeIndex,
    int32_t fromRow,
    int32_t toRow) const
{
    const CrossSectionBuildSpec spec =
        buildCrossSectionSpec(signalTimeIndex, fromRow, toRow);
    return buildPairsWithSpec(signalSet, closeView, spec);
}

IcSeriesSummary
AnalysisKernel::buildIcSeriesSummary(const SignalSet& signalSet, NumericConstMatrixView closeView) const
{
    IcSeriesSummary summary;
    if (!isValidIcSummaryInput(signalSet, closeView)) {
        return summary;
    }

    const IcSeriesSummaryBuilder builder(*this, signalSet, closeView);
    return builder.build();
}

std::optional<double>
AnalysisKernel::calculateLayeredSpread(const std::vector<SignalReturnPair>& pairs) const
{
    return invokeCalculator<LayeredSpreadCalculator>(pairs);
}

std::optional<double>
AnalysisKernel::calculateTurnover(const SignalSet& signalSet) const
{
    return calculateTurnoverSummary(signalSet).value;
}

TurnoverSummary
AnalysisKernel::calculateTurnoverSummary(const SignalSet& signalSet) const
{
    const TurnoverCalculator calculator(signalSet);
    return calculator.buildSummary();
}

LongShortSeriesSummary
AnalysisKernel::buildLongShortSeriesSummary(const SignalSet& signalSet, NumericConstMatrixView closeView) const
{
    LongShortSeriesSummary summary;
    if (!isValidLongShortSummaryInput(signalSet, closeView)) {
        return summary;
    }

    const LongShortSeriesSummaryBuilder builder(*this, signalSet, closeView);
    return builder.build();
}

} // namespace factor::compute::detail
