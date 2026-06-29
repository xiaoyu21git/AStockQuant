#pragma once

#include "../../strategies/include/StrategyDefinitionTypes.h"
#include "../../types/InstrumentId.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
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

enum class EngineExecutionMode : std::uint8_t {
    Live = 0,
    Backtest = 1,
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
    const void* m_historicalView{nullptr};
    int m_currentEvaluationRow{-1};  // -1 = 实盘/未设置, 使用最后一行

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

    // ── 非因子策略：OHLCV 历史数据视图 (回测传入, 实盘为 nullptr) ──
    void setHistoricalView(const void* view) { m_historicalView = view; }
    [[nodiscard]] const void* historicalViewPtr() const noexcept { return m_historicalView; }

    // ── 回测：当前评估行号 (从 0 开始), 实盘为 -1 表示使用最后一行 ──
    void setCurrentEvaluationRow(int row) { m_currentEvaluationRow = row; }
    [[nodiscard]] int currentEvaluationRow() const noexcept { return m_currentEvaluationRow; }

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

using InstrumentId = ::domain::InstrumentId;

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
    double score_{0.5};

public:
    OrderRequest() = default;
    OrderRequest(StrategyInstanceId strategyInstanceId,
                 InstrumentId instrumentId,
                 RuntimeOrderSide side,
                 std::uint32_t quantity,
                 double score = 0.5)
        : strategyInstanceId_(strategyInstanceId)
        , instrumentId_(instrumentId)
        , side_(side)
        , quantity_(quantity)
        , score_(score)
    {
    }

    [[nodiscard]] StrategyInstanceId strategyInstanceId() const noexcept { return strategyInstanceId_; }
    [[nodiscard]] const InstrumentId& instrumentId() const noexcept { return instrumentId_; }
    [[nodiscard]] RuntimeOrderSide side() const noexcept { return side_; }
    [[nodiscard]] std::uint32_t quantity() const noexcept { return quantity_; }
    [[nodiscard]] double score() const noexcept { return score_; }

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

// ══════════════════════════════════════════════════════════════════════════════
// StrategyCreationParams — 纯 C++ 策略创建参数（依赖前面全部类型）
// ══════════════════════════════════════════════════════════════════════════════

struct StrategyCreationParams final {
    std::string strategyId;
    std::vector<::domain::strategies::FactorWeight> factorWeights;
    int topN{0};
    ::domain::strategies::WeightScheme weightScheme{::domain::strategies::WeightScheme::EQUAL};
    ::domain::strategies::RebalanceFrequency rebalanceFrequency{::domain::strategies::RebalanceFrequency::DAILY};
    bool industryNeutral{false};
    bool allowShort{false};
    int maxPositions{100};
    double maxWeightPerStock{0.1};
    double minWeightPerStock{0.0};
    std::uint32_t maxOrderQuantity{100};
    double stopLossPercent{10.0};
    double takeProfitPercent{20.0};
    int fastPeriod{5};
    int slowPeriod{20};
    int signalPeriod{14};
    int macdFast{12};
    int macdSlow{26};
    int macdSignal{9};
    int bbPeriod{20};
    double bbStdDev{2.0};
    std::uint64_t snapshotVersion{1};
    std::string strategyName;
    std::string description;
    ::domain::strategies::StrategyBehaviorKind behaviorKind{::domain::strategies::StrategyBehaviorKind::Custom};
    std::vector<std::string> factorIds;  // instance_id 字符串

    // 因子回调（桥接层填充）
    std::function<StrategyServiceFlowResult(const MarketDataPoint&)> onIncremental;
    std::function<StrategyServiceFlowResult(const std::vector<MarketDataPoint>&)> onBatch;
    std::function<void(std::vector<RuntimeFactorSnapshot>&)> onCopySnapshots;
};

} // namespace domain::strategy
