#pragma once

#include "../../../domain/backtest/include/BacktestResultDto.h"

namespace infrastructure::database {

class BacktestResultRepository {
public:
    virtual ~BacktestResultRepository() = default;

    virtual domain::backtest::BacktestResultDto save(const domain::backtest::BacktestResultDto& result) = 0;
    virtual domain::backtest::BacktestResultDto findLatestByStrategyId(const domain::strategy::StrategyId& strategyId) const = 0;
    virtual QVector<domain::backtest::BacktestResultDto> findHistoryByStrategyId(const domain::strategy::StrategyId& strategyId,
                                                                                 int limit) const = 0;
};

} // namespace infrastructure::database