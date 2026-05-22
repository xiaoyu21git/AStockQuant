#pragma once

#include <QString>
#include <QVariantMap>

#include "../../../domain/factor/include/factor_enums.h"
#include "RiskConfigService.h"
#include "StrategyStructureResolvers.h"

namespace bridge {

inline void insertMarketEnvironmentProfile(QVariantMap& snapshot,
                                           factor::MarketEnvironmentProfile marketEnvironmentProfile)
{
    risk::config::setMarketEnvironmentProfile(
        snapshot,
        factor::marketEnvironmentProfileIndex(marketEnvironmentProfile));
}

inline int strategyExecutionLagTradingDays(factor::MarketEnvironmentProfile marketEnvironmentProfile)
{
    return marketEnvironmentProfile == factor::MarketEnvironmentProfile::CN_A_SHARE ? 1 : 0;
}

inline bool strategyAllowSameDaySellAfterBuy(factor::MarketEnvironmentProfile marketEnvironmentProfile)
{
    return strategyExecutionLagTradingDays(marketEnvironmentProfile) == 0;
}

inline factor::StrategyShortSellingMode strategyShortSellingMode(factor::MarketEnvironmentProfile marketEnvironmentProfile,
                                                                 bool enableShortSelling)
{
    if (marketEnvironmentProfile == factor::MarketEnvironmentProfile::CN_A_SHARE
        || marketEnvironmentProfile == factor::MarketEnvironmentProfile::GENERIC_EQUITY) {
        return factor::StrategyShortSellingMode::LONG_ONLY;
    }
    return enableShortSelling
        ? factor::StrategyShortSellingMode::MARKET_AND_STRATEGY_ENABLED
        : factor::StrategyShortSellingMode::MARKET_ALLOWED_BUT_STRATEGY_DISABLED;
}

inline factor::StrategyExecutionPriceModel strategyExecutionPriceModel(const QVariantMap& configMap)
{
    return configMap.value(QStringLiteral("useMarketOnClose"), true).toBool()
        ? factor::StrategyExecutionPriceModel::MARKET_ON_CLOSE
        : factor::StrategyExecutionPriceModel::NEXT_SESSION_OPEN;
}

inline factor::StrategyReturnAttributionMode strategyReturnAttributionMode(factor::MarketEnvironmentProfile marketEnvironmentProfile)
{
    return strategyExecutionLagTradingDays(marketEnvironmentProfile) > 0
        ? factor::StrategyReturnAttributionMode::POST_SIGNAL_NEXT_TRADING_DAY_RETURN
        : factor::StrategyReturnAttributionMode::POST_SIGNAL_SAME_TRADING_DAY_RETURN;
}

inline factor::StrategyPriceLimitMode strategyPriceLimitMode(factor::MarketEnvironmentProfile marketEnvironmentProfile)
{
    return marketEnvironmentProfile == factor::MarketEnvironmentProfile::CN_A_SHARE
        ? factor::StrategyPriceLimitMode::DAILY_PRICE_LIMIT
        : factor::StrategyPriceLimitMode::NONE;
}

inline factor::StrategyCalendarProfile strategyCalendarProfile(factor::MarketEnvironmentProfile marketEnvironmentProfile)
{
    switch (marketEnvironmentProfile) {
    case factor::MarketEnvironmentProfile::CN_A_SHARE:
        return factor::StrategyCalendarProfile::CN_A_SHARE_TRADING_CALENDAR;
    case factor::MarketEnvironmentProfile::HK_EQUITY:
        return factor::StrategyCalendarProfile::HK_EQUITY_TRADING_CALENDAR;
    case factor::MarketEnvironmentProfile::US_EQUITY:
        return factor::StrategyCalendarProfile::US_EQUITY_TRADING_CALENDAR;
    case factor::MarketEnvironmentProfile::GENERIC_EQUITY:
    default:
        return factor::StrategyCalendarProfile::GENERIC_EQUITY_TRADING_CALENDAR;
    }
}

inline factor::StrategyCostProfile strategyCostProfile(factor::MarketEnvironmentProfile marketEnvironmentProfile)
{
    switch (marketEnvironmentProfile) {
    case factor::MarketEnvironmentProfile::CN_A_SHARE:
        return factor::StrategyCostProfile::CN_A_SHARE_DEFAULT_COST;
    case factor::MarketEnvironmentProfile::HK_EQUITY:
        return factor::StrategyCostProfile::HK_EQUITY_DEFAULT_COST;
    case factor::MarketEnvironmentProfile::US_EQUITY:
        return factor::StrategyCostProfile::US_EQUITY_DEFAULT_COST;
    case factor::MarketEnvironmentProfile::GENERIC_EQUITY:
    default:
        return factor::StrategyCostProfile::GENERIC_EQUITY_DEFAULT_COST;
    }
}

inline void insertExecutionPolicySnapshotSemantics(QVariantMap& snapshot,
                                                   const QVariantMap& configMap,
                                                   factor::MarketEnvironmentProfile marketEnvironmentProfile)
{
    snapshot.insert(QStringLiteral("executionLagTradingDays"), strategyExecutionLagTradingDays(marketEnvironmentProfile));
    snapshot.insert(QStringLiteral("allowSameDaySellAfterBuy"), strategyAllowSameDaySellAfterBuy(marketEnvironmentProfile));
    snapshot.insert(QStringLiteral("shortSellingMode"), factor::strategyShortSellingModeIndex(strategyShortSellingMode(
        marketEnvironmentProfile,
        configMap.value(QStringLiteral("enableShortSelling"), false).toBool())));
    snapshot.insert(QStringLiteral("executionPriceModel"), factor::strategyExecutionPriceModelIndex(strategyExecutionPriceModel(configMap)));
    snapshot.insert(QStringLiteral("priceLimitMode"), factor::strategyPriceLimitModeIndex(strategyPriceLimitMode(marketEnvironmentProfile)));
}

inline void insertBacktestAssumptionsSnapshotSemantics(QVariantMap& snapshot,
                                                       const QVariantMap& configMap,
                                                       factor::MarketEnvironmentProfile marketEnvironmentProfile)
{
    snapshot.insert(QStringLiteral("returnAttributionMode"), factor::strategyReturnAttributionModeIndex(strategyReturnAttributionMode(marketEnvironmentProfile)));
    snapshot.insert(QStringLiteral("calendarProfile"), factor::strategyCalendarProfileIndex(strategyCalendarProfile(marketEnvironmentProfile)));
    snapshot.insert(QStringLiteral("costProfile"), factor::strategyCostProfileIndex(strategyCostProfile(marketEnvironmentProfile)));
    if (configMap.contains(QStringLiteral("riskFreeRate"))) {
        snapshot.insert(QStringLiteral("riskFreeRateProfile"), configMap.value(QStringLiteral("riskFreeRate")));
    }
}

inline void insertStrategyScopeContextSemantics(QVariantMap& snapshot,
                                                factor::MarketEnvironmentProfile marketEnvironmentProfile)
{
    insertMarketEnvironmentProfile(snapshot, marketEnvironmentProfile);
}

inline QVariantMap buildStrategyBacktestResultConfigMap(
    QVariantMap configMap,
    factor::MarketEnvironmentProfile marketEnvironmentProfile)
{
    insertMarketEnvironmentProfile(configMap, marketEnvironmentProfile);
    QVariantMap executionPolicy = configMap.value("executionPolicySnapshot").toMap();
    insertExecutionPolicySnapshotSemantics(executionPolicy, configMap, marketEnvironmentProfile);
    QVariantMap backtestAssumptions = configMap.value("backtestAssumptionsSnapshot").toMap();
    insertBacktestAssumptionsSnapshotSemantics(backtestAssumptions, configMap, marketEnvironmentProfile);
    QVariantMap strategyScopeContext = configMap.value("strategyScopeContextSnapshot").toMap();
    insertStrategyScopeContextSemantics(strategyScopeContext, marketEnvironmentProfile);
    configMap.insert(QStringLiteral("executionLagTradingDays"), executionPolicy.value(QStringLiteral("executionLagTradingDays")));
    configMap.insert(QStringLiteral("executionPriceModel"), executionPolicy.value(QStringLiteral("executionPriceModel")));
    configMap.insert(QStringLiteral("returnAttributionMode"), backtestAssumptions.value(QStringLiteral("returnAttributionMode")));
    configMap.insert("executionPolicySnapshot", executionPolicy);
    configMap.insert("backtestAssumptionsSnapshot", backtestAssumptions);
    configMap.insert("strategyScopeContextSnapshot", strategyScopeContext);
    return configMap;
}

inline QVariantMap buildStrategyBacktestResultConfigMap(
    QVariantMap configMap,
    const QString& dataSourceMode,
    int selectedDatasetId,
    factor::MarketEnvironmentProfile marketEnvironmentProfile)
{
    const QVariantMap strategyParamsMap = configMap.value("strategyParams").toMap();
    configMap.insert("strategyParams", strategyParamsMap);
    configMap.insert("dataSourceMode", dataSourceMode);
    configMap.insert("selectedDatasetId", selectedDatasetId);

    QVariantMap structureSource = configMap;
    QVariantMap resolverParameters = strategyParamsMap;
    if (configMap.contains("maxPositionRatio")) {
        risk::config::setMaxTotalExposure(resolverParameters, configMap.value("maxPositionRatio").toDouble());
    }
    if (configMap.contains("maxSinglePositionRatio")) {
        risk::config::setMaxPositionPercent(resolverParameters, configMap.value("maxSinglePositionRatio").toDouble());
    }
    if (configMap.contains("stopLossRate")) {
        risk::config::setStopLossPercent(resolverParameters, configMap.value("stopLossRate").toDouble());
    }
    if (configMap.contains("takeProfitRate")) {
        risk::config::setTakeProfitPercent(resolverParameters, configMap.value("takeProfitRate").toDouble());
    }
    if (configMap.contains("rebalanceFrequency")) {
        risk::config::setRebalanceDays(resolverParameters, configMap.value("rebalanceFrequency").toInt());
    }
    structureSource.insert("parameters", resolverParameters);
    const bridge::config::StrategyStructureResolverSet resolverSet;
    const bridge::config::StrategyStructureResolution resolvedStructures = resolverSet.resolve(structureSource);
    configMap.insert("ruleProfileSnapshot", resolvedStructures.ruleProfile);
    configMap.insert("executionPolicySnapshot", resolvedStructures.executionPolicy);
    configMap.insert("backtestAssumptionsSnapshot", resolvedStructures.backtestAssumptions);
    configMap.insert("strategyScopeContextSnapshot", resolvedStructures.strategyScopeContext);
    configMap.insert("factorOverlaySnapshot", resolvedStructures.factorOverlay);
    return buildStrategyBacktestResultConfigMap(std::move(configMap), marketEnvironmentProfile);
}

} // namespace bridge