#include "StrategyStructureResolvers.h"
#include "RiskConfigService.h"

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

AbstractStrategyStructureResolver::AliasGroup aliasGroupFromKeys(const QStringList& keys,
                                                                 const QVariant& defaultValue = {})
{
    AbstractStrategyStructureResolver::AliasGroup group;
    if (keys.isEmpty()) {
        return group;
    }

    group.canonicalKey = keys.front();
    group.aliases = keys.mid(1);
    group.defaultValue = defaultValue;
    return group;
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
        aliasGroupFromKeys(risk::config::maxTotalExposureKeys()),
        aliasGroupFromKeys(risk::config::maxPositionPercentKeys()),
        aliasGroupFromKeys(risk::config::maxDrawdownLimitKeys()),
        aliasGroupFromKeys(risk::config::maxDailyLossKeys()),
        aliasGroupFromKeys(risk::config::stopLossPercentKeys()),
        aliasGroupFromKeys(risk::config::takeProfitPercentKeys()),
        aliasGroupFromKeys(risk::config::varWarningPercentKeys()),
        aliasGroupFromKeys(risk::config::level1BreakerKeys()),
        aliasGroupFromKeys(risk::config::level2BreakerKeys()),
        aliasGroupFromKeys(risk::config::level3BreakerKeys()),
        aliasGroupFromKeys(risk::config::autoStopEnabledKeys()),
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
        aliasGroupFromKeys(risk::config::positionSizingMethodKeys()),
        aliasGroupFromKeys(risk::config::rebalanceDaysKeys()),
        aliasGroupFromKeys(risk::config::orderSizeLimitKeys()),
        aliasGroupFromKeys(risk::config::turnoverLimitKeys()),
        {QStringLiteral("maxBatchOrders"), {QStringLiteral("batchOrderLimit")}, {}},
        {QStringLiteral("maxBatchNotionalWan"), {QStringLiteral("batchNotionalLimitWan")}, {}},
        {QStringLiteral("maxBatchNotional"), {QStringLiteral("batchNotionalLimit")}, {}},
        aliasGroupFromKeys(risk::config::minWeightPercentKeys()),
        aliasGroupFromKeys(risk::config::maxWeightPercentKeys()),
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
        aliasGroupFromKeys(risk::config::commissionRateKeys()),
        aliasGroupFromKeys(risk::config::slippageRateKeys()),
        aliasGroupFromKeys(risk::config::forwardDaysKeys()),
        aliasGroupFromKeys(risk::config::riskFreeRateKeys()),
        aliasGroupFromKeys(risk::config::benchmarkSymbolKeys()),
        {QStringLiteral("dataSourceMode"), {}, {}},
        {QStringLiteral("startDate"), {QStringLiteral("start_date")}, {}},
        {QStringLiteral("endDate"), {QStringLiteral("end_date")}, {}},
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

QString FactorOverlayResolver::structureKeyImpl() const
{
    return QStringLiteral("factor_overlay");
}

QVariantMap FactorOverlayResolver::readStructuredValues(const StrategyResolverSourceContext& context) const
{
    QVariantMap resolved = readEmbeddedStructure(context.parameters, structureKey());
    mergeConfiguredValues(resolved, readEmbeddedStructure(context.strategy, structureKey()));
    return resolved;
}

QList<QVariantMap> FactorOverlayResolver::fallbackSources(const StrategyResolverSourceContext& context) const
{
    return {
        context.backtestRuntime,
        context.strategyView,
        context.optimizationConfig
    };
}

QList<AbstractStrategyStructureResolver::AliasGroup> FactorOverlayResolver::aliasGroups() const
{
    return {
        {QStringLiteral("enabled"), {QStringLiteral("factorOverlayEnabled")}, {}},
        {QStringLiteral("targetPositionCount"), {QStringLiteral("target_position_count"), QStringLiteral("top_n"), QStringLiteral("topN")}, {}},
        {QStringLiteral("minimumCompositeScore"), {QStringLiteral("minimum_composite_score"), QStringLiteral("minCompositeScore")}, {}},
        {QStringLiteral("allocations"), {}, {}},
        {QStringLiteral("factorIds"), {QStringLiteral("factor_ids"), QStringLiteral("selectedFactorIds")}, {}},
        {QStringLiteral("combineMode"), {QStringLiteral("combine_mode")}, {QStringLiteral("rank_only")}},
        {QStringLiteral("selectionScope"), {QStringLiteral("selection_scope")}, {QStringLiteral("rule_eligible")}}
    };
}

StrategyStructureResolverSet::StrategyStructureResolverSet()
{
    registerResolver(std::make_unique<RuleProfileResolver>());
    registerResolver(std::make_unique<ExecutionPolicyResolver>());
    registerResolver(std::make_unique<BacktestAssumptionsResolver>());
    registerResolver(std::make_unique<StrategyScopeContextResolver>());
    registerResolver(std::make_unique<FactorOverlayResolver>());
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
        } else if (key == QStringLiteral("factor_overlay")) {
            resolution.factorOverlay = resolvedMap;
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