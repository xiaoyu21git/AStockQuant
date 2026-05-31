#include "MachineLearningSelectionStrategy.h"

#include <algorithm>

namespace {

constexpr int kZeroCount = 0;
constexpr int kFirstRank = 1;

}

namespace domain::strategies {

bool MachineLearningSelectionStrategy::isConfigured() const noexcept
{
    return IStrategy::isConfigured()
        && hasUsableParameters();
}

std::vector<RankedPrediction> MachineLearningSelectionStrategy::computePredictionRanking(
    const std::vector<ModelPredictionSnapshot>& predictions) const
{
    if (!isConfigured() || predictions.empty()) {
        return {};
    }

    std::vector<RankedPrediction> rankedPredictions;
    rankedPredictions.reserve(predictions.size());
    for (const ModelPredictionSnapshot& prediction : predictions) {
        if (prediction.symbolId == 0) {
            continue;
        }

        rankedPredictions.push_back(RankedPrediction{prediction.symbolId, prediction.prediction, 0});
    }

    if (rankedPredictions.empty()) {
        return {};
    }

    const std::size_t selectionCount = std::min(static_cast<std::size_t>(spec_.topN), rankedPredictions.size());
    if (selectionCount < rankedPredictions.size()) {
        auto selectionEnd = rankedPredictions.begin() + static_cast<std::ptrdiff_t>(selectionCount);
        std::nth_element(rankedPredictions.begin(),
                         selectionEnd,
                         rankedPredictions.end(),
                         [](const RankedPrediction& left, const RankedPrediction& right) {
                             return left.prediction > right.prediction;
                         });
        rankedPredictions.resize(selectionCount);
    }

    std::sort(rankedPredictions.begin(),
              rankedPredictions.end(),
              [](const RankedPrediction& left, const RankedPrediction& right) {
                  return left.prediction > right.prediction;
              });

    for (std::size_t index = 0; index < rankedPredictions.size(); ++index) {
        rankedPredictions[index].rank = static_cast<int>(index) + kFirstRank;
    }

    return rankedPredictions;
}

bool MachineLearningSelectionStrategy::hasUsableParameters() const noexcept
{
    return spec_.modelId != 0
        && !spec_.featureIds.empty()
        && spec_.topN > kZeroCount;
}

} // namespace domain::strategies