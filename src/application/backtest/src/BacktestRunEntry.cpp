#include "BacktestRunEntry.h"

namespace application::backtest {

RunBacktestIngressResult BacktestRunEntry::runBacktest(
    const ExistingModuleSlots& slots,
    RunSpec spec)
{
    BacktestRuntimeBuildError buildError;
    std::optional<BacktestRuntime> runtime = BacktestRuntime::build(slots, &buildError);
    if (!runtime.has_value()) {
        RunBacktestIngressResult result;
        result.code = buildError.code;
        result.result.code = buildError.code;
        result.result.failureReason = toRunFailureReason(buildError.code);
        result.result.completedStage = RunStage::Validate;
        result.result.partial = false;
        return result;
    }

    return runtime->runBacktest(std::move(spec));
}

RunBacktestIngressResult BacktestRunEntry::runBacktestWithFillSideMode(
    const ExistingModuleSlots& slots,
    RunSpec spec,
    FillOrderSideMode fillSideMode)
{
    spec.fillOrderSideMode = fillSideMode;
    return runBacktest(slots, std::move(spec));
}

} // namespace application::backtest