#include "VolatilitySpreadStrategy.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

constexpr int kZeroCount = 0;
constexpr int kInvalidTradingDay = -1;
constexpr std::size_t kZeroSize = 0U;
constexpr std::size_t kSingleStep = 1U;
constexpr double kZeroValue = 0.0;
constexpr double kVolatilityEpsilon = 1e-12;
constexpr double kAnnualizationTradingDays = 252.0;

struct UnderlyingCloseSnapshot final {
    int tradingDay{-1};
    double closePrice{0.0};
    bool hasClose{false};
};

}

namespace domain::strategies {

bool VolatilitySpreadStrategy::isConfigured() const noexcept
{
    return IStrategy::isConfigured()
        && hasUsableParameters();
}

std::vector<VolatilitySpreadMetric> VolatilitySpreadStrategy::computeVolatilityMetrics(
    const MarketBarList& bars,
    const std::vector<ImpliedVolatilitySnapshot>& impliedVolatilities) const
{
    if (!isConfigured() || bars.empty() || impliedVolatilities.empty()) {
        return {};
    }

    std::unordered_map<int, std::size_t> closeTradingDayIndex;
    closeTradingDayIndex.reserve(bars.size());
    std::vector<UnderlyingCloseSnapshot> closeByTradingDay;
    closeByTradingDay.reserve(bars.size());
    for (const MarketBar& bar : bars) {
        if (!bar.isValid() || bar.symbolId != spec_.underlying) {
            continue;
        }

        auto [iterator, inserted] = closeTradingDayIndex.try_emplace(bar.tradingDay, closeByTradingDay.size());
        if (inserted) {
            closeByTradingDay.push_back(UnderlyingCloseSnapshot{bar.tradingDay});
        }

        UnderlyingCloseSnapshot& snapshot = closeByTradingDay[iterator->second];
        snapshot.closePrice = bar.closePrice;
        snapshot.hasClose = true;
    }

    if (closeByTradingDay.empty()) {
        return {};
    }

    std::sort(closeByTradingDay.begin(),
              closeByTradingDay.end(),
              [](const UnderlyingCloseSnapshot& left, const UnderlyingCloseSnapshot& right) {
                  return left.tradingDay < right.tradingDay;
              });

    std::unordered_map<int, double> impliedVolatilityByTradingDay;
    impliedVolatilityByTradingDay.reserve(impliedVolatilities.size());
    for (const ImpliedVolatilitySnapshot& snapshot : impliedVolatilities) {
        if (snapshot.underlying != spec_.underlying || snapshot.tradingDay <= kInvalidTradingDay) {
            continue;
        }

        impliedVolatilityByTradingDay[snapshot.tradingDay] = snapshot.impliedVolatility;
    }

    RollingReturnWindow returnWindow(resolveLookback(spec_.historicalVolatilityWindow));
    std::vector<VolatilitySpreadMetric> metrics;
    metrics.reserve(closeByTradingDay.size());
    double previousClose = kZeroValue;
    bool hasPreviousClose = false;
    for (const UnderlyingCloseSnapshot& snapshot : closeByTradingDay) {
        const int tradingDay = snapshot.tradingDay;
        if (!snapshot.hasClose || tradingDay <= kInvalidTradingDay) {
            continue;
        }

        if (hasPreviousClose) {
            returnWindow.push(std::log(snapshot.closePrice / previousClose));
        }
        previousClose = snapshot.closePrice;
        hasPreviousClose = true;
        if (!returnWindow.isReady()) {
            continue;
        }

        const auto impliedIt = impliedVolatilityByTradingDay.find(tradingDay);
        if (impliedIt == impliedVolatilityByTradingDay.end()) {
            continue;
        }

        const double historicalVolatility = std::sqrt(kAnnualizationTradingDays) * returnWindow.standardDeviation();
        metrics.push_back(VolatilitySpreadMetric{
            historicalVolatility,
            impliedIt->second,
            impliedIt->second - historicalVolatility,
            tradingDay});
    }

    return metrics;
}

VolatilitySpreadStrategy::RollingReturnWindow::RollingReturnWindow(std::size_t capacity)
    : buffer_(capacity, kZeroValue)
    , capacity_(capacity)
{
}

void VolatilitySpreadStrategy::RollingReturnWindow::push(double value)
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

bool VolatilitySpreadStrategy::RollingReturnWindow::isReady() const noexcept
{
    return capacity_ > kZeroSize && count_ == capacity_;
}

double VolatilitySpreadStrategy::RollingReturnWindow::standardDeviation() const noexcept
{
    if (count_ == kZeroSize) {
        return kZeroValue;
    }

    const double mean = sum_ / static_cast<double>(count_);
    const double variance = (sumSquares_ / static_cast<double>(count_)) - (mean * mean);
    const double clampedVariance = variance > kZeroValue ? variance : kZeroValue;
    return std::sqrt(clampedVariance > kVolatilityEpsilon ? clampedVariance : kZeroValue);
}

bool VolatilitySpreadStrategy::hasUsableParameters() const noexcept
{
    return spec_.underlying != 0
        && spec_.historicalVolatilityWindow > kZeroCount
        && spec_.entrySpreadUpper > spec_.entrySpreadLower;
}

std::size_t VolatilitySpreadStrategy::resolveLookback(int lookback) noexcept
{
    return lookback > kZeroCount ? static_cast<std::size_t>(lookback) : kZeroSize;
}

} // namespace domain::strategies