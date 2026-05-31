#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "IStrategy.h"

namespace domain::strategies {

struct ImpliedVolatilitySnapshot final {
    SymbolId underlying{0};
    double impliedVolatility{0.0};
    int tradingDay{-1};
};

struct VolatilitySpreadMetric final {
    double historicalVolatility{0.0};
    double impliedVolatility{0.0};
    double volatilitySpread{0.0};
    int tradingDay{-1};
};

class VolatilitySpreadStrategy : public IStrategy {
public:
    VolatilitySpreadStrategy(const StrategyCommonConfig& commonConfig,
                             const StrategyMetadata& metadata,
                             const VolatilitySpreadStrategySpec& spec);

    StrategyType strategyType() const override;

    const VolatilitySpreadStrategySpec& spec() const noexcept;

    [[nodiscard]] bool isConfigured() const noexcept override;

    [[nodiscard]] std::vector<VolatilitySpreadMetric> computeVolatilityMetrics(
        const MarketBarList& bars,
        const std::vector<ImpliedVolatilitySnapshot>& impliedVolatilities) const;

private:
    class RollingReturnWindow final {
    public:
        RollingReturnWindow() = default;
        explicit RollingReturnWindow(std::size_t capacity);

        void push(double value);

        [[nodiscard]] bool isReady() const noexcept;

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

    VolatilitySpreadStrategySpec spec_;
};

} // namespace domain::strategies