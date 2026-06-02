#pragma once

#include "BacktestContracts.hpp"

#include <optional>

namespace application::backtest {

struct StageResult final {
    RunErrorCode code{RunErrorCode::None};
    RunStage stage{RunStage::Validate};

    [[nodiscard]] bool ok() const noexcept
    {
        return code == RunErrorCode::None;
    }
};

struct RunSpecValidationResult final {
    RunErrorCode code{RunErrorCode::None};

    [[nodiscard]] bool ok() const noexcept
    {
        return code == RunErrorCode::None;
    }
};

class IRunSpecValidator {
public:
    virtual ~IRunSpecValidator() = default;

    [[nodiscard]] virtual RunSpecValidationResult validate(const RunSpec& spec) const = 0;
};

struct ModeRoute final {
    RunMode mode{RunMode::FactorBacktest};
    bool hasSignalProducer{false};
    bool hasFillEngine{false};

    [[nodiscard]] bool isValid() const noexcept
    {
        return hasSignalProducer && hasFillEngine;
    }
};

struct ModeRouteResult final {
    RunErrorCode code{RunErrorCode::None};
    std::optional<ModeRoute> route;

    [[nodiscard]] bool ok() const noexcept
    {
        return code == RunErrorCode::None && route.has_value() && route->isValid();
    }
};

class IModeRouter {
public:
    virtual ~IModeRouter() = default;

    [[nodiscard]] virtual ModeRouteResult resolve(const RunSpec& spec) const = 0;
};

class IRuntimeGuard {
public:
    virtual ~IRuntimeGuard() = default;

    [[nodiscard]] virtual StageResult checkStageBudget(
        RunStage stage,
        const StageBudgetSpec& budget,
        const RunContext& context) const = 0;
};

class IMarketDataWindowProvider {
public:
    virtual ~IMarketDataWindowProvider() = default;

    [[nodiscard]] virtual StageResult loadWindowData(RunContext& context) const = 0;
};

class ISignalProducer {
public:
    virtual ~ISignalProducer() = default;

    [[nodiscard]] virtual StageResult generateSignal(RunContext& context) const = 0;
};

class IPortfolioConstructionEngine {
public:
    virtual ~IPortfolioConstructionEngine() = default;

    [[nodiscard]] virtual StageResult constructTargetPosition(RunContext& context) const = 0;
};

class IRiskApprovalStageEngine {
public:
    virtual ~IRiskApprovalStageEngine() = default;

    [[nodiscard]] virtual StageResult approve(RunContext& context) const = 0;
};

class IOrderGenerationEngine {
public:
    virtual ~IOrderGenerationEngine() = default;

    [[nodiscard]] virtual StageResult generateOrders(RunContext& context) const = 0;
};

class IFillEngine {
public:
    virtual ~IFillEngine() = default;

    [[nodiscard]] virtual StageResult executeFill(RunContext& context) const = 0;
};

class IPositionStateEngine {
public:
    virtual ~IPositionStateEngine() = default;

    [[nodiscard]] virtual StageResult updatePositionState(RunContext& context) const = 0;
};

class IMetricsEngine {
public:
    virtual ~IMetricsEngine() = default;

    [[nodiscard]] virtual StageResult aggregateMetrics(RunContext& context) const = 0;
};

class IDiagnosticsEngine {
public:
    virtual ~IDiagnosticsEngine() = default;

    [[nodiscard]] virtual StageResult buildDiagnostics(RunContext& context) const = 0;
};

class IResultRepository {
public:
    virtual ~IResultRepository() = default;

    [[nodiscard]] virtual StageResult persistArtifacts(RunContext& context) const = 0;
};

class IExportArtifactBuilder {
public:
    virtual ~IExportArtifactBuilder() = default;

    [[nodiscard]] virtual StageResult buildExportArtifacts(RunContext& context) const = 0;
};

struct RunModuleRegistry final {
    const IRunSpecValidator* runSpecValidator{nullptr};
    const IModeRouter* modeRouter{nullptr};
    const IRuntimeGuard* runtimeGuard{nullptr};
    const IMarketDataWindowProvider* marketDataWindowProvider{nullptr};
    const ISignalProducer* factorSignalProducer{nullptr};
    const ISignalProducer* strategySignalProducer{nullptr};
    const IPortfolioConstructionEngine* portfolioConstructionEngine{nullptr};
    const IRiskApprovalStageEngine* riskApprovalEngine{nullptr};
    const IOrderGenerationEngine* orderGenerationEngine{nullptr};
    const IFillEngine* backtestFillEngine{nullptr};
    const IFillEngine* liveFillEngine{nullptr};
    const IPositionStateEngine* positionStateEngine{nullptr};
    const IMetricsEngine* metricsEngine{nullptr};
    const IDiagnosticsEngine* diagnosticsEngine{nullptr};
    const IResultRepository* resultRepository{nullptr};
    const IExportArtifactBuilder* exportArtifactBuilder{nullptr};

    [[nodiscard]] bool hasSharedPipelineModules() const noexcept
    {
        return runSpecValidator != nullptr
            && modeRouter != nullptr
            && runtimeGuard != nullptr
            && marketDataWindowProvider != nullptr
            && portfolioConstructionEngine != nullptr
            && riskApprovalEngine != nullptr
            && orderGenerationEngine != nullptr
            && positionStateEngine != nullptr
            && metricsEngine != nullptr
            && diagnosticsEngine != nullptr
            && resultRepository != nullptr
            && exportArtifactBuilder != nullptr;
    }

    [[nodiscard]] bool hasModeModules(RunMode mode) const noexcept
    {
        if (mode == RunMode::FactorBacktest) {
            return factorSignalProducer != nullptr && backtestFillEngine != nullptr;
        }
        return strategySignalProducer != nullptr && backtestFillEngine != nullptr;
    }
};

} // namespace application::backtest