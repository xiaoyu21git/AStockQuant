#pragma once

#include "../../strategies/include/StrategyDefinitionTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace domain::strategy {

using StrategyInstanceId = std::uint64_t;
using StrategyCount = std::size_t;

enum class StrategyServiceFlowCode : std::uint8_t {
    Ok = 0,
    InvalidInput = 1,
    DuplicateStrategy = 2,
    StrategyNotFound = 3,
    FactorUpdateFailed = 4,
    StrategyEvaluateFailed = 5,
    RuleCheckFailed = 6,
    CapacityExceeded = 7,
    InvalidState = 8,
    OrderBuildFailed = 9,
    OrderSubmitFailed = 10,
};

enum class StrategyServiceState : std::uint8_t {
    Stopped = 0,
    Running = 1,
    Paused = 2,
};

enum class DiagnosticsEventCode : std::uint8_t {
    MarketDataAccepted = 0,
    MarketDataRejected = 1,
    StrategyEvaluated = 2,
    RuleAccepted = 3,
    RuleRejected = 4,
    OrderBuilt = 5,
    OrderSubmitted = 6,
    StateChanged = 7,
};

struct StrategyServiceFlowResult final {
private:
    StrategyServiceFlowCode code_{StrategyServiceFlowCode::Ok};

public:
    StrategyServiceFlowResult() = default;
    explicit StrategyServiceFlowResult(StrategyServiceFlowCode code)
        : code_(code)
    {
    }

    [[nodiscard]] StrategyServiceFlowCode code() const noexcept
    {
        return code_;
    }

    [[nodiscard]] bool isOk() const noexcept
    {
        return code_ == StrategyServiceFlowCode::Ok;
    }
};

struct StrategyServiceExecutionPlan final {
private:
    StrategyCount maxStrategyCount_{0};
    StrategyCount maxMarketDataPerBatch_{0};
    StrategyCount maxSignalPerBatch_{0};
    StrategyCount maxRuleResultPerBatch_{0};

public:
    StrategyServiceExecutionPlan() = default;
    StrategyServiceExecutionPlan(StrategyCount maxStrategyCount,
                                 StrategyCount maxMarketDataPerBatch,
                                 StrategyCount maxSignalPerBatch,
                                 StrategyCount maxRuleResultPerBatch)
        : maxStrategyCount_(maxStrategyCount)
        , maxMarketDataPerBatch_(maxMarketDataPerBatch)
        , maxSignalPerBatch_(maxSignalPerBatch)
        , maxRuleResultPerBatch_(maxRuleResultPerBatch)
    {
    }

    [[nodiscard]] StrategyCount maxStrategyCount() const noexcept
    {
        return maxStrategyCount_;
    }

    [[nodiscard]] StrategyCount maxMarketDataPerBatch() const noexcept
    {
        return maxMarketDataPerBatch_;
    }

    [[nodiscard]] StrategyCount maxSignalPerBatch() const noexcept
    {
        return maxSignalPerBatch_;
    }

    [[nodiscard]] StrategyCount maxRuleResultPerBatch() const noexcept
    {
        return maxRuleResultPerBatch_;
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return maxStrategyCount_ > 0
            && maxMarketDataPerBatch_ > 0
            && maxSignalPerBatch_ > 0
            && maxRuleResultPerBatch_ > 0;
    }
};

struct RuntimeStrategyContext final {
private:
    StrategyInstanceId strategyInstanceId_{0};
    std::uint64_t snapshotVersion_{0};
    std::uint32_t maxOrderQuantity_{0};
    double maxTargetWeight_{0.0};
    bool autoExecutionEnabled_{false};

public:
    RuntimeStrategyContext() = default;
    RuntimeStrategyContext(StrategyInstanceId strategyInstanceId,
                           std::uint64_t snapshotVersion,
                           std::uint32_t maxOrderQuantity,
                           double maxTargetWeight,
                           bool autoExecutionEnabled)
        : strategyInstanceId_(strategyInstanceId)
        , snapshotVersion_(snapshotVersion)
        , maxOrderQuantity_(maxOrderQuantity)
        , maxTargetWeight_(maxTargetWeight)
        , autoExecutionEnabled_(autoExecutionEnabled)
    {
    }

    [[nodiscard]] StrategyInstanceId strategyInstanceId() const noexcept
    {
        return strategyInstanceId_;
    }

    [[nodiscard]] std::uint64_t snapshotVersion() const noexcept
    {
        return snapshotVersion_;
    }

    [[nodiscard]] std::uint32_t maxOrderQuantity() const noexcept
    {
        return maxOrderQuantity_;
    }

    [[nodiscard]] double maxTargetWeight() const noexcept
    {
        return maxTargetWeight_;
    }

    [[nodiscard]] bool autoExecutionEnabled() const noexcept
    {
        return autoExecutionEnabled_;
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return strategyInstanceId_ > 0
            && snapshotVersion_ > 0
            && maxOrderQuantity_ > 0
            && maxTargetWeight_ > 0.0
            && maxTargetWeight_ <= 1.0;
    }
};

struct InstrumentId final {
private:
    std::uint32_t value_{0};

public:
    InstrumentId() = default;
    explicit InstrumentId(std::uint32_t value)
        : value_(value)
    {
    }

    [[nodiscard]] std::uint32_t value() const noexcept
    {
        return value_;
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return value_ > 0;
    }
};

using RuntimeFactorId = ::domain::strategies::FactorId;
using RuntimeFactorSnapshot = ::domain::strategies::FactorSnapshot;

struct DiagnosticsEvent final {
private:
    DiagnosticsEventCode code_{DiagnosticsEventCode::MarketDataAccepted};
    StrategyServiceFlowCode flowCode_{StrategyServiceFlowCode::Ok};
    StrategyInstanceId strategyInstanceId_{0};
    InstrumentId instrumentId_{};
    double valueA_{0.0};
    double valueB_{0.0};

public:
    DiagnosticsEvent() = default;
    DiagnosticsEvent(DiagnosticsEventCode code,
                     StrategyServiceFlowCode flowCode,
                     StrategyInstanceId strategyInstanceId,
                     InstrumentId instrumentId,
                     double valueA,
                     double valueB)
        : code_(code)
        , flowCode_(flowCode)
        , strategyInstanceId_(strategyInstanceId)
        , instrumentId_(instrumentId)
        , valueA_(valueA)
        , valueB_(valueB)
    {
    }

    [[nodiscard]] DiagnosticsEventCode code() const noexcept
    {
        return code_;
    }

    [[nodiscard]] StrategyServiceFlowCode flowCode() const noexcept
    {
        return flowCode_;
    }

    [[nodiscard]] StrategyInstanceId strategyInstanceId() const noexcept
    {
        return strategyInstanceId_;
    }

    [[nodiscard]] const InstrumentId& instrumentId() const noexcept
    {
        return instrumentId_;
    }

    [[nodiscard]] double valueA() const noexcept
    {
        return valueA_;
    }

    [[nodiscard]] double valueB() const noexcept
    {
        return valueB_;
    }
};

enum class RuntimeOrderSide : std::uint8_t {
    Buy = 0,
    Sell = 1,
};

enum class RuleRejectReason : std::uint8_t {
    None = 0,
    MarketGuardBlocked = 1,
    RiskGuardBlocked = 2,
    RuleTemplateBlocked = 3,
    InvalidSignal = 4,
};

struct MarketDataPoint final {
private:
    InstrumentId instrumentId_{};
    double lastPrice_{0.0};
    double volume_{0.0};
    std::int32_t tradingDay_{-1};

public:
    MarketDataPoint() = default;
    MarketDataPoint(InstrumentId instrumentId,
                    double lastPrice,
                    double volume,
                    std::int32_t tradingDay)
        : instrumentId_(instrumentId)
        , lastPrice_(lastPrice)
        , volume_(volume)
        , tradingDay_(tradingDay)
    {
    }

    [[nodiscard]] const InstrumentId& instrumentId() const noexcept
    {
        return instrumentId_;
    }

    [[nodiscard]] double lastPrice() const noexcept
    {
        return lastPrice_;
    }

    [[nodiscard]] double volume() const noexcept
    {
        return volume_;
    }

    [[nodiscard]] std::int32_t tradingDay() const noexcept
    {
        return tradingDay_;
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return instrumentId_.isValid() && lastPrice_ > 0.0 && volume_ >= 0.0 && tradingDay_ >= 0;
    }
};

struct StrategySignal final {
private:
    StrategyInstanceId strategyInstanceId_{0};
    InstrumentId instrumentId_{};
    RuntimeOrderSide side_{RuntimeOrderSide::Buy};
    double score_{0.0};
    double targetWeight_{0.0};

public:
    StrategySignal() = default;
    StrategySignal(StrategyInstanceId strategyInstanceId,
                   InstrumentId instrumentId,
                   RuntimeOrderSide side,
                   double score,
                   double targetWeight)
        : strategyInstanceId_(strategyInstanceId)
        , instrumentId_(instrumentId)
        , side_(side)
        , score_(score)
        , targetWeight_(targetWeight)
    {
    }

    [[nodiscard]] StrategyInstanceId strategyInstanceId() const noexcept
    {
        return strategyInstanceId_;
    }

    [[nodiscard]] const InstrumentId& instrumentId() const noexcept
    {
        return instrumentId_;
    }

    [[nodiscard]] RuntimeOrderSide side() const noexcept
    {
        return side_;
    }

    [[nodiscard]] double score() const noexcept
    {
        return score_;
    }

    [[nodiscard]] double targetWeight() const noexcept
    {
        return targetWeight_;
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return strategyInstanceId_ > 0 && instrumentId_.isValid();
    }
};

struct RuleCheckResult final {
private:
    bool passed_{false};
    StrategySignal signal_{};
    RuleRejectReason rejectReason_{RuleRejectReason::None};

public:
    RuleCheckResult() = default;
    RuleCheckResult(bool passed, const StrategySignal& signal, RuleRejectReason rejectReason)
        : passed_(passed)
        , signal_(signal)
        , rejectReason_(rejectReason)
    {
    }

    [[nodiscard]] bool passed() const noexcept
    {
        return passed_;
    }

    [[nodiscard]] const StrategySignal& signal() const noexcept
    {
        return signal_;
    }

    [[nodiscard]] RuleRejectReason rejectReason() const noexcept
    {
        return rejectReason_;
    }
};

struct OrderRequest final {
private:
    StrategyInstanceId strategyInstanceId_{0};
    InstrumentId instrumentId_{};
    RuntimeOrderSide side_{RuntimeOrderSide::Buy};
    std::uint32_t quantity_{0};

public:
    OrderRequest() = default;
    OrderRequest(StrategyInstanceId strategyInstanceId,
                 InstrumentId instrumentId,
                 RuntimeOrderSide side,
                 std::uint32_t quantity)
        : strategyInstanceId_(strategyInstanceId)
        , instrumentId_(instrumentId)
        , side_(side)
        , quantity_(quantity)
    {
    }

    [[nodiscard]] StrategyInstanceId strategyInstanceId() const noexcept
    {
        return strategyInstanceId_;
    }

    [[nodiscard]] const InstrumentId& instrumentId() const noexcept
    {
        return instrumentId_;
    }

    [[nodiscard]] RuntimeOrderSide side() const noexcept
    {
        return side_;
    }

    [[nodiscard]] std::uint32_t quantity() const noexcept
    {
        return quantity_;
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return strategyInstanceId_ > 0 && instrumentId_.isValid() && quantity_ > 0;
    }
};

struct StrategyExecutionStats final {
private:
    StrategyCount strategyCount_{0};
    StrategyCount generatedSignalCount_{0};
    StrategyCount passedRuleCount_{0};
    StrategyCount rejectedRuleCount_{0};

public:
    [[nodiscard]] StrategyCount strategyCount() const noexcept
    {
        return strategyCount_;
    }

    [[nodiscard]] StrategyCount generatedSignalCount() const noexcept
    {
        return generatedSignalCount_;
    }

    [[nodiscard]] StrategyCount passedRuleCount() const noexcept
    {
        return passedRuleCount_;
    }

    [[nodiscard]] StrategyCount rejectedRuleCount() const noexcept
    {
        return rejectedRuleCount_;
    }

    void setStrategyCount(StrategyCount value) noexcept
    {
        strategyCount_ = value;
    }

    void setGeneratedSignalCount(StrategyCount value) noexcept
    {
        generatedSignalCount_ = value;
    }

    void setPassedRuleCount(StrategyCount value) noexcept
    {
        passedRuleCount_ = value;
    }

    void setRejectedRuleCount(StrategyCount value) noexcept
    {
        rejectedRuleCount_ = value;
    }
};

inline constexpr std::size_t kDefaultSignalBufferReserve = 256;
inline constexpr std::size_t kDefaultRuleResultBufferReserve = 256;
inline constexpr std::size_t kDefaultMarketDataBatchReserve = 256;

[[nodiscard]] inline StrategyServiceExecutionPlan defaultExecutionPlan() noexcept
{
    return StrategyServiceExecutionPlan(
        kDefaultSignalBufferReserve,
        kDefaultMarketDataBatchReserve,
        kDefaultSignalBufferReserve,
        kDefaultRuleResultBufferReserve);
}

} // namespace domain::strategy
