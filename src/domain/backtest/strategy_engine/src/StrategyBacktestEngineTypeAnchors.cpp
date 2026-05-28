#include "BacktestRequestValidator.h"
#include "BacktestRuntimeSession.h"
#include "DiagnosticsRecorder.h"
#include "LayerExecutionPipeline.h"
#include "ResultAssembler.h"
#include "StrategyBacktestEngine.h"
#include "StrategyBacktestEngineFailure.h"
#include "StrategyBacktestEngineInterfaces.h"
#include "StrategyBacktestEngineTypes.h"

#include <type_traits>

namespace domain::backtest::strategy_engine {

static_assert(std::is_default_constructible_v<BacktestRequest>);
static_assert(std::is_default_constructible_v<BacktestResultDto>);
static_assert(std::is_default_constructible_v<BacktestRuntimeSessionState>);
static_assert(std::is_class_v<EngineFailure>);
static_assert(std::is_abstract_v<IMarketDataCache>);
static_assert(std::is_abstract_v<ILayerSelectionStrategy>);
static_assert(std::is_abstract_v<IRuleChecker>);
static_assert(std::is_abstract_v<IExecutionSimulator>);
static_assert(std::is_abstract_v<IPortfolioOptimizer>);
static_assert(std::is_abstract_v<IExecutionPolicyStrategy>);
static_assert(std::is_abstract_v<IStrategyBacktestEngine>);
static_assert(std::is_abstract_v<IAsyncBacktestScheduler>);
static_assert(std::is_class_v<BacktestRequestValidator>);
static_assert(std::is_class_v<BacktestRuntimeSession>);
static_assert(std::is_class_v<LayerExecutionPipeline>);
static_assert(std::is_class_v<DiagnosticsRecorder>);
static_assert(std::is_class_v<ResultAssembler>);
static_assert(std::is_class_v<StrategyBacktestEngine>);

} // namespace domain::backtest::strategy_engine