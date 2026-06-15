#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>
#include "../../types/InstrumentId.h"

namespace astock::domain::backtest::grouping_allocation {

using ::domain::InstrumentId;

struct GroupId final {
    static constexpr int32_t kInvalidValue = 0;

    int32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value > kInvalidValue;
    }
};

struct GroupCount final {
    static constexpr int32_t kInvalidValue = 0;

    int32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value > kInvalidValue;
    }
};

struct RankScore final {
    static constexpr int32_t kInvalidValue = 0;

    int32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value > kInvalidValue;
    }
};

struct WeightBps final {
    static constexpr int32_t kZero = 0;
    static constexpr int32_t kFull = 10000;

    int32_t value{kZero};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value >= kZero && value <= kFull;
    }
};

struct RankedInstrument final {
    InstrumentId instrument{};
    RankScore score{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return instrument.isValid() && score.isValid();
    }
};

enum class AllocationMode {
    EqualWithinGroup,
    ScoreProportionalWithinGroup
};

struct GroupingAllocationSpec final {
    GroupCount groupCount{};
    AllocationMode mode{AllocationMode::EqualWithinGroup};

    [[nodiscard]] bool isValid() const noexcept
    {
        return groupCount.isValid();
    }
};

struct GroupBucket final {
    GroupId group{};
    std::vector<RankedInstrument> members;
};

struct GroupedUniverse final {
    std::vector<GroupBucket> buckets;
};

struct PortfolioWeights final {
    std::unordered_map<uint32_t, WeightBps> byInstrument;
};

struct GroupingAllocationOutput final {
    GroupedUniverse grouped{};
    PortfolioWeights weights{};
};

enum class GroupingAllocationError {
    None,
    InvalidInput,
    EmptyUniverse,
    InvalidRankedInstrument,
    NonPositiveGroupScore
};

struct GroupingAllocationResult final {
    GroupingAllocationError error{GroupingAllocationError::None};
    std::optional<GroupingAllocationOutput> value;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == GroupingAllocationError::None && value.has_value();
    }
};

class IGroupingAllocator {
public:
    virtual ~IGroupingAllocator() = default;

    virtual GroupingAllocationResult build(GroupingAllocationSpec spec,
                                           std::vector<RankedInstrument> ranked) const = 0;
};

class QuantileGroupingAllocator final : public IGroupingAllocator {
public:
    static constexpr int32_t kFirstGroup = 1;

    GroupingAllocationResult build(GroupingAllocationSpec spec,
                                   std::vector<RankedInstrument> ranked) const override;

private:
    static GroupedUniverse buildBuckets(GroupCount groupCount,
                                        const std::vector<RankedInstrument>& ranked);

    static GroupingAllocationError allocateWeights(AllocationMode mode,
                                                   const GroupedUniverse& grouped,
                                                   PortfolioWeights& weights);

    static void allocateEqualWithinBucket(const GroupBucket& bucket,
                                          int32_t bucketBudget,
                                          std::unordered_map<uint32_t, WeightBps>& out);

    static GroupingAllocationError allocateScoreProportionalWithinBucket(
        const GroupBucket& bucket,
        int32_t bucketBudget,
        std::unordered_map<uint32_t, WeightBps>& out);
};

} // namespace astock::domain::backtest::grouping_allocation
