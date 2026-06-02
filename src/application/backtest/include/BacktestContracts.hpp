#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace factor::compute {
struct SignalSet;
class IFactorComputeEngine;
class ISignalEngine;
}

namespace domain::strategy {
class IStrategyService;
}

namespace domain::backtest {
struct BacktestRequest;
}

namespace astock::domain::trading::signal_orders {
class ISignalOrderTranslator;
}

namespace astock::domain::trading::risk_approval {
class IRiskApprovalEngine;
}

namespace application::backtest {

enum class RunMode : std::uint8_t {
    FactorBacktest = 0,
    StrategyBacktest = 1,
};

enum class FillOrderSideMode : std::uint8_t {
    LongOnlyBuy = 0,
    AlternatingLongShort = 1,
};

inline constexpr FillOrderSideMode kDefaultFillOrderSideMode = FillOrderSideMode::LongOnlyBuy;

[[nodiscard]] inline bool isValidFillOrderSideMode(FillOrderSideMode mode) noexcept
{
    return mode == FillOrderSideMode::LongOnlyBuy
        || mode == FillOrderSideMode::AlternatingLongShort;
}

enum class RunStage : std::uint8_t {
    Validate = 0,
    Plan = 1,
    LoadWindowData = 2,
    GenerateSignal = 3,
    ConstructTargetPosition = 4,
    RiskApprove = 5,
    GenerateOrders = 6,
    ExecuteFill = 7,
    UpdatePositionState = 8,
    AggregateMetrics = 9,
    BuildDiagnostics = 10,
    PersistArtifacts = 11,
    Finalize = 12,
};

enum class RunErrorCode : std::uint16_t {
    None = 0,
    InvalidRunSpec = 1,
    MissingModeRoute = 2,
    MissingSignalProducer = 3,
    MissingWindowProvider = 4,
    MissingExecutionModule = 5,
    StageExecutionFailed = 6,
    StageBudgetExceeded = 7,
    PersistFailed = 8,
    MissingResolvedSymbols = 9,
    MissingSelectedFactors = 10,
};

enum class RunFailureReason : std::uint8_t {
    None = 0,
    InvalidRunSpec = 1,
    MissingResolvedSymbols = 2,
    MissingSelectedFactors = 3,
    MissingModeRoute = 4,
    MissingSignalProducer = 5,
    MissingWindowProvider = 6,
    MissingExecutionModule = 7,
    StageBudgetExceeded = 8,
    StageExecutionFailed = 9,
    PersistFailed = 10,
    Unknown = 255,
};

[[nodiscard]] inline RunFailureReason toRunFailureReason(
    RunErrorCode code) noexcept
{
    switch (code) {
    case RunErrorCode::None:
        return RunFailureReason::None;
    case RunErrorCode::InvalidRunSpec:
        return RunFailureReason::InvalidRunSpec;
    case RunErrorCode::MissingResolvedSymbols:
        return RunFailureReason::MissingResolvedSymbols;
    case RunErrorCode::MissingSelectedFactors:
        return RunFailureReason::MissingSelectedFactors;
    case RunErrorCode::MissingModeRoute:
        return RunFailureReason::MissingModeRoute;
    case RunErrorCode::MissingSignalProducer:
        return RunFailureReason::MissingSignalProducer;
    case RunErrorCode::MissingWindowProvider:
        return RunFailureReason::MissingWindowProvider;
    case RunErrorCode::MissingExecutionModule:
        return RunFailureReason::MissingExecutionModule;
    case RunErrorCode::StageBudgetExceeded:
        return RunFailureReason::StageBudgetExceeded;
    case RunErrorCode::StageExecutionFailed:
        return RunFailureReason::StageExecutionFailed;
    case RunErrorCode::PersistFailed:
        return RunFailureReason::PersistFailed;
    default:
        return RunFailureReason::Unknown;
    }
}

struct RunTaskId final {
    static constexpr std::uint64_t kInvalidValue = 0ULL;

    std::uint64_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value != kInvalidValue;
    }
};

struct StageBudgetSpec final {
    static constexpr std::int64_t kDefaultTimeoutMilliseconds = 120000;
    static constexpr std::uint64_t kDefaultMemoryLimitBytes = 1024ULL * 1024ULL * 1024ULL;

    std::int64_t timeoutMilliseconds{kDefaultTimeoutMilliseconds};
    std::uint64_t memoryLimitBytes{kDefaultMemoryLimitBytes};

    [[nodiscard]] bool isValid() const noexcept
    {
        return timeoutMilliseconds > 0 && memoryLimitBytes > 0U;
    }
};

inline constexpr std::size_t kRunStageCount = 13U;

struct RuntimeBudgetPlan final {
    std::array<StageBudgetSpec, kRunStageCount> stageBudgets{};

    RuntimeBudgetPlan()
    {
        stageBudgets.fill(StageBudgetSpec{});
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        for (const StageBudgetSpec& budget : stageBudgets) {
            if (!budget.isValid()) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] const StageBudgetSpec& forStage(RunStage stage) const noexcept
    {
        return stageBudgets[static_cast<std::size_t>(stage)];
    }
};

struct RunSpec final {
    RunTaskId taskId;
    RunMode mode{RunMode::FactorBacktest};
    FillOrderSideMode fillOrderSideMode{kDefaultFillOrderSideMode};
    std::shared_ptr<const domain::backtest::BacktestRequest> request;
    RuntimeBudgetPlan runtimeBudget;

    [[nodiscard]] bool isValid() const noexcept
    {
        return taskId.isValid()
            && isValidFillOrderSideMode(fillOrderSideMode)
            && request != nullptr
            && runtimeBudget.isValid();
    }
};

struct SignalBatch final {
    std::shared_ptr<const factor::compute::SignalSet> factorSignalSet;
    std::uint32_t strategySignalCount{0U};

    [[nodiscard]] bool isValid() const noexcept
    {
        return factorSignalSet != nullptr || strategySignalCount > 0U;
    }
};

struct RunWorkingSet final {
    SignalBatch signalBatch;
    std::int32_t effectiveWindowStartDate{0};
    std::int32_t effectiveWindowEndDate{0};
    std::uint32_t rebalancePointCount{0U};
    std::uint32_t targetPositionCount{0U};
    std::uint32_t approvedOrderCount{0U};
    std::uint32_t generatedOrderCount{0U};
    std::uint32_t filledOrderCount{0U};
    std::uint32_t positionSnapshotCount{0U};
    std::uint32_t metricCount{0U};
    std::uint32_t diagnosticsCount{0U};
    std::uint32_t persistedArtifactCount{0U};
};

struct RunContext final {
    RunSpec spec;
    RunWorkingSet workingSet;
};

struct RunResult final {
    RunErrorCode code{RunErrorCode::None};
    RunFailureReason failureReason{RunFailureReason::None};
    RunStage completedStage{RunStage::Validate};
    bool partial{false};
    std::uint32_t persistedArtifactCount{0U};
    std::uint32_t diagnosticsCount{0U};

    [[nodiscard]] bool ok() const noexcept
    {
        return code == RunErrorCode::None;
    }
};

struct ExistingModuleSlots final {
    factor::compute::IFactorComputeEngine* factorComputeEngine{nullptr};
    factor::compute::ISignalEngine* signalEngine{nullptr};
    const domain::strategy::IStrategyService* strategyService{nullptr};
    const astock::domain::trading::signal_orders::ISignalOrderTranslator* signalOrderTranslator{nullptr};
    const astock::domain::trading::risk_approval::IRiskApprovalEngine* riskApprovalEngine{nullptr};
};

} // namespace application::backtest