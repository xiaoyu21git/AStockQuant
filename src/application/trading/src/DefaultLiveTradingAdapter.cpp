#include "../include/DefaultLiveTradingAdapter.h"

#include "../../../domain/types/DomainDate.h"

#include <algorithm>
#include <string>

namespace application::trading {
namespace {

domain::trading::OrderSide resolveOrderSide(const std::string& rawSide)
{
    std::string normalized = rawSide;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    if (normalized == "sell") {
        return domain::trading::OrderSide::Sell;
    }
    if (normalized == "sellshort") {
        return domain::trading::OrderSide::SellShort;
    }
    if (normalized == "buytocover") {
        return domain::trading::OrderSide::BuyToCover;
    }
    return domain::trading::OrderSide::Buy;
}

domain::trading::OrderType resolveOrderType(const std::string& rawType)
{
    std::string normalized = rawType;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    if (normalized == "market") {
        return domain::trading::OrderType::Market;
    }
    if (normalized == "market_on_close") {
        return domain::trading::OrderType::MarketOnClose;
    }
    if (normalized == "next_session_open") {
        return domain::trading::OrderType::NextSessionOpen;
    }
    return domain::trading::OrderType::Limit;
}

domain::DomainDate fromIsoString(const std::string& isoStr)
{
    domain::DomainDate date;
    if (isoStr.size() >= 10) {
        int y = std::stoi(isoStr.substr(0, 4));
        int m = std::stoi(isoStr.substr(5, 2));
        int d = std::stoi(isoStr.substr(8, 2));
        date.value = y * 10000 + m * 100 + d;
    }
    return date;
}

domain::strategy::StrategyIdentity buildStrategyIdentity(const QVariantMap& request)
{
    domain::strategy::StrategyIdentity identity;
    const QString strategyId = request.value(QStringLiteral("strategyId")).toString().trimmed();
    const QString strategyName = request.value(QStringLiteral("strategyName"), strategyId).toString().trimmed();
    identity.strategyId = domain::strategy::StrategyId(strategyId.toStdString());
    identity.strategyCode = domain::strategy::StrategyCode(strategyId.toStdString());
    identity.strategyName = domain::strategy::StrategyName(strategyName.toStdString());
    identity.storedType = domain::backtest::StrategyStoredType::Custom;
    identity.behaviorKind = domain::backtest::StrategyBehaviorKind::Custom;
    identity.executionKind = domain::strategy::StrategyExecutionKind::Standard;
    return identity;
}

} // namespace

domain::trading::TradeIntentBatch DefaultLiveTradingAdapter::buildIntentBatch(
    const QVariantMap& request) const
{
    domain::trading::TradeIntentBatch batch;
    batch.batchId = foundation::utils::Uuid::generate_v4();
    batch.mode = domain::trading::TradingMode::Live;
    batch.source = request.value(QStringLiteral("strategyId")).toString().trimmed().isEmpty()
        ? domain::trading::IntentSource::ManualTrader
        : domain::trading::IntentSource::LiveStrategy;

    domain::trading::TradeIntent intent;
    intent.intentId = foundation::utils::Uuid::generate_v4();
    intent.source = batch.source;
    intent.strategyIdentity = buildStrategyIdentity(request);
    intent.side = resolveOrderSide(request.value(QStringLiteral("side")).toString().toStdString());
    intent.orderType = resolveOrderType(request.value(QStringLiteral("orderType")).toString().toStdString());
    intent.symbol = domain::strategy::SymbolCode(request.value(QStringLiteral("symbol")).toString().trimmed().toStdString());
    intent.quantity.value = request.value(QStringLiteral("quantity")).toLongLong();
    intent.referencePrice.value = request.value(QStringLiteral("price")).toDouble();
    intent.signalDate = fromIsoString(request.value(QStringLiteral("signalDate")).toString().trimmed().toStdString());
    intent.effectiveDate = fromIsoString(request.value(QStringLiteral("effectiveDate")).toString().trimmed().toStdString());
    batch.intents.push_back(intent);
    return batch;
}

domain::trading::TradingExecutionContext DefaultLiveTradingAdapter::buildExecutionContext(
    const QVariantMap& tradingConfiguration,
    const QVariantMap& riskConfiguration) const
{
    domain::trading::TradingExecutionContext context;
    context.mode = domain::trading::TradingMode::Live;
    context.marketProfile = factor::MarketEnvironmentProfile::CN_A_SHARE;
    context.costProfile.initialCapital.value = 0.0;
    context.costProfile.commissionRate.value = tradingConfiguration.value(QStringLiteral("commissionRate")).toDouble();
    context.costProfile.slippageRate.value = tradingConfiguration.value(QStringLiteral("slippageRate")).toDouble();
    context.costProfile.taxRate.value = tradingConfiguration.value(QStringLiteral("taxRate")).toDouble();
    context.riskProfile.maxPositionRatio.value = riskConfiguration.value(QStringLiteral("maxTotalExposure")).toDouble();
    context.riskProfile.maxSinglePositionRatio.value = riskConfiguration.value(QStringLiteral("maxPositionPercent")).toDouble();
    context.riskProfile.maxDrawdownLimit.value = riskConfiguration.value(QStringLiteral("maxDrawdownLimit")).toDouble();
    context.riskProfile.stopLossRate.value = riskConfiguration.value(QStringLiteral("stopLossRate")).toDouble();
    context.riskProfile.maxBatchOrders = riskConfiguration.value(QStringLiteral("maxBatchOrders")).toInt();
    context.riskProfile.maxBatchNotional.value = riskConfiguration.value(QStringLiteral("maxBatchNotional")).toDouble();
    context.riskProfile.enableTradingHalt = riskConfiguration.value(QStringLiteral("enableTradingHalt")).toBool();
    context.executionProfile.executionKind = domain::strategy::StrategyExecutionKind::Standard;
    context.executionProfile.positionSizingMethod = domain::strategy::PositionSizingMethod::Discretionary;
    context.executionProfile.priceModel = tradingConfiguration.value(QStringLiteral("useMarketOnClose")).toBool()
        ? domain::trading::ExecutionPriceModel::MarketOnClose
        : domain::trading::ExecutionPriceModel::Custom;
    context.executionProfile.shortSellingMode = tradingConfiguration.value(QStringLiteral("enableShortSelling")).toBool()
        ? domain::strategy::ShortSellingMode::Enabled
        : domain::strategy::ShortSellingMode::Disabled;
    context.executionProfile.rebalanceFrequencyDays = domain::strategy::RebalanceFrequencyDays{1};
    context.runtimeOptions.maxThreads = 1;
    context.runtimeOptions.enableCache = false;
    context.runtimeOptions.cacheTtlSeconds = 0;
    diagnosticSet(context.metadata, "adapter", "DefaultLiveTradingAdapter");
    diagnosticSet(context.metadata, "missingEffectiveDateRequiresCallerFill", "1");
    return context;
}

std::vector<domain::trading::FillEvent> DefaultLiveTradingAdapter::translateRuntimeFeedback(
    const QVariantList& runtimeEvents) const
{
    std::vector<domain::trading::FillEvent> fills;
    fills.reserve(runtimeEvents.size());

    for (const QVariant& runtimeEventValue : runtimeEvents) {
        const QVariantMap runtimeEvent = runtimeEventValue.toMap();
        if (runtimeEvent.isEmpty()) {
            continue;
        }

        domain::trading::FillEvent fill;
        fill.orderRef.orderId = domain::strategy::OrderId(runtimeEvent.value(QStringLiteral("orderId")).toString().trimmed().toStdString());
        fill.orderRef.batchId = domain::strategy::BatchId(runtimeEvent.value(QStringLiteral("batchId")).toString().trimmed().toStdString());
        fill.orderRef.executionScopeId = domain::strategy::ExecutionScopeId(runtimeEvent.value(QStringLiteral("executionScopeId")).toString().trimmed().toStdString());
        fill.symbol = domain::strategy::SymbolCode(runtimeEvent.value(QStringLiteral("symbol")).toString().trimmed().toStdString());
        fill.side = resolveOrderSide(runtimeEvent.value(QStringLiteral("side")).toString().toStdString());
        fill.fillQuantity.value = runtimeEvent.value(QStringLiteral("quantity")).toLongLong();
        fill.fillPrice.value = runtimeEvent.value(QStringLiteral("price")).toDouble();
        fill.fillDate = fromIsoString(runtimeEvent.value(QStringLiteral("fillDate")).toString().trimmed().toStdString());

        if (fill.isValid()) {
            fills.push_back(fill);
        }
    }

    return fills;
}

} // namespace application::trading