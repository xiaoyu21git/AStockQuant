#include "../include/DefaultStrategyBacktestTradingAdapter.h"

#include <QString>

namespace application::trading {
namespace {

QVector<domain::strategy::SymbolCode> resolvedSymbolsForRequest(const domain::backtest::BacktestRequest& request)
{
    if (!request.universeSpec.backtestSymbolPool.isEmpty()) {
        return request.universeSpec.backtestSymbolPool;
    }
    if (!request.universeSpec.resolvedSymbols.isEmpty()) {
        return request.universeSpec.resolvedSymbols;
    }
    if (!request.universeSpec.explicitSymbols.isEmpty()) {
        return request.universeSpec.explicitSymbols;
    }
    return {};
}

} // namespace

domain::trading::TradeIntentBatch DefaultStrategyBacktestTradingAdapter::buildIntentBatch(
    const domain::backtest::BacktestRequest& request) const
{
    domain::trading::TradeIntentBatch batch;
    batch.batchId = foundation::utils::Uuid::generate_v4();
    batch.mode = domain::trading::TradingMode::Backtest;
    batch.source = domain::trading::IntentSource::StrategyBacktest;

    const QVector<domain::strategy::SymbolCode> symbols = resolvedSymbolsForRequest(request);
    if (symbols.isEmpty()) {
        return batch;
    }

    const double rawWeight = 1.0 / static_cast<double>(symbols.size());
    for (const domain::strategy::SymbolCode& symbol : symbols) {
        domain::trading::TargetPosition position;
        position.symbol = symbol;
        position.targetWeight.value = rawWeight;
        position.referencePrice.value = 0.0;
        batch.targetPositions.append(position);
    }

    return batch;
}

domain::trading::TradingExecutionContext DefaultStrategyBacktestTradingAdapter::buildExecutionContext(
    const domain::backtest::BacktestRequest& request) const
{
    domain::trading::TradingExecutionContext context;
    context.mode = domain::trading::TradingMode::Backtest;
    context.marketProfile = request.marketEnvironmentSpec.profile;
    context.window.startDate = request.window.startDate;
    context.window.endDate = request.window.endDate;
    context.costProfile.initialCapital = request.costSpec.initialCapital;
    context.costProfile.commissionRate = request.costSpec.commissionRate;
    context.costProfile.slippageRate = request.costSpec.slippageRate;
    context.costProfile.taxRate = request.costSpec.taxRate;
    context.riskProfile.maxPositionRatio = request.riskSpec.maxPositionRatio;
    context.riskProfile.maxSinglePositionRatio = request.riskSpec.maxSinglePositionRatio;
    context.riskProfile.maxDrawdownLimit = request.riskSpec.maxDrawdownLimit;
    context.riskProfile.stopLossRate = request.riskSpec.stopLossRate;
    context.riskProfile.maxBatchOrders = request.strategySpec.executionPolicy.batchExecution.maxBatchOrders;
    context.riskProfile.maxBatchNotional = request.strategySpec.executionPolicy.batchExecution.maxBatchNotional;
    context.executionProfile.executionKind = request.executionSpec.executionKind;
    context.executionProfile.positionSizingMethod = request.executionSpec.positionSizingMethod;
    context.executionProfile.priceModel = request.executionSpec.useMarketOnClose
        ? domain::trading::ExecutionPriceModel::MarketOnClose
        : domain::trading::ExecutionPriceModel::NextSessionOpen;
    context.executionProfile.enableShortSelling = request.executionSpec.enableShortSelling;
    context.executionProfile.rebalanceFrequencyDays = request.executionSpec.rebalanceFrequencyDays;
    context.runtimeOptions.maxThreads = request.runtimeOptions.maxThreads;
    context.runtimeOptions.enableCache = request.runtimeOptions.enableCache;
    context.runtimeOptions.cacheTtlSeconds = request.runtimeOptions.cacheTtlSeconds;
    context.metadata.insert(QStringLiteral("adapter"), QStringLiteral("DefaultStrategyBacktestTradingAdapter"));
    return context;
}

} // namespace application::trading