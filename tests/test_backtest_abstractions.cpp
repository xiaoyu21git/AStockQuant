#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "domain/backtest/include/BacktestLayerGuard.h"
#include "domain/backtest/include/BacktestWindowAbstraction.h"
#include "domain/backtest/include/DynamicUniverseAbstraction.h"
#include "domain/backtest/include/OrderRoutingEngineAbstraction.h"
#include "domain/backtest/include/PerformanceMetricsAggregatorAbstraction.h"
#include "domain/backtest/include/GroupingAllocationAbstraction.h"
#include "domain/backtest/include/PortfolioValuationEngineAbstraction.h"
#include "domain/backtest/include/PositionConstraintAllocationAbstraction.h"
#include "domain/backtest/include/RebalanceScheduleAbstraction.h"
#include "domain/backtest/include/RiskApprovalEngineAbstraction.h"
#include "domain/backtest/include/SignalOrderTranslatorAbstraction.h"
#include "domain/backtest/include/PositionStateMachineAbstraction.h"
#include "domain/backtest/include/TradingCostModelAbstraction.h"

namespace {

using astock::domain::backtest::windowing::BacktestWindowBuilder;
using astock::domain::backtest::windowing::DayCount;
using astock::domain::backtest::windowing::DayRange;
using astock::domain::backtest::windowing::FixedWarmupDaysPolicy;
using astock::domain::backtest::windowing::ITradingCalendar;
using astock::domain::backtest::windowing::TradingDay;
using astock::domain::backtest::windowing::WindowBuildError;
using astock::domain::backtest::windowing::WindowBuildSpec;
using astock::domain::backtest::windowing::WindowingMode;

class FakeWindowCalendar final : public ITradingCalendar {
public:
    void setTradingDay(TradingDay day, bool isTrading)
    {
        tradingDayTable_[day.value] = isTrading;
    }

    void setShiftResult(TradingDay anchor, int32_t offset, std::optional<TradingDay> result)
    {
        const ShiftKey key{anchor.value, offset};
        shiftTable_[key] = result;
    }

    bool isTradingDay(TradingDay day) const override
    {
        const auto it = tradingDayTable_.find(day.value);
        if (it == tradingDayTable_.end()) {
            return false;
        }
        return it->second;
    }

    std::optional<TradingDay> shiftTradingDays(TradingDay anchor, int32_t offset) const override
    {
        const ShiftKey key{anchor.value, offset};
        const auto it = shiftTable_.find(key);
        if (it == shiftTable_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    struct ShiftKey final {
        int32_t anchorValue{0};
        int32_t offset{0};

        bool operator==(const ShiftKey& other) const noexcept
        {
            return anchorValue == other.anchorValue && offset == other.offset;
        }
    };

    struct ShiftKeyHash final {
        std::size_t operator()(const ShiftKey& key) const noexcept
        {
            const std::size_t anchorHash = std::hash<int32_t>{}(key.anchorValue);
            const std::size_t offsetHash = std::hash<int32_t>{}(key.offset);
            return anchorHash ^ (offsetHash << 1U);
        }
    };

    std::unordered_map<int32_t, bool> tradingDayTable_;
    std::unordered_map<ShiftKey, std::optional<TradingDay>, ShiftKeyHash> shiftTable_;
};

TEST(BacktestWindowAbstractionTest, ReturnsInvalidInputWhenRequestedRangeInvalid)
{
    FakeWindowCalendar calendar;
    const FixedWarmupDaysPolicy policy(DayCount{0}, DayCount{0});
    const BacktestWindowBuilder builder(calendar, policy);

    const WindowBuildSpec spec{DayRange{TradingDay{20250102}, TradingDay{20240102}}, WindowingMode::CrossSection};

    const auto result = builder.build(spec);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, WindowBuildError::InvalidInput);
}

TEST(BacktestWindowAbstractionTest, ExpandsEffectiveStartWithWarmupDays)
{
    FakeWindowCalendar calendar;
    const TradingDay requestedStart{20250110};
    const TradingDay requestedEnd{20250120};
    const TradingDay expandedStart{20250103};
    const int32_t warmupDaysValue = 5;
    const int32_t backwardOffset = -5;

    calendar.setTradingDay(requestedStart, true);
    calendar.setTradingDay(requestedEnd, true);
    calendar.setTradingDay(expandedStart, true);
    calendar.setShiftResult(requestedStart, backwardOffset, expandedStart);

    const FixedWarmupDaysPolicy policy(DayCount{warmupDaysValue}, DayCount{warmupDaysValue});
    const BacktestWindowBuilder builder(calendar, policy);

    const WindowBuildSpec spec{DayRange{requestedStart, requestedEnd}, WindowingMode::CrossSection};

    const auto result = builder.build(spec);
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(result.value->requested.start.value, requestedStart.value);
    EXPECT_EQ(result.value->effective.start.value, expandedStart.value);
    EXPECT_EQ(result.value->effective.end.value, requestedEnd.value);
    EXPECT_EQ(result.value->warmupDays.value, warmupDaysValue);
}

TEST(BacktestWindowAbstractionTest, ReturnsMissingHistoricalDayWhenShiftFailed)
{
    FakeWindowCalendar calendar;
    const TradingDay requestedStart{20250110};
    const TradingDay requestedEnd{20250120};
    const int32_t warmupDaysValue = 3;
    const int32_t backwardOffset = -3;

    calendar.setTradingDay(requestedStart, true);
    calendar.setTradingDay(requestedEnd, true);
    calendar.setShiftResult(requestedStart, backwardOffset, std::nullopt);

    const FixedWarmupDaysPolicy policy(DayCount{warmupDaysValue}, DayCount{warmupDaysValue});
    const BacktestWindowBuilder builder(calendar, policy);

    const WindowBuildSpec spec{DayRange{requestedStart, requestedEnd}, WindowingMode::CrossSection};
    const auto result = builder.build(spec);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, WindowBuildError::MissingHistoricalTradingDay);
}

TEST(BacktestWindowAbstractionTest, ReturnsMissingHistoricalDayWhenShiftMovesForward)
{
    FakeWindowCalendar calendar;
    const TradingDay requestedStart{20250110};
    const TradingDay requestedEnd{20250120};
    const TradingDay invalidExpandedStart{20250112};
    const int32_t warmupDaysValue = 2;
    const int32_t backwardOffset = -2;

    calendar.setTradingDay(requestedStart, true);
    calendar.setTradingDay(requestedEnd, true);
    calendar.setTradingDay(invalidExpandedStart, true);
    calendar.setShiftResult(requestedStart, backwardOffset, invalidExpandedStart);

    const FixedWarmupDaysPolicy policy(DayCount{warmupDaysValue}, DayCount{warmupDaysValue});
    const BacktestWindowBuilder builder(calendar, policy);
    const WindowBuildSpec spec{DayRange{requestedStart, requestedEnd}, WindowingMode::CrossSection};

    const auto result = builder.build(spec);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, WindowBuildError::MissingHistoricalTradingDay);
}

TEST(BacktestWindowAbstractionTest, ReturnsNonTradingBoundaryWhenExpandedStartNotTradingDay)
{
    FakeWindowCalendar calendar;
    const TradingDay requestedStart{20250110};
    const TradingDay requestedEnd{20250120};
    const TradingDay expandedStart{20250108};
    const int32_t warmupDaysValue = 2;
    const int32_t backwardOffset = -2;

    calendar.setTradingDay(requestedStart, true);
    calendar.setTradingDay(requestedEnd, true);
    calendar.setTradingDay(expandedStart, false);
    calendar.setShiftResult(requestedStart, backwardOffset, expandedStart);

    const FixedWarmupDaysPolicy policy(DayCount{warmupDaysValue}, DayCount{warmupDaysValue});
    const BacktestWindowBuilder builder(calendar, policy);
    const WindowBuildSpec spec{DayRange{requestedStart, requestedEnd}, WindowingMode::CrossSection};

    const auto result = builder.build(spec);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, WindowBuildError::NonTradingBoundary);
}

using astock::domain::backtest::dynamic_universe::ConstituentInterval;
using astock::domain::backtest::dynamic_universe::DynamicUniverseBuildError;
using astock::domain::backtest::dynamic_universe::DynamicUniverseBuilder;
using astock::domain::backtest::dynamic_universe::IConstituentRepository;
using astock::domain::backtest::dynamic_universe::IMissingCoveragePolicy;
using astock::domain::backtest::dynamic_universe::IndexId;
using astock::domain::backtest::dynamic_universe::InstrumentId;
using astock::domain::backtest::dynamic_universe::MissingCoverage;
using astock::domain::backtest::dynamic_universe::MissingCoverageAction;
using UniverseDayRange = astock::domain::backtest::dynamic_universe::DayRange;
using UniverseTradingCalendar = astock::domain::backtest::dynamic_universe::ITradingCalendar;
using UniverseTradingDay = astock::domain::backtest::dynamic_universe::TradingDay;

class FakeUniverseCalendar final : public UniverseTradingCalendar {
public:
    void setDays(std::vector<UniverseTradingDay> days)
    {
        days_ = std::move(days);
    }

    std::vector<UniverseTradingDay> enumerate(UniverseDayRange) const override
    {
        return days_;
    }

    std::optional<UniverseTradingDay> nextDayAfter(UniverseTradingDay) const override
    {
        return std::nullopt;
    }

private:
    std::vector<UniverseTradingDay> days_;
};

class FakeUniverseRepository final : public IConstituentRepository {
public:
    void setIntervals(std::vector<ConstituentInterval> intervals)
    {
        intervals_ = std::move(intervals);
    }

    std::vector<ConstituentInterval> load(IndexId, UniverseDayRange) const override
    {
        return intervals_;
    }

private:
    std::vector<ConstituentInterval> intervals_;
};

class SkipCoveragePolicy final : public IMissingCoveragePolicy {
public:
    MissingCoverageAction onMissing(MissingCoverage) const override
    {
        return MissingCoverageAction::Skip;
    }
};

TEST(DynamicUniverseAbstractionTest, ReturnsInvalidInputWhenIndexOrRangeInvalid)
{
    FakeUniverseCalendar calendar;
    FakeUniverseRepository repository;
    SkipCoveragePolicy policy;
    const DynamicUniverseBuilder builder(calendar, repository, policy);

    const auto result = builder.build(IndexId{0}, UniverseDayRange{UniverseTradingDay{20250101}, UniverseTradingDay{20250102}});

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, DynamicUniverseBuildError::InvalidInput);
}

TEST(DynamicUniverseAbstractionTest, ReturnsMissingCoverageWhenPolicyRequiresFail)
{
    class FailCoveragePolicy final : public IMissingCoveragePolicy {
    public:
        MissingCoverageAction onMissing(MissingCoverage) const override
        {
            return MissingCoverageAction::Fail;
        }
    };

    FakeUniverseCalendar calendar;
    FakeUniverseRepository repository;
    FailCoveragePolicy policy;
    const DynamicUniverseBuilder builder(calendar, repository, policy);

    calendar.setDays({UniverseTradingDay{20250101}});
    repository.setIntervals({});

    const auto result = builder.build(IndexId{300}, UniverseDayRange{UniverseTradingDay{20250101}, UniverseTradingDay{20250101}});

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, DynamicUniverseBuildError::MissingCoverage);
}

TEST(DynamicUniverseAbstractionTest, ReturnsInvalidInputWhenCalendarEnumerationInvalid)
{
    FakeUniverseCalendar calendar;
    FakeUniverseRepository repository;
    SkipCoveragePolicy policy;
    const DynamicUniverseBuilder builder(calendar, repository, policy);

    calendar.setDays({UniverseTradingDay{20250102}, UniverseTradingDay{20250101}});
    repository.setIntervals({ConstituentInterval{InstrumentId{100001}, UniverseTradingDay{20240101}, UniverseTradingDay{0}, true}});

    const auto result = builder.build(IndexId{300}, UniverseDayRange{UniverseTradingDay{20250101}, UniverseTradingDay{20250131}});

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, DynamicUniverseBuildError::InvalidInput);
}

TEST(DynamicUniverseAbstractionTest, ReturnsInvalidInputWhenIntervalInvalid)
{
    FakeUniverseCalendar calendar;
    FakeUniverseRepository repository;
    SkipCoveragePolicy policy;
    const DynamicUniverseBuilder builder(calendar, repository, policy);

    calendar.setDays({UniverseTradingDay{20250108}});
    repository.setIntervals({ConstituentInterval{InstrumentId{100001}, UniverseTradingDay{20250110}, UniverseTradingDay{20250101}, false}});

    const auto result = builder.build(IndexId{905}, UniverseDayRange{UniverseTradingDay{20250101}, UniverseTradingDay{20250131}});

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, DynamicUniverseBuildError::InvalidInput);
}

TEST(DynamicUniverseAbstractionTest, BuildsDeduplicatedUniverseByDay)
{
    FakeUniverseCalendar calendar;
    FakeUniverseRepository repository;
    SkipCoveragePolicy policy;
    const DynamicUniverseBuilder builder(calendar, repository, policy);

    const UniverseTradingDay day{20250108};
    calendar.setDays({day});

    const ConstituentInterval intervalA{InstrumentId{100001}, UniverseTradingDay{20240101}, UniverseTradingDay{0}, true};
    const ConstituentInterval intervalB{InstrumentId{100001}, UniverseTradingDay{20240101}, UniverseTradingDay{0}, true};
    const ConstituentInterval intervalC{InstrumentId{100003}, UniverseTradingDay{20240101}, UniverseTradingDay{0}, true};
    repository.setIntervals({intervalC, intervalA, intervalB});

    const auto result = builder.build(IndexId{905}, UniverseDayRange{day, day});

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    const auto dayIt = result.value->data.find(day.value);
    ASSERT_NE(dayIt, result.value->data.end());
    ASSERT_EQ(dayIt->second.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(dayIt->second[0].value, 100001U);
    EXPECT_EQ(dayIt->second[1].value, 100003U);
}

using astock::domain::backtest::rebalancing::RebalanceAnchor;
using astock::domain::backtest::rebalancing::RebalanceIntervalDays;
using astock::domain::backtest::rebalancing::RebalancePlanError;
using astock::domain::backtest::rebalancing::RebalancePlanSpec;
using astock::domain::backtest::rebalancing::RebalanceScheduleBuilder;
using RebalanceDayRange = astock::domain::backtest::rebalancing::DayRange;
using RebalanceTradingCalendar = astock::domain::backtest::rebalancing::ITradingCalendar;
using RebalanceTradingDay = astock::domain::backtest::rebalancing::TradingDay;

class FakeRebalanceCalendar final : public RebalanceTradingCalendar {
public:
    void setNextTradingDayOnOrAfter(RebalanceTradingDay anchor, std::optional<RebalanceTradingDay> result)
    {
        nextOnOrAfterTable_[anchor.value] = result;
    }

    void setPreviousTradingDayOnOrBefore(RebalanceTradingDay anchor, std::optional<RebalanceTradingDay> result)
    {
        previousOnOrBeforeTable_[anchor.value] = result;
    }

    void setShiftResult(RebalanceTradingDay anchor, int32_t offset, std::optional<RebalanceTradingDay> result)
    {
        const ShiftKey key{anchor.value, offset};
        shiftTable_[key] = result;
    }

    std::optional<RebalanceTradingDay> nextTradingDayOnOrAfter(RebalanceTradingDay day) const override
    {
        const auto it = nextOnOrAfterTable_.find(day.value);
        if (it == nextOnOrAfterTable_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<RebalanceTradingDay> previousTradingDayOnOrBefore(RebalanceTradingDay day) const override
    {
        const auto it = previousOnOrBeforeTable_.find(day.value);
        if (it == previousOnOrBeforeTable_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<RebalanceTradingDay> shiftTradingDays(RebalanceTradingDay day, int32_t offset) const override
    {
        const ShiftKey key{day.value, offset};
        const auto it = shiftTable_.find(key);
        if (it == shiftTable_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    struct ShiftKey final {
        int32_t dayValue{0};
        int32_t offset{0};

        bool operator==(const ShiftKey& other) const noexcept
        {
            return dayValue == other.dayValue && offset == other.offset;
        }
    };

    struct ShiftKeyHash final {
        std::size_t operator()(const ShiftKey& key) const noexcept
        {
            const std::size_t dayHash = std::hash<int32_t>{}(key.dayValue);
            const std::size_t offsetHash = std::hash<int32_t>{}(key.offset);
            return dayHash ^ (offsetHash << 1U);
        }
    };

    std::unordered_map<int32_t, std::optional<RebalanceTradingDay>> nextOnOrAfterTable_;
    std::unordered_map<int32_t, std::optional<RebalanceTradingDay>> previousOnOrBeforeTable_;
    std::unordered_map<ShiftKey, std::optional<RebalanceTradingDay>, ShiftKeyHash> shiftTable_;
};

TEST(RebalanceScheduleAbstractionTest, ReturnsInvalidInputWhenIntervalInvalid)
{
    FakeRebalanceCalendar calendar;
    const RebalanceScheduleBuilder builder(calendar);

    const RebalancePlanSpec spec{RebalanceDayRange{RebalanceTradingDay{20250101}, RebalanceTradingDay{20250131}},
                                 RebalanceIntervalDays{0},
                                 RebalanceAnchor::StartDay};

    const auto result = builder.build(spec);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, RebalancePlanError::InvalidInput);
}

TEST(RebalanceScheduleAbstractionTest, BuildsScheduleFromStartAnchor)
{
    FakeRebalanceCalendar calendar;
    const RebalanceScheduleBuilder builder(calendar);
    const int32_t intervalDays = 5;

    const RebalanceTradingDay windowStart{20250101};
    const RebalanceTradingDay windowEnd{20250120};
    const RebalanceTradingDay firstTradingDay{20250102};
    const RebalanceTradingDay secondTradingDay{20250109};
    const RebalanceTradingDay thirdTradingDay{20250116};

    calendar.setNextTradingDayOnOrAfter(windowStart, firstTradingDay);
    calendar.setShiftResult(firstTradingDay, intervalDays, secondTradingDay);
    calendar.setShiftResult(secondTradingDay, intervalDays, thirdTradingDay);
    calendar.setShiftResult(thirdTradingDay, intervalDays, std::nullopt);

    const RebalancePlanSpec spec{RebalanceDayRange{windowStart, windowEnd},
                                 RebalanceIntervalDays{intervalDays},
                                 RebalanceAnchor::StartDay};

    const auto result = builder.build(spec);

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(result.value->schedule.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(result.value->schedule[0].value, firstTradingDay.value);
    EXPECT_EQ(result.value->schedule[1].value, secondTradingDay.value);
    EXPECT_EQ(result.value->schedule[2].value, thirdTradingDay.value);
}

TEST(RebalanceScheduleAbstractionTest, BuildsScheduleFromEndAnchor)
{
    FakeRebalanceCalendar calendar;
    const RebalanceScheduleBuilder builder(calendar);
    const int32_t intervalDays = 5;
    const int32_t backwardOffset = -5;

    const RebalanceTradingDay windowStart{20250101};
    const RebalanceTradingDay windowEnd{20250120};
    const RebalanceTradingDay lastTradingDay{20250117};
    const RebalanceTradingDay middleTradingDay{20250110};
    const RebalanceTradingDay firstTradingDay{20250103};

    calendar.setPreviousTradingDayOnOrBefore(windowEnd, lastTradingDay);
    calendar.setShiftResult(lastTradingDay, backwardOffset, middleTradingDay);
    calendar.setShiftResult(middleTradingDay, backwardOffset, firstTradingDay);
    calendar.setShiftResult(firstTradingDay, backwardOffset, std::nullopt);

    const RebalancePlanSpec spec{RebalanceDayRange{windowStart, windowEnd},
                                 RebalanceIntervalDays{intervalDays},
                                 RebalanceAnchor::EndDay};

    const auto result = builder.build(spec);

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(result.value->schedule.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(result.value->schedule[0].value, firstTradingDay.value);
    EXPECT_EQ(result.value->schedule[1].value, middleTradingDay.value);
    EXPECT_EQ(result.value->schedule[2].value, lastTradingDay.value);
}

TEST(RebalanceScheduleAbstractionTest, ReturnsMissingTradingDayWhenStartAnchorDayInvalid)
{
    FakeRebalanceCalendar calendar;
    const RebalanceScheduleBuilder builder(calendar);

    const RebalanceTradingDay windowStart{20250101};
    const RebalanceTradingDay windowEnd{20250120};
    const RebalanceTradingDay invalidTradingDay{};

    calendar.setNextTradingDayOnOrAfter(windowStart, invalidTradingDay);

    const RebalancePlanSpec spec{RebalanceDayRange{windowStart, windowEnd},
                                 RebalanceIntervalDays{5},
                                 RebalanceAnchor::StartDay};

    const auto result = builder.build(spec);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, RebalancePlanError::MissingTradingDay);
}

TEST(RebalanceScheduleAbstractionTest, ReturnsMissingTradingDayWhenEndAnchorDayInvalid)
{
    FakeRebalanceCalendar calendar;
    const RebalanceScheduleBuilder builder(calendar);

    const RebalanceTradingDay windowStart{20250101};
    const RebalanceTradingDay windowEnd{20250120};
    const RebalanceTradingDay invalidTradingDay{};

    calendar.setPreviousTradingDayOnOrBefore(windowEnd, invalidTradingDay);

    const RebalancePlanSpec spec{RebalanceDayRange{windowStart, windowEnd},
                                 RebalanceIntervalDays{5},
                                 RebalanceAnchor::EndDay};

    const auto result = builder.build(spec);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, RebalancePlanError::MissingTradingDay);
}

TEST(RebalanceScheduleAbstractionTest, ReturnsInvalidCalendarProgressWhenStartAnchorShiftNotForward)
{
    FakeRebalanceCalendar calendar;
    const RebalanceScheduleBuilder builder(calendar);
    const int32_t intervalDays = 5;

    const RebalanceTradingDay windowStart{20250101};
    const RebalanceTradingDay windowEnd{20250120};
    const RebalanceTradingDay firstTradingDay{20250102};

    calendar.setNextTradingDayOnOrAfter(windowStart, firstTradingDay);
    calendar.setShiftResult(firstTradingDay, intervalDays, firstTradingDay);

    const RebalancePlanSpec spec{RebalanceDayRange{windowStart, windowEnd},
                                 RebalanceIntervalDays{intervalDays},
                                 RebalanceAnchor::StartDay};

    const auto result = builder.build(spec);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, RebalancePlanError::InvalidCalendarProgress);
}

TEST(RebalanceScheduleAbstractionTest, ReturnsInvalidCalendarProgressWhenEndAnchorShiftNotBackward)
{
    FakeRebalanceCalendar calendar;
    const RebalanceScheduleBuilder builder(calendar);
    const int32_t intervalDays = 5;
    const int32_t backwardOffset = -5;

    const RebalanceTradingDay windowStart{20250101};
    const RebalanceTradingDay windowEnd{20250120};
    const RebalanceTradingDay lastTradingDay{20250117};

    calendar.setPreviousTradingDayOnOrBefore(windowEnd, lastTradingDay);
    calendar.setShiftResult(lastTradingDay, backwardOffset, lastTradingDay);

    const RebalancePlanSpec spec{RebalanceDayRange{windowStart, windowEnd},
                                 RebalanceIntervalDays{intervalDays},
                                 RebalanceAnchor::EndDay};

    const auto result = builder.build(spec);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, RebalancePlanError::InvalidCalendarProgress);
}

using astock::domain::backtest::grouping_allocation::AllocationMode;
using astock::domain::backtest::grouping_allocation::GroupCount;
using astock::domain::backtest::grouping_allocation::GroupingAllocationError;
using astock::domain::backtest::grouping_allocation::GroupingAllocationSpec;
using astock::domain::backtest::grouping_allocation::QuantileGroupingAllocator;
using astock::domain::backtest::grouping_allocation::RankScore;
using astock::domain::backtest::grouping_allocation::RankedInstrument;
using GroupedInstrumentId = astock::domain::backtest::grouping_allocation::InstrumentId;

TEST(GroupingAllocationAbstractionTest, ReturnsEmptyUniverseWhenRankedInputEmpty)
{
    const QuantileGroupingAllocator allocator;
    const GroupingAllocationSpec spec{GroupCount{2}, AllocationMode::EqualWithinGroup};

    const auto result = allocator.build(spec, {});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, GroupingAllocationError::EmptyUniverse);
}

TEST(GroupingAllocationAbstractionTest, BuildsEqualWithinGroupWeights)
{
    const QuantileGroupingAllocator allocator;
    const GroupingAllocationSpec spec{GroupCount{2}, AllocationMode::EqualWithinGroup};

    std::vector<RankedInstrument> ranked;
    ranked.reserve(4U);
    ranked.push_back(RankedInstrument{GroupedInstrumentId{100004}, RankScore{40}});
    ranked.push_back(RankedInstrument{GroupedInstrumentId{100003}, RankScore{30}});
    ranked.push_back(RankedInstrument{GroupedInstrumentId{100002}, RankScore{20}});
    ranked.push_back(RankedInstrument{GroupedInstrumentId{100001}, RankScore{10}});

    const auto result = allocator.build(spec, std::move(ranked));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(result.value->grouped.buckets.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(result.value->weights.byInstrument.size(), static_cast<std::size_t>(4));
    EXPECT_EQ(result.value->weights.byInstrument.at(100004U).value, 2500);
    EXPECT_EQ(result.value->weights.byInstrument.at(100003U).value, 2500);
    EXPECT_EQ(result.value->weights.byInstrument.at(100002U).value, 2500);
    EXPECT_EQ(result.value->weights.byInstrument.at(100001U).value, 2500);
}

TEST(GroupingAllocationAbstractionTest, BuildsScoreProportionalWithinGroupWeights)
{
    const QuantileGroupingAllocator allocator;
    const GroupingAllocationSpec spec{GroupCount{2}, AllocationMode::ScoreProportionalWithinGroup};

    std::vector<RankedInstrument> ranked;
    ranked.reserve(4U);
    ranked.push_back(RankedInstrument{GroupedInstrumentId{100004}, RankScore{100}});
    ranked.push_back(RankedInstrument{GroupedInstrumentId{100003}, RankScore{50}});
    ranked.push_back(RankedInstrument{GroupedInstrumentId{100002}, RankScore{40}});
    ranked.push_back(RankedInstrument{GroupedInstrumentId{100001}, RankScore{10}});

    const auto result = allocator.build(spec, std::move(ranked));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(result.value->weights.byInstrument.size(), static_cast<std::size_t>(4));
    EXPECT_EQ(result.value->weights.byInstrument.at(100004U).value, 3333);
    EXPECT_EQ(result.value->weights.byInstrument.at(100003U).value, 1667);
    EXPECT_EQ(result.value->weights.byInstrument.at(100002U).value, 4000);
    EXPECT_EQ(result.value->weights.byInstrument.at(100001U).value, 1000);
}

TEST(GroupingAllocationAbstractionTest, ReturnsInvalidRankedInstrumentWhenDuplicateInstrumentExists)
{
    const QuantileGroupingAllocator allocator;
    const GroupingAllocationSpec spec{GroupCount{2}, AllocationMode::EqualWithinGroup};

    std::vector<RankedInstrument> ranked;
    ranked.reserve(2U);
    ranked.push_back(RankedInstrument{GroupedInstrumentId{100001}, RankScore{20}});
    ranked.push_back(RankedInstrument{GroupedInstrumentId{100001}, RankScore{10}});

    const auto result = allocator.build(spec, std::move(ranked));

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, GroupingAllocationError::InvalidRankedInstrument);
}

using astock::domain::backtest::position_constraints::PositionConstraintAllocator;
using astock::domain::backtest::position_constraints::PositionConstraintError;
using astock::domain::backtest::position_constraints::PositionConstraintSpec;
using astock::domain::backtest::position_constraints::PositionCount;
using ConstrainedRankedCandidate = astock::domain::backtest::position_constraints::RankedCandidate;
using ConstrainedRankScore = astock::domain::backtest::position_constraints::RankScore;
using ConstrainedWeightBps = astock::domain::backtest::position_constraints::WeightBps;
using ConstrainedInstrumentId = astock::domain::backtest::position_constraints::InstrumentId;

TEST(PositionConstraintAllocationAbstractionTest, ReturnsInvalidInputWhenSpecInvalid)
{
    const PositionConstraintAllocator allocator;
    const PositionConstraintSpec spec{PositionCount{0}, ConstrainedWeightBps{10000}, ConstrainedWeightBps{5000}};

    const auto result = allocator.allocate(spec, {ConstrainedRankedCandidate{ConstrainedInstrumentId{100001}, ConstrainedRankScore{10}}});

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, PositionConstraintError::InvalidInput);
}

TEST(PositionConstraintAllocationAbstractionTest, AllocatesEqualSharesWithinMaxSingleLimit)
{
    const PositionConstraintAllocator allocator;
    const PositionConstraintSpec spec{PositionCount{2}, ConstrainedWeightBps{10000}, ConstrainedWeightBps{6000}};

    std::vector<ConstrainedRankedCandidate> candidates;
    candidates.reserve(3U);
    candidates.push_back(ConstrainedRankedCandidate{ConstrainedInstrumentId{100001}, ConstrainedRankScore{30}});
    candidates.push_back(ConstrainedRankedCandidate{ConstrainedInstrumentId{100002}, ConstrainedRankScore{20}});
    candidates.push_back(ConstrainedRankedCandidate{ConstrainedInstrumentId{100003}, ConstrainedRankScore{10}});

    const auto result = allocator.allocate(spec, std::move(candidates));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(result.value->byInstrument.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(result.value->byInstrument.at(100001U).value, 5000);
    EXPECT_EQ(result.value->byInstrument.at(100002U).value, 5000);
    EXPECT_EQ(result.value->unallocatedBudget.value, 0);
}

TEST(PositionConstraintAllocationAbstractionTest, KeepsUnallocatedBudgetWhenSingleCapTooLow)
{
    const PositionConstraintAllocator allocator;
    const PositionConstraintSpec spec{PositionCount{2}, ConstrainedWeightBps{10000}, ConstrainedWeightBps{4000}};

    std::vector<ConstrainedRankedCandidate> candidates;
    candidates.reserve(2U);
    candidates.push_back(ConstrainedRankedCandidate{ConstrainedInstrumentId{100002}, ConstrainedRankScore{50}});
    candidates.push_back(ConstrainedRankedCandidate{ConstrainedInstrumentId{100001}, ConstrainedRankScore{40}});

    const auto result = allocator.allocate(spec, std::move(candidates));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(result.value->byInstrument.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(result.value->byInstrument.at(100002U).value, 4000);
    EXPECT_EQ(result.value->byInstrument.at(100001U).value, 4000);
    EXPECT_EQ(result.value->unallocatedBudget.value, 2000);
}

TEST(PositionConstraintAllocationAbstractionTest, ReturnsInvalidCandidateWhenDuplicateInstrumentExists)
{
    const PositionConstraintAllocator allocator;
    const PositionConstraintSpec spec{PositionCount{2}, ConstrainedWeightBps{10000}, ConstrainedWeightBps{5000}};

    std::vector<ConstrainedRankedCandidate> candidates;
    candidates.reserve(2U);
    candidates.push_back(ConstrainedRankedCandidate{ConstrainedInstrumentId{100001}, ConstrainedRankScore{30}});
    candidates.push_back(ConstrainedRankedCandidate{ConstrainedInstrumentId{100001}, ConstrainedRankScore{20}});

    const auto result = allocator.allocate(spec, std::move(candidates));

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, PositionConstraintError::InvalidCandidate);
}

using astock::domain::backtest::trading_cost::BpsRate;
using astock::domain::backtest::trading_cost::LinearBpsTradingCostModel;
using astock::domain::backtest::trading_cost::OrderSide;
using astock::domain::backtest::trading_cost::PriceTicks;
using astock::domain::backtest::trading_cost::QuantityLots;
using astock::domain::backtest::trading_cost::TradeFill;
using astock::domain::backtest::trading_cost::TradingCostError;
using astock::domain::backtest::trading_cost::TradingCostSpec;

TEST(TradingCostAbstractionTest, ReturnsInvalidInputForInvalidFill)
{
    const LinearBpsTradingCostModel model;
    const TradeFill fill{PriceTicks{0}, QuantityLots{100}, OrderSide::Buy};
    const TradingCostSpec spec{BpsRate{10}, BpsRate{5}, BpsRate{10}};

    const auto result = model.calculate(fill, spec);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, TradingCostError::InvalidInput);
}

TEST(TradingCostAbstractionTest, BuySideHasNoTaxCost)
{
    const LinearBpsTradingCostModel model;
    const TradeFill fill{PriceTicks{10}, QuantityLots{100}, OrderSide::Buy};
    const TradingCostSpec spec{BpsRate{10}, BpsRate{5}, BpsRate{10}};

    const int64_t notionalMicros = 10LL * 100LL * LinearBpsTradingCostModel::kMicrosPerPriceTick;
    const int64_t expectedCommission = (notionalMicros * 10LL) / LinearBpsTradingCostModel::kBpsBase;
    const int64_t expectedSlippage = (notionalMicros * 5LL) / LinearBpsTradingCostModel::kBpsBase;
    const int64_t expectedTax = 0LL;
    const int64_t expectedTotal = expectedCommission + expectedSlippage + expectedTax;

    const auto result = model.calculate(fill, spec);

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(result.value->commission.value, expectedCommission);
    EXPECT_EQ(result.value->slippage.value, expectedSlippage);
    EXPECT_EQ(result.value->tax.value, expectedTax);
    EXPECT_EQ(result.value->total.value, expectedTotal);
}

TEST(TradingCostAbstractionTest, SellSideIncludesTaxCost)
{
    const LinearBpsTradingCostModel model;
    const TradeFill fill{PriceTicks{10}, QuantityLots{100}, OrderSide::Sell};
    const TradingCostSpec spec{BpsRate{10}, BpsRate{5}, BpsRate{10}};

    const int64_t notionalMicros = 10LL * 100LL * LinearBpsTradingCostModel::kMicrosPerPriceTick;
    const int64_t expectedCommission = (notionalMicros * 10LL) / LinearBpsTradingCostModel::kBpsBase;
    const int64_t expectedSlippage = (notionalMicros * 5LL) / LinearBpsTradingCostModel::kBpsBase;
    const int64_t expectedTax = (notionalMicros * 10LL) / LinearBpsTradingCostModel::kBpsBase;
    const int64_t expectedTotal = expectedCommission + expectedSlippage + expectedTax;

    const auto result = model.calculate(fill, spec);

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(result.value->commission.value, expectedCommission);
    EXPECT_EQ(result.value->slippage.value, expectedSlippage);
    EXPECT_EQ(result.value->tax.value, expectedTax);
    EXPECT_EQ(result.value->total.value, expectedTotal);
}

TEST(TradingCostAbstractionTest, ReturnsInvalidInputWhenNotionalOverflows)
{
    const LinearBpsTradingCostModel model;
    const TradeFill fill{PriceTicks{2147483647}, QuantityLots{2147483647}, OrderSide::Buy};
    const TradingCostSpec spec{BpsRate{10}, BpsRate{5}, BpsRate{10}};

    const auto result = model.calculate(fill, spec);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, TradingCostError::InvalidInput);
}

using astock::domain::backtest::signal_orders::LinearSignalOrderTranslator;
using astock::domain::backtest::signal_orders::OrderAction;
using astock::domain::backtest::signal_orders::SignalBps;
using astock::domain::backtest::signal_orders::SignalSnapshot;
using astock::domain::backtest::signal_orders::TranslationError;
using astock::domain::backtest::signal_orders::TranslationSpec;
using SignalWeightDeltaBps = astock::domain::backtest::signal_orders::WeightDeltaBps;
using SignalInstrumentId = astock::domain::backtest::signal_orders::InstrumentId;

TEST(SignalOrderTranslatorAbstractionTest, ReturnsInvalidInputWhenSpecInvalid)
{
    const LinearSignalOrderTranslator translator;
    const TranslationSpec spec{SignalWeightDeltaBps{0}, SignalWeightDeltaBps{3000}};

    std::vector<SignalSnapshot> signalSnapshots;
    signalSnapshots.push_back(SignalSnapshot{SignalInstrumentId{100001}, SignalBps{1000}});

    const auto result = translator.translate(spec, std::move(signalSnapshots));
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, TranslationError::InvalidInput);
}

TEST(SignalOrderTranslatorAbstractionTest, TranslatesSignalsWithBuySellCaps)
{
    const LinearSignalOrderTranslator translator;
    const TranslationSpec spec{SignalWeightDeltaBps{3000}, SignalWeightDeltaBps{2000}};

    std::vector<SignalSnapshot> signalSnapshots;
    signalSnapshots.push_back(SignalSnapshot{SignalInstrumentId{100002}, SignalBps{-4000}});
    signalSnapshots.push_back(SignalSnapshot{SignalInstrumentId{100001}, SignalBps{5000}});
    signalSnapshots.push_back(SignalSnapshot{SignalInstrumentId{100003}, SignalBps{0}});

    const auto result = translator.translate(spec, std::move(signalSnapshots));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(result.value->items.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(result.value->items[0].instrument.value, 100001U);
    EXPECT_EQ(result.value->items[0].action, OrderAction::Buy);
    EXPECT_EQ(result.value->items[0].delta.value, 3000);
    EXPECT_EQ(result.value->items[1].instrument.value, 100002U);
    EXPECT_EQ(result.value->items[1].action, OrderAction::Sell);
    EXPECT_EQ(result.value->items[1].delta.value, 2000);
}

TEST(SignalOrderTranslatorAbstractionTest, ReturnsDuplicateInstrumentWhenSameInstrumentRepeated)
{
    const LinearSignalOrderTranslator translator;
    const TranslationSpec spec{SignalWeightDeltaBps{3000}, SignalWeightDeltaBps{3000}};

    std::vector<SignalSnapshot> signalSnapshots;
    signalSnapshots.push_back(SignalSnapshot{SignalInstrumentId{100001}, SignalBps{2000}});
    signalSnapshots.push_back(SignalSnapshot{SignalInstrumentId{100001}, SignalBps{-1000}});

    const auto result = translator.translate(spec, std::move(signalSnapshots));
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, TranslationError::DuplicateInstrument);
}

using astock::domain::backtest::execution_state::ExecutionFill;
using astock::domain::backtest::execution_state::FillSide;
using astock::domain::backtest::execution_state::LongOnlyPositionStateMachine;
using astock::domain::backtest::execution_state::NetPositionStateMachine;
using astock::domain::backtest::execution_state::PositionState;
using astock::domain::backtest::execution_state::PositionTransitionError;
using ExecutionQuantityLots = astock::domain::backtest::execution_state::QuantityLots;
using ExecutionInstrumentId = astock::domain::backtest::execution_state::InstrumentId;

TEST(PositionStateMachineAbstractionTest, NetModelCoversShortBeforeAddingLong)
{
    const NetPositionStateMachine machine;
    PositionState current{ExecutionInstrumentId{100001}, ExecutionQuantityLots{0}, ExecutionQuantityLots{50}};
    const ExecutionFill fill{ExecutionInstrumentId{100001}, FillSide::Buy, ExecutionQuantityLots{80}};

    const auto result = machine.apply(current, fill);

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(result.value->longQuantity.value, 30);
    EXPECT_EQ(result.value->shortQuantity.value, 0);
}

TEST(PositionStateMachineAbstractionTest, NetModelSellCreatesShortAfterClosingLong)
{
    const NetPositionStateMachine machine;
    PositionState current{ExecutionInstrumentId{100001}, ExecutionQuantityLots{40}, ExecutionQuantityLots{0}};
    const ExecutionFill fill{ExecutionInstrumentId{100001}, FillSide::Sell, ExecutionQuantityLots{70}};

    const auto result = machine.apply(current, fill);

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(result.value->longQuantity.value, 0);
    EXPECT_EQ(result.value->shortQuantity.value, 30);
}

TEST(PositionStateMachineAbstractionTest, LongOnlyModelRejectsOversell)
{
    const LongOnlyPositionStateMachine machine;
    PositionState current{ExecutionInstrumentId{100001}, ExecutionQuantityLots{20}, ExecutionQuantityLots{0}};
    const ExecutionFill fill{ExecutionInstrumentId{100001}, FillSide::Sell, ExecutionQuantityLots{25}};

    const auto result = machine.apply(current, fill);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, PositionTransitionError::InsufficientLongPosition);
}

TEST(PositionStateMachineAbstractionTest, LongOnlyModelReturnsInvalidInputWhenBuyOverflows)
{
    const LongOnlyPositionStateMachine machine;
    PositionState current{ExecutionInstrumentId{100001}, ExecutionQuantityLots{2147483647}, ExecutionQuantityLots{0}};
    const ExecutionFill fill{ExecutionInstrumentId{100001}, FillSide::Buy, ExecutionQuantityLots{1}};

    const auto result = machine.apply(current, fill);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, PositionTransitionError::InvalidInput);
}

using RiskDeltaBps = astock::domain::backtest::risk_approval::DeltaBps;
using RiskOrderAction = astock::domain::backtest::risk_approval::OrderAction;
using astock::domain::backtest::risk_approval::OrderCandidate;
using astock::domain::backtest::risk_approval::RejectReason;
using astock::domain::backtest::risk_approval::RiskApprovalError;
using astock::domain::backtest::risk_approval::RiskLimitsSpec;
using astock::domain::backtest::risk_approval::RiskRuntimeContext;
using astock::domain::backtest::risk_approval::SequentialRiskApprovalEngine;
using RiskInstrumentId = astock::domain::backtest::risk_approval::InstrumentId;

TEST(RiskApprovalAbstractionTest, ReturnsInvalidInputWhenLimitsInvalid)
{
    const SequentialRiskApprovalEngine engine;
    const RiskLimitsSpec limits{RiskDeltaBps{2000}, RiskDeltaBps{5000}, 0};
    const RiskRuntimeContext context{RiskDeltaBps{0}};

    std::vector<OrderCandidate> orders;
    orders.push_back(OrderCandidate{RiskInstrumentId{100001}, RiskOrderAction::Buy, RiskDeltaBps{1000}});

    const auto result = engine.evaluate(limits, context, std::move(orders));

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, RiskApprovalError::InvalidInput);
}

TEST(RiskApprovalAbstractionTest, RejectsOrderWhenSingleOrderLimitExceeded)
{
    const SequentialRiskApprovalEngine engine;
    const RiskLimitsSpec limits{RiskDeltaBps{1500}, RiskDeltaBps{5000}, 10};
    const RiskRuntimeContext context{RiskDeltaBps{0}};

    std::vector<OrderCandidate> orders;
    orders.push_back(OrderCandidate{RiskInstrumentId{100001}, RiskOrderAction::Buy, RiskDeltaBps{2000}});

    const auto result = engine.evaluate(limits, context, std::move(orders));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    EXPECT_TRUE(result.value->approved.empty());
    ASSERT_EQ(result.value->rejected.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(result.value->rejected[0].reason, RejectReason::SingleOrderLimitExceeded);
}

TEST(RiskApprovalAbstractionTest, AppliesTurnoverAndOrderCountLimitsSequentially)
{
    const SequentialRiskApprovalEngine engine;
    const RiskLimitsSpec limits{RiskDeltaBps{3000}, RiskDeltaBps{5000}, 2};
    const RiskRuntimeContext context{RiskDeltaBps{1000}};

    std::vector<OrderCandidate> orders;
    orders.push_back(OrderCandidate{RiskInstrumentId{100001}, RiskOrderAction::Buy, RiskDeltaBps{2000}});
    orders.push_back(OrderCandidate{RiskInstrumentId{100002}, RiskOrderAction::Sell, RiskDeltaBps{1500}});
    orders.push_back(OrderCandidate{RiskInstrumentId{100003}, RiskOrderAction::Buy, RiskDeltaBps{1200}});

    const auto result = engine.evaluate(limits, context, std::move(orders));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(result.value->approved.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(result.value->rejected.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(result.value->approved[0].instrument.value, 100001U);
    EXPECT_EQ(result.value->approved[1].instrument.value, 100002U);
    EXPECT_EQ(result.value->rejected[0].reason, RejectReason::OrderCountLimitExceeded);
    EXPECT_EQ(result.value->finalConsumedTurnover.value, 4500);
}

TEST(RiskApprovalAbstractionTest, ReturnsInvalidOrderWhenDuplicateInstrumentExists)
{
    const SequentialRiskApprovalEngine engine;
    const RiskLimitsSpec limits{RiskDeltaBps{3000}, RiskDeltaBps{5000}, 2};
    const RiskRuntimeContext context{RiskDeltaBps{1000}};

    std::vector<OrderCandidate> orders;
    orders.push_back(OrderCandidate{RiskInstrumentId{100001}, RiskOrderAction::Buy, RiskDeltaBps{1000}});
    orders.push_back(OrderCandidate{RiskInstrumentId{100001}, RiskOrderAction::Sell, RiskDeltaBps{800}});

    const auto result = engine.evaluate(limits, context, std::move(orders));

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, RiskApprovalError::InvalidOrder);
}

using astock::domain::backtest::order_routing::ExecutionIntent;
using astock::domain::backtest::order_routing::ExecutionVenue;
using astock::domain::backtest::order_routing::LiquidityIntent;
using RoutingOrderAction = astock::domain::backtest::order_routing::OrderAction;
using astock::domain::backtest::order_routing::OrderRoutingError;
using astock::domain::backtest::order_routing::RoutingSpec;
using astock::domain::backtest::order_routing::SimpleOrderRouter;
using RoutingDeltaBps = astock::domain::backtest::order_routing::DeltaBps;
using RoutingInstrumentId = astock::domain::backtest::order_routing::InstrumentId;

TEST(OrderRoutingAbstractionTest, ReturnsInvalidInputWhenSpecInvalid)
{
    const SimpleOrderRouter router;
    const RoutingSpec spec{RoutingDeltaBps{3000}, RoutingDeltaBps{0}, RoutingDeltaBps{800}};

    std::vector<ExecutionIntent> intents;
    intents.push_back(ExecutionIntent{RoutingInstrumentId{100001}, RoutingOrderAction::Buy, RoutingDeltaBps{500}, RoutingDeltaBps{500}});

    const auto result = router.route(spec, std::move(intents));

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, OrderRoutingError::InvalidInput);
}

TEST(OrderRoutingAbstractionTest, RoutesAggressiveIntentToPrimaryVenue)
{
    const SimpleOrderRouter router;
    const RoutingSpec spec{RoutingDeltaBps{3000}, RoutingDeltaBps{1200}, RoutingDeltaBps{800}};

    std::vector<ExecutionIntent> intents;
    intents.push_back(ExecutionIntent{RoutingInstrumentId{100001}, RoutingOrderAction::Buy, RoutingDeltaBps{1000}, RoutingDeltaBps{1600}});

    const auto result = router.route(spec, std::move(intents));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(result.value->items.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(result.value->items[0].venue, ExecutionVenue::Primary);
    EXPECT_EQ(result.value->items[0].intent, LiquidityIntent::Aggressive);
}

TEST(OrderRoutingAbstractionTest, RoutesLowUrgencySmallOrderToSecondaryPassive)
{
    const SimpleOrderRouter router;
    const RoutingSpec spec{RoutingDeltaBps{3000}, RoutingDeltaBps{1200}, RoutingDeltaBps{800}};

    std::vector<ExecutionIntent> intents;
    intents.push_back(ExecutionIntent{RoutingInstrumentId{100001}, RoutingOrderAction::Sell, RoutingDeltaBps{700}, RoutingDeltaBps{300}});

    const auto result = router.route(spec, std::move(intents));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    ASSERT_EQ(result.value->items.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(result.value->items[0].venue, ExecutionVenue::Secondary);
    EXPECT_EQ(result.value->items[0].intent, LiquidityIntent::Passive);
}

TEST(OrderRoutingAbstractionTest, ReturnsInvalidIntentWhenDuplicateInstrumentExists)
{
    const SimpleOrderRouter router;
    const RoutingSpec spec{RoutingDeltaBps{3000}, RoutingDeltaBps{1200}, RoutingDeltaBps{800}};

    std::vector<ExecutionIntent> intents;
    intents.push_back(ExecutionIntent{RoutingInstrumentId{100001}, RoutingOrderAction::Buy, RoutingDeltaBps{500}, RoutingDeltaBps{900}});
    intents.push_back(ExecutionIntent{RoutingInstrumentId{100001}, RoutingOrderAction::Sell, RoutingDeltaBps{500}, RoutingDeltaBps{500}});

    const auto result = router.route(spec, std::move(intents));

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, OrderRoutingError::InvalidIntent);
}

using astock::domain::backtest::portfolio_valuation::HoldingState;
using astock::domain::backtest::portfolio_valuation::MarkToMarketValuationEngine;
using astock::domain::backtest::portfolio_valuation::MarketQuote;
using astock::domain::backtest::portfolio_valuation::PortfolioValuationError;
using astock::domain::backtest::portfolio_valuation::ValuationSpec;
using ValuationInstrumentId = astock::domain::backtest::portfolio_valuation::InstrumentId;
using ValuationPriceTicks = astock::domain::backtest::portfolio_valuation::PriceTicks;
using ValuationQuantityLots = astock::domain::backtest::portfolio_valuation::QuantityLots;

TEST(PortfolioValuationAbstractionTest, ReturnsInvalidInputWhenSpecInvalid)
{
    const MarkToMarketValuationEngine engine;
    const ValuationSpec spec{0};

    const auto result = engine.evaluate(spec, {}, {});

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, PortfolioValuationError::InvalidInput);
}

TEST(PortfolioValuationAbstractionTest, ReturnsMissingQuoteWhenQuoteAbsent)
{
    const MarkToMarketValuationEngine engine;
    const ValuationSpec spec{ValuationSpec::kDefaultMicrosPerTick};

    std::vector<HoldingState> holdings;
    holdings.push_back(HoldingState{ValuationInstrumentId{100001}, ValuationQuantityLots{10}, ValuationQuantityLots{0}});

    std::vector<MarketQuote> quotes;
    quotes.push_back(MarketQuote{ValuationInstrumentId{100002}, ValuationPriceTicks{10}});

    const auto result = engine.evaluate(spec, std::move(holdings), std::move(quotes));

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, PortfolioValuationError::MissingQuote);
}

TEST(PortfolioValuationAbstractionTest, ReturnsInvalidQuoteWhenQuoteDuplicated)
{
    const MarkToMarketValuationEngine engine;
    const ValuationSpec spec{ValuationSpec::kDefaultMicrosPerTick};

    std::vector<HoldingState> holdings;
    holdings.push_back(HoldingState{ValuationInstrumentId{100001}, ValuationQuantityLots{10}, ValuationQuantityLots{0}});

    std::vector<MarketQuote> quotes;
    quotes.push_back(MarketQuote{ValuationInstrumentId{100001}, ValuationPriceTicks{20}});
    quotes.push_back(MarketQuote{ValuationInstrumentId{100001}, ValuationPriceTicks{21}});

    const auto result = engine.evaluate(spec, std::move(holdings), std::move(quotes));

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, PortfolioValuationError::InvalidQuote);
}

TEST(PortfolioValuationAbstractionTest, ReturnsInvalidHoldingWhenHoldingDuplicated)
{
    const MarkToMarketValuationEngine engine;
    const ValuationSpec spec{ValuationSpec::kDefaultMicrosPerTick};

    std::vector<HoldingState> holdings;
    holdings.push_back(HoldingState{ValuationInstrumentId{100001}, ValuationQuantityLots{10}, ValuationQuantityLots{0}});
    holdings.push_back(HoldingState{ValuationInstrumentId{100001}, ValuationQuantityLots{5}, ValuationQuantityLots{0}});

    std::vector<MarketQuote> quotes;
    quotes.push_back(MarketQuote{ValuationInstrumentId{100001}, ValuationPriceTicks{20}});

    const auto result = engine.evaluate(spec, std::move(holdings), std::move(quotes));

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, PortfolioValuationError::InvalidHolding);
}

TEST(PortfolioValuationAbstractionTest, ComputesNetValueFromLongAndShort)
{
    const MarkToMarketValuationEngine engine;
    const ValuationSpec spec{ValuationSpec::kDefaultMicrosPerTick};

    std::vector<HoldingState> holdings;
    holdings.push_back(HoldingState{ValuationInstrumentId{100001}, ValuationQuantityLots{10}, ValuationQuantityLots{2}});

    std::vector<MarketQuote> quotes;
    quotes.push_back(MarketQuote{ValuationInstrumentId{100001}, ValuationPriceTicks{20}});

    const auto result = engine.evaluate(spec, std::move(holdings), std::move(quotes));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(result.value->grossLongMicros, 200000000);
    EXPECT_EQ(result.value->grossShortMicros, 40000000);
    EXPECT_EQ(result.value->netMicros, 160000000);
}

using astock::domain::backtest::performance_metrics::BasicPerformanceMetricsAggregator;
using astock::domain::backtest::performance_metrics::EquityPoint;
using astock::domain::backtest::performance_metrics::MetricsError;
using astock::domain::backtest::performance_metrics::MetricsSpec;

using domain::backtest::BacktestLayerViolationCode;
using domain::backtest::BacktestRequest;
using domain::backtest::StrictBacktestLayerGuard;

BacktestRequest makeValidLayerGuardRequest()
{
    BacktestRequest request;

    request.strategyIdentity.strategyId = domain::strategy::StrategyId(QStringLiteral("strategy_1"));
    request.strategyIdentity.strategyCode = domain::strategy::StrategyCode(QStringLiteral("code_1"));
    request.strategyIdentity.strategyName = domain::strategy::StrategyName(QStringLiteral("name_1"));
    request.strategyIdentity.storedType = domain::backtest::StrategyStoredType::DOUBLE_MOVING_AVERAGE;

    request.strategySpec.ruleProfile.maxPositionRatio = domain::strategy::Ratio{0.9};
    request.strategySpec.ruleProfile.maxTotalExposureRatio = domain::strategy::Ratio{1.0};
    request.strategySpec.ruleProfile.stopLossRatio = domain::strategy::Ratio{0.1};
    request.strategySpec.ruleProfile.takeProfitRatio = domain::strategy::Ratio{0.2};
    request.strategySpec.ruleProfile.rebalanceDays = 5;

    request.strategySpec.executionPolicy.rebalanceFrequencyDays = domain::strategy::RebalanceFrequencyDays{5};

    request.strategySpec.strategyScopeContext.universe.universeMode = domain::strategy::UniverseMode::ExplicitSymbols;
    request.strategySpec.strategyScopeContext.universe.explicitSymbols.append(
        domain::strategy::SymbolCode(QStringLiteral("000001.SZ")));

    request.strategySpec.factorOverlay.enabled = true;
    request.strategySpec.factorOverlay.targetPositionCount = 12;
    request.strategySpec.factorOverlay.minimumCompositeScore = 0.5;
    request.strategySpec.factorOverlay.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_1")));
    request.strategySpec.factorOverlay.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_1")), 100.0});

    request.universeSpec.universeMode = domain::strategy::UniverseMode::ExplicitSymbols;
    request.universeSpec.explicitSymbols.append(domain::strategy::SymbolCode(QStringLiteral("000001.SZ")));

    request.costSpec.initialCapital = domain::strategy::Money{1000000.0};
    request.costSpec.commissionRate = domain::strategy::Ratio{0.001};
    request.costSpec.slippageRate = domain::strategy::Ratio{0.001};
    request.costSpec.taxRate = domain::strategy::Ratio{0.001};

    request.riskSpec.maxPositionRatio = domain::strategy::Ratio{0.9};
    request.riskSpec.maxSinglePositionRatio = domain::strategy::Ratio{0.2};
    request.riskSpec.maxDrawdownLimit = domain::strategy::Ratio{0.3};
    request.riskSpec.stopLossRate = domain::strategy::Ratio{0.1};

    request.executionSpec.rebalanceFrequencyDays = 5;

    request.factorOverlaySpec.enabled = true;
    request.factorOverlaySpec.targetPositionCount = 12;
    request.factorOverlaySpec.minimumCompositeScore = 0.5;
    request.factorOverlaySpec.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_1")));
    request.factorOverlaySpec.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_1")), 100.0});

    request.runtimeOptions.maxThreads = 4;
    request.runtimeOptions.cacheTtlSeconds = 0;

    request.window.startDate = QDate(2024, 1, 1);
    request.window.endDate = QDate(2024, 12, 31);
    return request;
}

TEST(PerformanceMetricsAbstractionTest, ReturnsInsufficientDataWhenSeriesTooShort)
{
    const BasicPerformanceMetricsAggregator aggregator;
    const MetricsSpec spec{252};

    std::vector<EquityPoint> series;
    series.push_back(EquityPoint{0, 1000000});

    const auto result = aggregator.aggregate(spec, std::move(series));

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, MetricsError::InsufficientData);
}

TEST(PerformanceMetricsAbstractionTest, ComputesReturnAndDrawdown)
{
    const BasicPerformanceMetricsAggregator aggregator;
    const MetricsSpec spec{252};

    std::vector<EquityPoint> series;
    series.push_back(EquityPoint{0, 1000000});
    series.push_back(EquityPoint{1, 1200000});
    series.push_back(EquityPoint{2, 900000});
    series.push_back(EquityPoint{3, 1300000});

    const auto result = aggregator.aggregate(spec, std::move(series));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(result.value->totalReturnBps, 3000);
    EXPECT_EQ(result.value->maxDrawdownBps, 2500);
}

TEST(PerformanceMetricsAbstractionTest, ComputesPositiveVolatilityOnFluctuatingSeries)
{
    const BasicPerformanceMetricsAggregator aggregator;
    const MetricsSpec spec{252};

    std::vector<EquityPoint> series;
    series.push_back(EquityPoint{0, 1000000});
    series.push_back(EquityPoint{1, 1100000});
    series.push_back(EquityPoint{2, 1000000});

    const auto result = aggregator.aggregate(spec, std::move(series));

    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result.value.has_value());
    EXPECT_GT(result.value->volatilityBps, 0);
}

TEST(PerformanceMetricsAbstractionTest, ReturnsInvalidPointWhenDayIndexNotStrictlyIncreasing)
{
    const BasicPerformanceMetricsAggregator aggregator;
    const MetricsSpec spec{252};

    std::vector<EquityPoint> series;
    series.push_back(EquityPoint{0, 1000000});
    series.push_back(EquityPoint{0, 1100000});
    series.push_back(EquityPoint{2, 1200000});

    const auto result = aggregator.aggregate(spec, std::move(series));

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, MetricsError::InvalidPoint);
}

TEST(PerformanceMetricsAbstractionTest, ReturnsInvalidPointWhenTotalReturnOverflowsInt32Range)
{
    const BasicPerformanceMetricsAggregator aggregator;
    const MetricsSpec spec{252};

    std::vector<EquityPoint> series;
    series.push_back(EquityPoint{0, 1});
    series.push_back(EquityPoint{1, 9223372036854775807LL});

    const auto result = aggregator.aggregate(spec, std::move(series));

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error, MetricsError::InvalidPoint);
}

TEST(BacktestLayerGuardTest, AcceptsAlignedSessionAndStrategyOwnership)
{
    const StrictBacktestLayerGuard guard;
    const BacktestRequest request = makeValidLayerGuardRequest();

    const auto result = guard.validate(request);
    EXPECT_TRUE(result.ok());
}

TEST(BacktestLayerGuardTest, RejectsRebalanceFrequencyOverrideFromSession)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();
    request.executionSpec.rebalanceFrequencyDays = 7;

    const auto result = guard.validate(request);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.violations.contains(BacktestLayerViolationCode::RebalanceDaysMustComeFromStrategyDefinition));
}

TEST(BacktestLayerGuardTest, RejectsStrategyExecutionPolicyRebalanceMismatch)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();
    request.strategySpec.executionPolicy.rebalanceFrequencyDays = domain::strategy::RebalanceFrequencyDays{7};

    const auto result = guard.validate(request);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.violations.contains(BacktestLayerViolationCode::StrategyExecutionPolicyRebalanceDaysMismatch));
}

TEST(BacktestLayerGuardTest, RejectsOverlayEnableFlagMismatch)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();
    request.factorOverlaySpec.enabled = false;

    const auto result = guard.validate(request);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.violations.contains(BacktestLayerViolationCode::OverlayEnableFlagMismatch));
}

TEST(BacktestLayerGuardTest, RejectsOverlayTargetPositionMismatch)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();
    request.factorOverlaySpec.targetPositionCount = 8;

    const auto result = guard.validate(request);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.violations.contains(BacktestLayerViolationCode::OverlayTargetPositionCountMismatch));
}

TEST(BacktestLayerGuardTest, RejectsExecutionPositionSizingMismatch)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();
    request.executionSpec.positionSizingMethod = domain::strategy::PositionSizingMethod::EqualWeight;

    const auto result = guard.validate(request);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.violations.contains(BacktestLayerViolationCode::ExecutionPositionSizingMethodMismatch));
}

TEST(BacktestLayerGuardTest, RejectsExecutionShortSellingModeMismatch)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();
    request.executionSpec.enableShortSelling = true;

    const auto result = guard.validate(request);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.violations.contains(BacktestLayerViolationCode::ExecutionShortSellingModeMismatch));
}

TEST(BacktestLayerGuardTest, RejectsOverlaySelectedFactorsMismatch)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();
    request.factorOverlaySpec.selectedFactors.clear();
    request.factorOverlaySpec.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_2")));

    const auto result = guard.validate(request);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.violations.contains(BacktestLayerViolationCode::OverlaySelectedFactorsMismatch));
}

TEST(BacktestLayerGuardTest, AcceptsOverlaySelectedFactorsWithDifferentOrder)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();

    request.strategySpec.factorOverlay.selectedFactors.clear();
    request.strategySpec.factorOverlay.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_1")));
    request.strategySpec.factorOverlay.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_2")));

    request.factorOverlaySpec.selectedFactors.clear();
    request.factorOverlaySpec.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_2")));
    request.factorOverlaySpec.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_1")));

    request.strategySpec.factorOverlay.allocations.clear();
    request.strategySpec.factorOverlay.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_1")), 50.0});
    request.strategySpec.factorOverlay.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_2")), 50.0});

    request.factorOverlaySpec.allocations.clear();
    request.factorOverlaySpec.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_1")), 50.0});
    request.factorOverlaySpec.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_2")), 50.0});

    const auto result = guard.validate(request);
    EXPECT_TRUE(result.ok());
}

TEST(BacktestLayerGuardTest, RejectsOverlayMinimumCompositeScoreMismatch)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();
    request.factorOverlaySpec.minimumCompositeScore = 0.6;

    const auto result = guard.validate(request);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.violations.contains(BacktestLayerViolationCode::OverlayMinimumCompositeScoreMismatch));
}

TEST(BacktestLayerGuardTest, RejectsRiskStopLossMismatchAgainstRuleProfile)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();
    request.riskSpec.stopLossRate = domain::strategy::Ratio{0.2};

    const auto result = guard.validate(request);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.violations.contains(BacktestLayerViolationCode::RiskStopLossMustAlignRuleProfile));
}

TEST(BacktestLayerGuardTest, RejectsRiskMaxPositionMismatchAgainstRuleProfile)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();
    request.riskSpec.maxPositionRatio = domain::strategy::Ratio{0.8};

    const auto result = guard.validate(request);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.violations.contains(BacktestLayerViolationCode::RiskMaxPositionMustAlignRuleProfile));
}

TEST(BacktestLayerGuardTest, RejectsInvalidRequest)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();
    request.window.endDate = QDate();

    const auto result = guard.validate(request);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.violations.contains(BacktestLayerViolationCode::InvalidRequest));
}

TEST(BacktestLayerGuardTest, KeepsNonOverlayChecksWhenOverlayDisabled)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();
    request.strategySpec.factorOverlay.enabled = false;
    request.factorOverlaySpec.enabled = false;
    request.executionSpec.rebalanceFrequencyDays = 7;

    const auto result = guard.validate(request);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.violations.contains(BacktestLayerViolationCode::RebalanceDaysMustComeFromStrategyDefinition));
}

TEST(BacktestLayerGuardTest, AcceptsOverlayAllocationsWithDifferentOrder)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();

    request.strategySpec.factorOverlay.selectedFactors.clear();
    request.strategySpec.factorOverlay.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_1")));
    request.strategySpec.factorOverlay.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_2")));
    request.strategySpec.factorOverlay.allocations.clear();
    request.strategySpec.factorOverlay.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_1")), 60.0});
    request.strategySpec.factorOverlay.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_2")), 40.0});

    request.factorOverlaySpec.selectedFactors.clear();
    request.factorOverlaySpec.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_1")));
    request.factorOverlaySpec.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_2")));
    request.factorOverlaySpec.allocations.clear();
    request.factorOverlaySpec.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_2")), 40.0});
    request.factorOverlaySpec.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_1")), 60.0});

    const auto result = guard.validate(request);
    EXPECT_TRUE(result.ok());
}

TEST(BacktestLayerGuardTest, RejectsOverlayAllocationsWithWeightMismatch)
{
    const StrictBacktestLayerGuard guard;
    BacktestRequest request = makeValidLayerGuardRequest();

    request.strategySpec.factorOverlay.selectedFactors.clear();
    request.strategySpec.factorOverlay.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_1")));
    request.strategySpec.factorOverlay.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_2")));
    request.strategySpec.factorOverlay.allocations.clear();
    request.strategySpec.factorOverlay.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_1")), 60.0});
    request.strategySpec.factorOverlay.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_2")), 40.0});

    request.factorOverlaySpec.selectedFactors.clear();
    request.factorOverlaySpec.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_1")));
    request.factorOverlaySpec.selectedFactors.append(domain::strategy::FactorId(QStringLiteral("factor_2")));
    request.factorOverlaySpec.allocations.clear();
    request.factorOverlaySpec.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_1")), 55.0});
    request.factorOverlaySpec.allocations.append(
        domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(QStringLiteral("factor_2")), 45.0});

    const auto result = guard.validate(request);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.violations.contains(BacktestLayerViolationCode::OverlayAllocationsMismatch));
}

} // namespace