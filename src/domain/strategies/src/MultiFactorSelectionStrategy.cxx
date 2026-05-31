#include "MultiFactorSelectionStrategy.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

constexpr int kZeroCount = 0;
constexpr double kZeroValue = 0.0;
constexpr double kStandardDeviationEpsilon = 1e-12;

struct FactorStats final {
    double sum{0.0};
    double sumSquares{0.0};
    std::size_t count{0};
};

struct IndustryStats final {
    double sum{0.0};
    std::size_t count{0};
};

}

namespace domain::strategies {

bool MultiFactorSelectionStrategy::isConfigured() const noexcept
{
    return IStrategy::isConfigured()
        && hasUsableParameters();
}

std::vector<MultiFactorScore> MultiFactorSelectionStrategy::computeCompositeScores(
    const std::vector<FactorSnapshot>& factorSnapshots) const
{
    if (!isConfigured() || factorSnapshots.empty()) {
        return {};
    }

    std::vector<MultiFactorScore> scores = buildCompositeScores(factorSnapshots);
    if (scores.empty()) {
        return {};
    }

    const std::size_t selectionCount = std::min(static_cast<std::size_t>(spec_.topN), scores.size());
    if (selectionCount < scores.size()) {
        auto selectionEnd = scores.begin() + static_cast<std::ptrdiff_t>(selectionCount);
        std::nth_element(scores.begin(),
                         selectionEnd,
                         scores.end(),
                         [](const MultiFactorScore& left, const MultiFactorScore& right) {
                             return left.score > right.score;
                         });
        scores.resize(selectionCount);
    }

    std::sort(scores.begin(),
              scores.end(),
              [](const MultiFactorScore& left, const MultiFactorScore& right) {
                  return left.score > right.score;
              });
    return scores;
}

bool MultiFactorSelectionStrategy::hasUsableParameters() const noexcept
{
    return !spec_.factorWeights.empty()
        && spec_.topN > kZeroCount;
}

std::vector<MultiFactorScore> MultiFactorSelectionStrategy::buildCompositeScores(
    const std::vector<FactorSnapshot>& factorSnapshots) const
{
    std::unordered_map<std::uint64_t, double> factorWeightById;
    factorWeightById.reserve(spec_.factorWeights.size());
    for (const FactorWeight& factorWeight : spec_.factorWeights) {
        factorWeightById[factorWeight.factorId] = factorWeight.weight;
    }

    std::unordered_map<std::uint64_t, FactorStats> factorStatsById;
    factorStatsById.reserve(spec_.factorWeights.size());
    for (const FactorSnapshot& snapshot : factorSnapshots) {
        const auto weightIt = factorWeightById.find(snapshot.factorId);
        if (snapshot.symbolId == 0 || weightIt == factorWeightById.end()) {
            continue;
        }

        FactorStats& stats = factorStatsById[snapshot.factorId];
        stats.sum += snapshot.factorValue;
        stats.sumSquares += snapshot.factorValue * snapshot.factorValue;
        ++stats.count;
    }

    std::unordered_map<std::uint64_t, double> factorMeanById;
    std::unordered_map<std::uint64_t, double> factorInverseStandardDeviationById;
    factorMeanById.reserve(factorStatsById.size());
    factorInverseStandardDeviationById.reserve(factorStatsById.size());
    for (const auto& entry : factorStatsById) {
        const auto weightIt = factorWeightById.find(entry.first);
        if (weightIt == factorWeightById.end() || entry.second.count == 0) {
            continue;
        }

        const double mean = entry.second.sum / static_cast<double>(entry.second.count);
        const double variance = (entry.second.sumSquares / static_cast<double>(entry.second.count)) - (mean * mean);
        const double standardDeviation = std::sqrt(variance > kZeroValue ? variance : kZeroValue);
        const double denominator = standardDeviation > kStandardDeviationEpsilon
            ? standardDeviation
            : kStandardDeviationEpsilon;
        factorMeanById.emplace(entry.first, mean);
        factorInverseStandardDeviationById.emplace(entry.first,
                                                   kZeroValue == denominator ? kZeroValue : (1.0 / denominator));
    }

    std::unordered_map<std::uint64_t, std::unordered_map<std::int32_t, IndustryStats>> industryStatsByFactor;
    if (spec_.industryNeutral) {
        industryStatsByFactor.reserve(factorMeanById.size());
    }
    if (spec_.industryNeutral) {
        for (const FactorSnapshot& snapshot : factorSnapshots) {
            const auto meanIt = factorMeanById.find(snapshot.factorId);
            const auto inverseStdDevIt = factorInverseStandardDeviationById.find(snapshot.factorId);
            if (snapshot.symbolId == 0
                || meanIt == factorMeanById.end()
                || inverseStdDevIt == factorInverseStandardDeviationById.end()) {
                continue;
            }

            const double zScore = (snapshot.factorValue - meanIt->second) * inverseStdDevIt->second;

            IndustryStats& industryStats = industryStatsByFactor[snapshot.factorId][snapshot.industryBucket];
            industryStats.sum += zScore;
            ++industryStats.count;
        }
    }

    std::unordered_map<SymbolId, double> scoreBySymbol;
    std::unordered_map<SymbolId, std::size_t> factorCountBySymbol;
    scoreBySymbol.reserve(factorSnapshots.size());
    factorCountBySymbol.reserve(factorSnapshots.size());
    for (const FactorSnapshot& snapshot : factorSnapshots) {
        const auto weightIt = factorWeightById.find(snapshot.factorId);
        const auto meanIt = factorMeanById.find(snapshot.factorId);
        const auto inverseStdDevIt = factorInverseStandardDeviationById.find(snapshot.factorId);
        if (snapshot.symbolId == 0
            || weightIt == factorWeightById.end()
            || meanIt == factorMeanById.end()
            || inverseStdDevIt == factorInverseStandardDeviationById.end()) {
            continue;
        }

        double usedZScore = (snapshot.factorValue - meanIt->second) * inverseStdDevIt->second;

        if (spec_.industryNeutral) {
            const auto factorIndustryIt = industryStatsByFactor.find(snapshot.factorId);
            if (factorIndustryIt != industryStatsByFactor.end()) {
                const auto industryIt = factorIndustryIt->second.find(snapshot.industryBucket);
                if (industryIt != factorIndustryIt->second.end() && industryIt->second.count > 0) {
                    usedZScore -= industryIt->second.sum / static_cast<double>(industryIt->second.count);
                }
            }
        }

        scoreBySymbol[snapshot.symbolId] += weightIt->second * usedZScore;
        ++factorCountBySymbol[snapshot.symbolId];
    }

    std::vector<MultiFactorScore> scores;
    scores.reserve(scoreBySymbol.size());
    const std::size_t requiredFactorCount = spec_.factorWeights.size();
    for (const auto& entry : scoreBySymbol) {
        const auto countIt = factorCountBySymbol.find(entry.first);
        if (countIt == factorCountBySymbol.end() || countIt->second != requiredFactorCount) {
            continue;
        }

        scores.push_back(MultiFactorScore{entry.first, entry.second});
    }

    return scores;
}

} // namespace domain::strategies