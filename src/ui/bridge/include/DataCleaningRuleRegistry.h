#pragma once

#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>

#include "DataCleaningEngine.h"

class DataCleaningRuleRegistry {
public:
    struct RuleDescriptor {
        QString id;
        QString displayName;
        QString description;
        bool defaultEnabled{false};
        std::function<QVector<DataCleaningEngine::CleaningRule>(const QVariant&)> builder;
    };

    DataCleaningRuleRegistry();

    QVector<DataCleaningEngine::CleaningRule> buildRules(const QVariantMap& rawRules) const;
    QVariantList availableRules() const;

private:
    static bool isRuleEnabled(const QVariant& rawValue, bool defaultEnabled = false);
    static QVariantMap toRuleMap(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildTimeRangeRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildOutlierRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildMissingValueRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildDataCleaningRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildDuplicateRules(const QVariant& rawValue);
    static bool appendDirectEngineRule(const QVariantMap& ruleMap, QVector<DataCleaningEngine::CleaningRule>* rules);

    QVector<RuleDescriptor> m_descriptors;
};