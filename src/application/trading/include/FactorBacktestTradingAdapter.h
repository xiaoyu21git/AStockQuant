#pragma once

#include "../../../domain/trading/include/TradingTypes.h"

namespace factor {
struct CalculationResult;
struct BacktestConfig;
}

namespace application::trading {

class FactorBacktestTradingAdapter {
public:
    virtual ~FactorBacktestTradingAdapter() = default;

    [[nodiscard]] virtual domain::trading::TargetPortfolio buildTargetPortfolio(
        const factor::CalculationResult& factorResult,
        const factor::BacktestConfig& config) const = 0;

    [[nodiscard]] virtual domain::trading::TradeIntentBatch buildIntentBatch(
        const domain::trading::TargetPortfolio& targetPortfolio,
        const factor::BacktestConfig& config) const = 0;

    [[nodiscard]] virtual domain::trading::TradingExecutionContext buildExecutionContext(
        const factor::BacktestConfig& config) const = 0;
};

} // namespace application::trading