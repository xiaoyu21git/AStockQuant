#include "PortfolioConstructionAdapter.h"

#include "../../../domain/backtest/include/BacktestRequest.h"
#include "../../../domain/factor/include/factor_compute/FactorSignalTypes.h"

#include <algorithm>

namespace application::backtest {

StageResult RequestedTargetPositionConstructionAdapter::constructTargetPosition(RunContext& context) const
{
    StageResult result;
    result.stage = RunStage::ConstructTargetPosition;
    result.code = RunErrorCode::None;

    if (!context.spec.request) {
        result.code = RunErrorCode::StageExecutionFailed;
        return result;
    }

    const domain::backtest::BacktestRequest& request = *context.spec.request;
    const int requestedTargetCount = request.factorOverlaySpec.targetPositionCount;
    if (requestedTargetCount < kMinimumRequestedTargetCount) {
        result.code = RunErrorCode::StageExecutionFailed;
        return result;
    }

    std::uint32_t availableSignalCount = context.workingSet.signalBatch.strategySignalCount;
    if (context.workingSet.signalBatch.factorSignalSet
        && context.workingSet.signalBatch.factorSignalSet->isValid()) {
        availableSignalCount = static_cast<std::uint32_t>(
            context.workingSet.signalBatch.factorSignalSet->instruments.size());
    }

    if (availableSignalCount < kMinimumSignalCount) {
        result.code = RunErrorCode::StageExecutionFailed;
        return result;
    }

    context.workingSet.targetPositionCount = (std::min)(
        availableSignalCount,
        static_cast<std::uint32_t>(requestedTargetCount));

    if (context.workingSet.targetPositionCount == 0U) {
        result.code = RunErrorCode::StageExecutionFailed;
    }

    return result;
}

} // namespace application::backtest