#include "BollingerBandMeanReversionStrategy.h"

#include <cmath>
#include <unordered_map>

namespace {

constexpr int kZeroCount = 0;
constexpr int kInvalidTradingDay = -1;
constexpr std::size_t kZeroSize = 0U;
constexpr std::size_t kSingleStep = 1U;
constexpr double kZeroValue = 0.0;
constexpr double kMinimumThreshold = 0.0;
constexpr double kStandardDeviationEpsilon = 1e-12;

}

namespace domain::strategies {

BollingerBandMeanReversionStrategy::BollingerBandMeanReversionStrategy(
    const StrategyCommonConfig& commonConfig,
    const StrategyMetadata& metadata,
    const BollingerBandMeanReversionStrategySpec& spec)
    : IStrategy(commonConfig, metadata)
    , spec_(spec)
{
}

StrategyType BollingerBandMeanReversionStrategy::strategyType() const
{
    return StrategyType::BOLLINGER_BAND_MEAN_REVERSION;
}

const BollingerBandMeanReversionStrategySpec& BollingerBandMeanReversionStrategy::spec() const noexcept
{
    return spec_;
}

bool BollingerBandMeanReversionStrategy::isConfigured() const noexcept
{
    return IStrategy::isConfigured()
        && spec_.period > kZeroCount
        && hasUsableThresholds();
}

std::vector<BollingerBandMetric> BollingerBandMeanReversionStrategy::computeBandMetrics(
    const MarketBarList& bars) const
{
    if (!isConfigured() || bars.empty()) {
        return {};
    }

    std::unordered_map<std::uint32_t, SymbolState> symbolStates;
    symbolStates.reserve(bars.size());

    std::vector<BollingerBandMetric> metrics;
    metrics.reserve(bars.size());
    for (const MarketBar& bar : bars) {
        if (!bar.isValid() || bar.tradingDay <= kInvalidTradingDay) {
            continue;
        }

        auto [iterator, inserted] = symbolStates.try_emplace(bar.symbolId, resolvePeriod(spec_.period));
        (void)inserted;
        SymbolState& state = iterator->second;
        state.window.push(bar.closePrice);
        if (!state.window.isReady()) {
            continue;
        }

        const double middle = state.window.mean();
        const double standardDeviation = state.window.standardDeviation();
        const double bandOffset = spec_.standardDeviationMultiplier * standardDeviation;
        metrics.push_back(BollingerBandMetric{
            bar.symbolId,
            bar.closePrice,
            middle,
            middle + bandOffset,
            middle - bandOffset,
            standardDeviation,
            computeZScore(state, bar.closePrice),
            bar.tradingDay});
    }

    return metrics;
}

BollingerBandMeanReversionStrategy::RollingStatisticsWindow::RollingStatisticsWindow(std::size_t capacity)
    : buffer_(capacity, kZeroValue)
    , capacity_(capacity)
{
}

void BollingerBandMeanReversionStrategy::RollingStatisticsWindow::push(double value)
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

bool BollingerBandMeanReversionStrategy::RollingStatisticsWindow::isReady() const noexcept
{
    return capacity_ > kZeroSize && count_ == capacity_;
}

double BollingerBandMeanReversionStrategy::RollingStatisticsWindow::mean() const noexcept
{
    if (count_ == kZeroSize) {
        return kZeroValue;
    }

    return sum_ / static_cast<double>(count_);
}

double BollingerBandMeanReversionStrategy::RollingStatisticsWindow::standardDeviation() const noexcept
{
    if (count_ == kZeroSize) {
        return kZeroValue;
    }

    const double currentMean = mean();
    const double variance = (sumSquares_ / static_cast<double>(count_)) - (currentMean * currentMean);
    const double clampedVariance = variance > kZeroValue ? variance : kZeroValue;
    return std::sqrt(clampedVariance);
}

BollingerBandMeanReversionStrategy::SymbolState::SymbolState(std::size_t period)
    : window(period)
{
}

std::size_t BollingerBandMeanReversionStrategy::resolvePeriod(int period) noexcept
{
    return period > kZeroCount ? static_cast<std::size_t>(period) : kZeroSize;
}

bool BollingerBandMeanReversionStrategy::hasUsableThresholds() const noexcept
{
    return spec_.standardDeviationMultiplier > kMinimumThreshold;
}

double BollingerBandMeanReversionStrategy::computeZScore(const SymbolState& state, double closePrice) const noexcept
{
    const double middle = state.window.mean();
    const double standardDeviation = state.window.standardDeviation();
    const double denominator = standardDeviation > kStandardDeviationEpsilon
        ? standardDeviation
        : kStandardDeviationEpsilon;
    return (closePrice - middle) / denominator;
}

} // namespace domain::strategies