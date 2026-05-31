#pragma once

#include <cstddef>
#include <vector>

#include "IStrategy.h"

namespace domain::strategies {

struct RsiMetric final {
    SymbolId symbolId{0};
    double closePrice{0.0};
    double averageGain{0.0};
    double averageLoss{0.0};
    double rsi{0.0};
    int tradingDay{-1};
};

class RsiMeanReversionStrategy : public IStrategy {
public:
    RsiMeanReversionStrategy(const StrategyCommonConfig& commonConfig,
                             const StrategyMetadata& metadata,
                             const RsiMeanReversionStrategySpec& spec);

    StrategyType strategyType() const override;

    const RsiMeanReversionStrategySpec& spec() const noexcept;

    [[nodiscard]] bool isConfigured() const noexcept override;

    [[nodiscard]] std::vector<RsiMetric> computeRsiMetrics(const MarketBarList& bars) const;

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
        explicit SymbolState(std::size_t period);

        double previousClose{0.0};
        bool hasPreviousClose{false};
        RollingAverageWindow gains;
        RollingAverageWindow losses;
    };

    [[nodiscard]] static std::size_t resolvePeriod(int period) noexcept;

    [[nodiscard]] double computeRsi(const SymbolState& state) const noexcept;

    RsiMeanReversionStrategySpec spec_;
};

} // namespace domain::strategies