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
    static QVector<DataCleaningEngine::CleaningRule> buildSurvivorBiasRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildReportDateAlignmentRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildPriceValidityRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildAdjustedPriceRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildNewStockFilterRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildStFilterRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildLimitMoveRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildSuspensionFillRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildWinsorizationRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildMarketCapFilterRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildIndexAlignmentRules(const QVariant& rawValue);
    static QVector<DataCleaningEngine::CleaningRule> buildContinuousSuspensionRules(const QVariant& rawValue);
    static bool appendDirectEngineRule(const QVariantMap& ruleMap, QVector<DataCleaningEngine::CleaningRule>* rules);

    QVector<RuleDescriptor> m_descriptors;
};