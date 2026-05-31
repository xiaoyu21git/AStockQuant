#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "IStrategy.h"

namespace domain::strategies {

struct StatisticalPairTradingMetric final {
    double spread{0.0};
    double meanSpread{0.0};
    double standardDeviation{0.0};
    double zScore{0.0};
    int tradingDay{-1};
};

class StatisticalPairTradingStrategy : public IStrategy {
public:
    StatisticalPairTradingStrategy(const StrategyCommonConfig& commonConfig,
                                   const StrategyMetadata& metadata,
                                   const StatisticalPairTradingStrategySpec& spec);

    StrategyType strategyType() const override;

    const StatisticalPairTradingStrategySpec& spec() const noexcept;

    [[nodiscard]] bool isConfigured() const noexcept override;

    [[nodiscard]] std::vector<StatisticalPairTradingMetric> computeSpreadMetrics(const MarketBarList& bars) const;

private:
    class RollingSpreadWindow final {
    public:
        RollingSpreadWindow() = default;
        explicit RollingSpreadWindow(std::size_t capacity);

        void push(double value);

        [[nodiscard]] bool isReady() const noexcept;

        [[nodiscard]] double mean() const noexcept;

        [[nodiscard]] double standardDeviation() const noexcept;

    private:
        std::vector<double> buffer_;
        std::size_t capacity_{0};
        std::size_t count_{0};
        std::size_t cursor_{0};
        double sum_{0.0};
        double sumSquares_{0.0};
    };

    [[nodiscard]] bool hasUsableParameters() const noexcept;

    [[nodiscard]] static std::size_t resolveLookback(int lookback) noexcept;

    StatisticalPairTradingStrategySpec spec_;
};

} // namespace domain::strategies