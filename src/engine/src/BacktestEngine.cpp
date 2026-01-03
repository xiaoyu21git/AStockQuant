#include "BacktestEngine.h"

namespace engine {

BacktestResult BacktestEngine::run(
    domain::Strategy& strategy,
    const std::vector<domain::model::Bar>& bars)
{
    BacktestResult result;

    for (const auto& bar : bars) {
        auto action = strategy.onBar(bar);

        if (action == domain::StrategyAction::OpenLong) {
            m_entryPrice = bar.close;
            result.tradeCount++;

            result.trades.push_back(engine::TradeRecord{
                std::to_string(bar.time),
                bar.close,
                true
            });
        }
        else if (action == domain::StrategyAction::CloseLong) {
            double profit = bar.close - m_entryPrice;
            result.pnl += profit;

            result.trades.push_back(engine::TradeRecord{
                std::to_string(bar.time),
                bar.close,
                false
            });
        }
    }

    return result;
}

}
