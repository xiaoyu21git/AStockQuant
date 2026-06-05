#include "StrategyStructureResolvers.h"

#include <QMetaType>

namespace bridge::config {

namespace {

QVariantMap variantMapValue(const QVariant& value)
{
    return value.canConvert<QVariantMap>() ? value.toMap() : QVariantMap{};
}

bool isConfiguredVariant(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return false;
    }

    if (value.typeId() == QMetaType::QString) {
        return !value.toString().trimmed().isEmpty();
    }

    return true;
}

void mergeConfiguredValues(QVariantMap& target, const QVariantMap& source)
{
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        if (!isConfiguredVariant(it.value())) {
            continue;
        }
        target.insert(it.key(), it.value());
    }
}

QStringList aliasListWithCanonical(const AbstractStrategyStructureResolver::AliasGroup& group)
{
    QStringList keys = group.aliases;
    if (!keys.contains(group.canonicalKey)) {
        keys.prepend(group.canonicalKey);
    }
    return keys;
}

} // namespace

QString AbstractStrategyStructureResolver::structureKey() const
{
    return structureKeyImpl();
}

QVariantMap AbstractStrategyStructureResolver::resolve(const StrategyResolverSourceContext& context) const
{
    QVariantMap resolved = readStructuredValues(context);
    QList<QVariantMap> sources = fallbackSources(context);
    sources.prepend(resolved);

    for (const AliasGroup& group : aliasGroups()) {
        QVariant value = firstDefinedValue(sources, aliasListWithCanonical(group));
        if (!isConfiguredValue(value) && isConfiguredValue(group.defaultValue)) {
            value = group.defaultValue;
        }
        insertIfConfigured(resolved, group.canonicalKey, value);
    }

    finalize(resolved, context);
    return resolved;
}

void AbstractStrategyStructureResolver::finalize(QVariantMap& resolved, const StrategyResolverSourceContext& context) const
{
    Q_UNUSED(resolved)
    Q_UNUSED(context)
}

QVariantMap AbstractStrategyStructureResolver::readEmbeddedStructure(const QVariantMap& container, const QString& key)
{
    return variantMapValue(container.value(key));
}

QVariant AbstractStrategyStructureResolver::firstDefinedValue(const QList<QVariantMap>& sources, const QStringList& keys)
{
    for (const QVariantMap& source : sources) {
        for (const QString& key : keys) {
            if (!source.contains(key)) {
                continue;
            }

            const QVariant value = source.value(key);
            if (isConfiguredValue(value)) {
                return value;
            }
        }
    }

    return {};
}

bool AbstractStrategyStructureResolver::isConfiguredValue(const QVariant& value)
{
    return isConfiguredVariant(value);
}

void AbstractStrategyStructureResolver::insertIfConfigured(QVariantMap& target, const QString& key, const QVariant& value)
{
    if (!isConfiguredValue(value)) {
        return;
    }
    target.insert(key, value);
}

QString RuleProfileResolver::structureKeyImpl() const
{
    return QStringLiteral("rule_profile");
}

QVariantMap RuleProfileResolver::readStructuredValues(const StrategyResolverSourceContext& context) const
{
    QVariantMap resolved = readEmbeddedStructure(context.parameters, structureKey());
    mergeConfiguredValues(resolved, readEmbeddedStructure(context.strategy, structureKey()));
    return resolved;
}

QList<QVariantMap> RuleProfileResolver::fallbackSources(const StrategyResolverSourceContext& context) const
{
    return {
        context.backtestRuntime,
        context.optimizationConfig,
        context.backtestSettings,
        context.strategyView,
        context.appliedRiskConfig
    };
}

QList<AbstractStrategyStructureResolver::AliasGroup> RuleProfileResolver::aliasGroups() const
{
    return {
        {QStringLiteral("maxTotalExposure"), {QStringLiteral("maxPositionRatio")}, {}},
        {QStringLiteral("maxPositionPercent"), {QStringLiteral("maxSinglePositionRatio"), QStringLiteral("positionPercent"), QStringLiteral("position_size"), QStringLiteral("positionSize")}, {}},
        {QStringLiteral("maxDrawdownLimit"), {QStringLiteral("max_drawdown_limit")}, {}},
        {QStringLiteral("stopLossPercent"), {QStringLiteral("stop_loss"), QStringLiteral("stopLoss")}, {}},
        {QStringLiteral("takeProfitPercent"), {QStringLiteral("take_profit"), QStringLiteral("takeProfit")}, {}},
        {QStringLiteral("varWarningPercent"), {}, {}},
        {QStringLiteral("level1Breaker"), {}, {}},
        {QStringLiteral("level2Breaker"), {}, {}},
        {QStringLiteral("level3Breaker"), {}, {}},
        {QStringLiteral("autoStopEnabled"), {}, {}},
        {QStringLiteral("maxPositions"), {}, {}},
        {QStringLiteral("maxIndustryExposure"), {}, {}},
        {QStringLiteral("maxThemeExposure"), {}, {}},
        {QStringLiteral("maxCorrelation"), {}, {}}
    };
}

QString ExecutionPolicyResolver::structureKeyImpl() const
{
    return QStringLiteral("execution_policy");
}

QVariantMap ExecutionPolicyResolver::readStructuredValues(const StrategyResolverSourceContext& context) const
{
    QVariantMap resolved = readEmbeddedStructure(context.parameters, structureKey());
    mergeConfiguredValues(resolved, readEmbeddedStructure(context.strategy, structureKey()));
    return resolved;
}

QList<QVariantMap> ExecutionPolicyResolver::fallbackSources(const StrategyResolverSourceContext& context) const
{
    return {
        context.optimizationConfig,
        context.backtestRuntime,
        context.backtestSettings,
        context.strategyView,
        context.appliedRiskConfig
    };
}

QList<AbstractStrategyStructureResolver::AliasGroup> ExecutionPolicyResolver::aliasGroups() const
{
    return {
        {QStringLiteral("positionSizingMethod"), {QStringLiteral("position_sizing_method")}, {}},
        {QStringLiteral("rebalanceDays"), {QStringLiteral("rebalance_days"), QStringLiteral("rebalancingPeriod"), QStringLiteral("rebalanceFrequency")}, {}},
        {QStringLiteral("orderSizeLimit"), {}, {}},
        {QStringLiteral("turnoverLimit"), {}, {}},
        {QStringLiteral("maxBatchOrders"), {QStringLiteral("batchOrderLimit")}, {}},
        {QStringLiteral("maxBatchNotionalWan"), {QStringLiteral("batchNotionalLimitWan")}, {}},
        {QStringLiteral("maxBatchNotional"), {QStringLiteral("batchNotionalLimit")}, {}},
        {QStringLiteral("minWeightPercent"), {QStringLiteral("min_weight_percent")}, {}},
        {QStringLiteral("maxWeightPercent"), {QStringLiteral("max_weight_percent")}, {}},
        {QStringLiteral("maxPositions"), {}, {}}
    };
}

QString BacktestAssumptionsResolver::structureKeyImpl() const
{
    return QStringLiteral("backtest_assumptions");
}

QVariantMap BacktestAssumptionsResolver::readStructuredValues(const StrategyResolverSourceContext& context) const
{
    QVariantMap resolved = readEmbeddedStructure(context.parameters, structureKey());
    mergeConfiguredValues(resolved, readEmbeddedStructure(context.strategy, structureKey()));
    return resolved;
}

QList<QVariantMap> BacktestAssumptionsResolver::fallbackSources(const StrategyResolverSourceContext& context) const
{
    return {
        context.backtestRuntime,
        context.backtestSettings,
        context.optimizationConfig,
        context.strategyView,
        context.appliedRiskConfig
    };
}

QList<AbstractStrategyStructureResolver::AliasGroup> BacktestAssumptionsResolver::aliasGroups() const
{
    return {
        {QStringLiteral("initialCapital"), {}, {}},
        {QStringLiteral("commissionRate"), {QStringLiteral("commission"), QStringLiteral("transactionCost"), QStringLiteral("transaction_cost")}, {}},
        {QStringLiteral("slippageRate"), {QStringLiteral("slippage"), QStringLiteral("slippageCost"), QStringLiteral("slippageLimit")}, {}},
        {QStringLiteral("dataSourceMode"), {}, {}},
        {QStringLiteral("startDate"), {QStringLiteral("start_date")}, {}},
        {QStringLiteral("endDate"), {QStringLiteral("end_date")}, {}},
        {QStringLiteral("benchmark"), {}, {}},
        {QStringLiteral("backtestYears"), {QStringLiteral("backtestPeriod"), QStringLiteral("years")}, {}}
    };
}

QString StrategyScopeContextResolver::structureKeyImpl() const
{
    return QStringLiteral("strategy_scope_context");
}

QVariantMap StrategyScopeContextResolver::readStructuredValues(const StrategyResolverSourceContext& context) const
{
    QVariantMap resolved = readEmbeddedStructure(context.parameters, structureKey());
    mergeConfiguredValues(resolved, readEmbeddedStructure(context.strategy, structureKey()));
    return resolved;
}

QList<QVariantMap> StrategyScopeContextResolver::fallbackSources(const StrategyResolverSourceContext& context) const
{
    return {
        context.backtestRuntime,
        context.optimizationConfig,
        context.strategyView
    };
}

QList<AbstractStrategyStructureResolver::AliasGroup> StrategyScopeContextResolver::aliasGroups() const
{
    return {
        {QStringLiteral("symbol_pool"), {QStringLiteral("symbolPool"), QStringLiteral("linked_stock_pool_symbols"), QStringLiteral("linkedStockPoolSymbols")}, {}},
        {QStringLiteral("universeType"), {}, {}},
        {QStringLiteral("universeId"), {QStringLiteral("indexSymbol")}, {}},
        {QStringLiteral("selectedStrategyType"), {}, {}},
        {QStringLiteral("selectedStrategySubtype"), {}, {}},
        {QStringLiteral("selectedStrategyName"), {QStringLiteral("strategy_name"), QStringLiteral("strategyName")}, {}},
        {QStringLiteral("portfolio_source"), {}, {}},
        {QStringLiteral("portfolio_name"), {}, {}},
        {QStringLiteral("portfolio_allocations_json"), {QStringLiteral("factor_allocations"), QStringLiteral("allocations")}, {}}
    };
}

StrategyStructureResolverSet::StrategyStructureResolverSet()
{
    registerResolver(std::make_unique<RuleProfileResolver>());
    registerResolver(std::make_unique<ExecutionPolicyResolver>());
    registerResolver(std::make_unique<BacktestAssumptionsResolver>());
    registerResolver(std::make_unique<StrategyScopeContextResolver>());
}

void StrategyStructureResolverSet::registerResolver(std::unique_ptr<AbstractStrategyStructureResolver> resolver)
{
    if (!resolver) {
        return;
    }
    m_resolvers.push_back(std::move(resolver));
}

StrategyStructureResolution StrategyStructureResolverSet::resolve(const QVariantMap& strategy,
                                                                 const QVariantMap& appliedRiskConfig) const
{
    const StrategyResolverSourceContext context = buildContext(strategy, appliedRiskConfig);

    StrategyStructureResolution resolution;
    resolution.strategyView = context.strategyView;

    for (const auto& resolver : m_resolvers) {
        const QVariantMap resolvedMap = resolver->resolve(context);
        const QString key = resolver->structureKey();
        if (key == QStringLiteral("rule_profile")) {
            resolution.ruleProfile = resolvedMap;
        } else if (key == QStringLiteral("execution_policy")) {
            resolution.executionPolicy = resolvedMap;
        } else if (key == QStringLiteral("backtest_assumptions")) {
            resolution.backtestAssumptions = resolvedMap;
        } else if (key == QStringLiteral("strategy_scope_context")) {
            resolution.strategyScopeContext = resolvedMap;
        }
    }

    return resolution;
}

StrategyResolverSourceContext StrategyStructureResolverSet::buildContext(const QVariantMap& strategy,
                                                                        const QVariantMap& appliedRiskConfig) const
{
    StrategyResolverSourceContext context;
    context.strategy = strategy;
    context.parameters = variantMapValue(strategy.value(QStringLiteral("parameters")));
    context.strategyView = context.parameters;
    mergeConfiguredValues(context.strategyView, strategy);

    context.advancedOptions = variantMapValue(strategy.value(QStringLiteral("advanced_options")));
    if (context.advancedOptions.isEmpty()) {
        context.advancedOptions = variantMapValue(strategy.value(QStringLiteral("advancedOptions")));
    }

    context.optimizationConfig = variantMapValue(context.advancedOptions.value(QStringLiteral("optimization_config")));
    if (context.optimizationConfig.isEmpty()) {
        context.optimizationConfig = variantMapValue(context.advancedOptions.value(QStringLiteral("optimizationConfig")));
    }

    mergeConfiguredValues(context.backtestRuntime, variantMapValue(context.parameters.value(QStringLiteral("backtest_runtime"))));
    mergeConfiguredValues(context.backtestRuntime, variantMapValue(strategy.value(QStringLiteral("backtest_runtime"))));

    mergeConfiguredValues(context.backtestSettings, variantMapValue(context.parameters.value(QStringLiteral("backtest_settings"))));
    mergeConfiguredValues(context.backtestSettings, variantMapValue(strategy.value(QStringLiteral("backtest_settings"))));

    context.appliedRiskConfig = appliedRiskConfig;
    return context;
}

} // namespace bridge::config