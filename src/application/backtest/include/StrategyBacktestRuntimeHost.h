#pragma once

#include "BacktestApplicationService.h"
#include "BacktestRequestFactory.h"
#include "StrategyBacktestEngineGateway.h"
#include "StrategyBacktestEntryService.h"
#include "ThreadedAsyncBacktestScheduler.h"

#include "../../../domain/backtest/include/HistoricalMarketDataCache.h"
#include "../../../domain/backtest/strategy_engine/include/CanonicalRuntimeStrategies.h"
#include "../../../domain/backtest/strategy_engine/include/StrategyBacktestEngine.h"

namespace application::backtest {

class StrategyBacktestRuntimeHost final {
public:
    StrategyBacktestRuntimeHost(astock::market::IDataProvider& dataProvider,
                                const domain::backtest::ITradingDayRangeResolver& tradingDayRangeResolver,
                                std::uint32_t marketDataPeriod,
                                domain::backtest::strategy_engine::CandidateCount warmupDayCount,
                                const domain::backtest::IFactorSnapshotProvider* factorSnapshotProvider = nullptr,
                                IOverlayFactorBindingSink* overlayFactorBindingSink = nullptr);

    [[nodiscard]] StrategyBacktestEntryService& entryService();
    [[nodiscard]] const StrategyBacktestEntryService& entryService() const;

private:
    domain::backtest::HistoricalMarketDataCache marketDataCache_;
    domain::backtest::strategy_engine::CanonicalLayerSelectionStrategy layerSelectionStrategy_;
    domain::backtest::strategy_engine::CanonicalRuleChecker ruleChecker_;
    domain::backtest::strategy_engine::CanonicalExecutionSimulator executionSimulator_;
    domain::backtest::strategy_engine::CanonicalPortfolioOptimizer portfolioOptimizer_;
    domain::backtest::strategy_engine::CanonicalExecutionPolicyStrategy executionPolicyStrategy_;
    domain::backtest::strategy_engine::StrategyBacktestEngine engine_;
    StrategyBacktestEngineGateway engineGateway_;
    ThreadedAsyncBacktestScheduler asyncScheduler_;
    BacktestApplicationService applicationService_;
    CanonicalBacktestRequestFactory requestFactory_;
    StrategyBacktestEntryService entryService_;
};

} // namespace application::backtest