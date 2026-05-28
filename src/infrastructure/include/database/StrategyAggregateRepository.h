#pragma once

#include "../../../domain/strategy/include/StrategyAggregate.h"

namespace infrastructure::database {

class StrategyAggregateRepository {
public:
    virtual ~StrategyAggregateRepository() = default;

    virtual domain::strategy::StrategyAggregate findById(const domain::strategy::StrategyId& strategyId) const = 0;
    virtual QVector<domain::strategy::StrategyAggregate> findAll() const = 0;
    virtual QVector<domain::strategy::StrategyAggregate> findByStoredType(domain::backtest::StrategyStoredType storedType) const = 0;
    virtual QVector<domain::strategy::StrategyAggregate> findByStatus(strategy_view::StrategyLifecycleStatus status) const = 0;
    virtual domain::strategy::StrategyAggregate save(const domain::strategy::StrategyAggregate& aggregate) = 0;
    virtual domain::strategy::StrategyAggregate update(const domain::strategy::StrategyAggregate& aggregate) = 0;
    virtual domain::strategy::StrategyAggregate updateStatus(const domain::strategy::StrategyId& strategyId,
                                                            strategy_view::StrategyLifecycleStatus status) = 0;
    virtual void remove(const domain::strategy::StrategyId& strategyId) = 0;
    virtual bool exists(const domain::strategy::StrategyId& strategyId) const = 0;
};

} // namespace infrastructure::database