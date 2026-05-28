#include "../include/StrategyBacktestRuntimeHost.h"

namespace application::backtest {

StrategyBacktestRuntimeHost::StrategyBacktestRuntimeHost(
    astock::market::IDataProvider& dataProvider,
    const domain::backtest::ITradingDayRangeResolver& tradingDayRangeResolver,
    const std::uint32_t marketDataPeriod,
    const domain::backtest::strategy_engine::CandidateCount warmupDayCount,
    const domain::backtest::IFactorSnapshotProvider* factorSnapshotProvider,
    IOverlayFactorBindingSink* overlayFactorBindingSink)
    : marketDataCache_(dataProvider,
                       tradingDayRangeResolver,
                       marketDataPeriod,
                       warmupDayCount,
                       factorSnapshotProvider)
    , layerSelectionStrategy_()
    , ruleChecker_()
    , executionSimulator_()
    , portfolioOptimizer_()
    , executionPolicyStrategy_()
    , engine_(marketDataCache_,
              layerSelectionStrategy_,
              ruleChecker_,
              executionSimulator_,
              portfolioOptimizer_,
              executionPolicyStrategy_,
              domain::backtest::strategy_engine::BacktestRequestValidator{},
              domain::backtest::strategy_engine::ResultAssembler{})
    , engineGateway_(engine_)
    , asyncScheduler_(engineGateway_)
    , applicationService_(engineGateway_, asyncScheduler_)
    , requestFactory_()
    , entryService_(requestFactory_, applicationService_, overlayFactorBindingSink)
{
}

StrategyBacktestEntryService& StrategyBacktestRuntimeHost::entryService()
{
    return entryService_;
}

const StrategyBacktestEntryService& StrategyBacktestRuntimeHost::entryService() const
{
    return entryService_;
}

} // namespace application::backtest