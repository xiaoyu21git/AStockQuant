#include "StrategyStructureResolvers.h"
#include "RiskConfigService.h"

#include "../../../domain/backtest/include/ResolvedStrategyBehaviorVariant.h"

#include <QDateTime>
#include <QMetaType>

#include <cmath>
#include <optional>

namespace bridge::config {

namespace {

namespace rawkeys {

constexpr const char* kParameters = "parameters";
constexpr const char* kPerformanceMetrics = "performance_metrics";
constexpr const char* kAdvancedOptions = "advanced_options";
constexpr const char* kOptimizationConfig = "optimization_config";
constexpr const char* kStages = "stages";
constexpr const char* kGroups = "groups";
constexpr const char* kRules = "rules";

} // namespace rawkeys

namespace structurekeys {

constexpr const char* kRuleProfile = "rule_profile";
constexpr const char* kExecutionPolicy = "execution_policy";
constexpr const char* kStrategyScopeContext = "strategy_scope_context";
constexpr const char* kFactorOverlay = "factor_overlay";

} // namespace structurekeys

QString keyText(const char* key)
{
    return QString::fromLatin1(key);
}

QVariant rawMapValue(const QVariantMap& map, const char* key)
{
    return map.value(keyText(key));
}

QVariant rawMapValue(const QVariantMap& map, const QString& key)
{
    return map.value(key);
}

QVariantMap variantMapValue(const QVariant& value)
{
    return value.canConvert<QVariantMap>() ? value.toMap() : QVariantMap{};
}

QVariantList variantListValue(const QVariant& value)
{
    return value.canConvert<QVariantList>() ? value.toList() : QVariantList{};
}

QVariantMap rawNestedMap(const QVariantMap& map, const char* key)
{
    return variantMapValue(rawMapValue(map, key));
}

QVariantList rawNestedList(const QVariantMap& map, const char* key)
{
    return variantListValue(rawMapValue(map, key));
}

domain::strategy::Ratio normalizedPercentRatio(double value)
{
    if (value > 1.0 && value <= 100.0) {
        value /= 100.0;
    }
    return domain::strategy::Ratio{value};
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

QVariantMap firstConfiguredMap(const QVariantMap& container,
                              std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QVariantMap map = variantMapValue(rawMapValue(container, key));
        if (!map.isEmpty()) {
            return map;
        }
    }
    return {};
}

void mergeConfiguredEmbeddedMap(QVariantMap& target,
                               const QVariantMap& container,
                               const char* key)
{
    mergeConfiguredValues(target, variantMapValue(rawMapValue(container, key)));
}

void assignResolvedStructure(StrategyStructureResolution& resolution,
                             const QString& key,
                             const QVariantMap& resolvedMap)
{
    if (key == keyText(structurekeys::kRuleProfile)) {
        resolution.ruleProfile = resolvedMap;
        return;
    }
    if (key == keyText(structurekeys::kExecutionPolicy)) {
        resolution.executionPolicy = resolvedMap;
        return;
    }
    if (key == keyText(structurekeys::kStrategyScopeContext)) {
        resolution.strategyScopeContext = resolvedMap;
        return;
    }
    if (key == keyText(structurekeys::kFactorOverlay)) {
        resolution.factorOverlay = resolvedMap;
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

class VariantSourceReader final {
public:
    [[nodiscard]] QVariant firstConfiguredValue(const QList<QVariantMap>& sources,
                                                std::initializer_list<const char*> keys) const
    {
        for (const QVariantMap& source : sources) {
            for (const char* key : keys) {
                const QVariant value = rawMapValue(source, key);
                if (!isConfiguredVariant(value)) {
                    continue;
                }
                if (value.canConvert<QVariantMap>() && value.toMap().isEmpty()) {
                    continue;
                }
                if (value.canConvert<QVariantList>() && value.toList().isEmpty()) {
                    continue;
                }
                return value;
            }
        }

        return {};
    }

    [[nodiscard]] QString configuredText(const QList<QVariantMap>& sources,
                                         std::initializer_list<const char*> keys) const
    {
        return firstConfiguredValue(sources, keys).toString().trimmed();
    }

    [[nodiscard]] double configuredDouble(const QList<QVariantMap>& sources,
                                          std::initializer_list<const char*> keys,
                                          double fallback = 0.0) const
    {
        const QVariant value = firstConfiguredValue(sources, keys);
        if (!value.isValid()) {
            return fallback;
        }

        bool ok = false;
        const double parsedValue = value.toDouble(&ok);
        return ok ? parsedValue : fallback;
    }

    [[nodiscard]] bool configuredBool(const QList<QVariantMap>& sources,
                                      std::initializer_list<const char*> keys,
                                      bool fallback = false) const
    {
        const QVariant value = firstConfiguredValue(sources, keys);
        return value.isValid() ? value.toBool() : fallback;
    }

    [[nodiscard]] domain::strategy::Ratio configuredRatio(const QList<QVariantMap>& sources,
                                                          std::initializer_list<const char*> keys) const
    {
        double value = configuredDouble(sources, keys);
        if (value > 1.0 && value <= 100.0) {
            value /= 100.0;
        }
        return domain::strategy::Ratio{value};
    }

    [[nodiscard]] domain::strategy::Money configuredMoney(const QList<QVariantMap>& sources,
                                                          std::initializer_list<const char*> keys) const
    {
        return domain::strategy::Money{configuredDouble(sources, keys)};
    }

    [[nodiscard]] int configuredInteger(const QList<QVariantMap>& sources,
                                        std::initializer_list<const char*> keys,
                                        int fallback = 0) const
    {
        const QVariant value = firstConfiguredValue(sources, keys);
        if (!value.isValid()) {
            return fallback;
        }

        bool ok = false;
        const int integerValue = value.toInt(&ok);
        return ok ? integerValue : fallback;
    }

    [[nodiscard]] QDateTime configuredDateTime(const QList<QVariantMap>& sources,
                                               std::initializer_list<const char*> keys) const
    {
        const QVariant value = firstConfiguredValue(sources, keys);
        if (!value.isValid()) {
            return {};
        }

        const QDateTime dateTime = value.toDateTime();
        if (dateTime.isValid()) {
            return dateTime;
        }

        return QDateTime::fromString(value.toString().trimmed(), Qt::ISODate);
    }

    [[nodiscard]] domain::strategy::StrategyExecutionKind configuredExecutionKind(
        const QList<QVariantMap>& sources,
        domain::backtest::StrategyStoredType storedType) const
    {
        const QVariant value = firstConfiguredValue(sources, {"executionKind"});
        bool ok = false;
        const int index = value.toInt(&ok);
        if (ok) {
            switch (static_cast<domain::strategy::StrategyExecutionKind>(index)) {
            case domain::strategy::StrategyExecutionKind::Standard:
            case domain::strategy::StrategyExecutionKind::FactorWeightedPortfolio:
                return static_cast<domain::strategy::StrategyExecutionKind>(index);
            }
        }

        return storedType == domain::backtest::StrategyStoredType::Portfolio
            ? domain::strategy::StrategyExecutionKind::FactorWeightedPortfolio
            : domain::strategy::StrategyExecutionKind::Standard;
    }

    [[nodiscard]] domain::strategy::StrategyLanguage configuredLanguage(const QList<QVariantMap>& sources) const
    {
        const int index = configuredInteger(sources, {"language"});
        switch (static_cast<domain::strategy::StrategyLanguage>(index)) {
        case domain::strategy::StrategyLanguage::Python:
        case domain::strategy::StrategyLanguage::Cpp:
        case domain::strategy::StrategyLanguage::Julia:
        case domain::strategy::StrategyLanguage::R:
            return static_cast<domain::strategy::StrategyLanguage>(index);
        }

        return domain::strategy::StrategyLanguage::Python;
    }

    [[nodiscard]] domain::strategy::PositionSizingMethod configuredPositionSizingMethod(
        const QList<QVariantMap>& sources) const
    {
        const int index = configuredInteger(sources, {"positionSizingMethod"});
        switch (static_cast<domain::strategy::PositionSizingMethod>(index)) {
        case domain::strategy::PositionSizingMethod::FixedFraction:
        case domain::strategy::PositionSizingMethod::EqualWeight:
        case domain::strategy::PositionSizingMethod::SpreadNeutral:
        case domain::strategy::PositionSizingMethod::Discretionary:
            return static_cast<domain::strategy::PositionSizingMethod>(index);
        }

        return domain::strategy::PositionSizingMethod::FixedFraction;
    }

    [[nodiscard]] domain::strategy::UniverseType configuredUniverseType(const QList<QVariantMap>& sources) const
    {
        const int index = configuredInteger(sources, {"universeType"});
        switch (static_cast<domain::strategy::UniverseType>(index)) {
        case domain::strategy::UniverseType::Equity:
        case domain::strategy::UniverseType::Index:
        case domain::strategy::UniverseType::Basket:
            return static_cast<domain::strategy::UniverseType>(index);
        }

        return domain::strategy::UniverseType::Equity;
    }

    [[nodiscard]] domain::strategy::UniverseMode configuredUniverseMode(
        const QList<QVariantMap>& sources,
        const domain::strategy::UniverseSourceId& sourceId,
        const QVector<domain::strategy::SymbolCode>& explicitSymbols,
        domain::strategy::UniverseType universeType) const
    {
        const int index = configuredInteger(sources, {"universeMode"}, -1);
        switch (static_cast<domain::strategy::UniverseMode>(index)) {
        case domain::strategy::UniverseMode::ExplicitSymbols:
        case domain::strategy::UniverseMode::SavedUniverse:
        case domain::strategy::UniverseMode::LinkedWatchlist:
        case domain::strategy::UniverseMode::IndexConstituents:
            return static_cast<domain::strategy::UniverseMode>(index);
        default:
            break;
        }

        if (!explicitSymbols.isEmpty()) {
            return domain::strategy::UniverseMode::ExplicitSymbols;
        }
        if (sourceId.isValid() && universeType == domain::strategy::UniverseType::Index) {
            return domain::strategy::UniverseMode::IndexConstituents;
        }
        if (sourceId.isValid()) {
            return domain::strategy::UniverseMode::SavedUniverse;
        }
        return domain::strategy::UniverseMode::ExplicitSymbols;
    }

    [[nodiscard]] strategy_view::StrategyLifecycleStatus configuredLifecycleStatus(
        const QList<QVariantMap>& sources) const
    {
        const QVariant indexValue = firstConfiguredValue(sources, {"statusIndex"});
        const strategy_view::StrategyLifecycleStatus status =
            strategy_view::resolveStrategyLifecycleStatus(indexValue);
        if (strategy_view::isKnownStrategyLifecycleStatus(status)) {
            return status;
        }

        return strategy_view::StrategyLifecycleStatus::Unknown;
    }

    [[nodiscard]] QVector<domain::strategy::StrategyTag> readTags(const QList<QVariantMap>& sources) const
    {
        const QVariantList items = variantListValue(firstConfiguredValue(sources, {"tags"}));
        QVector<domain::strategy::StrategyTag> tags;
        tags.reserve(items.size());
        for (const QVariant& item : items) {
            const QString value = item.toString().trimmed();
            if (!value.isEmpty()) {
                tags.push_back(domain::strategy::StrategyTag(value));
            }
        }
        return tags;
    }

    [[nodiscard]] QVector<domain::strategy::SymbolCode> readSymbolCodes(
        const QVariantMap& source,
        std::initializer_list<const char*> keys) const
    {
        const QVariantList items = variantListValue(firstConfiguredValue({source}, keys));
        QVector<domain::strategy::SymbolCode> symbols;
        symbols.reserve(items.size());
        for (const QVariant& item : items) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty()) {
                symbols.push_back(domain::strategy::SymbolCode(text));
            }
        }
        return symbols;
    }

    [[nodiscard]] QVector<domain::strategy::FactorId> readFactorIds(const QVariant& value) const
    {
        const QVariantList items = variantListValue(value);
        QVector<domain::strategy::FactorId> factorIds;
        factorIds.reserve(items.size());
        for (const QVariant& item : items) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty()) {
                factorIds.push_back(domain::strategy::FactorId(text));
            }
        }
        return factorIds;
    }

    [[nodiscard]] QVector<domain::strategy::FactorOverlayAllocation> readFactorOverlayAllocations(
        const QVariant& value) const
    {
        const QVariantList rawAllocations = variantListValue(value);
        QVector<domain::strategy::FactorOverlayAllocation> allocations;
        allocations.reserve(rawAllocations.size());
        for (const QVariant& allocationValue : rawAllocations) {
            const QVariantMap allocationMap = variantMapValue(allocationValue);
            if (allocationMap.isEmpty()) {
                continue;
            }

            const QString factorIdText = firstConfiguredValue({allocationMap}, {"factorId"}).toString().trimmed();
            if (factorIdText.isEmpty()) {
                continue;
            }

            bool ok = false;
            const double weightPercent = firstConfiguredValue(
                {allocationMap}, {"weightPercent", "weight"}).toDouble(&ok);
            if (!ok || !std::isfinite(weightPercent) || weightPercent <= 0.0) {
                continue;
            }

            allocations.append(domain::strategy::FactorOverlayAllocation{domain::strategy::FactorId(factorIdText), weightPercent});
        }
        return allocations;
    }

};

class StrategyRuleStateAssembler final {
public:
    explicit StrategyRuleStateAssembler(const VariantSourceReader& reader)
        : reader_(reader)
    {
    }

    [[nodiscard]] domain::strategy::RuleGroupOperator configuredGroupOperator(const QVariantMap& map) const
    {
        bool ok = false;
        const int index = reader_.firstConfiguredValue({map}, {"groupOperator"}).toInt(&ok);
        if (ok) {
            switch (static_cast<domain::strategy::RuleGroupOperator>(index)) {
            case domain::strategy::RuleGroupOperator::All:
            case domain::strategy::RuleGroupOperator::Any:
            case domain::strategy::RuleGroupOperator::MinimumMatch:
            case domain::strategy::RuleGroupOperator::FirstMatch:
            case domain::strategy::RuleGroupOperator::ScoreSum:
                return static_cast<domain::strategy::RuleGroupOperator>(index);
            }
        }

        return domain::strategy::RuleGroupOperator::All;
    }

    [[nodiscard]] domain::strategy::RuleGroupRole configuredGroupRole(const QVariantMap& map) const
    {
        bool ok = false;
        const int index = reader_.firstConfiguredValue({map}, {"groupRole"}).toInt(&ok);
        if (ok) {
            switch (static_cast<domain::strategy::RuleGroupRole>(index)) {
            case domain::strategy::RuleGroupRole::Unspecified:
            case domain::strategy::RuleGroupRole::MustPass:
            case domain::strategy::RuleGroupRole::AnyPass:
            case domain::strategy::RuleGroupRole::Trigger:
            case domain::strategy::RuleGroupRole::ScoreBoost:
            case domain::strategy::RuleGroupRole::EntryGuard:
            case domain::strategy::RuleGroupRole::ExitGuard:
            case domain::strategy::RuleGroupRole::PositionManagement:
                return static_cast<domain::strategy::RuleGroupRole>(index);
            }
        }

        return domain::strategy::RuleGroupRole::Unspecified;
    }

    [[nodiscard]] QVector<domain::strategy::RuleTemplateBinding> readRuleTemplateBindings(
        const domain::strategy::RuleComposerState& composerState) const
    {
        QVector<domain::strategy::RuleTemplateBinding> bindings;
        for (const domain::strategy::RuleComposerStage& stage : composerState.stages) {
            for (const domain::strategy::RuleComposerGroup& group : stage.groups) {
                for (const domain::strategy::RuleComposerRule& rule : group.rules) {
                    if (rule.binding.isValid()) {
                        bindings.push_back(rule.binding);
                    }
                }
            }
        }
        return bindings;
    }

    [[nodiscard]] domain::strategy::RuleComposerState readRuleComposerState(const QVariantMap& source) const
    {
        domain::strategy::RuleComposerState state;
        const QVariantMap composerStateMap = variantMapValue(
            reader_.firstConfiguredValue({source}, {"ruleComposerState"}));
        const QVariantList stageValues = rawNestedList(composerStateMap, rawkeys::kStages);
        state.stages.reserve(stageValues.size());
        for (const QVariant& stageValue : stageValues) {
            const QVariantMap stageMap = variantMapValue(stageValue);
            if (stageMap.isEmpty()) {
                continue;
            }

            const std::optional<domain::strategy::RuleBindingPhase> stagePhase =
                configuredComposerPhase(stageMap);
            if (!stagePhase.has_value()) {
                continue;
            }

            domain::strategy::RuleComposerStage stage;
            stage.phase = *stagePhase;

            const QVariantList groupValues = rawNestedList(stageMap, rawkeys::kGroups);
            stage.groups.reserve(groupValues.size());
            for (const QVariant& groupValue : groupValues) {
                const QVariantMap groupMap = variantMapValue(groupValue);
                if (groupMap.isEmpty()) {
                    continue;
                }

                domain::strategy::RuleComposerGroup group;
                group.groupId = domain::strategy::GroupId(reader_.configuredText({groupMap}, {"groupId"}));
                group.title = domain::strategy::GroupTitle(reader_.configuredText({groupMap}, {"title"}));
                group.groupRole = configuredGroupRole(groupMap);
                group.groupOperator = configuredGroupOperator(groupMap);
                group.minimumMatchCount = reader_.configuredInteger(
                    {groupMap}, {"minimumMatchCount"});

                const QVariantList ruleValues = rawNestedList(groupMap, rawkeys::kRules);
                group.rules.reserve(ruleValues.size());
                for (const QVariant& ruleValue : ruleValues) {
                    const QVariantMap ruleMap = variantMapValue(ruleValue);
                    if (ruleMap.isEmpty()) {
                        continue;
                    }

                    domain::strategy::RuleComposerRule rule;
                    rule.binding.phase = stage.phase;
                    rule.binding.templateId = domain::strategy::RuleTemplateId(
                        reader_.configuredText({ruleMap}, {"templateId"}));
                    rule.binding.filePath = domain::strategy::FilePathToken(
                        reader_.configuredText({ruleMap}, {"filePath"}));
                    rule.binding.namespaceId = domain::strategy::NamespaceId(
                        reader_.configuredText({ruleMap}, {"namespaceId"}));
                    rule.binding.groupId = group.groupId;
                    rule.binding.groupTitle = group.title;
                    rule.binding.groupRole = group.groupRole;
                    rule.binding.groupOperator = group.groupOperator;
                    rule.binding.groupMinMatchCount = group.minimumMatchCount;
                    if (rule.isValid()) {
                        group.rules.push_back(rule);
                    }
                }

                if (group.isValid()) {
                    stage.groups.push_back(group);
                }
            }

            if (stage.isValid()) {
                state.stages.push_back(stage);
            }
        }
        return state;
    }

private:
    [[nodiscard]] std::optional<domain::strategy::RuleBindingPhase> configuredComposerPhase(
        const QVariantMap& map) const
    {
        bool ok = false;
        const int index = reader_.firstConfiguredValue({map}, {"phase"}).toInt(&ok);
        if (!ok) {
            return std::nullopt;
        }

        switch (static_cast<domain::strategy::RuleBindingPhase>(index)) {
        case domain::strategy::RuleBindingPhase::Market:
        case domain::strategy::RuleBindingPhase::Signal:
        case domain::strategy::RuleBindingPhase::Entry:
        case domain::strategy::RuleBindingPhase::Rebalance:
        case domain::strategy::RuleBindingPhase::Exit:
        case domain::strategy::RuleBindingPhase::Risk:
        case domain::strategy::RuleBindingPhase::Watch:
            return static_cast<domain::strategy::RuleBindingPhase>(index);
        }

        return std::nullopt;
    }

    const VariantSourceReader& reader_;
};

class StrategyAggregateAssembler final {
public:
    StrategyAggregateAssembler()
        : ruleStateAssembler_(reader_)
    {
    }

    [[nodiscard]] domain::strategy::StrategyAggregate assemble(const QVariantMap& rawStrategy,
                                                               const QVariantMap& appliedRiskConfig) const
    {
        domain::strategy::StrategyAggregate aggregate;
        const QVariantMap parameters = rawNestedMap(rawStrategy, rawkeys::kParameters);
        const QList<QVariantMap> sources{rawStrategy, parameters};
        const StrategyStructureResolverSet resolverSet;
        const StrategyStructureResolution resolution = resolverSet.resolve(rawStrategy, appliedRiskConfig);
        const domain::backtest::ResolvedStrategyIdentity resolvedIdentity =
            domain::backtest::resolveStrategyIdentity(rawStrategy);

        aggregate.identity.strategyId = domain::strategy::StrategyId(
            reader_.configuredText(sources, {"strategyId"}));
        aggregate.identity.strategyCode = domain::strategy::StrategyCode(
            reader_.configuredText(sources, {"strategyCode"}));
        aggregate.identity.strategyName = domain::strategy::StrategyName(
            reader_.configuredText(sources, {"strategyName"}));
        aggregate.identity.storedType = resolvedIdentity.storedType;
        aggregate.identity.behaviorKind = resolvedIdentity.behavior.valid
            ? resolvedIdentity.behavior.kind
            : domain::backtest::StrategyBehaviorKind::Custom;
        aggregate.identity.executionKind = reader_.configuredExecutionKind(sources, resolvedIdentity.storedType);

        aggregate.metadata.description = domain::strategy::DescriptionText(
            reader_.configuredText(sources, {"description"}));
        aggregate.metadata.version = domain::strategy::VersionText(reader_.configuredText(sources, {"version"}));
        aggregate.metadata.author = domain::strategy::AuthorName(reader_.configuredText(sources, {"author"}));
        aggregate.metadata.language = reader_.configuredLanguage(sources);
        aggregate.metadata.tags = reader_.readTags(sources);
        aggregate.metadata.createdAt = reader_.configuredDateTime(sources, {"createdAt"});
        aggregate.metadata.updatedAt = reader_.configuredDateTime(sources, {"updatedAt"});

        aggregate.lifecycle.status = reader_.configuredLifecycleStatus(sources);
        aggregate.runtime.assetTypeIndex = reader_.configuredInteger(sources, {"assetTypeIndex"});
        aggregate.runtime.timeFrameIndex = reader_.configuredInteger(sources, {"timeFrameIndex"});
        aggregate.runtime.riskLevelIndex = reader_.configuredInteger(sources, {"riskLevelIndex"});

        aggregate.spec.ruleProfile = buildRuleProfile(resolution.ruleProfile);
        aggregate.spec.executionPolicy = buildExecutionPolicy(resolution.executionPolicy);
        aggregate.spec.strategyScopeContext = buildScopeContext(resolution.strategyScopeContext,
                                                                aggregate.runtime);

        aggregate.spec.factorOverlay = buildFactorOverlay(resolution.factorOverlay);
        aggregate.spec.ruleComposerState = ruleStateAssembler_.readRuleComposerState(resolution.ruleProfile);
        aggregate.spec.ruleTemplateBindings = ruleStateAssembler_.readRuleTemplateBindings(aggregate.spec.ruleComposerState);

        const QVariantMap performanceSummaryData = rawNestedMap(rawStrategy, rawkeys::kPerformanceMetrics);
        aggregate.performanceSummary.lastRecordedAt = reader_.configuredDateTime(
            {performanceSummaryData, rawStrategy},
            {"recordedAt"});
        aggregate.performanceSummary.latestMetrics = buildPerformanceSummary(performanceSummaryData);
        return aggregate;
    }

private:
    [[nodiscard]] domain::strategy::PerformanceSummaryMetrics buildPerformanceSummary(const QVariantMap& source) const
    {
        const QList<QVariantMap> sources{source};
        domain::strategy::PerformanceSummaryMetrics metrics;
        metrics.totalReturn = reader_.configuredDouble(sources, {"totalReturn"});
        metrics.annualizedReturn = reader_.configuredDouble(sources, {"annualizedReturn"});
        metrics.volatility = reader_.configuredDouble(sources, {"volatility"});
        metrics.sharpeRatio = reader_.configuredDouble(sources, {"sharpeRatio"});
        metrics.sortinoRatio = reader_.configuredDouble(sources, {"sortinoRatio"});
        metrics.calmarRatio = reader_.configuredDouble(sources, {"calmarRatio"});
        metrics.maxDrawdown = reader_.configuredDouble(sources, {"maxDrawdown"});
        metrics.winRate = reader_.configuredDouble(sources, {"winRate"});
        metrics.profitFactor = reader_.configuredDouble(sources, {"profitFactor"});
        metrics.averageWin = reader_.configuredDouble(sources, {"averageWin"});
        metrics.averageLoss = reader_.configuredDouble(sources, {"averageLoss"});
        metrics.alpha = reader_.configuredDouble(sources, {"alpha"});
        metrics.beta = reader_.configuredDouble(sources, {"beta"});
        metrics.informationRatio = reader_.configuredDouble(sources, {"informationRatio"});
        metrics.trackingError = reader_.configuredDouble(sources, {"trackingError"});
        return metrics;
    }

    [[nodiscard]] domain::strategy::RuleProfileSnapshot buildRuleProfile(const QVariantMap& source) const
    {
        domain::strategy::RuleProfileSnapshot snapshot;
        snapshot.maxPositionRatio = normalizedPercentRatio(risk::config::maxPositionPercent(source, 0.0));
        snapshot.maxTotalExposureRatio = normalizedPercentRatio(risk::config::maxTotalExposure(source, 0.0));
        snapshot.stopLossRatio = normalizedPercentRatio(risk::config::stopLossPercent(source, 0.0));
        snapshot.takeProfitRatio = normalizedPercentRatio(risk::config::takeProfitPercent(source, 0.0));
        snapshot.rebalanceDays = risk::config::rebalanceDays(source, 0);
        return snapshot;
    }

    [[nodiscard]] domain::strategy::ExecutionPolicySnapshot buildExecutionPolicy(const QVariantMap& source) const
    {
        const QList<QVariantMap> sources{source};
        domain::strategy::ExecutionPolicySnapshot snapshot;
        snapshot.positionSizingMethod = reader_.configuredPositionSizingMethod(sources);
        snapshot.rebalanceFrequencyDays = domain::strategy::RebalanceFrequencyDays{
            reader_.configuredInteger(sources, {"rebalanceFrequencyDays"}, 1)};
        snapshot.defaultOrderType = reader_.configuredBool(sources, {"useMarketOnClose"}, true)
            ? domain::strategy::DefaultOrderType::MarketOnClose
            : domain::strategy::DefaultOrderType::Market;
        snapshot.shortSellingMode = reader_.configuredBool(sources, {"enableShortSelling"}, false)
            ? domain::strategy::ShortSellingMode::Enabled
            : domain::strategy::ShortSellingMode::Disabled;
        snapshot.batchExecution.maxBatchOrders = reader_.configuredInteger(sources, {"maxBatchOrders"});
        snapshot.batchExecution.maxBatchNotional = reader_.configuredMoney(sources, {"maxBatchNotional", "maxBatchNotionalWan"});
        snapshot.batchExecution.waitPreviousBatchFilled = reader_.configuredBool(sources, {"waitPreviousBatchFilled"}, true);
        snapshot.batchExecution.pauseOnConflict = reader_.configuredBool(sources, {"pauseOnConflict"}, false);
        snapshot.batchExecution.pauseOnAbnormalReject = reader_.configuredBool(sources, {"pauseOnAbnormalReject"}, false);
        snapshot.batchExecution.requireManualCheckpoint = reader_.configuredBool(sources, {"requireManualCheckpoint"}, false);
        snapshot.batchExecution.manualCheckpointBatchIndex = reader_.configuredInteger(sources, {"manualCheckpointBatchIndex"});
        return snapshot;
    }

    [[nodiscard]] domain::strategy::StrategyScopeContextSnapshot buildScopeContext(
        const QVariantMap& source,
        const domain::strategy::StrategyRuntimeProfile& runtime) const
    {
        const QList<QVariantMap> sources{source};
        domain::strategy::StrategyScopeContextSnapshot snapshot;
        snapshot.marketEnvironmentProfile = factor::marketEnvironmentProfileFromIndex(
            risk::config::marketEnvironmentProfile(
                source,
                factor::marketEnvironmentProfileIndex(factor::MarketEnvironmentProfile::GENERIC_EQUITY)));

        domain::strategy::UniverseSpec universe;
        universe.universeType = reader_.configuredUniverseType(sources);
        universe.sourceId = domain::strategy::UniverseSourceId(
            reader_.firstConfiguredValue(sources, {"universeId", "indexSymbol", "sourceId"}).toString().trimmed());

        const QVector<domain::strategy::SymbolCode> explicitSymbols =
            reader_.readSymbolCodes(source, {"explicitSymbols", "selectedSymbols"});
        universe.explicitSymbols = explicitSymbols;
        universe.resolvedSymbols = explicitSymbols;
        universe.universeMode = reader_.configuredUniverseMode(
            sources,
            universe.sourceId,
            explicitSymbols,
            universe.universeType);

        snapshot.universe = universe;
        snapshot.selectedStrategyName = domain::strategy::StrategyName(
            reader_.firstConfiguredValue(sources, {"selectedStrategyName", "strategyName"}).toString().trimmed());
        snapshot.executionTimeFrameIndex = reader_.configuredInteger(
            sources,
            {"executionTimeFrameIndex", "timeFrameIndex"},
            runtime.timeFrameIndex);
        return snapshot;
    }

    [[nodiscard]] domain::strategy::FactorOverlaySpec buildFactorOverlay(const QVariantMap& source) const
    {
        const QList<QVariantMap> sources{source};
        domain::strategy::FactorOverlaySpec overlay;
        overlay.enabled = reader_.configuredBool(sources, {"enabled"}, false);
        overlay.targetPositionCount = reader_.configuredInteger(sources, {"targetPositionCount"}, 10);
        if (overlay.targetPositionCount <= 0) {
            overlay.targetPositionCount = 10;
        }
        overlay.minimumCompositeScore = reader_.configuredDouble(sources, {"minimumCompositeScore"}, 0.0);
        overlay.allocations = reader_.readFactorOverlayAllocations(reader_.firstConfiguredValue(sources, {"allocations"}));
        overlay.selectedFactors = reader_.readFactorIds(reader_.firstConfiguredValue(sources, {"factorIds"}));
        if (overlay.selectedFactors.isEmpty()) {
            overlay.selectedFactors.reserve(overlay.allocations.size());
            for (const domain::strategy::FactorOverlayAllocation& allocation : overlay.allocations) {
                if (allocation.factorId.isValid()) {
                    overlay.selectedFactors.append(allocation.factorId);
                }
            }
        }
        return overlay;
    }

    VariantSourceReader reader_;
    StrategyRuleStateAssembler ruleStateAssembler_;
};

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
    return variantMapValue(rawMapValue(container, key));
}

QVariant AbstractStrategyStructureResolver::firstDefinedValue(const QList<QVariantMap>& sources, const QStringList& keys)
{
    for (const QVariantMap& source : sources) {
        for (const QString& key : keys) {
            if (!source.contains(key)) {
                continue;
            }

            const QVariant value = rawMapValue(source, key);
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

StrategyStructureResolverSet::StrategyStructureResolverSet() = default;

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
        assignResolvedStructure(resolution, resolver->structureKey(), resolvedMap);
    }

    return resolution;
}

StrategyResolverSourceContext StrategyStructureResolverSet::buildContext(const QVariantMap& strategy,
                                                                        const QVariantMap& appliedRiskConfig) const
{
    StrategyResolverSourceContext context;
    context.strategy = strategy;
    context.parameters = variantMapValue(rawMapValue(strategy, rawkeys::kParameters));
    context.strategyView = context.parameters;
    mergeConfiguredValues(context.strategyView, strategy);

    context.advancedOptions = firstConfiguredMap(strategy,
        {rawkeys::kAdvancedOptions});

    context.optimizationConfig = firstConfiguredMap(
        context.advancedOptions,
        {rawkeys::kOptimizationConfig});

    context.appliedRiskConfig = appliedRiskConfig;
    return context;
}

domain::strategy::StrategyAggregate buildStrategyAggregate(const QVariantMap& rawStrategy,
                                                           const QVariantMap& appliedRiskConfig)
{
    return StrategyAggregateAssembler().assemble(rawStrategy, appliedRiskConfig);
}

} // namespace bridge::config