#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "IStrategy.h"

namespace domain::strategies {

struct RiskParityAllocationMetric final {
    SymbolId symbolId{0};
    double volatility{0.0};
    double rawWeight{0.0};
    double normalizedWeight{0.0};
    double targetWeight{0.0};
};

class RiskParityAllocationStrategy : public IStrategy {
public:
    RiskParityAllocationStrategy(const StrategyCommonConfig& commonConfig,
                                 const StrategyMetadata& metadata,
                                 const RiskParityAllocationStrategySpec& spec);

    StrategyType strategyType() const override;

    const RiskParityAllocationStrategySpec& spec() const noexcept;

    [[nodiscard]] bool isConfigured() const noexcept override;

    [[nodiscard]] std::vector<RiskParityAllocationMetric> computeAllocationMetrics(const MarketBarList& bars) const;

private:
    class RollingReturnWindow final {
    public:
        RollingReturnWindow() = default;
        explicit RollingReturnWindow(std::size_t capacity);

        void push(double value);

        [[nodiscard]] bool isReady() const noexcept;

        [[nodiscard]] double standardDeviation() const noexcept;

        [[nodiscard]] std::vector<double> orderedValues() const;

    private:
        std::vector<double> buffer_;
        std::size_t capacity_{0};
        std::size_t count_{0};
        std::size_t cursor_{0};
        double sum_{0.0};
        double sumSquares_{0.0};
    };

    struct SymbolState final {
        explicit SymbolState(std::size_t lookback);

        RollingReturnWindow returns;
        double previousClose{0.0};
        bool hasPreviousClose{false};
    };

    struct AllocationSample final {
        SymbolId symbolId{0};
        double volatility{0.0};
    };

    [[nodiscard]] static std::size_t resolveLookback(int lookback) noexcept;

    [[nodiscard]] bool hasUsableParameters() const noexcept;

    [[nodiscard]] bool isTrackedAsset(SymbolId symbolId) const noexcept;

    [[nodiscard]] static double computeReturn(double currentClose, double previousClose) noexcept;

    [[nodiscard]] double computePortfolioVolatility(const std::vector<AllocationSample>& samples,
                                                    const std::vector<double>& normalizedWeights,
                                                    const std::vector<std::vector<double>>& orderedReturnsByAsset) const;

    RiskParityAllocationStrategySpec spec_;
    std::unordered_set<std::uint32_t> trackedAssets_;
};

} // namespace domain::strategies