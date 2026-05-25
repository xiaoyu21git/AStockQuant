#pragma once

#include "FactorBacktestTradingAdapter.h"

namespace application::trading {

class DefaultFactorBacktestTradingAdapter final : public FactorBacktestTradingAdapter {
public:
    [[nodiscard]] domain::trading::TargetPortfolio buildTargetPortfolio(
        const factor::CalculationResult& factorResult,
        const factor::BacktestConfig& config) const override;

    [[nodiscard]] domain::trading::TradeIntentBatch buildIntentBatch(
        const domain::trading::TargetPortfolio& targetPortfolio,
        const factor::BacktestConfig& config) const override;

    [[nodiscard]] domain::trading::TradingExecutionContext buildExecutionContext(
        const factor::BacktestConfig& config) const override;
};

} // namespace application::trading