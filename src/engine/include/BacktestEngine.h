#pragma once
#include <vector>
#include <memory>
#include "BacktestResult.h"
#include "Account.h"
#include "Strategy.h"
#include "Event.h"

namespace engine {

class BacktestEngine {
public:
    explicit BacktestEngine(double initialCash = 100000.0);

    void addStrategy(const std::shared_ptr<domain::Strategy>& strategy);
    void addStrategy(const std::string& strategyName);

    BacktestResult run(const std::vector<domain::model::Bar>& bars);

private:
    Account m_account;
    std::vector<std::shared_ptr<domain::Strategy>> m_strategies;
    std::shared_ptr<EventBus> eventBus_;
};

} // namespace engine
