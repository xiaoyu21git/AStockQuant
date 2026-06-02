#include "GroupingAllocator.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace astock::domain::backtest::grouping_allocation {

GroupingAllocationResult QuantileGroupingAllocator::build(
    GroupingAllocationSpec spec,
    std::vector<RankedInstrument> ranked) const
{
    if (!spec.isValid()) {
        return GroupingAllocationResult{GroupingAllocationError::InvalidInput, std::nullopt};
    }
    if (ranked.empty()) {
        return GroupingAllocationResult{GroupingAllocationError::EmptyUniverse, std::nullopt};
    }

    std::unordered_set<uint32_t> seenInstruments;
    seenInstruments.reserve(ranked.size());
    for (const RankedInstrument& item : ranked) {
        if (!item.isValid()) {
            return GroupingAllocationResult{GroupingAllocationError::InvalidRankedInstrument, std::nullopt};
        }
        if (!seenInstruments.insert(item.instrument.value).second) {
            return GroupingAllocationResult{GroupingAllocationError::InvalidRankedInstrument, std::nullopt};
        }
    }

    std::sort(ranked.begin(), ranked.end(),
              [](const RankedInstrument& left, const RankedInstrument& right) {
                  if (left.score.value != right.score.value) {
                      return left.score.value > right.score.value;
                  }
                  return left.instrument.value < right.instrument.value;
              });

    GroupedUniverse grouped = buildBuckets(spec.groupCount, ranked);
    PortfolioWeights weights;
    const GroupingAllocationError allocationError = allocateWeights(spec.mode, grouped, weights);
    if (allocationError != GroupingAllocationError::None) {
        return GroupingAllocationResult{allocationError, std::nullopt};
    }

    GroupingAllocationOutput output{std::move(grouped), std::move(weights)};
    return GroupingAllocationResult{GroupingAllocationError::None, std::move(output)};
}

GroupedUniverse QuantileGroupingAllocator::buildBuckets(
    GroupCount groupCount,
    const std::vector<RankedInstrument>& ranked)
{
    const std::size_t instrumentCount = ranked.size();
    const std::size_t bucketCount = static_cast<std::size_t>(groupCount.value);

    GroupedUniverse grouped;
    grouped.buckets.resize(bucketCount);
    for (std::size_t index = 0; index < bucketCount; ++index) {
        grouped.buckets[index].group = GroupId{static_cast<int32_t>(index) + kFirstGroup};
    }

    for (std::size_t index = 0; index < instrumentCount; ++index) {
        const std::size_t bucketIndex = (index * bucketCount) / instrumentCount;
        grouped.buckets[bucketIndex].members.push_back(ranked[index]);
    }
    return grouped;
}

GroupingAllocationError QuantileGroupingAllocator::allocateWeights(
    AllocationMode mode,
    const GroupedUniverse& grouped,
    PortfolioWeights& weights)
{
    const std::size_t bucketCount = grouped.buckets.size();
    if (bucketCount == 0U) {
        return GroupingAllocationError::InvalidInput;
    }

    const int32_t baseBucketBudget = WeightBps::kFull / static_cast<int32_t>(bucketCount);
    int32_t bucketBudgetRemainder = WeightBps::kFull % static_cast<int32_t>(bucketCount);

    for (const GroupBucket& bucket : grouped.buckets) {
        if (bucket.members.empty()) {
            continue;
        }

        int32_t bucketBudget = baseBucketBudget;
        if (bucketBudgetRemainder > 0) {
            ++bucketBudget;
            --bucketBudgetRemainder;
        }

        if (mode == AllocationMode::EqualWithinGroup) {
            allocateEqualWithinBucket(bucket, bucketBudget, weights.byInstrument);
            continue;
        }

        const GroupingAllocationError err =
            allocateScoreProportionalWithinBucket(bucket, bucketBudget, weights.byInstrument);
        if (err != GroupingAllocationError::None) {
            return err;
        }
    }

    return GroupingAllocationError::None;
}

void QuantileGroupingAllocator::allocateEqualWithinBucket(
    const GroupBucket& bucket,
    int32_t bucketBudget,
    std::unordered_map<uint32_t, WeightBps>& out)
{
    const int32_t memberCount = static_cast<int32_t>(bucket.members.size());
    const int32_t baseWeight = bucketBudget / memberCount;
    int32_t remainder = bucketBudget % memberCount;

    for (const RankedInstrument& item : bucket.members) {
        int32_t weight = baseWeight;
        if (remainder > 0) {
            ++weight;
            --remainder;
        }
        out[item.instrument.value] = WeightBps{weight};
    }
}

GroupingAllocationError QuantileGroupingAllocator::allocateScoreProportionalWithinBucket(
    const GroupBucket& bucket,
    int32_t bucketBudget,
    std::unordered_map<uint32_t, WeightBps>& out)
{
    int32_t scoreSum = 0;
    for (const RankedInstrument& item : bucket.members) {
        scoreSum += item.score.value;
    }
    if (scoreSum <= 0) {
        return GroupingAllocationError::NonPositiveGroupScore;
    }

    int32_t assigned = 0;
    for (std::size_t index = 0; index < bucket.members.size(); ++index) {
        const RankedInstrument& item = bucket.members[index];
        int32_t weight = (bucketBudget * item.score.value) / scoreSum;
        if (index + 1U == bucket.members.size()) {
            weight = bucketBudget - assigned;
        }
        assigned += weight;
        out[item.instrument.value] = WeightBps{weight};
    }

    return GroupingAllocationError::None;
}

} // namespace astock::domain::backtest::grouping_allocation


