#pragma once

#include <cstddef>
#include <vector>

#include "IStrategy.h"

namespace domain::strategies {

struct DoubleMovingAverageMetric final {
    SymbolId symbolId{0};
    double selectedPrice{0.0};
    double fastAverage{0.0};
    double slowAverage{0.0};
    double spread{0.0};
    int tradingDay{-1};
};

class DoubleMovingAverageStrategy : public IStrategy {
public:
    DoubleMovingAverageStrategy(const StrategyCommonConfig& commonConfig,
                                const StrategyMetadata& metadata,
                                const DoubleMovingAverageStrategySpec& spec);

    StrategyType strategyType() const override;

    const DoubleMovingAverageStrategySpec& spec() const noexcept;

    [[nodiscard]] bool isConfigured() const noexcept override;

    [[nodiscard]] std::vector<DoubleMovingAverageMetric> computeMovingAverageMetrics(const MarketBarList& bars) const;

private:
    class RollingAverageWindow final {
    public:
        RollingAverageWindow() = default;
        explicit RollingAverageWindow(std::size_t capacity);

        void push(double value);

        [[nodiscard]] bool isReady() const noexcept;

        [[nodiscard]] double average() const noexcept;

    private:
        std::vector<double> buffer_;
        std::size_t capacity_{0};
        std::size_t count_{0};
        std::size_t cursor_{0};
        double sum_{0.0};
    };

    struct SymbolState final {
        SymbolState(std::size_t fastPeriod, std::size_t slowPeriod);

        RollingAverageWindow fastWindow;
        RollingAverageWindow slowWindow;
    };

    [[nodiscard]] static std::size_t resolvePeriod(int period) noexcept;

    [[nodiscard]] bool hasUsablePeriods() const noexcept;

    [[nodiscard]] double selectPrice(const MarketBar& bar) const noexcept;

    DoubleMovingAverageStrategySpec spec_;
};

} // namespace domain::strategies