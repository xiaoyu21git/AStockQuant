#pragma once

#include <cstdint>
#include <vector>

#include "IStrategy.h"

namespace domain::strategies {

struct EarningsEventSnapshot final {
    SymbolId symbolId{0};
    double actualValue{0.0};
    double expectedValue{0.0};
    EventSourceKind eventSource{EventSourceKind::EARNINGS};
    int tradingDay{-1};
};

struct EarningsSurpriseValue final {
    SymbolId symbolId{0};
    double surprise{0.0};
    EventSourceKind eventSource{EventSourceKind::EARNINGS};
    int tradingDay{-1};
};

class EarningsSurpriseStrategy : public IStrategy {
public:
    EarningsSurpriseStrategy(const StrategyCommonConfig& commonConfig,
                             const StrategyMetadata& metadata,
                             const EarningsSurpriseStrategySpec& spec);

    StrategyType strategyType() const override;

    const EarningsSurpriseStrategySpec& spec() const noexcept;

    [[nodiscard]] bool isConfigured() const noexcept override;

    [[nodiscard]] std::vector<EarningsSurpriseValue> computeSurpriseValues(
        const std::vector<EarningsEventSnapshot>& events) const;

private:
    [[nodiscard]] bool hasUsableParameters() const noexcept;

    [[nodiscard]] bool acceptsEventSource(EventSourceKind eventSource) const noexcept;

    [[nodiscard]] static double computeSurprise(const EarningsEventSnapshot& event) noexcept;

    EarningsSurpriseStrategySpec spec_;
};

} // namespace domain::strategies