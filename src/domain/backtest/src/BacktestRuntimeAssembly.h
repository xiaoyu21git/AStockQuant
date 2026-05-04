#pragma once

#include "BacktestDecisionRuntime.h"
#include "BacktestExecutionRuntime.h"

#include <memory>
#include <string>
#include <vector>

namespace domain::backtest {
class FactorDataProvider;
}

namespace domain::backtest::runtime {

struct BacktestRunSupport {
    BacktestRuntimeState state;
    StrategyProfile profile;
    RuleTemplateRuntimeSupport ruleTemplateSupport;
    FactorOverlayRuntimeSupport factorOverlaySupport;
};

BacktestRunSupport buildBacktestRunSupport(
    double initialCapital,
    const std::string& strategyName,
    double maxPositionRatio,
    const std::map<std::string, double>& strategyParams,
    const std::map<std::string, std::string>& strategyOptions,
    const std::vector<domain::model::Bar>& overlayBars,
    const std::shared_ptr<domain::backtest::FactorDataProvider>& factorDataProvider);

} // namespace domain::backtest::runtime