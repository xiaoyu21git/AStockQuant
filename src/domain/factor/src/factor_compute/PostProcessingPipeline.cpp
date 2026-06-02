#include "factor_compute/PostProcessingPipeline.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace factor::compute {

namespace {

constexpr uint8_t kMissingMaskValue = 1U;
constexpr double kZeroZScoreValue = 0.0;
constexpr size_t kDefaultStepCount = 2U;

struct SliceStatistics final {
    double mean{0.0};
    double stdDev{0.0};
};

bool checkedMultiply(size_t lhs, size_t rhs, size_t& product)
{
    if (rhs != 0U && lhs > (std::numeric_limits<size_t>::max() / rhs)) {
        return false;
    }
    product = lhs * rhs;
    return true;
}

bool checkedAdd(size_t lhs, size_t rhs, size_t& sum)
{
    if (lhs > (std::numeric_limits<size_t>::max() - rhs)) {
        return false;
    }
    sum = lhs + rhs;
    return true;
}

std::optional<FactorError> validateTensorShape(const SignalTensorBuffer& tensor)
{
    if (tensor.timeCount <= 0 || tensor.instrumentCount <= 0 || tensor.factorCount <= 0) {
        return FactorError::InternalError;
    }

    size_t expectedCount = 0U;
    const size_t timeCount = static_cast<size_t>(tensor.timeCount);
    const size_t instrumentCount = static_cast<size_t>(tensor.instrumentCount);
    const size_t factorCount = static_cast<size_t>(tensor.factorCount);
    if (!checkedMultiply(timeCount, instrumentCount, expectedCount)
        || !checkedMultiply(expectedCount, factorCount, expectedCount)) {
        return FactorError::MemoryExceeded;
    }

    if (tensor.values.size() != expectedCount || tensor.mask.size() != expectedCount) {
        return FactorError::InternalError;
    }

    return std::nullopt;
}

std::optional<size_t> calculateTensorByteFootprint(const SignalTensorBuffer& tensor)
{
    size_t valueBytes = 0U;
    size_t maskBytes = 0U;
    if (!checkedMultiply(tensor.values.size(), sizeof(double), valueBytes)) {
        return std::nullopt;
    }
    if (!checkedMultiply(tensor.mask.size(), sizeof(uint8_t), maskBytes)) {
        return std::nullopt;
    }

    size_t totalBytes = 0U;
    if (!checkedAdd(valueBytes, maskBytes, totalBytes)) {
        return std::nullopt;
    }

    return totalBytes;
}

bool isTimeoutExceeded(
    const std::chrono::steady_clock::time_point& startedAt,
    int64_t timeoutMilliseconds)
{
    const auto elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt)
                                       .count();
    return elapsedMilliseconds >= timeoutMilliseconds;
}

size_t buildSliceBaseOffset(
    int32_t timeIndex,
    int32_t factorIndex,
    int32_t instrumentCount,
    int32_t factorCount)
{
    return static_cast<size_t>(timeIndex) * static_cast<size_t>(instrumentCount) * static_cast<size_t>(factorCount)
        + static_cast<size_t>(factorIndex);
}

size_t buildSliceFlatIndex(size_t sliceBaseOffset, int32_t instrumentIndex, int32_t factorCount)
{
    return sliceBaseOffset + static_cast<size_t>(instrumentIndex) * static_cast<size_t>(factorCount);
}

template <typename TElementCallback>
void forEachValidElementInSlice(
    SignalTensorBuffer& tensor,
    int32_t timeIndex,
    int32_t factorIndex,
    TElementCallback callback)
{
    const size_t sliceBaseOffset = buildSliceBaseOffset(
        timeIndex,
        factorIndex,
        tensor.instrumentCount,
        tensor.factorCount);

    for (int32_t instrumentIndex = 0; instrumentIndex < tensor.instrumentCount; ++instrumentIndex) {
        const size_t flat = buildSliceFlatIndex(sliceBaseOffset, instrumentIndex, tensor.factorCount);
        if (tensor.mask[flat] == kMissingMaskValue) {
            continue;
        }
        callback(flat);
    }
}

template <typename TSliceCallback>
void forEachSlice(const SignalTensorBuffer& tensor, TSliceCallback callback)
{
    for (int32_t timeIndex = 0; timeIndex < tensor.timeCount; ++timeIndex) {
        for (int32_t factorIndex = 0; factorIndex < tensor.factorCount; ++factorIndex) {
            callback(timeIndex, factorIndex);
        }
    }
}

std::optional<SliceStatistics> computeSliceStatistics(
    const SignalTensorBuffer& tensor,
    int32_t timeIndex,
    int32_t factorIndex,
    int32_t minimumValidSampleCount)
{
    int32_t validCount = 0;
    double mean = 0.0;
    double m2 = 0.0;
    const size_t sliceBaseOffset = buildSliceBaseOffset(
        timeIndex,
        factorIndex,
        tensor.instrumentCount,
        tensor.factorCount);

    for (int32_t instrumentIndex = 0; instrumentIndex < tensor.instrumentCount; ++instrumentIndex) {
        const size_t flat = buildSliceFlatIndex(sliceBaseOffset, instrumentIndex, tensor.factorCount);
        if (tensor.mask[flat] == kMissingMaskValue) {
            continue;
        }

        const double value = tensor.values[flat];
        ++validCount;
        const double delta = value - mean;
        mean += delta / static_cast<double>(validCount);
        const double delta2 = value - mean;
        m2 += delta * delta2;
    }

    if (validCount < minimumValidSampleCount) {
        return std::nullopt;
    }

    const double count = static_cast<double>(validCount);
    double variance = m2 / count;
    if (variance < 0.0) {
        variance = 0.0;
    }

    SliceStatistics stats;
    stats.mean = mean;
    stats.stdDev = std::sqrt(variance);
    return stats;
}

void winsorizeByStdBand(
    SignalTensorBuffer& tensor,
    int32_t timeIndex,
    int32_t factorIndex,
    const SliceStatistics& stats,
    const PostProcessingConfig& config)
{
    const double lowerBound = stats.mean - config.winsorizeStdBand * stats.stdDev;
    const double upperBound = stats.mean + config.winsorizeStdBand * stats.stdDev;
    forEachValidElementInSlice(tensor, timeIndex, factorIndex, [&](size_t flat) {
        const double value = tensor.values[flat];
        if (value < lowerBound) {
            tensor.values[flat] = lowerBound;
        } else if (value > upperBound) {
            tensor.values[flat] = upperBound;
        }
    });
}

void zScoreNormalize(
    SignalTensorBuffer& tensor,
    int32_t timeIndex,
    int32_t factorIndex,
    const SliceStatistics& stats,
    const PostProcessingConfig& config)
{
    if (stats.stdDev <= config.stdEpsilon) {
        forEachValidElementInSlice(tensor, timeIndex, factorIndex, [&](size_t flat) {
            tensor.values[flat] = kZeroZScoreValue;
        });
        return;
    }

    const double inverseStdDev = 1.0 / stats.stdDev;
    forEachValidElementInSlice(tensor, timeIndex, factorIndex, [&](size_t flat) {
        tensor.values[flat] = (tensor.values[flat] - stats.mean) * inverseStdDev;
    });
}

class WinsorizeByStdBandStep final : public IPostProcessingStep {
public:
    void apply(SignalTensorBuffer& tensor, const PostProcessingConfig& config) const override
    {
        forEachSlice(tensor, [&](int32_t timeIndex, int32_t factorIndex) {
            const std::optional<SliceStatistics> stats = computeSliceStatistics(
                tensor,
                timeIndex,
                factorIndex,
                config.minimumValidSampleCount);
            if (!stats) {
                return;
            }

            if (stats->stdDev > config.stdEpsilon) {
                winsorizeByStdBand(tensor, timeIndex, factorIndex, *stats, config);
            }
        });
    }
};

class ZScoreNormalizeStep final : public IPostProcessingStep {
public:
    void apply(SignalTensorBuffer& tensor, const PostProcessingConfig& config) const override
    {
        forEachSlice(tensor, [&](int32_t timeIndex, int32_t factorIndex) {
            const std::optional<SliceStatistics> stats = computeSliceStatistics(
                tensor,
                timeIndex,
                factorIndex,
                config.minimumValidSampleCount);
            if (!stats) {
                return;
            }

            zScoreNormalize(tensor, timeIndex, factorIndex, *stats, config);
        });
    }
};

std::vector<std::unique_ptr<const IPostProcessingStep>> buildDefaultSteps()
{
    std::vector<std::unique_ptr<const IPostProcessingStep>> steps;
    steps.reserve(kDefaultStepCount);
    steps.push_back(std::make_unique<WinsorizeByStdBandStep>());
    steps.push_back(std::make_unique<ZScoreNormalizeStep>());
    return steps;
}

} // namespace

std::vector<std::unique_ptr<const IPostProcessingStep>>
DefaultPostProcessingStepFactory::createDefaultSteps() const
{
    return buildDefaultSteps();
}

PostProcessingPipeline::PostProcessingPipeline()
    : PostProcessingPipeline(DefaultPostProcessingStepFactory())
{
}

PostProcessingPipeline::PostProcessingPipeline(const IPostProcessingStepFactory& stepFactory) noexcept
    : steps_(stepFactory.createDefaultSteps())
{
}

PostProcessingPipeline::PostProcessingPipeline(std::vector<std::unique_ptr<const IPostProcessingStep>> steps) noexcept
    : steps_(std::move(steps))
{
}

FactorResult<SignalTensorBuffer>
PostProcessingPipeline::run(SignalTensorBuffer rawTensor, const GenerateSpec& spec) const
{
    if (!spec.isValid()) {
        return FactorResult<SignalTensorBuffer>::failure(FactorError::InvalidUniverse);
    }

    const std::optional<FactorError> tensorShapeError = validateTensorShape(rawTensor);
    if (tensorShapeError) {
        return FactorResult<SignalTensorBuffer>::failure(*tensorShapeError);
    }

    const std::optional<size_t> tensorBytes = calculateTensorByteFootprint(rawTensor);
    if (!tensorBytes) {
        return FactorResult<SignalTensorBuffer>::failure(FactorError::MemoryExceeded);
    }
    if (*tensorBytes > spec.runtimeBudget.memoryLimitBytes) {
        return FactorResult<SignalTensorBuffer>::failure(FactorError::MemoryExceeded);
    }

    if (steps_.empty()) {
        return FactorResult<SignalTensorBuffer>::failure(FactorError::InternalError);
    }

    const auto startedAt = std::chrono::steady_clock::now();
    if (isTimeoutExceeded(startedAt, spec.runtimeBudget.timeoutMilliseconds)) {
        return FactorResult<SignalTensorBuffer>::failure(FactorError::Timeout);
    }

    for (const std::unique_ptr<const IPostProcessingStep>& step : steps_) {
        if (!step) {
            return FactorResult<SignalTensorBuffer>::failure(FactorError::InternalError);
        }

        if (isTimeoutExceeded(startedAt, spec.runtimeBudget.timeoutMilliseconds)) {
            return FactorResult<SignalTensorBuffer>::failure(FactorError::Timeout);
        }

        step->apply(rawTensor, spec.postProcessingConfig);

        if (isTimeoutExceeded(startedAt, spec.runtimeBudget.timeoutMilliseconds)) {
            return FactorResult<SignalTensorBuffer>::failure(FactorError::Timeout);
        }
    }

    return FactorResult<SignalTensorBuffer>::success(std::move(rawTensor));
}

} // namespace factor::compute
