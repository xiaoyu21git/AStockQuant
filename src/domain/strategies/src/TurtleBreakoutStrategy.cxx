#include "TurtleBreakoutStrategy.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

constexpr int kZeroCount = 0;
constexpr int kInvalidTradingDay = -1;
constexpr std::size_t kZeroSize = 0U;
constexpr std::size_t kSingleStep = 1U;
constexpr double kZeroValue = 0.0;
constexpr double kMinimumMultiplier = 0.0;

}

namespace domain::strategies {

TurtleBreakoutStrategy::TurtleBreakoutStrategy(
    const StrategyCommonConfig& commonConfig,
    const StrategyMetadata& metadata,
    const TurtleBreakoutStrategySpec& spec)
    : IStrategy(commonConfig, metadata)
    , spec_(spec)
{
}

StrategyType TurtleBreakoutStrategy::strategyType() const
{
    return StrategyType::TURTLE_BREAKOUT;
}

const TurtleBreakoutStrategySpec& TurtleBreakoutStrategy::spec() const noexcept
{
    return spec_;
}

bool TurtleBreakoutStrategy::isConfigured() const noexcept
{
    return IStrategy::isConfigured()
        && hasUsableParameters();
}

std::vector<TurtleBreakoutMetric> TurtleBreakoutStrategy::computeBreakoutMetrics(const MarketBarList& bars) const
{
    if (!isConfigured() || bars.empty()) {
        return {};
    }

    const std::size_t channelPeriod = resolvePeriod(spec_.channelPeriod);
    std::unordered_map<std::uint32_t, SymbolState> symbolStates;
    symbolStates.reserve(bars.size());

    std::vector<TurtleBreakoutMetric> metrics;
    metrics.reserve(bars.size());
    for (const MarketBar& bar : bars) {
        if (!bar.isValid() || bar.tradingDay <= kInvalidTradingDay) {
            continue;
        }

        auto [iterator, inserted] = symbolStates.try_emplace(bar.symbolId, resolvePeriod(spec_.atrPeriod));
        (void)inserted;
        SymbolState& state = iterator->second;
        const bool hasReadyChannel = state.processedBars >= channelPeriod
            && !state.upperChannelCandidates.empty()
            && !state.lowerChannelCandidates.empty();

        if (state.hasPreviousClose) {
            const double trueRange = computeTrueRange(bar, state.previousClose);
            state.atrWindow.push(trueRange);
            if (hasReadyChannel && state.atrWindow.isReady()) {
                const double upperChannel = state.upperChannelCandidates.front().value;
                const double lowerChannel = state.lowerChannelCandidates.front().value;
                const double atr = state.atrWindow.average();
                metrics.push_back(TurtleBreakoutMetric{
                    bar.symbolId,
                    bar.closePrice,
                    upperChannel,
                    lowerChannel,
                    atr,
                    upperChannel + (spec_.breakoutMultiplier * atr),
                    bar.tradingDay});
            }
        }

        state.previousClose = bar.closePrice;
        state.hasPreviousClose = true;
        appendChannelHigh(state, bar.highPrice, channelPeriod);
        appendChannelLow(state, bar.lowPrice, channelPeriod);
        ++state.processedBars;
    }

    return metrics;
}

TurtleBreakoutStrategy::RollingAverageWindow::RollingAverageWindow(std::size_t capacity)
    : buffer_(capacity, kZeroValue)
    , capacity_(capacity)
{
}

void TurtleBreakoutStrategy::RollingAverageWindow::push(double value)
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

bool TurtleBreakoutStrategy::RollingAverageWindow::isReady() const noexcept
{
    return capacity_ > kZeroSize && count_ == capacity_;
}

double TurtleBreakoutStrategy::RollingAverageWindow::average() const noexcept
{
    if (count_ == kZeroSize) {
        return kZeroValue;
    }

    return sum_ / static_cast<double>(count_);
}

TurtleBreakoutStrategy::SymbolState::SymbolState(std::size_t atrPeriod)
    : atrWindow(atrPeriod)
{
}

std::size_t TurtleBreakoutStrategy::resolvePeriod(int period) noexcept
{
    return period > kZeroCount ? static_cast<std::size_t>(period) : kZeroSize;
}

bool TurtleBreakoutStrategy::hasUsableParameters() const noexcept
{
    return spec_.channelPeriod > kZeroCount
        && spec_.atrPeriod > kZeroCount
        && spec_.breakoutMultiplier >= kMinimumMultiplier;
}

double TurtleBreakoutStrategy::computeTrueRange(const MarketBar& bar, double previousClose) noexcept
{
    const double intradayRange = bar.highPrice - bar.lowPrice;
    const double highGap = std::abs(bar.highPrice - previousClose);
    const double lowGap = std::abs(bar.lowPrice - previousClose);
    return std::max(intradayRange, std::max(highGap, lowGap));
}

void TurtleBreakoutStrategy::appendChannelHigh(SymbolState& state, double value, std::size_t channelPeriod)
{
    while (!state.upperChannelCandidates.empty()
           && state.upperChannelCandidates.back().value <= value) {
        state.upperChannelCandidates.pop_back();
    }

    state.upperChannelCandidates.push_back(IndexedValue{state.processedBars, value});

    const std::size_t minimumIndex = state.processedBars >= channelPeriod
        ? state.processedBars - channelPeriod + kSingleStep
        : kZeroSize;
    while (!state.upperChannelCandidates.empty()
           && state.upperChannelCandidates.front().index < minimumIndex) {
        state.upperChannelCandidates.pop_front();
    }
}

void TurtleBreakoutStrategy::appendChannelLow(SymbolState& state, double value, std::size_t channelPeriod)
{
    while (!state.lowerChannelCandidates.empty()
           && state.lowerChannelCandidates.back().value >= value) {
        state.lowerChannelCandidates.pop_back();
    }

    state.lowerChannelCandidates.push_back(IndexedValue{state.processedBars, value});

    const std::size_t minimumIndex = state.processedBars >= channelPeriod
        ? state.processedBars - channelPeriod + kSingleStep
        : kZeroSize;
    while (!state.lowerChannelCandidates.empty()
           && state.lowerChannelCandidates.front().index < minimumIndex) {
        state.lowerChannelCandidates.pop_front();
    }
}

} // namespace domain::strategies