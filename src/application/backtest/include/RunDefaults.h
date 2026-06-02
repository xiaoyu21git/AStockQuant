#pragma once

#include "BacktestInterfaces.hpp"

#include <array>

namespace application::backtest {

struct StaticModeRouteConfig final {
    bool enableFactorBacktest{true};
    bool enableStrategyBacktest{true};
    bool factorHasSignalProducer{true};
    bool strategyHasSignalProducer{true};
    bool factorHasFillEngine{true};
    bool strategyHasFillEngine{true};
};

class StrictRunSpecValidator final : public IRunSpecValidator {
public:
    [[nodiscard]] RunSpecValidationResult validate(const RunSpec& spec) const override;
};

class StaticModeRouter final : public IModeRouter {
public:
    explicit StaticModeRouter(StaticModeRouteConfig config);

    [[nodiscard]] ModeRouteResult resolve(const RunSpec& spec) const override;

private:
    StaticModeRouteConfig config_;
};

class StrictRuntimeGuard final : public IRuntimeGuard {
public:
    [[nodiscard]] StageResult checkStageBudget(
        RunStage stage,
        const StageBudgetSpec& budget,
        const RunContext& context) const override;

private:
    [[nodiscard]] static bool isContextConsistent(const RunContext& context) noexcept;
};

class StageBudgetRegistry final {
public:
    StageBudgetRegistry();

    [[nodiscard]] const StageBudgetSpec& at(RunStage stage) const noexcept;
    void set(RunStage stage, StageBudgetSpec budget) noexcept;
    [[nodiscard]] bool isValid() const noexcept;

private:
    std::array<StageBudgetSpec, kRunStageCount> budgets_;
};

} // namespace application::backtest