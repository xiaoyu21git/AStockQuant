#include "StatisticalPairTradingStrategy.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

constexpr int kZeroCount = 0;
constexpr int kInvalidTradingDay = -1;
constexpr std::size_t kZeroSize = 0U;
constexpr std::size_t kSingleStep = 1U;
constexpr double kZeroValue = 0.0;
constexpr double kSpreadEpsilon = 1e-12;

struct PairCloseSnapshot final {
    int tradingDay{-1};
    double firstClose{0.0};
    double secondClose{0.0};
    bool hasFirstClose{false};
    bool hasSecondClose{false};
};

}

namespace domain::strategies {

bool StatisticalPairTradingStrategy::isConfigured() const noexcept
{
    return IStrategy::isConfigured()
        && hasUsableParameters();
}

std::vector<StatisticalPairTradingMetric> StatisticalPairTradingStrategy::computeSpreadMetrics(const MarketBarList& bars) const
{
    if (!isConfigured() || bars.empty()) {
        return {};
    }

    std::unordered_map<int, std::size_t> tradingDayIndex;
    tradingDayIndex.reserve(bars.size());
    std::vector<PairCloseSnapshot> closeByTradingDay;
    closeByTradingDay.reserve(bars.size());
    for (const MarketBar& bar : bars) {
        if (!bar.isValid()) {
            continue;
        }

        if (bar.symbolId == spec_.tradingPair.first) {
            auto [iterator, inserted] = tradingDayIndex.try_emplace(bar.tradingDay, closeByTradingDay.size());
            if (inserted) {
                closeByTradingDay.push_back(PairCloseSnapshot{bar.tradingDay});
            }

            PairCloseSnapshot& snapshot = closeByTradingDay[iterator->second];
            snapshot.firstClose = bar.closePrice;
            snapshot.hasFirstClose = true;
        } else if (bar.symbolId == spec_.tradingPair.second) {
            auto [iterator, inserted] = tradingDayIndex.try_emplace(bar.tradingDay, closeByTradingDay.size());
            if (inserted) {
                closeByTradingDay.push_back(PairCloseSnapshot{bar.tradingDay});
            }

            PairCloseSnapshot& snapshot = closeByTradingDay[iterator->second];
            snapshot.secondClose = bar.closePrice;
            snapshot.hasSecondClose = true;
        }
    }

    if (closeByTradingDay.empty()) {
        return {};
    }

    std::sort(closeByTradingDay.begin(),
              closeByTradingDay.end(),
              [](const PairCloseSnapshot& left, const PairCloseSnapshot& right) {
                  return left.tradingDay < right.tradingDay;
              });

    RollingSpreadWindow spreadWindow(resolveLookback(spec_.lookback));
    std::vector<StatisticalPairTradingMetric> metrics;
    metrics.reserve(closeByTradingDay.size());
    for (const PairCloseSnapshot& snapshot : closeByTradingDay) {
        const int tradingDay = snapshot.tradingDay;
        if (!snapshot.hasFirstClose || !snapshot.hasSecondClose || tradingDay <= kInvalidTradingDay) {
            continue;
        }

        const double spread = std::log(snapshot.firstClose) - (spec_.hedgeRatio * std::log(snapshot.secondClose));
        spreadWindow.push(spread);
        if (!spreadWindow.isReady()) {
            continue;
        }

        const double meanSpread = spreadWindow.mean();
        const double standardDeviation = spreadWindow.standardDeviation();
        const double denominator = standardDeviation > kSpreadEpsilon
            ? standardDeviation
            : kSpreadEpsilon;
        metrics.push_back(StatisticalPairTradingMetric{
            spread,
            meanSpread,
            standardDeviation,
            (spread - meanSpread) / denominator,
            tradingDay});
    }

    return metrics;
}

StatisticalPairTradingStrategy::RollingSpreadWindow::RollingSpreadWindow(std::size_t capacity)
    : buffer_(capacity, kZeroValue)
    , capacity_(capacity)
{
}

void StatisticalPairTradingStrategy::RollingSpreadWindow::push(double value)
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

bool StatisticalPairTradingStrategy::RollingSpreadWindow::isReady() const noexcept
{
    return capacity_ > kZeroSize && count_ == capacity_;
}

double StatisticalPairTradingStrategy::RollingSpreadWindow::mean() const noexcept
{
    if (count_ == kZeroSize) {
        return kZeroValue;
    }

    return sum_ / static_cast<double>(count_);
}

double StatisticalPairTradingStrategy::RollingSpreadWindow::standardDeviation() const noexcept
{
    if (count_ == kZeroSize) {
        return kZeroValue;
    }

    const double currentMean = mean();
    const double variance = (sumSquares_ / static_cast<double>(count_)) - (currentMean * currentMean);
    const double clampedVariance = variance > kZeroValue ? variance : kZeroValue;
    return std::sqrt(clampedVariance);
}

bool StatisticalPairTradingStrategy::hasUsableParameters() const noexcept
{
    return spec_.tradingPair.first != 0
        && spec_.tradingPair.second != 0
        && spec_.tradingPair.first != spec_.tradingPair.second
        && spec_.hedgeRatio > kZeroValue
        && spec_.lookback > kZeroCount
        && spec_.entryZScore > kZeroValue
        && spec_.exitZScore >= kZeroValue
        && spec_.exitZScore <= spec_.entryZScore;
}

std::size_t StatisticalPairTradingStrategy::resolveLookback(int lookback) noexcept
{
    return lookback > kZeroCount ? static_cast<std::size_t>(lookback) : kZeroSize;
}

} // namespace domain::strategies