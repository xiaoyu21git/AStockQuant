#include "DynamicUniverseBuilder.h"

#include <algorithm>
#include <utility>

namespace astock::domain::backtest::dynamic_universe {

bool DynamicUniverseBuilder::isValidConstituentInterval(const ConstituentInterval& interval)
{
    if (!interval.instrument.isValid() || !interval.effectiveStart.isValid()) {
        return false;
    }
    if (interval.openEnded) {
        return true;
    }
    if (!interval.effectiveEnd.isValid()) {
        return false;
    }
    return interval.effectiveStart <= interval.effectiveEnd;
}

bool DynamicUniverseBuilder::isValidEnumeratedDays(const std::vector<TradingDay>& days, DayRange range)
{
    TradingDay previous{};
    bool hasPrevious = false;
    for (const TradingDay day : days) {
        if (!day.isValid() || day < range.start || range.end < day) {
            return false;
        }
        if (hasPrevious && day <= previous) {
            return false;
        }
        previous = day;
        hasPrevious = true;
    }
    return true;
}

MissingCoverageAction StrictMissingCoveragePolicy::onMissing(MissingCoverage) const
{
    return MissingCoverageAction::Fail;
}

DynamicUniverseBuilder::DynamicUniverseBuilder(const ITradingCalendar& calendar,
                                               const IConstituentRepository& repository,
                                               const IMissingCoveragePolicy& coveragePolicy)
    : calendar_(calendar)
    , repository_(repository)
    , coveragePolicy_(coveragePolicy)
{
}

DynamicUniverseBuildResult DynamicUniverseBuilder::build(IndexId index, DayRange range) const
{
    if (!index.isValid() || !range.isValid()) {
        return DynamicUniverseBuildResult{DynamicUniverseBuildError::InvalidInput, std::nullopt};
    }

    const std::vector<TradingDay> days = calendar_.enumerate(range);
    const std::vector<ConstituentInterval> intervals = repository_.load(index, range);

    if (!DynamicUniverseBuilder::isValidEnumeratedDays(days, range)) {
        return DynamicUniverseBuildResult{DynamicUniverseBuildError::InvalidInput, std::nullopt};
    }

    for (const ConstituentInterval& interval : intervals) {
        if (!DynamicUniverseBuilder::isValidConstituentInterval(interval)) {
            return DynamicUniverseBuildResult{DynamicUniverseBuildError::InvalidInput, std::nullopt};
        }
    }

    UniverseByDay result;
    result.data.reserve(days.size());

    for (const TradingDay day : days) {
        std::vector<InstrumentId> active;
        active.reserve(intervals.size());

        for (const ConstituentInterval& interval : intervals) {
            if (!interval.isActiveOn(day)) {
                continue;
            }
            active.push_back(interval.instrument);
        }

        if (active.empty()) {
            const MissingCoverageAction action = coveragePolicy_.onMissing(MissingCoverage{day});
            if (action == MissingCoverageAction::Fail) {
                return DynamicUniverseBuildResult{DynamicUniverseBuildError::MissingCoverage, std::nullopt};
            }
            continue;
        }

        std::sort(active.begin(), active.end());
        active.erase(std::unique(active.begin(), active.end(),
                                 [](InstrumentId left, InstrumentId right) {
                                     return left.value == right.value;
                                 }),
                     active.end());

        result.data.emplace(day.value, std::move(active));
    }

    return DynamicUniverseBuildResult{DynamicUniverseBuildError::None, std::move(result)};
}

} // namespace astock::domain::backtest::dynamic_universe


