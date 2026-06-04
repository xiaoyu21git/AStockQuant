#include "RunDefaults.h"

#include "../../../domain/backtest/include/BacktestRequest.h"

namespace application::backtest {

namespace {

[[nodiscard]] RunErrorCode validateFactorBacktestRuntimeRequirements(
    const domain::backtest::BacktestRequest& request) noexcept
{
    if (request.universeSpec.resolvedSymbols.empty()) {
        return RunErrorCode::MissingResolvedSymbols;
    }

    if (request.factorOverlaySpec.selectedFactors.empty()) {
        return RunErrorCode::MissingSelectedFactors;
    }

    return RunErrorCode::None;
}

} // namespace

RunSpecValidationResult StrictRunSpecValidator::validate(const RunSpec& spec) const
{
    RunSpecValidationResult result;

    if (!spec.isValid()) {
        result.code = RunErrorCode::InvalidRunSpec;
        return result;
    }

    if (spec.mode == RunMode::FactorBacktest) {
        const RunErrorCode requirementCode =
            validateFactorBacktestRuntimeRequirements(*spec.request);
        if (requirementCode != RunErrorCode::None) {
            result.code = requirementCode;
            return result;
        }
    }

    result.code = RunErrorCode::None;
    return result;
}

StaticModeRouter::StaticModeRouter(StaticModeRouteConfig config)
    : config_(config)
{
}

ModeRouteResult StaticModeRouter::resolve(const RunSpec& spec) const
{
    ModeRouteResult result;
    result.code = RunErrorCode::MissingModeRoute;

    ModeRoute route;
    route.mode = spec.mode;

    if (spec.mode == RunMode::FactorBacktest) {
        if (!config_.enableFactorBacktest) {
            return result;
        }
        route.hasSignalProducer = config_.factorHasSignalProducer;
        route.hasFillEngine = config_.factorHasFillEngine;
    } else if (spec.mode == RunMode::StrategyBacktest) {
        if (!config_.enableStrategyBacktest) {
            return result;
        }
        route.hasSignalProducer = config_.strategyHasSignalProducer;
        route.hasFillEngine = config_.strategyHasFillEngine;
    } else {
        return result;
    }

    if (!route.isValid()) {
        return result;
    }

    result.code = RunErrorCode::None;
    result.route = route;
    return result;
}

StageResult StrictRuntimeGuard::checkStageBudget(
    RunStage stage,
    const StageBudgetSpec& budget,
    const RunContext& context) const
{
    StageResult result;
    result.stage = stage;
    result.code = RunErrorCode::None;

    if (!budget.isValid() || !isContextConsistent(context)) {
        result.code = RunErrorCode::StageBudgetExceeded;
        return result;
    }

    return result;
}

bool StrictRuntimeGuard::isContextConsistent(const RunContext& context) noexcept
{
    return context.spec.isValid();
}

StageBudgetRegistry::StageBudgetRegistry()
{
    budgets_.fill(StageBudgetSpec{});
}

const StageBudgetSpec& StageBudgetRegistry::at(RunStage stage) const noexcept
{
    return budgets_[static_cast<std::size_t>(stage)];
}

void StageBudgetRegistry::set(RunStage stage, StageBudgetSpec budget) noexcept
{
    budgets_[static_cast<std::size_t>(stage)] = budget;
}

bool StageBudgetRegistry::isValid() const noexcept
{
    for (const StageBudgetSpec& budget : budgets_) {
        if (!budget.isValid()) {
            return false;
        }
    }
    return true;
}

} // namespace application::backtest