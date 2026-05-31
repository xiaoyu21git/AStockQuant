#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "IStrategy.h"

namespace domain::strategies {

struct TurtleBreakoutMetric final {
    SymbolId symbolId{0};
    double closePrice{0.0};
    double upperChannel{0.0};
    double lowerChannel{0.0};
    double atr{0.0};
    double breakoutLevel{0.0};
    int tradingDay{-1};
};

class TurtleBreakoutStrategy : public IStrategy {
public:
    TurtleBreakoutStrategy(const StrategyCommonConfig& commonConfig,
                           const StrategyMetadata& metadata,
                           const TurtleBreakoutStrategySpec& spec);

    StrategyType strategyType() const override;

    const TurtleBreakoutStrategySpec& spec() const noexcept;

    [[nodiscard]] bool isConfigured() const noexcept override;

    [[nodiscard]] std::vector<TurtleBreakoutMetric> computeBreakoutMetrics(const MarketBarList& bars) const;

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

    struct IndexedValue final {
        std::size_t index{0};
        double value{0.0};
    };

    struct SymbolState final {
        explicit SymbolState(std::size_t atrPeriod);

        RollingAverageWindow atrWindow;
        std::deque<IndexedValue> upperChannelCandidates;
        std::deque<IndexedValue> lowerChannelCandidates;
        double previousClose{0.0};
        bool hasPreviousClose{false};
        std::size_t processedBars{0};
    };

    [[nodiscard]] static std::size_t resolvePeriod(int period) noexcept;

    [[nodiscard]] bool hasUsableParameters() const noexcept;

    [[nodiscard]] static double computeTrueRange(const MarketBar& bar, double previousClose) noexcept;

    static void appendChannelHigh(SymbolState& state, double value, std::size_t channelPeriod);

    static void appendChannelLow(SymbolState& state, double value, std::size_t channelPeriod);

    TurtleBreakoutStrategySpec spec_;
};

} // namespace domain::strategies