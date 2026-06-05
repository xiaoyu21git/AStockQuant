#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <memory>
#include <vector>

namespace bridge::config {

struct StrategyResolverSourceContext {
    QVariantMap strategy;
    QVariantMap parameters;
    QVariantMap strategyView;
    QVariantMap advancedOptions;
    QVariantMap optimizationConfig;
    QVariantMap backtestRuntime;
    QVariantMap backtestSettings;
    QVariantMap appliedRiskConfig;
};

struct StrategyStructureResolution {
    QVariantMap strategyView;
    QVariantMap ruleProfile;
    QVariantMap executionPolicy;
    QVariantMap backtestAssumptions;
    QVariantMap strategyScopeContext;
};

class AbstractStrategyStructureResolver {
public:
    struct AliasGroup {
        QString canonicalKey;
        QStringList aliases;
        QVariant defaultValue;
    };

    virtual ~AbstractStrategyStructureResolver() = default;

    QString structureKey() const;
    QVariantMap resolve(const StrategyResolverSourceContext& context) const;

protected:
    virtual QString structureKeyImpl() const = 0;
    virtual QVariantMap readStructuredValues(const StrategyResolverSourceContext& context) const = 0;
    virtual QList<QVariantMap> fallbackSources(const StrategyResolverSourceContext& context) const = 0;
    virtual QList<AliasGroup> aliasGroups() const = 0;
    virtual void finalize(QVariantMap& resolved, const StrategyResolverSourceContext& context) const;

    static QVariantMap readEmbeddedStructure(const QVariantMap& container, const QString& key);
    static QVariant firstDefinedValue(const QList<QVariantMap>& sources, const QStringList& keys);
    static bool isConfiguredValue(const QVariant& value);
    static void insertIfConfigured(QVariantMap& target, const QString& key, const QVariant& value);
};

class RuleProfileResolver final : public AbstractStrategyStructureResolver {
protected:
    QString structureKeyImpl() const override;
    QVariantMap readStructuredValues(const StrategyResolverSourceContext& context) const override;
    QList<QVariantMap> fallbackSources(const StrategyResolverSourceContext& context) const override;
    QList<AliasGroup> aliasGroups() const override;
};

class ExecutionPolicyResolver final : public AbstractStrategyStructureResolver {
protected:
    QString structureKeyImpl() const override;
    QVariantMap readStructuredValues(const StrategyResolverSourceContext& context) const override;
    QList<QVariantMap> fallbackSources(const StrategyResolverSourceContext& context) const override;
    QList<AliasGroup> aliasGroups() const override;
};

class BacktestAssumptionsResolver final : public AbstractStrategyStructureResolver {
protected:
    QString structureKeyImpl() const override;
    QVariantMap readStructuredValues(const StrategyResolverSourceContext& context) const override;
    QList<QVariantMap> fallbackSources(const StrategyResolverSourceContext& context) const override;
    QList<AliasGroup> aliasGroups() const override;
};

class StrategyScopeContextResolver final : public AbstractStrategyStructureResolver {
protected:
    QString structureKeyImpl() const override;
    QVariantMap readStructuredValues(const StrategyResolverSourceContext& context) const override;
    QList<QVariantMap> fallbackSources(const StrategyResolverSourceContext& context) const override;
    QList<AliasGroup> aliasGroups() const override;
};

class StrategyStructureResolverSet {
public:
    StrategyStructureResolverSet();

    void registerResolver(std::unique_ptr<AbstractStrategyStructureResolver> resolver);
    StrategyStructureResolution resolve(const QVariantMap& strategy,
                                       const QVariantMap& appliedRiskConfig = QVariantMap()) const;

private:
    StrategyResolverSourceContext buildContext(const QVariantMap& strategy,
                                              const QVariantMap& appliedRiskConfig) const;

    std::vector<std::unique_ptr<AbstractStrategyStructureResolver>> m_resolvers;
};

} // namespace bridge::config