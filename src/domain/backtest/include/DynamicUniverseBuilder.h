#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>
#include "../../types/DomainDate.h"
#include "../../types/InstrumentId.h"

namespace astock::domain::backtest::dynamic_universe {

using ::domain::TradingDay;
using ::domain::DayRange;
using ::domain::InstrumentId;

struct IndexId final {
    static constexpr uint32_t kInvalidValue = 0U;

    uint32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value != kInvalidValue;
    }
};

struct ConstituentInterval final {
    InstrumentId instrument{};
    TradingDay effectiveStart{};
    TradingDay effectiveEnd{};
    bool openEnded{false};

    [[nodiscard]] bool isActiveOn(TradingDay day) const noexcept
    {
        if (!instrument.isValid() || !effectiveStart.isValid() || !day.isValid()) {
            return false;
        }
        if (day < effectiveStart) {
            return false;
        }
        if (!openEnded && effectiveEnd.isValid() && effectiveEnd < day) {
            return false;
        }
        return true;
    }
};

struct MissingCoverage final {
    TradingDay day{};
};

struct UniverseByDay final {
    std::unordered_map<int32_t, std::vector<InstrumentId>> data;
};

enum class DynamicUniverseBuildError {
    None,
    InvalidInput,
    MissingCoverage
};

struct DynamicUniverseBuildResult final {
    DynamicUniverseBuildError error{DynamicUniverseBuildError::None};
    std::optional<UniverseByDay> value;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == DynamicUniverseBuildError::None && value.has_value();
    }
};

class ITradingCalendar {
public:
    virtual ~ITradingCalendar() = default;

    virtual std::vector<TradingDay> enumerate(DayRange range) const = 0;
    virtual std::optional<TradingDay> nextDayAfter(TradingDay day) const = 0;
};

class IConstituentRepository {
public:
    virtual ~IConstituentRepository() = default;

    virtual std::vector<ConstituentInterval> load(IndexId index, DayRange range) const = 0;
};

enum class MissingCoverageAction {
    Fail,
    Skip
};

class IMissingCoveragePolicy {
public:
    virtual ~IMissingCoveragePolicy() = default;

    virtual MissingCoverageAction onMissing(MissingCoverage missing) const = 0;
};

class StrictMissingCoveragePolicy final : public IMissingCoveragePolicy {
public:
    MissingCoverageAction onMissing(MissingCoverage) const override;
};

class IDynamicUniverseBuilder {
public:
    virtual ~IDynamicUniverseBuilder() = default;

    virtual DynamicUniverseBuildResult build(IndexId index, DayRange range) const = 0;
};

class DynamicUniverseBuilder final : public IDynamicUniverseBuilder {
public:
    DynamicUniverseBuilder(const ITradingCalendar& calendar,
                           const IConstituentRepository& repository,
                           const IMissingCoveragePolicy& coveragePolicy);

    DynamicUniverseBuildResult build(IndexId index, DayRange range) const override;

private:
    static bool isValidConstituentInterval(const ConstituentInterval& interval);
    static bool isValidEnumeratedDays(const std::vector<TradingDay>& days, DayRange range);

    const ITradingCalendar& calendar_;
    const IConstituentRepository& repository_;
    const IMissingCoveragePolicy& coveragePolicy_;
};

} // namespace astock::domain::backtest::dynamic_universe
