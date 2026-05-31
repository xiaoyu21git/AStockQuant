#include "RsiMeanReversionStrategy.h"

#include <unordered_map>

namespace {

constexpr int kZeroCount = 0;
constexpr int kInvalidTradingDay = -1;
constexpr std::size_t kZeroSize = 0U;
constexpr std::size_t kSingleStep = 1U;
constexpr double kZeroValue = 0.0;
constexpr double kUnitValue = 1.0;
constexpr double kMaximumRsi = 100.0;
constexpr double kRsiEpsilon = 1e-12;

}

namespace domain::strategies {

RsiMeanReversionStrategy::RsiMeanReversionStrategy(
    const StrategyCommonConfig& commonConfig,
    const StrategyMetadata& metadata,
    const RsiMeanReversionStrategySpec& spec)
    : IStrategy(commonConfig, metadata)
    , spec_(spec)
{
}

StrategyType RsiMeanReversionStrategy::strategyType() const
{
    return StrategyType::RSI_MEAN_REVERSION;
}

const RsiMeanReversionStrategySpec& RsiMeanReversionStrategy::spec() const noexcept
{
    return spec_;
}

bool RsiMeanReversionStrategy::isConfigured() const noexcept
{
    return IStrategy::isConfigured()
        && spec_.period > kZeroCount;
}

std::vector<RsiMetric> RsiMeanReversionStrategy::computeRsiMetrics(const MarketBarList& bars) const
{
    if (!isConfigured() || bars.empty()) {
        return {};
    }

    std::unordered_map<std::uint32_t, SymbolState> symbolStates;
    symbolStates.reserve(bars.size());

    std::vector<RsiMetric> metrics;
    metrics.reserve(bars.size());
    for (const MarketBar& bar : bars) {
        if (!bar.isValid() || bar.tradingDay <= kInvalidTradingDay) {
            continue;
        }

        auto [iterator, inserted] = symbolStates.try_emplace(bar.symbolId, resolvePeriod(spec_.period));
        (void)inserted;
        SymbolState& state = iterator->second;
        if (state.hasPreviousClose) {
            const double delta = bar.closePrice - state.previousClose;
            state.gains.push(delta > kZeroValue ? delta : kZeroValue);
            state.losses.push(delta < kZeroValue ? -delta : kZeroValue);
            if (state.gains.isReady() && state.losses.isReady()) {
                metrics.push_back(RsiMetric{
                    bar.symbolId,
                    bar.closePrice,
                    state.gains.average(),
                    state.losses.average(),
                    computeRsi(state),
                    bar.tradingDay});
            }
        }

        state.previousClose = bar.closePrice;
        state.hasPreviousClose = true;
    }

    return metrics;
}

RsiMeanReversionStrategy::RollingAverageWindow::RollingAverageWindow(std::size_t capacity)
    : buffer_(capacity, kZeroValue)
    , capacity_(capacity)
{
}

void RsiMeanReversionStrategy::RollingAverageWindow::push(double value)
{
    if (capacity_ == kZeroSize) {
        return;
    }

    if (count_ < capacity_) {
        buffer_[count_] = value;
        sum_ += value;
        ++count_;
        return;
    }

    sum_ -= buffer_[cursor_];
    buffer_[cursor_] = value;
    sum_ += value;
    cursor_ = (cursor_ + kSingleStep) % capacity_;
}

bool RsiMeanReversionStrategy::RollingAverageWindow::isReady() const noexcept
{
    return capacity_ > kZeroSize && count_ == capacity_;
}

double RsiMeanReversionStrategy::RollingAverageWindow::average() const noexcept
{
    if (count_ == kZeroSize) {
        return kZeroValue;
    }

    return sum_ / static_cast<double>(count_);
}

RsiMeanReversionStrategy::SymbolState::SymbolState(std::size_t period)
    : gains(period)
    , losses(period)
{
}

std::size_t RsiMeanReversionStrategy::resolvePeriod(int period) noexcept
{
    return period > kZeroCount ? static_cast<std::size_t>(period) : kZeroSize;
}

double RsiMeanReversionStrategy::computeRsi(const SymbolState& state) const noexcept
{
    const double averageGain = state.gains.average();
    const double averageLoss = state.losses.average();
    const double relativeStrength = averageGain / (averageLoss > kRsiEpsilon ? averageLoss : kRsiEpsilon);
    return kMaximumRsi - (kMaximumRsi / (kUnitValue + relativeStrength));
}

} // namespace domain::strategies