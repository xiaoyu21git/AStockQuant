#include "RiskParityAllocationStrategy.h"

#include <cmath>
#include <unordered_map>

namespace {

constexpr int kZeroCount = 0;
constexpr int kInvalidTradingDay = -1;
constexpr std::size_t kZeroSize = 0U;
constexpr std::size_t kSingleStep = 1U;
constexpr double kZeroValue = 0.0;
constexpr double kUnitValue = 1.0;
constexpr double kVolatilityEpsilon = 1e-12;

}

namespace domain::strategies {

RiskParityAllocationStrategy::RiskParityAllocationStrategy(
    const StrategyCommonConfig& commonConfig,
    const StrategyMetadata& metadata,
    const RiskParityAllocationStrategySpec& spec)
    : IStrategy(commonConfig, metadata)
    , spec_(spec)
{
    trackedAssets_.reserve(spec_.assets.size());
    for (const SymbolId symbolId : spec_.assets) {
        trackedAssets_.insert(symbolId);
    }
}

StrategyType RiskParityAllocationStrategy::strategyType() const
{
    return StrategyType::RISK_PARITY_ALLOCATION;
}

const RiskParityAllocationStrategySpec& RiskParityAllocationStrategy::spec() const noexcept
{
    return spec_;
}

bool RiskParityAllocationStrategy::isConfigured() const noexcept
{
    return IStrategy::isConfigured()
        && hasUsableParameters();
}

std::vector<RiskParityAllocationMetric> RiskParityAllocationStrategy::computeAllocationMetrics(
    const MarketBarList& bars) const
{
    if (!isConfigured() || bars.empty()) {
        return {};
    }

    std::unordered_map<std::uint32_t, SymbolState> symbolStates;
    symbolStates.reserve(spec_.assets.size());

    std::vector<AllocationSample> samples;
    samples.reserve(spec_.assets.size());
    for (const MarketBar& bar : bars) {
        if (!bar.isValid() || bar.tradingDay <= kInvalidTradingDay || !isTrackedAsset(bar.symbolId)) {
            continue;
        }

        auto [iterator, inserted] = symbolStates.try_emplace(bar.symbolId,
                                                             resolveLookback(spec_.volatilityLookback));
        (void)inserted;
        SymbolState& state = iterator->second;
        if (state.hasPreviousClose) {
            const double currentReturn = computeReturn(bar.closePrice, state.previousClose);
            state.returns.push(currentReturn);
        }

        state.previousClose = bar.closePrice;
        state.hasPreviousClose = true;
    }

    std::vector<std::vector<double>> orderedReturnsByAsset;
    orderedReturnsByAsset.reserve(samples.size());
    const std::size_t lookback = resolveLookback(spec_.volatilityLookback);
    for (const SymbolId symbolId : spec_.assets) {
        const auto iterator = symbolStates.find(symbolId);
        if (iterator == symbolStates.end() || !iterator->second.returns.isReady()) {
            continue;
        }

        samples.push_back(AllocationSample{symbolId,
                                           iterator->second.returns.standardDeviation()});
        orderedReturnsByAsset.push_back(iterator->second.returns.orderedValues());
    }

    if (samples.empty()) {
        return {};
    }

    std::vector<double> rawWeights;
    rawWeights.reserve(samples.size());
    double rawWeightSum = kZeroValue;
    for (const AllocationSample& sample : samples) {
        const double rawWeight = kUnitValue / (sample.volatility > kVolatilityEpsilon
            ? sample.volatility
            : kVolatilityEpsilon);
        rawWeights.push_back(rawWeight);
        rawWeightSum += rawWeight;
    }

    if (rawWeightSum <= kZeroValue) {
        return {};
    }

    std::vector<double> normalizedWeights;
    normalizedWeights.reserve(rawWeights.size());
    for (const double rawWeight : rawWeights) {
        normalizedWeights.push_back(rawWeight / rawWeightSum);
    }

    double scalingFactor = kUnitValue;
    if (spec_.targetVolatility > kZeroValue) {
        const double portfolioVolatility = computePortfolioVolatility(samples,
                                                                      normalizedWeights,
                                                                      orderedReturnsByAsset);
        scalingFactor = spec_.targetVolatility / (portfolioVolatility > kVolatilityEpsilon
            ? portfolioVolatility
            : kVolatilityEpsilon);
    }

    std::vector<RiskParityAllocationMetric> metrics;
    metrics.reserve(samples.size());
    for (std::size_t index = kZeroSize; index < samples.size(); ++index) {
        metrics.push_back(RiskParityAllocationMetric{
            samples[index].symbolId,
            samples[index].volatility,
            rawWeights[index],
            normalizedWeights[index],
            normalizedWeights[index] * scalingFactor});
    }

    return metrics;
}

RiskParityAllocationStrategy::RollingReturnWindow::RollingReturnWindow(std::size_t capacity)
    : buffer_(capacity, kZeroValue)
    , capacity_(capacity)
{
}

void RiskParityAllocationStrategy::RollingReturnWindow::push(double value)
{
    if (capacity_ == kZeroSize) {
        return;
    }

    if (count_ < capacity_) {
        buffer_[count_] = value;
        sum_ += value;
        sumSquares_ += value * value;
        ++count_;
        return;
    }

    const double removedValue = buffer_[cursor_];
    sum_ -= removedValue;
    sumSquares_ -= removedValue * removedValue;
    buffer_[cursor_] = value;
    sum_ += value;
    sumSquares_ += value * value;
    cursor_ = (cursor_ + kSingleStep) % capacity_;
}

bool RiskParityAllocationStrategy::RollingReturnWindow::isReady() const noexcept
{
    return capacity_ > kZeroSize && count_ == capacity_;
}

double RiskParityAllocationStrategy::RollingReturnWindow::standardDeviation() const noexcept
{
    if (count_ == kZeroSize) {
        return kZeroValue;
    }

    const double mean = sum_ / static_cast<double>(count_);
    const double variance = (sumSquares_ / static_cast<double>(count_)) - (mean * mean);
    const double clampedVariance = variance > kZeroValue ? variance : kZeroValue;
    return std::sqrt(clampedVariance);
}

std::vector<double> RiskParityAllocationStrategy::RollingReturnWindow::orderedValues() const
{
    std::vector<double> values;
    values.reserve(count_);
    if (count_ == kZeroSize) {
        return values;
    }

    if (count_ < capacity_) {
        values.insert(values.end(), buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(count_));
        return values;
    }

    for (std::size_t offset = kZeroSize; offset < count_; ++offset) {
        const std::size_t index = (cursor_ + offset) % capacity_;
        values.push_back(buffer_[index]);
    }

    return values;
}

RiskParityAllocationStrategy::SymbolState::SymbolState(std::size_t lookback)
    : returns(lookback)
{
}

std::size_t RiskParityAllocationStrategy::resolveLookback(int lookback) noexcept
{
    return lookback > kZeroCount ? static_cast<std::size_t>(lookback) : kZeroSize;
}

bool RiskParityAllocationStrategy::hasUsableParameters() const noexcept
{
    return !trackedAssets_.empty()
        && spec_.volatilityLookback > kZeroCount
        && spec_.targetVolatility >= kZeroValue;
}

bool RiskParityAllocationStrategy::isTrackedAsset(SymbolId symbolId) const noexcept
{
    return trackedAssets_.find(symbolId) != trackedAssets_.end();
}

double RiskParityAllocationStrategy::computeReturn(double currentClose, double previousClose) noexcept
{
    return (currentClose / previousClose) - kUnitValue;
}

double RiskParityAllocationStrategy::computePortfolioVolatility(
    const std::vector<AllocationSample>& samples,
    const std::vector<double>& normalizedWeights,
    const std::vector<std::vector<double>>& orderedReturnsByAsset) const
{
    if (samples.empty()
        || samples.size() != normalizedWeights.size()
        || samples.size() != orderedReturnsByAsset.size()) {
        return kZeroValue;
    }

    const std::size_t lookback = resolveLookback(spec_.volatilityLookback);
    if (lookback == kZeroSize) {
        return kZeroValue;
    }

    double sum = kZeroValue;
    double sumSquares = kZeroValue;
    for (std::size_t offset = kZeroSize; offset < lookback; ++offset) {
        double weightedReturn = kZeroValue;
        for (std::size_t sampleIndex = kZeroSize; sampleIndex < samples.size(); ++sampleIndex) {
            if (orderedReturnsByAsset[sampleIndex].size() <= offset) {
                return kZeroValue;
            }

            weightedReturn += normalizedWeights[sampleIndex] * orderedReturnsByAsset[sampleIndex][offset];
        }

        sum += weightedReturn;
        sumSquares += weightedReturn * weightedReturn;
    }

    const double sampleCount = static_cast<double>(lookback);
    const double mean = sum / sampleCount;
    const double variance = (sumSquares / sampleCount) - (mean * mean);
    const double clampedVariance = variance > kZeroValue ? variance : kZeroValue;
    return std::sqrt(clampedVariance);
}

} // namespace domain::strategies