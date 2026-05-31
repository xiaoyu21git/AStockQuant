#pragma once

#include <cstdint>
#include <vector>

#include "IStrategy.h"

namespace domain::strategies {

struct FactorSnapshot final {
    SymbolId symbolId{0};
    std::uint64_t factorId{0};
    double factorValue{0.0};
    std::int32_t industryBucket{0};
};

struct MultiFactorScore final {
    SymbolId symbolId{0};
    double score{0.0};
};

class MultiFactorSelectionStrategy : public IStrategy {
public:
    MultiFactorSelectionStrategy(const StrategyCommonConfig& commonConfig,
                                 const StrategyMetadata& metadata,
                                 const MultiFactorSelectionStrategySpec& spec);

    StrategyType strategyType() const override;

    const MultiFactorSelectionStrategySpec& spec() const noexcept;

    [[nodiscard]] bool isConfigured() const noexcept override;

    [[nodiscard]] std::vector<MultiFactorScore> computeCompositeScores(
        const std::vector<FactorSnapshot>& factorSnapshots) const;

private:
    [[nodiscard]] bool hasUsableParameters() const noexcept;

    [[nodiscard]] std::vector<MultiFactorScore> buildCompositeScores(
        const std::vector<FactorSnapshot>& factorSnapshots) const;

    MultiFactorSelectionStrategySpec spec_;
};

} // namespace domain::strategies