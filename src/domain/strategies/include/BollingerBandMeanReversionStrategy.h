#pragma once

#include <cstddef>
#include <vector>

#include "IStrategy.h"

namespace domain::strategies {

struct BollingerBandMetric final {
    SymbolId symbolId{0};
    double closePrice{0.0};
    double middle{0.0};
    double upperBand{0.0};
    double lowerBand{0.0};
    double standardDeviation{0.0};
    double zScore{0.0};
    int tradingDay{-1};
};

class BollingerBandMeanReversionStrategy : public IStrategy {
public:
    BollingerBandMeanReversionStrategy(const StrategyCommonConfig& commonConfig,
                                       const StrategyMetadata& metadata,
                                       const BollingerBandMeanReversionStrategySpec& spec);

    StrategyType strategyType() const override;

    const BollingerBandMeanReversionStrategySpec& spec() const noexcept;

    [[nodiscard]] bool isConfigured() const noexcept override;

    [[nodiscard]] std::vector<BollingerBandMetric> computeBandMetrics(const MarketBarList& bars) const;

private:
    class RollingStatisticsWindow final {
    public:
        RollingStatisticsWindow() = default;
        explicit RollingStatisticsWindow(std::size_t capacity);

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

    struct SymbolState final {
        explicit SymbolState(std::size_t period);

        RollingStatisticsWindow window;
    };

    [[nodiscard]] static std::size_t resolvePeriod(int period) noexcept;

    [[nodiscard]] bool hasUsableThresholds() const noexcept;

    [[nodiscard]] double computeZScore(const SymbolState& state, double closePrice) const noexcept;

    BollingerBandMeanReversionStrategySpec spec_;
};

} // namespace domain::strategies