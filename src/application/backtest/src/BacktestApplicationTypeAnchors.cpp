#include "../include/BacktestApplicationService.h"
#include "../include/BacktestRequestFactory.h"
#include "../include/StrategyBacktestEntryService.h"
#include "../include/StrategyBacktestEngineGateway.h"
#include "../include/StrategyBacktestRuntimeHost.h"
#include "../include/ThreadedAsyncBacktestScheduler.h"

#include <type_traits>

namespace application::backtest {

static_assert(std::is_class_v<BacktestApplicationService>);
static_assert(std::is_abstract_v<BacktestEngineGateway>);
static_assert(std::is_abstract_v<BacktestRequestFactory>);
static_assert(std::is_class_v<CanonicalBacktestRequestFactory>);
static_assert(std::is_class_v<StrategyBacktestEntryService>);
static_assert(std::is_class_v<StrategyBacktestEngineGateway>);
static_assert(std::is_class_v<StrategyBacktestRuntimeHost>);
static_assert(std::is_class_v<ThreadedAsyncBacktestScheduler>);

} // namespace application::backtest