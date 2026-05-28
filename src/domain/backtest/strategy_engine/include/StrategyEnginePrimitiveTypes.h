#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace domain::backtest::strategy_engine {

template <typename Tag, typename Rep = std::uint64_t>
class StrongId final {
    static_assert(std::is_integral_v<Rep>);

public:
    constexpr StrongId() = default;

    explicit constexpr StrongId(Rep value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isValid() const
    {
        return value_ != 0;
    }

    [[nodiscard]] constexpr Rep value() const
    {
        return value_;
    }

    [[nodiscard]] friend constexpr bool operator==(StrongId left, StrongId right)
    {
        return left.value_ == right.value_;
    }

    [[nodiscard]] friend constexpr bool operator!=(StrongId left, StrongId right)
    {
        return !(left == right);
    }

private:
    Rep value_{0};
};

class TradingDayIndex final {
public:
    constexpr TradingDayIndex() = default;

    explicit constexpr TradingDayIndex(std::int32_t value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isValid() const
    {
        return value_ >= 0;
    }

    [[nodiscard]] constexpr std::int32_t value() const
    {
        return value_;
    }

    [[nodiscard]] friend constexpr bool operator==(TradingDayIndex left, TradingDayIndex right)
    {
        return left.value_ == right.value_;
    }

    [[nodiscard]] friend constexpr bool operator!=(TradingDayIndex left, TradingDayIndex right)
    {
        return !(left == right);
    }

    [[nodiscard]] friend constexpr bool operator<=(TradingDayIndex left, TradingDayIndex right)
    {
        return left.value_ <= right.value_;
    }

private:
    std::int32_t value_{-1};
};

class TimestampNs final {
public:
    constexpr TimestampNs() = default;

    explicit constexpr TimestampNs(std::int64_t value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isValid() const
    {
        return value_ >= 0;
    }

    [[nodiscard]] constexpr std::int64_t value() const
    {
        return value_;
    }

private:
    std::int64_t value_{-1};
};

class DurationNs final {
public:
    constexpr DurationNs() = default;

    explicit constexpr DurationNs(std::int64_t value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isValid() const
    {
        return value_ >= 0;
    }

    [[nodiscard]] constexpr std::int64_t value() const
    {
        return value_;
    }

private:
    std::int64_t value_{0};
};

class MemoryBytes final {
public:
    constexpr MemoryBytes() = default;

    explicit constexpr MemoryBytes(std::uint64_t value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isValid() const
    {
        return true;
    }

    [[nodiscard]] constexpr std::uint64_t value() const
    {
        return value_;
    }

private:
    std::uint64_t value_{0};
};

class CashAmount final {
public:
    constexpr CashAmount() = default;

    explicit constexpr CashAmount(double value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isFinite() const
    {
        return value_ <= std::numeric_limits<double>::max()
            && value_ >= -std::numeric_limits<double>::max();
    }

    [[nodiscard]] constexpr bool isNonNegative() const
    {
        return isFinite() && value_ >= 0.0;
    }

    [[nodiscard]] constexpr bool isPositive() const
    {
        return isFinite() && value_ > 0.0;
    }

    [[nodiscard]] constexpr double value() const
    {
        return value_;
    }

private:
    double value_{0.0};
};

class PriceValue final {
public:
    constexpr PriceValue() = default;

    explicit constexpr PriceValue(double value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isFinite() const
    {
        return value_ <= std::numeric_limits<double>::max()
            && value_ >= -std::numeric_limits<double>::max();
    }

    [[nodiscard]] constexpr bool isPositive() const
    {
        return isFinite() && value_ > 0.0;
    }

    [[nodiscard]] constexpr double value() const
    {
        return value_;
    }

private:
    double value_{0.0};
};

class Ratio final {
public:
    constexpr Ratio() = default;

    explicit constexpr Ratio(double value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isValid() const
    {
        return value_ <= 1.0 && value_ >= 0.0;
    }

    [[nodiscard]] constexpr double value() const
    {
        return value_;
    }

private:
    double value_{0.0};
};

class ReturnValue final {
public:
    constexpr ReturnValue() = default;

    explicit constexpr ReturnValue(double value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isValid() const
    {
        return value_ <= std::numeric_limits<double>::max()
            && value_ >= -std::numeric_limits<double>::max();
    }

    [[nodiscard]] constexpr double value() const
    {
        return value_;
    }

private:
    double value_{0.0};
};

class Weight final {
public:
    constexpr Weight() = default;

    explicit constexpr Weight(double value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isValid() const
    {
        return value_ >= 0.0 && value_ <= 1.0;
    }

    [[nodiscard]] constexpr double value() const
    {
        return value_;
    }

private:
    double value_{0.0};
};

class ScoreValue final {
public:
    constexpr ScoreValue() = default;

    explicit constexpr ScoreValue(double value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isValid() const
    {
        return value_ <= std::numeric_limits<double>::max()
            && value_ >= -std::numeric_limits<double>::max();
    }

    [[nodiscard]] constexpr double value() const
    {
        return value_;
    }

private:
    double value_{0.0};
};

class ScoreThreshold final {
public:
    constexpr ScoreThreshold() = default;

    explicit constexpr ScoreThreshold(double value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isValid() const
    {
        return value_ <= std::numeric_limits<double>::max()
            && value_ >= -std::numeric_limits<double>::max();
    }

    [[nodiscard]] constexpr double value() const
    {
        return value_;
    }

private:
    double value_{0.0};
};

class ShareQuantity final {
public:
    constexpr ShareQuantity() = default;

    explicit constexpr ShareQuantity(std::uint64_t value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isPositive() const
    {
        return value_ > 0;
    }

    [[nodiscard]] constexpr std::uint64_t value() const
    {
        return value_;
    }

private:
    std::uint64_t value_{0};
};

class CandidateCount final {
public:
    constexpr CandidateCount() = default;

    explicit constexpr CandidateCount(std::uint32_t value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isPositive() const
    {
        return value_ > 0;
    }

    [[nodiscard]] constexpr std::uint32_t value() const
    {
        return value_;
    }

private:
    std::uint32_t value_{0};
};

class DatasetId final {
public:
    constexpr DatasetId() = default;

    explicit constexpr DatasetId(std::uint32_t value)
        : value_(value)
    {
    }

    [[nodiscard]] constexpr bool isValid() const
    {
        return value_ != 0;
    }

    [[nodiscard]] constexpr std::uint32_t value() const
    {
        return value_;
    }

private:
    std::uint32_t value_{0};
};

template <typename T>
class ObjectList final {
public:
    using Storage = std::vector<T>;
    using const_iterator = typename Storage::const_iterator;
    using iterator = typename Storage::iterator;

    ObjectList() = default;

    explicit ObjectList(Storage values)
        : values_(std::move(values))
    {
    }

    void add(const T& value)
    {
        values_.push_back(value);
    }

    void add(T&& value)
    {
        values_.push_back(std::move(value));
    }

    void reserve(std::size_t size)
    {
        values_.reserve(size);
    }

    [[nodiscard]] bool empty() const
    {
        return values_.empty();
    }

    [[nodiscard]] std::size_t size() const
    {
        return values_.size();
    }

    [[nodiscard]] const Storage& values() const
    {
        return values_;
    }

    [[nodiscard]] Storage& values()
    {
        return values_;
    }

    [[nodiscard]] const_iterator begin() const
    {
        return values_.begin();
    }

    [[nodiscard]] const_iterator end() const
    {
        return values_.end();
    }

    [[nodiscard]] iterator begin()
    {
        return values_.begin();
    }

    [[nodiscard]] iterator end()
    {
        return values_.end();
    }

private:
    Storage values_;
};

struct StrategyIdTag;
struct RunIdTag;
struct OverlayBindingScopeIdTag;
struct UniverseIdTag;
struct LayerIdTag;
struct FactorIdTag;
struct RuleTemplateIdTag;
struct SymbolIdTag;
struct OrderIdTag;

using StrategyId = StrongId<StrategyIdTag>;
using RunId = StrongId<RunIdTag>;
using OverlayBindingScopeId = StrongId<OverlayBindingScopeIdTag>;
using UniverseId = StrongId<UniverseIdTag>;
using LayerId = StrongId<LayerIdTag>;
using FactorId = StrongId<FactorIdTag>;
using RuleTemplateId = StrongId<RuleTemplateIdTag>;
using SymbolId = StrongId<SymbolIdTag, std::uint32_t>;
using OrderId = StrongId<OrderIdTag>;

enum class StrategyBehaviorKind : std::uint8_t {
    TrendFollowing = 0,
    MeanReversion = 1,
    Momentum = 2,
    Arbitrage = 3,
    MultiFactor = 4,
    MachineLearning = 5,
    EventDriven = 6,
    HighFrequency = 7,
    Custom = 8,
};

enum class LayerType : std::uint8_t {
    Strategic = 0,
    Tactical = 1,
    Execution = 2,
};

enum class DecisionType : std::uint8_t {
    Entry = 0,
    Hold = 1,
    Exit = 2,
};

enum class ExecutionMode : std::uint8_t {
    EndOfDay = 0,
    Intraday = 1,
};

enum class PositionSizingMethod : std::uint8_t {
    FixedFraction = 0,
    EqualWeight = 1,
    SpreadNeutral = 2,
    Discretionary = 3,
};

enum class UniverseSelectionMode : std::uint8_t {
    ExplicitSymbols = 0,
    IndexConstituents = 1,
    SavedUniverse = 2,
    LinkedUniverse = 3,
};

enum class MarketProfile : std::uint8_t {
    GenericEquity = 0,
    AshareEquity = 1,
};

enum class DataSourceMode : std::uint8_t {
    Raw = 0,
    Cleaned = 1,
    CacheDataset = 2,
};

enum class OrderSide : std::uint8_t {
    Buy = 0,
    Sell = 1,
};

enum class OrderType : std::uint8_t {
    Market = 0,
    Limit = 1,
    MarketOnClose = 2,
};

enum class BacktestRunState : std::uint8_t {
    Created = 0,
    Running = 1,
    Succeeded = 2,
    Failed = 3,
    Cancelled = 4,
};

enum class RuleDecisionCode : std::uint8_t {
    None = 0,
    EntryBlocked = 1,
    ForcedExit = 2,
    DomainMismatch = 3,
    TimeAlignmentFailure = 4,
    DataUnavailable = 5,
    RiskRejected = 6,
};

enum class ValidationIssueCode : std::uint8_t {
    None = 0,
    InvalidIdentity = 1,
    InvalidWindow = 2,
    InvalidUniverse = 3,
    InvalidLayer = 4,
    InvalidCost = 5,
    InvalidRisk = 6,
    InvalidExecution = 7,
    InvalidDataSource = 8,
    InvalidRuntimeOptions = 9,
};

enum class EngineAssumptionCode : std::uint8_t {
    None = 0,
    FullFillOrCancel = 1,
    UseClosingPrice = 2,
    LongOnly = 3,
    FixedTaxSchedule = 4,
};

enum class DiagnosticSeverity : std::uint8_t {
    Info = 0,
    Warning = 1,
    Error = 2,
};

} // namespace domain::backtest::strategy_engine