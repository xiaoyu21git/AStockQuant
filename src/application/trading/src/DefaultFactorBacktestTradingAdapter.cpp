#include "../include/DefaultFactorBacktestTradingAdapter.h"

#include "../../../domain/factor/include/BaseFactor.h"
#include "../../../domain/factor/include/FactorBacktestExecutor.h"

#include <QDate>

#include <cmath>

namespace application::trading {
namespace {

QDate parseIsoDateOrEmpty(const std::string& dateText)
{
    return QDate::fromString(QString::fromStdString(dateText), Qt::ISODate);
}

} // namespace

domain::trading::TargetPortfolio DefaultFactorBacktestTradingAdapter::buildTargetPortfolio(
    const factor::CalculationResult& factorResult,
    const factor::BacktestConfig&) const
{
    domain::trading::TargetPortfolio portfolio;
    portfolio.portfolioId = foundation::utils::Uuid::generate_v4();
    portfolio.source = domain::trading::IntentSource::FactorBacktest;
    portfolio.effectiveDate = parseIsoDateOrEmpty(factorResult.date);

    double positiveSum = 0.0;
    int finiteCount = 0;
    for (const auto& [symbol, value] : factorResult.values) {
        if (!std::isfinite(value)) {
            continue;
        }
        ++finiteCount;
        if (value > 0.0) {
            positiveSum += value;
        }
    }

    for (const auto& [symbol, value] : factorResult.values) {
        if (!std::isfinite(value)) {
            continue;
        }

        domain::trading::TargetPosition position;
        position.symbol = domain::strategy::SymbolCode(QString::fromStdString(symbol));
        position.referencePrice.value = 0.0;
        if (positiveSum > 0.0 && value > 0.0) {
            position.targetWeight.value = value / positiveSum;
        } else if (positiveSum <= 0.0 && finiteCount > 0) {
            position.targetWeight.value = 1.0 / static_cast<double>(finiteCount);
        }
        portfolio.positions.append(position);
    }

    return portfolio;
}

domain::trading::TradeIntentBatch DefaultFactorBacktestTradingAdapter::buildIntentBatch(
    const domain::trading::TargetPortfolio& targetPortfolio,
    const factor::BacktestConfig&) const
{
    domain::trading::TradeIntentBatch batch;
    batch.batchId = foundation::utils::Uuid::generate_v4();
    batch.mode = domain::trading::TradingMode::Backtest;
    batch.source = domain::trading::IntentSource::FactorBacktest;
    batch.targetPositions = targetPortfolio.positions;
    return batch;
}

domain::trading::TradingExecutionContext DefaultFactorBacktestTradingAdapter::buildExecutionContext(
    const factor::BacktestConfig& config) const
{
    domain::trading::TradingExecutionContext context;
    context.mode = domain::trading::TradingMode::Backtest;
    context.marketProfile = config.marketEnvironmentProfile;
    context.window.startDate = parseIsoDateOrEmpty(config.startDate);
    context.window.endDate = parseIsoDateOrEmpty(config.endDate);
    context.costProfile.initialCapital.value = config.initialCapital;
    context.costProfile.commissionRate.value = config.transactionCost;
    context.costProfile.slippageRate.value = config.slippageRate;
    context.costProfile.taxRate.value = 0.0;
    context.riskProfile.maxPositionRatio.value = config.maxTotalExposure;
    context.riskProfile.maxSinglePositionRatio.value = config.maxPositionPercent;
    context.riskProfile.maxDrawdownLimit.value = config.maxDrawdownLimit;
    context.riskProfile.stopLossRate.value = config.stopLossRate;
    context.executionProfile.executionKind = domain::strategy::StrategyExecutionKind::FactorWeightedPortfolio;
    context.executionProfile.positionSizingMethod = domain::strategy::PositionSizingMethod::EqualWeight;
    context.executionProfile.priceModel = domain::trading::ExecutionPriceModel::NextSessionOpen;
    context.executionProfile.enableShortSelling = false;
    context.executionProfile.rebalanceFrequencyDays = config.rebalanceDays;
    context.runtimeOptions.maxThreads = config.enableDateParallelism ? 2 : 1;
    context.runtimeOptions.enableCache = !config.marketDataCacheKey.empty();
    context.runtimeOptions.cacheTtlSeconds = 0;
    context.metadata.insert(QStringLiteral("adapter"), QStringLiteral("DefaultFactorBacktestTradingAdapter"));
    if (!(config.initialCapital > 0.0)) {
        context.metadata.insert(QStringLiteral("missingInitialCapital"), true);
    }
    return context;
}

} // namespace application::trading