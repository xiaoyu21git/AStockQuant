#include "DoubleMovingAverageStrategy.h"

#include <unordered_map>

namespace {

constexpr int kZeroCount = 0;
constexpr int kInvalidTradingDay = -1;
constexpr std::size_t kZeroSize = 0U;
constexpr std::size_t kSingleStep = 1U;
constexpr double kZeroValue = 0.0;

}

namespace domain::strategies {

DoubleMovingAverageStrategy::DoubleMovingAverageStrategy(
    const StrategyCommonConfig& commonConfig,
    const StrategyMetadata& metadata,
    const DoubleMovingAverageStrategySpec& spec)
    : IStrategy(commonConfig, metadata)
    , spec_(spec)
{
}

StrategyType DoubleMovingAverageStrategy::strategyType() const
{
    return StrategyType::DOUBLE_MOVING_AVERAGE;
}

const DoubleMovingAverageStrategySpec& DoubleMovingAverageStrategy::spec() const noexcept
{
    return spec_;
}

bool DoubleMovingAverageStrategy::isConfigured() const noexcept
{
    return IStrategy::isConfigured()
        && hasUsablePeriods();
}

std::vector<DoubleMovingAverageMetric> DoubleMovingAverageStrategy::computeMovingAverageMetrics(
    const MarketBarList& bars) const
{
    if (!isConfigured() || bars.empty()) {
        return {};
    }

    std::unordered_map<std::uint32_t, SymbolState> symbolStates;
    symbolStates.reserve(bars.size());

    std::vector<DoubleMovingAverageMetric> metrics;
    metrics.reserve(bars.size());
    for (const MarketBar& bar : bars) {
        if (!bar.isValid() || bar.tradingDay <= kInvalidTradingDay) {
            continue;
        }

        auto [iterator, inserted] = symbolStates.try_emplace(bar.symbolId,
                                                             resolvePeriod(spec_.fastPeriod),
                                                             resolvePeriod(spec_.slowPeriod));
        (void)inserted;
        SymbolState& state = iterator->second;

        const double selectedPrice = selectPrice(bar);
        state.fastWindow.push(selectedPrice);
        state.slowWindow.push(selectedPrice);
        if (!state.fastWindow.isReady() || !state.slowWindow.isReady()) {
            continue;
        }

        const double fastAverage = state.fastWindow.average();
        const double slowAverage = state.slowWindow.average();
        metrics.push_back(DoubleMovingAverageMetric{
            bar.symbolId,
            selectedPrice,
            fastAverage,
            slowAverage,
            fastAverage - slowAverage,
            bar.tradingDay});
    }

    return metrics;
}

DoubleMovingAverageStrategy::RollingAverageWindow::RollingAverageWindow(std::size_t capacity)
    : buffer_(capacity, kZeroValue)
    , capacity_(capacity)
{
}

void DoubleMovingAverageStrategy::RollingAverageWindow::push(double value)
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

bool DoubleMovingAverageStrategy::RollingAverageWindow::isReady() const noexcept
{
    return capacity_ > kZeroSize && count_ == capacity_;
}

double DoubleMovingAverageStrategy::RollingAverageWindow::average() const noexcept
{
    if (count_ == kZeroSize) {
        return kZeroValue;
    }

    return sum_ / static_cast<double>(count_);
}

DoubleMovingAverageStrategy::SymbolState::SymbolState(std::size_t fastPeriod, std::size_t slowPeriod)
    : fastWindow(fastPeriod)
    , slowWindow(slowPeriod)
{
}

std::size_t DoubleMovingAverageStrategy::resolvePeriod(int period) noexcept
{
    return period > kZeroCount ? static_cast<std::size_t>(period) : kZeroSize;
}

bool DoubleMovingAverageStrategy::hasUsablePeriods() const noexcept
{
    return spec_.fastPeriod > kZeroCount
        && spec_.slowPeriod > kZeroCount
        && spec_.fastPeriod < spec_.slowPeriod;
}

double DoubleMovingAverageStrategy::selectPrice(const MarketBar& bar) const noexcept
{
    switch (spec_.priceField) {
    case TechnicalPriceType::OPEN:
        return bar.openPrice;
    case TechnicalPriceType::HIGH:
        return bar.highPrice;
    case TechnicalPriceType::LOW:
        return bar.lowPrice;
    case TechnicalPriceType::CLOSE:
    case TechnicalPriceType::UNKNOWN:
        return bar.closePrice;
    }

    return bar.closePrice;
}

} // namespace domain::strategies