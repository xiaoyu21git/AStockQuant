#include "OrderFlowImbalanceStrategy.h"

namespace {

constexpr int kZeroCount = 0;
constexpr int kInvalidTradingDay = -1;
constexpr double kZeroValue = 0.0;
constexpr double kImbalanceEpsilon = 1e-12;

}

namespace domain::strategies {

bool OrderFlowImbalanceStrategy::isConfigured() const noexcept
{
    return IStrategy::isConfigured()
        && hasUsableParameters();
}

std::vector<OrderFlowImbalanceValue> OrderFlowImbalanceStrategy::computeImbalanceValues(
    const std::vector<OrderBookSnapshot>& orderBooks) const
{
    if (!isConfigured() || orderBooks.empty()) {
        return {};
    }

    std::vector<OrderFlowImbalanceValue> values;
    values.reserve(orderBooks.size());
    for (const OrderBookSnapshot& orderBook : orderBooks) {
        if (orderBook.symbolId == 0
            || orderBook.tradingDay <= kInvalidTradingDay
            || orderBook.elapsedSeconds < kZeroCount) {
            continue;
        }

        values.push_back(OrderFlowImbalanceValue{
            orderBook.symbolId,
            computeImbalance(orderBook),
            orderBook.tradingDay,
            orderBook.elapsedSeconds});
    }

    return values;
}

bool OrderFlowImbalanceStrategy::hasUsableParameters() const noexcept
{
    return spec_.depthLevels > kZeroCount
        && spec_.imbalanceThreshold > kZeroValue
        && spec_.maxHoldSeconds > kZeroCount;
}

double OrderFlowImbalanceStrategy::computeImbalance(const OrderBookSnapshot& orderBook) const noexcept
{
    if (orderBook.levels.empty()) {
        return kZeroValue;
    }

    const std::size_t configuredDepthLevels = spec_.depthLevels > kZeroCount
        ? static_cast<std::size_t>(spec_.depthLevels)
        : static_cast<std::size_t>(kZeroCount);
    const std::size_t depthLevels = std::min(configuredDepthLevels, orderBook.levels.size());
    double bidDepth = kZeroValue;
    double askDepth = kZeroValue;
    for (std::size_t levelIndex = 0; levelIndex < depthLevels; ++levelIndex) {
        bidDepth += static_cast<double>(orderBook.levels[levelIndex].bidVolume);
        askDepth += static_cast<double>(orderBook.levels[levelIndex].askVolume);
    }

    const double denominator = bidDepth + askDepth;
    return denominator > kImbalanceEpsilon
        ? (bidDepth - askDepth) / denominator
        : kZeroValue;
}

} // namespace domain::strategies