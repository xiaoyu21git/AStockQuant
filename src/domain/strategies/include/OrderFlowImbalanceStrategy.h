#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "IStrategy.h"

namespace domain::strategies {

struct OrderBookLevelSnapshot final {
    std::uint64_t bidVolume{0};
    std::uint64_t askVolume{0};
};

struct OrderBookSnapshot final {
    SymbolId symbolId{0};
    int tradingDay{-1};
    int elapsedSeconds{0};
    std::vector<OrderBookLevelSnapshot> levels;
};

struct OrderFlowImbalanceValue final {
    SymbolId symbolId{0};
    double imbalance{0.0};
    int tradingDay{-1};
    int elapsedSeconds{0};
};

class OrderFlowImbalanceStrategy : public IStrategy {
public:
    OrderFlowImbalanceStrategy(const StrategyCommonConfig& commonConfig,
                               const StrategyMetadata& metadata,
                               const OrderFlowImbalanceStrategySpec& spec);

    StrategyType strategyType() const override;

    const OrderFlowImbalanceStrategySpec& spec() const noexcept;

    [[nodiscard]] bool isConfigured() const noexcept override;

    [[nodiscard]] std::vector<OrderFlowImbalanceValue> computeImbalanceValues(
        const std::vector<OrderBookSnapshot>& orderBooks) const;

private:
    [[nodiscard]] bool hasUsableParameters() const noexcept;

    [[nodiscard]] double computeImbalance(const OrderBookSnapshot& orderBook) const noexcept;

    OrderFlowImbalanceStrategySpec spec_;
};

} // namespace domain::strategies