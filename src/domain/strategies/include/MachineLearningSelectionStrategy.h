#pragma once

#include <vector>

#include "IStrategy.h"

namespace domain::strategies {

struct ModelPredictionSnapshot final {
    SymbolId symbolId{0};
    double prediction{0.0};
};

struct RankedPrediction final {
    SymbolId symbolId{0};
    double prediction{0.0};
    int rank{0};
};

class MachineLearningSelectionStrategy : public IStrategy {
public:
    MachineLearningSelectionStrategy(const StrategyCommonConfig& commonConfig,
                                     const StrategyMetadata& metadata,
                                     const MachineLearningSelectionStrategySpec& spec);

    StrategyType strategyType() const override;

    const MachineLearningSelectionStrategySpec& spec() const noexcept;

    [[nodiscard]] bool isConfigured() const noexcept override;

    [[nodiscard]] std::vector<RankedPrediction> computePredictionRanking(
        const std::vector<ModelPredictionSnapshot>& predictions) const;

private:
    [[nodiscard]] bool hasUsableParameters() const noexcept;

    MachineLearningSelectionStrategySpec spec_;
};

} // namespace domain::strategies