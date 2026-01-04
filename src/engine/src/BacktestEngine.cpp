#include "BacktestEngine.h"

namespace engine {

BacktestEngine::BacktestEngine(double initialCash)
    : m_account(initialCash) {}

void BacktestEngine::addStrategy(const std::shared_ptr<domain::Strategy>& strategy) {
    m_strategies.push_back(strategy);
}

BacktestResult BacktestEngine::run(const std::vector<domain::model::Bar>& bars) {
    BacktestResult result;

    for (auto& s : m_strategies) s->onStart();

    for (const auto& bar : bars) {
        for (auto& s : m_strategies) {
            auto action = s->onBar(bar);

            if (action == domain::StrategyAction::OpenLong) {
                if (m_account.openLong(bar, 1)) {
                    m_account.setLastPrice(bar.close);
                    result.trades.emplace_back(
                        engine::TradeRecord{
                            s->name(),        // strategyName
                            bar.symbol,       // symbol
                            std::to_string(bar.time), // time
                            bar.close,        // price
                            true              // isBuy
                        }
                    );
                }
            }
            else if (action == domain::StrategyAction::CloseLong) {
                if (m_account.closeLong(bar)) {
                    result.trades.emplace_back(
                        engine::TradeRecord{
                            s->name(), 
                            bar.symbol,
                            std::to_string(bar.time),
                            bar.close,
                            false
                        }
                    );
                }
            }
        }
    }

    for (auto& s : m_strategies) s->onFinish();

    return result;
}

} // namespace engine
