#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include "DataFetchFieldContractUtils.h"

#include "../../../domain/factor/include/CustomExpressionUtils.h"
#include "../../../domain/factor/include/FactorInstanceManager.h"
#include "../../../domain/factor/include/FactorTypeUtils.h"

namespace factor::bridge {

inline QString normalizeRequirementFieldName(const QString& rawField)
{
    const QString field = rawField.trimmed().toLower();
    if (field.isEmpty()) {
        return {};
    }
    if (field == QStringLiteral("revenue_growth")) {
        return QString(FinancialFieldKeys::TOTAL_REVENUE);
    }
    return field;
}

inline bool requirementFieldSatisfiedByAvailableFields(const QString& rawField,
                                                       const QSet<QString>& availableFields)
{
    const QString field = normalizeRequirementFieldName(rawField);
    if (field.isEmpty()) {
        return false;
    }
    return availableFields.contains(field);
}

inline QStringList requirementDiagnosticFields(const QString& rawField,
                                              const QSet<QString>& availableFields)
{
    const QString field = normalizeRequirementFieldName(rawField);
    if (field.isEmpty()) {
        return {};
    }
    if (availableFields.contains(field)) {
        return {field};
    }
    return {field};
}

inline QVariantList normalizeRequirementList(const QVariant& rawValue)
{
    QVariantList normalized;
    const QVariantList values = rawValue.toList();
    if (!values.isEmpty()) {
        for (const QVariant& value : values) {
            const QString field = normalizeRequirementFieldName(value.toString());
            if (!field.isEmpty() && !normalized.contains(field)) {
                normalized.append(field);
            }
        }
        return normalized;
    }

    const QStringList parts = rawValue.toStringList();
    for (const QString& part : parts) {
        const QString field = normalizeRequirementFieldName(part);
        if (!field.isEmpty() && !normalized.contains(field)) {
            normalized.append(field);
        }
    }
    return normalized;
}

inline QStringList normalizeRequirementFieldNames(const QVariantList& rawFields)
{
    QStringList normalizedFields;
    const QVariantList normalizedValues = normalizeRequirementList(rawFields);
    normalizedFields.reserve(normalizedValues.size());
    for (const QVariant& value : normalizedValues) {
        const QString field = value.toString().trimmed();
        if (!field.isEmpty() && !normalizedFields.contains(field)) {
            normalizedFields.append(field);
        }
    }
    return normalizedFields;
}

inline QStringList normalizeRequirementFieldNames(const QStringList& rawFields)
{
    QVariantList values;
    values.reserve(rawFields.size());
    for (const QString& field : rawFields) {
        values.append(field);
    }
    return normalizeRequirementFieldNames(values);
}

inline QString requirementFieldName(const FieldKey& field)
{
    return QString(field);
}

inline void appendUniqueRequirementField(QVariantList& fields, const FieldKey& field)
{
    const QString value = requirementFieldName(field);
    if (!fields.contains(value)) {
        fields.append(value);
    }
}

inline void appendUniqueOptionalField(QVariantList& fields, const FieldKey& field)
{
    appendUniqueRequirementField(fields, field);
}

struct FactorRequirementProfile
{
    QString factorType;
    QString metric;
    QVariantList requiredFields;
    QVariantList optionalFields;
    QString sourceTable;
    bool supported{false};
    bool allowEmptyRequiredFields{false};
};

struct SupportMapRequirementResolution
{
    QStringList requiredFields;
    QString explicitSourceTable;
    QString failureCategory;
    QString failureReason;
    bool allowEmptyRequiredFields{false};
};

inline QString resolveSentimentSourceTable(const QVariantMap& calculation);

inline QString inferRequirementSourceTable(const QVariantList& requiredFields);

inline QString normalizeRequirementSourceTable(const QString& rawSourceTable);

inline QString normalizeConfigurableFactorType(const QString& rawType);

inline QString jsonScalarRequirementValue(const foundation::json::JsonFacade& object,
                                         std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        if (!object.has(key)) {
            continue;
        }
        const auto value = object.get(key);
        if (value.isString()) {
            const QString text = QString::fromStdString(value.asString()).trimmed();
            if (!text.isEmpty()) {
                return text;
            }
            continue;
        }
        if (value.isNumber()) {
            const double numericValue = value.asDouble();
            if (std::isfinite(numericValue)) {
                return QString::number(numericValue, 'g', 16).trimmed();
            }
            continue;
        }
        if (value.isBool()) {
            return value.asBool() ? QStringLiteral("true") : QStringLiteral("false");
        }
    }
    return {};
}

inline QVariantList jsonStringListRequirementValue(const foundation::json::JsonFacade& object,
                                                   std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        if (!object.has(key)) {
            continue;
        }

        const auto value = object.get(key);
        if (value.isArray()) {
            QVariantList result;
            for (size_t index = 0; index < value.size(); ++index) {
                const auto itemValue = value.at(index);
                QString item;
                if (itemValue.isString()) {
                    item = QString::fromStdString(itemValue.asString()).trimmed();
                } else if (itemValue.isNumber()) {
                    item = QString::number(itemValue.asDouble(), 'g', 16).trimmed();
                } else if (itemValue.isBool()) {
                    item = itemValue.asBool() ? QStringLiteral("true") : QStringLiteral("false");
                }
                if (!item.isEmpty() && !result.contains(item)) {
                    result.append(item);
                }
            }
            if (!result.isEmpty()) {
                return result;
            }
            continue;
        }

        if (value.isString()) {
            const QString text = QString::fromStdString(value.asString()).trimmed();
            if (!text.isEmpty()) {
                return QVariantList{text};
            }
        } else if (value.isNumber()) {
            const QString text = QString::number(value.asDouble(), 'g', 16).trimmed();
            if (!text.isEmpty()) {
                return QVariantList{text};
            }
        } else if (value.isBool()) {
            return QVariantList{value.asBool() ? QStringLiteral("true") : QStringLiteral("false")};
        }
    }
    return {};
}

inline QVariantList jsonNumberListRequirementValue(const foundation::json::JsonFacade& object,
                                                   std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        if (!object.has(key)) {
            continue;
        }

        const auto value = object.get(key);
        if (value.isArray()) {
            QVariantList result;
            for (size_t index = 0; index < value.size(); ++index) {
                const auto item = value.at(index);
                if (item.isNumber()) {
                    result.append(item.asDouble());
                    continue;
                }

                if (item.isString()) {
                    bool ok = false;
                    const double numericItem = QString::fromStdString(item.asString()).toDouble(&ok);
                    if (ok) {
                        result.append(numericItem);
                    }
                }
            }
            if (!result.isEmpty()) {
                return result;
            }
            continue;
        }

        if (value.isNumber()) {
            return QVariantList{value.asDouble()};
        }

        if (value.isString()) {
            bool ok = false;
            const double numericValue = QString::fromStdString(value.asString()).toDouble(&ok);
            if (ok) {
                return QVariantList{numericValue};
            }
        }
    }
    return {};
}

inline QVariantList configCalculationStringListRequirementValue(
    const foundation::json::JsonFacade& config,
    std::initializer_list<const char*> keys)
{
    if (!config.has("calculation")) {
        return {};
    }
    const auto calculation = config.get("calculation");
    if (!calculation.isObject()) {
        return {};
    }
    return jsonStringListRequirementValue(calculation, keys);
}

inline QVariantList configObjectStringListRequirementValue(const foundation::json::JsonFacade& config,
                                                           const char* objectKey,
                                                           std::initializer_list<const char*> keys)
{
    if (!config.has(objectKey)) {
        return {};
    }
    const auto object = config.get(objectKey);
    if (!object.isObject()) {
        return {};
    }
    return jsonStringListRequirementValue(object, keys);
}

inline QString configCalculationStringRequirementValue(const foundation::json::JsonFacade& config,
                                                       std::initializer_list<const char*> keys)
{
    if (!config.has("calculation")) {
        return {};
    }
    const auto calculation = config.get("calculation");
    if (!calculation.isObject()) {
        return {};
    }
    return jsonScalarRequirementValue(calculation, keys);
}

inline QString configObjectStringRequirementValue(const foundation::json::JsonFacade& config,
                                                  const char* objectKey,
                                                  std::initializer_list<const char*> keys)
{
    if (!config.has(objectKey)) {
        return {};
    }
    const auto object = config.get(objectKey);
    if (!object.isObject()) {
        return {};
    }
    return jsonScalarRequirementValue(object, keys);
}

inline QVariantMap extractRequirementCalculationMap(const factor::FactorInstanceInfo& info)
{
    QVariantMap calculation;

    auto insertString = [&](const QString& targetKey, std::initializer_list<const char*> keys) {
        QString value = configCalculationStringRequirementValue(info.config, keys);
        if (value.isEmpty()) {
            value = configObjectStringRequirementValue(info.config, "parameters", keys);
        }
        if (value.isEmpty()) {
            value = jsonScalarRequirementValue(info.config, keys);
        }
        if (!value.isEmpty()) {
            calculation.insert(targetKey, value);
        }
    };

    auto insertStringList = [&](const QString& targetKey, std::initializer_list<const char*> keys) {
        QVariantList value = configCalculationStringListRequirementValue(info.config, keys);
        if (value.isEmpty()) {
            value = configObjectStringListRequirementValue(info.config, "parameters", keys);
        }
        if (value.isEmpty()) {
            value = jsonStringListRequirementValue(info.config, keys);
        }
        if (!value.isEmpty()) {
            calculation.insert(targetKey, value);
        }
    };

    QVariantList valuationMetrics = configCalculationStringListRequirementValue(info.config, {"valuationMetrics"});
    if (valuationMetrics.isEmpty()) {
        valuationMetrics = configObjectStringListRequirementValue(info.config, "parameters", {"valuationMetrics"});
    }
    if (valuationMetrics.isEmpty()) {
        valuationMetrics = jsonStringListRequirementValue(info.config, {"valuationMetrics"});
    }
    if (!valuationMetrics.isEmpty()) {
        calculation.insert(QStringLiteral("valuationMetrics"), valuationMetrics);
    }
    insertString(QStringLiteral("sizeMetric"), {"sizeMetric"});
    insertString(QStringLiteral("metric"), {"metric"});
    insertStringList(QStringLiteral("growthMetrics"), {"growthMetrics"});
    const QVariantList growthWeights = jsonNumberListRequirementValue(info.config, {"growthWeights"});
    if (!growthWeights.isEmpty()) {
        calculation.insert(QStringLiteral("growthWeights"), growthWeights);
    }
    insertStringList(QStringLiteral("dividendMetrics"), {"dividendMetrics"});
    insertStringList(QStringLiteral("macroDimensions"), {"macroDimensions"});
    insertStringList(QStringLiteral("macroIndicators"), {"macroIndicators"});
    insertString(QStringLiteral("macroFrequency"), {"macroFrequency"});
    insertString(QStringLiteral("macroWindow"), {"macroWindow"});
    insertString(QStringLiteral("lookbackPeriod"), {"lookbackPeriod"});
    insertString(QStringLiteral("laggedEnabled"), {"laggedEnabled"});
    insertString(QStringLiteral("standardization"), {"standardization"});
    insertString(QStringLiteral("neutralizationEnabled"), {"neutralizationEnabled"});
    insertString(QStringLiteral("sentimentSource"), {"sentimentSource"});
    insertStringList(QStringLiteral("technicalIndicators"), {"technicalIndicators"});
    insertString(QStringLiteral("technicalPriceType"), {"technicalPriceType"});
    insertString(QStringLiteral("turnoverStabilityMetric"), {"turnoverStabilityMetric"});
    insertString(QStringLiteral("expression"), {"expression"});

    auto extractVariables = [](const foundation::json::JsonFacade& container) {
        QVariantList variables;
        if (!container.isArray()) {
            return variables;
        }

        for (size_t index = 0; index < container.size(); ++index) {
            const auto item = container.at(index);
            if (!item.isObject()) {
                continue;
            }

            QVariantMap variable;
            if (item.has("name")) {
                variable.insert(QStringLiteral("name"), QString::fromStdString(item.get("name").asString()));
            }
            if (item.has("field")) {
                variable.insert(QStringLiteral("field"), QString::fromStdString(item.get("field").asString()));
            }
            if (item.has("defaultValue")) {
                variable.insert(QStringLiteral("defaultValue"), item.get("defaultValue").asDouble());
            }

            if (!variable.isEmpty()) {
                variables.append(variable);
            }
        }

        return variables;
    };

    QVariantList customVariables;
    if (info.config.has("calculation")) {
        const auto calculationConfig = info.config.get("calculation");
        if (calculationConfig.isObject() && calculationConfig.has("variables")) {
            customVariables = extractVariables(calculationConfig.get("variables"));
        }
    }
    if (customVariables.isEmpty() && info.config.has("parameters")) {
        const auto parametersConfig = info.config.get("parameters");
        if (parametersConfig.isObject() && parametersConfig.has("variables")) {
            customVariables = extractVariables(parametersConfig.get("variables"));
        }
    }
    if (customVariables.isEmpty() && info.config.has("variables")) {
        customVariables = extractVariables(info.config.get("variables"));
    }
    if (!customVariables.isEmpty()) {
        calculation.insert(QStringLiteral("variables"), customVariables);
    }
    return calculation;
}

inline QStringList configuredRequirementFields(const factor::FactorInstanceInfo& info)
{
    QStringList requiredFields;

    QVariantList dataRequirementFields = configObjectStringListRequirementValue(
        info.config,
        "dataRequirements",
        {"required", "requiredFields"});
    for (const QVariant& fieldValue : dataRequirementFields) {
        const QString field = fieldValue.toString().trimmed();
        if (!field.isEmpty() && !requiredFields.contains(field)) {
            requiredFields.append(field);
        }
    }

    if (requiredFields.isEmpty()) {
        const QVariantList calculationRequirementFields =
            configCalculationStringListRequirementValue(info.config, {"requiredFields", "required_fields"});
        for (const QVariant& fieldValue : calculationRequirementFields) {
            const QString field = fieldValue.toString().trimmed();
            if (!field.isEmpty() && !requiredFields.contains(field)) {
                requiredFields.append(field);
            }
        }
    }

    return normalizeRequirementFieldNames(requiredFields);
}

inline QString configuredRequirementRuntimeType(const factor::FactorInstanceInfo& info)
{
    QString configuredType = jsonScalarRequirementValue(info.config, {"factorType", "majorCategory"});
    const QString calculationType = configCalculationStringRequirementValue(info.config, {"factorType"});
    if (!calculationType.isEmpty()) {
        configuredType = calculationType;
    }
    if (configuredType.isEmpty()) {
        configuredType = QString::fromStdString(info.factorType);
    }
    return normalizeConfigurableFactorType(configuredType);
}

inline QString resolveRequirementSourceTable(const factor::FactorInstanceInfo& info,
                                            const QStringList& requiredFields,
                                            const QString& explicitSourceTable = {})
{
    const QString resolvedExplicitSourceTable = normalizeRequirementSourceTable(explicitSourceTable);
    if (!resolvedExplicitSourceTable.isEmpty()) {
        return resolvedExplicitSourceTable;
    }

    if (info.config.has("dataRequirements")) {
        const auto dataRequirements = info.config.get("dataRequirements");
        if (dataRequirements.isObject()) {
            const QString sourceTable = normalizeRequirementSourceTable(
                jsonScalarRequirementValue(dataRequirements, {"sourceTable"}));
            if (!sourceTable.isEmpty()) {
                return sourceTable;
            }
        }
    }

    QVariantList normalizedRequiredFields;
    normalizedRequiredFields.reserve(requiredFields.size());
    for (const QString& requiredField : requiredFields) {
        normalizedRequiredFields.append(requiredField);
    }
    return inferRequirementSourceTable(normalizedRequiredFields);
}

inline QString normalizeConfigurableFactorType(const QString& rawType)
{
    return factor::normalizeFactorTypeId(rawType);
}

inline QString normalizeTechnicalRequirementIndicator(const QString& rawType)
{
    const QString normalized = rawType.trimmed().toLower();
    if (normalized == QStringLiteral("rsi")) {
        return QStringLiteral("rsi");
    }
    if (normalized == QStringLiteral("macd")) {
        return QStringLiteral("macd");
    }
    if (normalized == QStringLiteral("ma")) {
        return QStringLiteral("ma");
    }
    if (normalized == QStringLiteral("ema")) {
        return QStringLiteral("ema");
    }
    if (normalized == QStringLiteral("boll")) {
        return QStringLiteral("boll");
    }
    if (normalized == QStringLiteral("kdj")) {
        return QStringLiteral("kdj");
    }
    if (normalized == QStringLiteral("atr")) {
        return QStringLiteral("atr");
    }
    if (normalized == QStringLiteral("obv")) {
        return QStringLiteral("obv");
    }
    if (normalized == QStringLiteral("vwap")) {
        return QStringLiteral("vwap");
    }
    if (normalized == QStringLiteral("volume_ratio")) {
        return QStringLiteral("volume_ratio");
    }
    if (normalized == QStringLiteral("turnover_stability")) {
        return QStringLiteral("turnover_stability");
    }
    return {};
}

inline QString normalizeTechnicalRequirementPriceField(const QString& rawPriceType)
{
    const QString normalized = rawPriceType.trimmed().toLower();
    if (normalized == requirementFieldName(MarketBarFieldKeys::CLOSE)) {
        return requirementFieldName(MarketBarFieldKeys::CLOSE);
    }
    if (normalized == requirementFieldName(MarketBarFieldKeys::OPEN)) {
        return requirementFieldName(MarketBarFieldKeys::OPEN);
    }
    if (normalized == requirementFieldName(MarketBarFieldKeys::HIGH)) {
        return requirementFieldName(MarketBarFieldKeys::HIGH);
    }
    if (normalized == requirementFieldName(MarketBarFieldKeys::LOW)) {
        return requirementFieldName(MarketBarFieldKeys::LOW);
    }
    return {};
}

inline QString normalizeValuationRequirementMetric(const QString& rawMetric)
{
    const QString metric = rawMetric.trimmed().toLower();
    if (metric.isEmpty()) {
        return {};
    }
    if (metric == QStringLiteral("bp")) {
        return QStringLiteral("bp");
    }
    if (metric == QStringLiteral("ep")) {
        return QStringLiteral("ep");
    }
    if (metric == requirementFieldName(MarketBarFieldKeys::DIVIDEND_YIELD)) {
        return requirementFieldName(MarketBarFieldKeys::DIVIDEND_YIELD);
    }
    if (metric == QStringLiteral("cf_p")) {
        return QStringLiteral("cf_p");
    }
    return {};
}

inline QString normalizeSizeRequirementMetric(const QString& rawMetric)
{
    const QString metric = rawMetric.trimmed().toLower();
    if (metric.isEmpty()) {
        return {};
    }
    if (metric == requirementFieldName(MarketBarFieldKeys::MARKET_CAP) || rawMetric.startsWith(QString::fromUtf8("总市值"))) {
        return requirementFieldName(MarketBarFieldKeys::MARKET_CAP);
    }
    if (metric == requirementFieldName(MarketBarFieldKeys::CIRCULATING_MARKET_CAP) || rawMetric.startsWith(QString::fromUtf8("流通市值"))) {
        return requirementFieldName(MarketBarFieldKeys::CIRCULATING_MARKET_CAP);
    }
    if (metric == requirementFieldName(FinancialFieldKeys::TOTAL_ASSETS) || rawMetric.startsWith(QString::fromUtf8("总资产"))) {
        return requirementFieldName(FinancialFieldKeys::TOTAL_ASSETS);
    }
    return metric;
}

inline QString normalizeGrowthRequirementMetric(const QString& rawMetric)
{
    const QString metric = rawMetric.trimmed().toLower();
    if (metric.isEmpty()) {
        return {};
    }
    if (metric == QStringLiteral("revenue_growth")) {
        return QStringLiteral("revenue_growth");
    }
    if (metric == QStringLiteral("net_profit_growth")) {
        return QStringLiteral("net_profit_growth");
    }
    if (metric == QStringLiteral("delta_roe")) {
        return QStringLiteral("delta_roe");
    }
    if (metric == QStringLiteral("sue")) {
        return QStringLiteral("sue");
    }
    return metric;
}

inline QString normalizeQualityRequirementMetric(const QString& rawMetric)
{
    const QString metric = rawMetric.trimmed().toLower();
    if (metric.isEmpty()) {
        return {};
    }
    if (metric == requirementFieldName(FinancialFieldKeys::ROE) || rawMetric == QString::fromUtf8("净资产收益率")) {
        return requirementFieldName(FinancialFieldKeys::ROE);
    }
    if (metric == requirementFieldName(FinancialFieldKeys::ROA) || rawMetric == QString::fromUtf8("总资产收益率")) {
        return requirementFieldName(FinancialFieldKeys::ROA);
    }
    if (metric == requirementFieldName(FinancialFieldKeys::GROSS_MARGIN)
            || metric == requirementFieldName(FinancialFieldKeys::OPERATING_MARGIN)
            || rawMetric == QString::fromUtf8("毛利率")
            || rawMetric == QString::fromUtf8("营业利润率")
            || rawMetric == QString::fromUtf8("利润率")) {
        return metric == requirementFieldName(FinancialFieldKeys::OPERATING_MARGIN)
            ? requirementFieldName(FinancialFieldKeys::OPERATING_MARGIN)
            : requirementFieldName(FinancialFieldKeys::GROSS_MARGIN);
    }
    if (metric == requirementFieldName(FinancialFieldKeys::PROFIT_MARGIN)) {
        return requirementFieldName(FinancialFieldKeys::GROSS_MARGIN);
    }
    if (metric == QStringLiteral("net_profit_to_equity") || metric == QStringLiteral("earnings_quality")
            || rawMetric == QString::fromUtf8("盈利质量")) {
        return QStringLiteral("earnings_quality");
    }
    return metric;
}

inline QString normalizeRequirementSourceTable(const QString& rawSourceTable)
{
    const QString sourceTable = rawSourceTable.trimmed().toLower();
    if (sourceTable.isEmpty()) {
        return {};
    }

    const QString resolvedSentimentTable = resolveSentimentSourceTable(
        QVariantMap{{QStringLiteral("sentimentSource"), sourceTable}});
    if (!resolvedSentimentTable.isEmpty()) {
        return resolvedSentimentTable;
    }
    if (sourceTable == QStringLiteral("policy")) {
        return QStringLiteral("policy_data");
    }
    if (sourceTable == QStringLiteral("alternative")) {
        return QStringLiteral("alternative_data");
    }
    if (sourceTable == QStringLiteral("derivatives")) {
        return QStringLiteral("derivatives_data");
    }
    return sourceTable;
}

inline QString normalizeSentimentSourceText(const QString& rawSource)
{
    const QString source = rawSource.trimmed().toLower();
    if (source.isEmpty()) {
        return {};
    }
    if (source == QStringLiteral("news")
            || source == QStringLiteral("news_sentiment")
            || rawSource == QString::fromUtf8("新闻情绪")) {
        return QStringLiteral("news_sentiment");
    }
    if (source == QStringLiteral("social")
            || source == QStringLiteral("social_media")
            || rawSource == QString::fromUtf8("社交媒体")) {
        return QStringLiteral("social_media");
    }
    if (source == QStringLiteral("analyst")
            || source == QStringLiteral("analyst_rating")
            || rawSource == QString::fromUtf8("分析师评级")) {
        return QStringLiteral("analyst_rating");
    }
    if (source == QStringLiteral("market")
            || source == requirementFieldName(NewsFieldKeys::MARKET_SENTIMENT)
            || rawSource == QString::fromUtf8("市场情绪")) {
        return requirementFieldName(NewsFieldKeys::MARKET_SENTIMENT);
    }
    if (source == QStringLiteral("policy") || source == QStringLiteral("policy_data")) {
        return QStringLiteral("policy_data");
    }
    if (source == QStringLiteral("alternative") || source == QStringLiteral("alternative_data")) {
        return QStringLiteral("alternative_data");
    }
    if (source == QStringLiteral("derivatives") || source == QStringLiteral("derivatives_data")) {
        return QStringLiteral("derivatives_data");
    }
    return source;
}

inline QString normalizeSentimentMetricText(const QString& rawMetric)
{
    const QString metric = rawMetric.trimmed().toLower();
    if (metric.isEmpty()) {
        return {};
    }
    if (metric == requirementFieldName(NewsFieldKeys::SENTIMENT_SCORE) || metric == QStringLiteral("news_sentiment") || metric == QString::fromUtf8("新闻情绪")) {
        return requirementFieldName(NewsFieldKeys::SENTIMENT_SCORE);
    }
    if (metric == requirementFieldName(NewsFieldKeys::SOCIAL_SENTIMENT) || metric == QStringLiteral("social_media") || metric == QString::fromUtf8("社交媒体")) {
        return requirementFieldName(NewsFieldKeys::SOCIAL_SENTIMENT);
    }
    if (metric == requirementFieldName(NewsFieldKeys::INVESTOR_SENTIMENT) || metric == QStringLiteral("analyst_rating") || metric == QString::fromUtf8("分析师评级")) {
        return requirementFieldName(NewsFieldKeys::INVESTOR_SENTIMENT);
    }
    if (metric == requirementFieldName(NewsFieldKeys::MARKET_SENTIMENT) || metric == QString::fromUtf8("市场情绪")) {
        return requirementFieldName(NewsFieldKeys::MARKET_SENTIMENT);
    }
    if (metric == requirementFieldName(PolicyFieldKeys::POLICY_SCORE) || metric == QStringLiteral("policy")) {
        return requirementFieldName(PolicyFieldKeys::POLICY_SCORE);
    }
    if (metric == requirementFieldName(AlternativeFieldKeys::HOT_RANK) || metric == QStringLiteral("alternative")) {
        return requirementFieldName(AlternativeFieldKeys::HOT_RANK);
    }
    if (metric == requirementFieldName(DerivativesFieldKeys::BASIS_RATE) || metric == QStringLiteral("derivatives")) {
        return requirementFieldName(DerivativesFieldKeys::BASIS_RATE);
    }
    return metric;
}

inline QString defaultSentimentMetricForSource(const QString& rawSource)
{
    const QString source = normalizeSentimentSourceText(rawSource);
    if (source == QStringLiteral("social_media")) {
        return requirementFieldName(NewsFieldKeys::SOCIAL_SENTIMENT);
    }
    if (source == QStringLiteral("analyst_rating")) {
        return requirementFieldName(NewsFieldKeys::INVESTOR_SENTIMENT);
    }
    if (source == requirementFieldName(NewsFieldKeys::MARKET_SENTIMENT)) {
        return requirementFieldName(NewsFieldKeys::MARKET_SENTIMENT);
    }
    if (source == QStringLiteral("policy_data")) {
        return requirementFieldName(PolicyFieldKeys::POLICY_SCORE);
    }
    if (source == QStringLiteral("alternative_data")) {
        return requirementFieldName(AlternativeFieldKeys::HOT_RANK);
    }
    if (source == QStringLiteral("derivatives_data")) {
        return requirementFieldName(DerivativesFieldKeys::BASIS_RATE);
    }
    return requirementFieldName(NewsFieldKeys::SENTIMENT_SCORE);
}

inline QString inferSentimentSourceFromMetric(const QString& rawMetric)
{
    const QString metric = normalizeSentimentMetricText(rawMetric);
    if (metric == requirementFieldName(NewsFieldKeys::SOCIAL_SENTIMENT)) {
        return QStringLiteral("social_media");
    }
    if (metric == requirementFieldName(NewsFieldKeys::INVESTOR_SENTIMENT)) {
        return QStringLiteral("analyst_rating");
    }
    if (metric == requirementFieldName(NewsFieldKeys::MARKET_SENTIMENT)) {
        return requirementFieldName(NewsFieldKeys::MARKET_SENTIMENT);
    }
    if (metric == requirementFieldName(PolicyFieldKeys::POLICY_SCORE)) {
        return QStringLiteral("policy_data");
    }
    if (metric == requirementFieldName(AlternativeFieldKeys::HOT_RANK)) {
        return QStringLiteral("alternative_data");
    }
    if (metric == requirementFieldName(DerivativesFieldKeys::BASIS_RATE)) {
        return QStringLiteral("derivatives_data");
    }
    if (metric == requirementFieldName(NewsFieldKeys::SENTIMENT_SCORE)) {
        return QStringLiteral("news_sentiment");
    }
    return {};
}

inline QStringList normalizeDividendRequirementMetrics(const QVariantMap& calculation)
{
    const auto canonicalize = [](const QString& rawMetric) {
        const QString metric = rawMetric.trimmed().toLower();
        if (metric.isEmpty()) {
            return QString();
        }
        if (metric == requirementFieldName(FinancialFieldKeys::DIVIDEND_YIELD) || rawMetric.startsWith(QString::fromUtf8("股息率"))) {
            return requirementFieldName(FinancialFieldKeys::DIVIDEND_YIELD);
        }
        if (metric == requirementFieldName(FinancialFieldKeys::PAYOUT_RATIO)
                || rawMetric.startsWith(QString::fromUtf8("股利支付率"))
                || rawMetric.startsWith(QString::fromUtf8("派息率"))) {
            return requirementFieldName(FinancialFieldKeys::PAYOUT_RATIO);
        }
        if (metric == requirementFieldName(FinancialFieldKeys::DIVIDEND_STABILITY)
                || rawMetric.startsWith(QString::fromUtf8("分红稳定性"))
                || rawMetric.startsWith(QString::fromUtf8("股息稳定性"))) {
            return requirementFieldName(FinancialFieldKeys::DIVIDEND_STABILITY);
        }
        return metric;
    };

    QStringList metrics;
    const auto appendMetrics = [&](const QVariant& value) {
        const QVariantList values = normalizeRequirementList(value);
        for (const QVariant& item : values) {
            const QString metric = canonicalize(item.toString());
            if (!metric.isEmpty() && !metrics.contains(metric)) {
                metrics.append(metric);
            }
        }
    };

    appendMetrics(calculation.value(QStringLiteral("dividendMetrics")));
    if (metrics.isEmpty()) {
        appendMetrics(calculation.value(QStringLiteral("metric")));
    }

    if (metrics.isEmpty()) {
        metrics.append(requirementFieldName(FinancialFieldKeys::DIVIDEND_YIELD));
    }
    return metrics;
}

inline QString normalizeMacroRequirementDimension(const QString& rawDimension)
{
    const QString dimension = rawDimension.trimmed().toLower();
    if (dimension == QStringLiteral("growth")
            || dimension == QStringLiteral("growth_sensitivity")
            || rawDimension == QString::fromUtf8("经济增长")) {
        return QStringLiteral("growth");
    }
    if (dimension == QStringLiteral("inflation")
            || dimension == QStringLiteral("inflation_sensitivity")
            || rawDimension == QString::fromUtf8("通货膨胀")) {
        return QStringLiteral("inflation");
    }
    if (dimension == QStringLiteral("credit") || rawDimension == QString::fromUtf8("货币信用")) {
        return QStringLiteral("credit");
    }
    if (dimension == QStringLiteral("rates")
            || dimension == QStringLiteral("interest_rate_sensitivity")
            || rawDimension == QString::fromUtf8("利率水平")) {
        return QStringLiteral("rates");
    }
    if (dimension == QStringLiteral("policy") || rawDimension == QString::fromUtf8("政策环境")) {
        return QStringLiteral("policy");
    }
    if (dimension == QStringLiteral("risk_appetite") || rawDimension == QString::fromUtf8("风险偏好")) {
        return QStringLiteral("risk_appetite");
    }
    return {};
}

inline QString normalizeMacroRequirementIndicator(const QString& rawIndicator)
{
    const QString indicator = rawIndicator.trimmed().toLower();
    if (indicator.isEmpty()) {
        return {};
    }
    if (indicator == QStringLiteral("industrial_added_value_yoy") || rawIndicator.startsWith(QString::fromUtf8("工业增加值同比"))) {
        return QStringLiteral("industrial_added_value_yoy");
    }
    if (indicator == QStringLiteral("manufacturing_pmi") || rawIndicator.startsWith(QString::fromUtf8("制造业PMI"))) {
        return QStringLiteral("manufacturing_pmi");
    }
    if (indicator == QStringLiteral("gdp_yoy") || rawIndicator.startsWith(QString::fromUtf8("GDP同比"))) {
        return QStringLiteral("gdp_yoy");
    }
    if (indicator == QStringLiteral("cpi_yoy") || rawIndicator.startsWith(QString::fromUtf8("CPI同比"))) {
        return QStringLiteral("cpi_yoy");
    }
    if (indicator == QStringLiteral("ppi_yoy") || rawIndicator.startsWith(QString::fromUtf8("PPI同比"))) {
        return QStringLiteral("ppi_yoy");
    }
    if (indicator == QStringLiteral("m2_yoy") || rawIndicator.startsWith(QString::fromUtf8("M2同比"))) {
        return QStringLiteral("m2_yoy");
    }
    if (indicator == QStringLiteral("social_financing_stock_yoy") || rawIndicator.startsWith(QString::fromUtf8("社融存量同比"))) {
        return QStringLiteral("social_financing_stock_yoy");
    }
    if (indicator == QStringLiteral("m1_m2_spread") || rawIndicator.startsWith(QString::fromUtf8("M1-M2剪刀差"))) {
        return QStringLiteral("m1_m2_spread");
    }
    if (indicator == QStringLiteral("ten_year_bond_yield") || rawIndicator.startsWith(QString::fromUtf8("10年期国债收益率"))) {
        return QStringLiteral("ten_year_bond_yield");
    }
    if (indicator == QStringLiteral("shibor_3m") || rawIndicator.startsWith(QString::fromUtf8("SHIBOR"))) {
        return QStringLiteral("shibor_3m");
    }
    if (indicator == QStringLiteral("lpr_1y") || rawIndicator.startsWith(QString::fromUtf8("LPR"))) {
        return QStringLiteral("lpr_1y");
    }
    if (indicator == QStringLiteral("reserve_requirement_ratio") || rawIndicator.startsWith(QString::fromUtf8("存款准备金率"))) {
        return QStringLiteral("reserve_requirement_ratio");
    }
    if (indicator == QStringLiteral("aa_credit_spread") || rawIndicator.startsWith(QString::fromUtf8("信用利差"))) {
        return QStringLiteral("aa_credit_spread");
    }
    if (indicator == QStringLiteral("vix_proxy") || rawIndicator.startsWith(QString::fromUtf8("VIX")) || rawIndicator.startsWith(QString::fromUtf8("波动率代理"))) {
        return QStringLiteral("vix_proxy");
    }
    return {};
}

inline QStringList normalizeMacroRequirementDimensions(const QVariantMap& calculation)
{
    QStringList dimensions;
    const auto appendDimension = [&dimensions](const QString& dimension) {
        if (!dimension.isEmpty() && !dimensions.contains(dimension)) {
            dimensions.append(dimension);
        }
    };

    for (const QVariant& value : normalizeRequirementList(calculation.value(QStringLiteral("macroDimensions")))) {
        appendDimension(normalizeMacroRequirementDimension(value.toString()));
    }

    return dimensions;
}

inline QStringList normalizeMacroRequirementIndicators(const QVariantMap& calculation)
{
    QStringList indicators;
    for (const QVariant& value : normalizeRequirementList(calculation.value(QStringLiteral("macroIndicators")))) {
        const QString indicator = normalizeMacroRequirementIndicator(value.toString());
        if (!indicator.isEmpty() && !indicators.contains(indicator)) {
            indicators.append(indicator);
        }
    }

    if (indicators.isEmpty()) {
        const QStringList dimensions = normalizeMacroRequirementDimensions(calculation);
        for (const QString& dimension : dimensions) {
            if (dimension == QStringLiteral("growth")) {
                indicators.append(QStringLiteral("industrial_added_value_yoy"));
                indicators.append(QStringLiteral("manufacturing_pmi"));
                indicators.append(QStringLiteral("gdp_yoy"));
            } else if (dimension == QStringLiteral("inflation")) {
                indicators.append(QStringLiteral("cpi_yoy"));
                indicators.append(QStringLiteral("ppi_yoy"));
            } else if (dimension == QStringLiteral("credit")) {
                indicators.append(QStringLiteral("m2_yoy"));
                indicators.append(QStringLiteral("social_financing_stock_yoy"));
                indicators.append(QStringLiteral("m1_m2_spread"));
            } else if (dimension == QStringLiteral("rates")) {
                indicators.append(QStringLiteral("ten_year_bond_yield"));
                indicators.append(QStringLiteral("shibor_3m"));
            } else if (dimension == QStringLiteral("policy")) {
                indicators.append(QStringLiteral("lpr_1y"));
                indicators.append(QStringLiteral("reserve_requirement_ratio"));
            } else if (dimension == QStringLiteral("risk_appetite")) {
                indicators.append(QStringLiteral("aa_credit_spread"));
                indicators.append(QStringLiteral("vix_proxy"));
            }
        }
    }

    if (indicators.isEmpty()) {
        indicators = {QStringLiteral("industrial_added_value_yoy"), QStringLiteral("cpi_yoy"), QStringLiteral("m2_yoy"), QStringLiteral("ten_year_bond_yield"), QStringLiteral("lpr_1y"), QStringLiteral("aa_credit_spread")};
    }

    return indicators;
}

inline QStringList macroRequirementFieldsForIndicator(const QString& rawIndicator)
{
    const QString indicator = normalizeMacroRequirementIndicator(rawIndicator);
    if (indicator == QStringLiteral("industrial_added_value_yoy")
            || indicator == QStringLiteral("manufacturing_pmi")
            || indicator == QStringLiteral("gdp_yoy")
            || indicator == QStringLiteral("cpi_yoy")
            || indicator == QStringLiteral("ppi_yoy")) {
        return {requirementFieldName(MarketBarFieldKeys::CLOSE)};
    }
    if (indicator == QStringLiteral("m2_yoy")
            || indicator == QStringLiteral("social_financing_stock_yoy")
            || indicator == QStringLiteral("m1_m2_spread")) {
        return {requirementFieldName(MarketBarFieldKeys::CLOSE), requirementFieldName(MarketBarFieldKeys::TURNOVER_RATE)};
    }
    if (indicator == QStringLiteral("ten_year_bond_yield") || indicator == QStringLiteral("shibor_3m")) {
        return {requirementFieldName(MarketBarFieldKeys::CLOSE), requirementFieldName(MarketBarFieldKeys::PE_RATIO)};
    }
    if (indicator == QStringLiteral("lpr_1y") || indicator == QStringLiteral("reserve_requirement_ratio")) {
        return {requirementFieldName(MarketBarFieldKeys::CLOSE), requirementFieldName(MarketBarFieldKeys::PB_RATIO)};
    }
    if (indicator == QStringLiteral("aa_credit_spread") || indicator == QStringLiteral("vix_proxy")) {
        return {requirementFieldName(MarketBarFieldKeys::CLOSE), requirementFieldName(MarketBarFieldKeys::VOLUME)};
    }
    return {requirementFieldName(MarketBarFieldKeys::CLOSE)};
}

inline QVariantList macroRequirementFieldsForIndicators(const QVariantMap& calculation)
{
    QVariantList fields;
    const QStringList indicators = normalizeMacroRequirementIndicators(calculation);
    for (const QString& indicator : indicators) {
        for (const QString& field : macroRequirementFieldsForIndicator(indicator)) {
            if (!fields.contains(field)) {
                fields.append(field);
            }
        }
    }
    if (fields.isEmpty()) {
        fields.append(requirementFieldName(MarketBarFieldKeys::CLOSE));
    }
    return fields;
}

inline FactorRequirementProfile resolveFactorRequirementProfile(const QString& rawFactorType,
                                                               const QVariantMap& calculation)
{
    FactorRequirementProfile profile;
    profile.factorType = normalizeConfigurableFactorType(rawFactorType);

    if (profile.factorType == QStringLiteral("value")) {
        const QVariantList valuationMetrics = calculation.value(QStringLiteral("valuationMetrics")).toList();
        if (valuationMetrics.isEmpty()) {
            return profile;
        }
        QStringList normalizedMetrics;
        for (const QVariant& value : valuationMetrics) {
            const QString metric = normalizeValuationRequirementMetric(value.toString());
            if (metric.isEmpty()) {
                return profile;
            }
            if (!normalizedMetrics.contains(metric)) {
                normalizedMetrics.append(metric);
            }
            if (metric == QStringLiteral("bp")) {
                appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::PB_RATIO);
            } else if (metric == QStringLiteral("ep")) {
                appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::PE_RATIO);
            } else if (metric == requirementFieldName(MarketBarFieldKeys::DIVIDEND_YIELD)) {
                appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::DIVIDEND_YIELD);
            } else if (metric == QStringLiteral("cf_p")) {
                appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
                appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::OPERATING_CASH_FLOW);
            } else {
                return profile;
            }
        }
        profile.metric = normalizedMetrics.join(QStringLiteral(","));
        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            appendUniqueRequirementField(profile.requiredFields, SymbolInfoFieldKeys::INDUSTRY_CODE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        }
        profile.supported = true;
        return profile;
    }

        if (profile.factorType == QStringLiteral("momentum")) {
        appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::CLOSE);
        // 根据 adjustPriceType 配置决定使用前复权还是后复权因子字段，默认后复权
        const QString adjustPriceType = calculation.value(QStringLiteral("adjustPriceType"), QStringLiteral("post_adjust_factor")).toString().trimmed().toLower();
        const QString resolvedAdjustField = MarketBarFieldKeys::resolveAdjustField(adjustPriceType);
        if (resolvedAdjustField == requirementFieldName(MarketBarFieldKeys::PRE_ADJ_FACTOR)) {
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::PRE_ADJ_FACTOR);
        } else {
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::POST_ADJ_FACTOR);
        }
        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            appendUniqueRequirementField(profile.requiredFields, SymbolInfoFieldKeys::INDUSTRY_CODE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        }
        if (calculation.value(QStringLiteral("useVolume")).toBool()) {
            appendUniqueOptionalField(profile.optionalFields, MarketBarFieldKeys::VOLUME);
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("size")) {
        profile.metric = normalizeSizeRequirementMetric(
            calculation.value(QStringLiteral("sizeMetric"), requirementFieldName(MarketBarFieldKeys::MARKET_CAP)).toString());
        if (profile.metric.isEmpty()) {
            profile.metric = requirementFieldName(MarketBarFieldKeys::MARKET_CAP);
        }
        if (profile.metric == requirementFieldName(MarketBarFieldKeys::MARKET_CAP)) {
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        } else if (profile.metric == requirementFieldName(MarketBarFieldKeys::CIRCULATING_MARKET_CAP)) {
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::CIRCULATING_MARKET_CAP);
        } else if (profile.metric == requirementFieldName(FinancialFieldKeys::TOTAL_ASSETS)) {
            appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::TOTAL_ASSETS);
        } else {
            return profile;
        }
        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            appendUniqueRequirementField(profile.requiredFields, SymbolInfoFieldKeys::INDUSTRY_CODE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("quality")) {
        profile.metric = normalizeQualityRequirementMetric(
            calculation.value(QStringLiteral("metric"), requirementFieldName(FinancialFieldKeys::ROE)).toString());
        if (profile.metric == requirementFieldName(FinancialFieldKeys::ROE)) {
            appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::ROE);
        } else if (profile.metric == requirementFieldName(FinancialFieldKeys::ROA)) {
            appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::ROA);
        } else if (profile.metric == requirementFieldName(FinancialFieldKeys::GROSS_MARGIN)
                || profile.metric == requirementFieldName(FinancialFieldKeys::OPERATING_MARGIN)) {
            appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::PROFIT_MARGIN);
        } else {
            appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::NET_PROFIT);
            appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::EQUITY);
        }
        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            appendUniqueRequirementField(profile.requiredFields, SymbolInfoFieldKeys::INDUSTRY_CODE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("growth")) {
        QStringList metrics;
        const QVariantList rawMetrics = calculation.value(QStringLiteral("growthMetrics")).toList();
        const QVariantList rawWeights = calculation.value(QStringLiteral("growthWeights")).toList();
        if (rawMetrics.isEmpty() || rawWeights.isEmpty() || rawMetrics.size() != rawWeights.size()) {
            return profile;
        }
        for (const QVariant& value : rawMetrics) {
            const QString metric = normalizeGrowthRequirementMetric(value.toString());
            if (!metric.isEmpty() && !metrics.contains(metric)) {
                metrics.append(metric);
            }
        }
        if (metrics.isEmpty() || metrics.size() != rawMetrics.size()) {
            return profile;
        }

        profile.metric = metrics.first();
        for (const QString& metric : metrics) {
            if (metric == QStringLiteral("revenue_growth")) {
                appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::TOTAL_REVENUE);
            } else if (metric == QStringLiteral("net_profit_growth")) {
                appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::NET_PROFIT);
            } else if (metric == QStringLiteral("delta_roe")) {
                appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::ROE);
            } else if (metric == QStringLiteral("sue")) {
                appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::EPS);
            } else {
                appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::TOTAL_REVENUE);
            }
        }
        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            appendUniqueRequirementField(profile.requiredFields, SymbolInfoFieldKeys::INDUSTRY_CODE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("dividend")) {
        const QStringList metrics = normalizeDividendRequirementMetrics(calculation);
        profile.metric = metrics.isEmpty() ? requirementFieldName(FinancialFieldKeys::DIVIDEND_YIELD) : metrics.first();

        bool needsFinancialIndicator = false;
        for (const QString& metric : metrics) {
            if (metric == requirementFieldName(FinancialFieldKeys::PAYOUT_RATIO)) {
                appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::PAYOUT_RATIO);
                needsFinancialIndicator = true;
            } else if (metric == requirementFieldName(FinancialFieldKeys::DIVIDEND_STABILITY)) {
                appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::DIVIDEND_STABILITY);
            } else {
                appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::DIVIDEND_YIELD);
            }
        }

        if (profile.requiredFields.isEmpty()) {
            appendUniqueRequirementField(profile.requiredFields, FinancialFieldKeys::DIVIDEND_YIELD);
            profile.metric = requirementFieldName(FinancialFieldKeys::DIVIDEND_YIELD);
        }

        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            appendUniqueRequirementField(profile.requiredFields, SymbolInfoFieldKeys::INDUSTRY_CODE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        }

        if (needsFinancialIndicator) {
            profile.sourceTable = QStringLiteral("financial_indicator");
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("technical")) {
        QStringList indicators;
        const QVariantList configuredIndicators = calculation.value(QStringLiteral("technicalIndicators")).toList();
        for (const QVariant& configuredIndicator : configuredIndicators) {
            const QString indicator = normalizeTechnicalRequirementIndicator(configuredIndicator.toString());
            if (!indicator.isEmpty() && !indicators.contains(indicator)) {
                indicators.append(indicator);
            }
        }
        if (indicators.isEmpty()) {
            profile.supported = false;
            return profile;
        }

        profile.metric = indicators.first();
        const bool needTurnoverSeries = indicators.contains(QStringLiteral("turnover_stability"));
        const bool needPriceSeries = !needTurnoverSeries;
        const bool needHighLowSeries = indicators.contains(QStringLiteral("kdj"))
            || indicators.contains(QStringLiteral("atr"))
            || indicators.contains(QStringLiteral("boll"));
        const bool needVolumeSeries = indicators.contains(QStringLiteral("obv"))
            || indicators.contains(QStringLiteral("vwap"))
            || indicators.contains(QStringLiteral("volume_ratio"));
        const QString turnoverMetric = calculation.value(QStringLiteral("turnoverStabilityMetric"))
            .toString()
            .trimmed()
            .toLower();

        if (needPriceSeries) {
            const QString priceField = normalizeTechnicalRequirementPriceField(
                calculation.value(QStringLiteral("technicalPriceType")).toString());
            if (priceField.isEmpty()) {
                return profile;
            }
            profile.requiredFields.append(priceField);
        }
        if (needHighLowSeries) {
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::HIGH);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::LOW);
        }
        if (needVolumeSeries) {
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::VOLUME);
        }
        if (needTurnoverSeries) {
            appendUniqueRequirementField(
                profile.requiredFields,
                turnoverMetric == requirementFieldName(MarketBarFieldKeys::VOLUME)
                    ? MarketBarFieldKeys::VOLUME
                    : MarketBarFieldKeys::TURNOVER_RATE);
        }
        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            appendUniqueRequirementField(profile.requiredFields, SymbolInfoFieldKeys::INDUSTRY_CODE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("liquidity")) {
        profile.metric = calculation.value(QStringLiteral("metric"), requirementFieldName(MarketBarFieldKeys::TURNOVER_RATE))
            .toString()
            .trimmed()
            .toLower();
        if (profile.metric == requirementFieldName(MarketBarFieldKeys::VOLUME)) {
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::VOLUME);
        } else if (profile.metric == requirementFieldName(MarketBarFieldKeys::AMPLITUDE)) {
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::AMPLITUDE);
        } else if (profile.metric == QStringLiteral("amihud_illiquidity")) {
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::CLOSE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::VOLUME);
            appendUniqueOptionalField(profile.optionalFields, MarketBarFieldKeys::TURNOVER);
        } else {
            profile.metric = requirementFieldName(MarketBarFieldKeys::TURNOVER_RATE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::TURNOVER_RATE);
        }
        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            appendUniqueRequirementField(profile.requiredFields, SymbolInfoFieldKeys::INDUSTRY_CODE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("macro")) {
        const QVariantList indicators = normalizeRequirementList(calculation.value(QStringLiteral("macroIndicators")));
        const QStringList normalizedIndicators = normalizeMacroRequirementIndicators(calculation);
        if (normalizedIndicators.isEmpty()) {
            profile.supported = false;
            return profile;
        }
        profile.metric = normalizedIndicators.first();
        profile.requiredFields = macroRequirementFieldsForIndicators(calculation);
        if (profile.requiredFields.isEmpty()) {
            profile.supported = false;
            return profile;
        }
        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            appendUniqueRequirementField(profile.requiredFields, SymbolInfoFieldKeys::INDUSTRY_CODE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        }
        profile.sourceTable = QStringLiteral("daily_bar");
        Q_UNUSED(calculation.value(QStringLiteral("macroWindow")));
        Q_UNUSED(indicators);
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("industry")) {
        profile.metric = calculation.value(QStringLiteral("industryMetric")).toString().trimmed().toLower();
        if (profile.metric.isEmpty()) {
            profile.metric = QStringLiteral("industry_momentum");
        }
        profile.requiredFields.append(normalizeRequirementFieldName(profile.metric));
        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            appendUniqueRequirementField(profile.requiredFields, SymbolInfoFieldKeys::INDUSTRY_CODE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("sentiment")) {
        QString source = normalizeSentimentSourceText(
            calculation.value(QStringLiteral("sentimentSource")).toString());
        profile.metric = normalizeSentimentMetricText(
            calculation.value(QStringLiteral("metric")).toString());
        if (profile.metric.isEmpty()) {
            profile.metric = defaultSentimentMetricForSource(source);
        }
        if (source.isEmpty()) {
            source = inferSentimentSourceFromMetric(profile.metric);
        }
        if (source.isEmpty()) {
            source = QStringLiteral("news_sentiment");
        }

        if (profile.metric.isEmpty()) {
            profile.metric = requirementFieldName(NewsFieldKeys::SENTIMENT_SCORE);
        }
        profile.requiredFields.append(normalizeRequirementFieldName(profile.metric));
        profile.sourceTable = resolveSentimentSourceTable(
            QVariantMap{{QStringLiteral("sentimentSource"), source}});
        if (profile.sourceTable.isEmpty()) {
            profile.sourceTable = inferRequirementSourceTable(profile.requiredFields);
        }
        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            appendUniqueRequirementField(profile.requiredFields, SymbolInfoFieldKeys::INDUSTRY_CODE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("custom")) {
        const QString expression = calculation.value(QStringLiteral("expression")).toString().trimmed();
        if (expression.isEmpty()) {
            profile.supported = false;
            return profile;
        }
        std::vector<factor::custom_expression::VariableBinding> bindings;
        const QVariantList configuredVariables = calculation.value(QStringLiteral("variables")).toList();
        bindings.reserve(static_cast<size_t>(configuredVariables.size()));
        for (const QVariant& configuredVariable : configuredVariables) {
            const QVariantMap variableMap = configuredVariable.toMap();
            const QString name = variableMap.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty()) {
                continue;
            }

            factor::custom_expression::VariableBinding binding;
            binding.name = name;
            binding.field = variableMap.value(QStringLiteral("field")).toString().trimmed();
            if (variableMap.contains(QStringLiteral("defaultValue"))) {
                binding.hasDefaultValue = true;
                binding.defaultValue = variableMap.value(QStringLiteral("defaultValue")).toDouble();
            }
            bindings.push_back(std::move(binding));
        }

        const auto fieldRequirements = factor::custom_expression::resolveFieldRequirements(
            expression.toLower(),
            bindings);
        for (const QString& field : fieldRequirements.requiredFields) {
            if (!profile.requiredFields.contains(field)) {
                profile.requiredFields.append(field);
            }
        }
        for (const QString& field : fieldRequirements.optionalFields) {
            if (!profile.optionalFields.contains(field)) {
                profile.optionalFields.append(field);
            }
        }

        const QStringList variables = factor::custom_expression::extractVariables(expression.toLower());
        bool canUseEmptyFieldSet = true;
        std::unordered_map<std::string, double> defaultVariableValues;
        for (const QString& variable : variables) {
            const factor::custom_expression::VariableBinding* binding = factor::custom_expression::findBinding(bindings, variable);
            if (!binding) {
                canUseEmptyFieldSet = false;
                break;
            }

            const QString field = binding->field.trimmed();
            if (!field.isEmpty()) {
                canUseEmptyFieldSet = false;
                break;
            }

            if (!binding->hasDefaultValue) {
                canUseEmptyFieldSet = false;
                break;
            }

            defaultVariableValues.emplace(variable.toStdString(), binding->defaultValue);
        }

        if (canUseEmptyFieldSet && fieldRequirements.requiredFields.isEmpty() && fieldRequirements.optionalFields.isEmpty()) {
            QString parseError;
            const QStringList rpn = factor::custom_expression::toRpn(expression.toLower(), &parseError);
            if (rpn.isEmpty()) {
                return profile;
            }

            QString evalError;
            const auto evaluated = factor::custom_expression::evaluateRpn(rpn, defaultVariableValues, &evalError);
            if (!evaluated.has_value()) {
                return profile;
            }

            profile.allowEmptyRequiredFields = true;
        }

        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            appendUniqueRequirementField(profile.requiredFields, SymbolInfoFieldKeys::INDUSTRY_CODE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        }
        profile.supported = !profile.requiredFields.isEmpty() || !profile.optionalFields.isEmpty() || profile.allowEmptyRequiredFields;
        return profile;
    }

    if (profile.factorType == QStringLiteral("low_volatility")) {
        appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::CLOSE);
        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            appendUniqueRequirementField(profile.requiredFields, SymbolInfoFieldKeys::INDUSTRY_CODE);
            appendUniqueRequirementField(profile.requiredFields, MarketBarFieldKeys::MARKET_CAP);
        }
        profile.supported = true;
        return profile;
    }

    return profile;
}

inline bool isSupportMapConfigurableRuntimeType(const QString& runtimeType)
{
    return runtimeType == QStringLiteral("growth")
        || runtimeType == QStringLiteral("dividend")
        || runtimeType == QStringLiteral("technical")
        || runtimeType == QStringLiteral("liquidity")
        || runtimeType == QStringLiteral("macro")
        || runtimeType == QStringLiteral("industry")
        || runtimeType == QStringLiteral("sentiment")
        || runtimeType == QStringLiteral("custom");
}

inline QStringList supportMapRequirementFieldsForProfile(const QString& runtimeType,
                                                         const FactorRequirementProfile& profile)
{
    Q_UNUSED(runtimeType);
    return normalizeRequirementFieldNames(profile.requiredFields);
}

inline QString supportMapRequirementSourceTableForProfile(const QString& runtimeType,
                                                          const FactorRequirementProfile& profile)
{
    if (runtimeType == QStringLiteral("value")
        && profile.metric.split(QStringLiteral(","), Qt::SkipEmptyParts).contains(QStringLiteral("cf_p"))) {
        return QStringLiteral("financial_indicator");
    }
    return profile.sourceTable;
}

inline QString supportMapUnsupportedMetricReason(const QString& runtimeType,
                                                 const QVariantMap& calculation)
{
    if (runtimeType == QStringLiteral("value")) {
        const QVariantList valuationMetrics = calculation.value(QStringLiteral("valuationMetrics")).toList();
        const QString metric = valuationMetrics.isEmpty() ? QString() : valuationMetrics.first().toString().trimmed();
        return QStringLiteral("当前运行时暂不支持计算价值因子指标 %1")
            .arg(metric.isEmpty() ? QStringLiteral("unknown") : metric);
    }

    if (runtimeType == QStringLiteral("size")) {
        const QString metric = calculation.value(QStringLiteral("sizeMetric")).toString().trimmed();
        return QStringLiteral("当前运行时暂不支持计算规模因子指标 %1")
            .arg(metric.isEmpty() ? QStringLiteral("unknown") : metric);
    }

    return {};
}

inline SupportMapRequirementResolution resolveSupportMapRequirementResolution(
    const QString& runtimeType,
    const QVariantMap& calculation,
    const QStringList& configuredFields = {})
{
    SupportMapRequirementResolution resolution;

    if (runtimeType.isEmpty()) {
        return resolution;
    }

    const FactorRequirementProfile profile = resolveFactorRequirementProfile(runtimeType, calculation);
    resolution.allowEmptyRequiredFields = profile.allowEmptyRequiredFields;

    if (runtimeType == QStringLiteral("value") || runtimeType == QStringLiteral("size")) {
        if (!profile.supported) {
            resolution.failureCategory = QStringLiteral("unsupported-metric");
            resolution.failureReason = supportMapUnsupportedMetricReason(runtimeType, calculation);
            return resolution;
        }

        resolution.requiredFields = supportMapRequirementFieldsForProfile(runtimeType, profile);
        resolution.explicitSourceTable = supportMapRequirementSourceTableForProfile(runtimeType, profile);
        return resolution;
    }

    if (runtimeType == QStringLiteral("low_volatility")) {
        if (profile.supported && !profile.requiredFields.isEmpty()) {
            resolution.requiredFields = supportMapRequirementFieldsForProfile(runtimeType, profile);
            resolution.explicitSourceTable = supportMapRequirementSourceTableForProfile(runtimeType, profile);
        }
        return resolution;
    }

    if (!isSupportMapConfigurableRuntimeType(runtimeType)) {
        return resolution;
    }

    if (runtimeType == QStringLiteral("technical") && !profile.supported) {
        resolution.failureCategory = QStringLiteral("unsupported-metric");
        resolution.failureReason = QStringLiteral("技术因子缺少合法的 technicalIndicators 或 technicalPriceType 配置");
        return resolution;
    }

    if ((runtimeType == QStringLiteral("technical") || runtimeType == QStringLiteral("macro"))
        && profile.supported
        && !profile.requiredFields.isEmpty()) {
        resolution.requiredFields = supportMapRequirementFieldsForProfile(runtimeType, profile);
    } else {
        resolution.requiredFields = normalizeRequirementFieldNames(configuredFields);
        if (resolution.requiredFields.isEmpty() && profile.supported && !profile.requiredFields.isEmpty()) {
            resolution.requiredFields = supportMapRequirementFieldsForProfile(runtimeType, profile);
        }
    }

    if (resolution.requiredFields.isEmpty() && !resolution.allowEmptyRequiredFields) {
        resolution.failureCategory = QStringLiteral("missing-field");
        resolution.failureReason = QStringLiteral("因子配置缺少可用于支持校验的必需字段");
        return resolution;
    }

    resolution.explicitSourceTable = supportMapRequirementSourceTableForProfile(runtimeType, profile);
    return resolution;
}

inline bool isDailyBarRequirementField(const QString& rawField)
{
    return marketBarFields().contains(normalizeRequirementFieldName(rawField));
}

inline bool isFinancialRequirementField(const QString& rawField)
{
    return financialFields().contains(normalizeRequirementFieldName(rawField));
}

inline bool isSymbolInfoRequirementField(const QString& rawField)
{
    return symbolInfoFields().contains(normalizeRequirementFieldName(rawField));
}

inline bool isNewsRequirementField(const QString& rawField)
{
    return newsFields().contains(normalizeRequirementFieldName(rawField));
}

inline bool isPolicyRequirementField(const QString& rawField)
{
    return policyFields().contains(normalizeRequirementFieldName(rawField));
}

inline bool isAlternativeRequirementField(const QString& rawField)
{
    return alternativeFields().contains(normalizeRequirementFieldName(rawField));
}

inline bool isDerivativesRequirementField(const QString& rawField)
{
    return derivativesFields().contains(normalizeRequirementFieldName(rawField));
}

inline QString resolveSentimentSourceTable(const QVariantMap& calculation)
{
    const QString source = calculation.value(QStringLiteral("sentimentSource"))
        .toString()
        .trimmed()
        .toLower();
    if (source == QStringLiteral("news_sentiment") || source == QString::fromUtf8("新闻情绪")) {
        return QStringLiteral("news_sentiment");
    }
    if (source == QStringLiteral("policy") || source == QStringLiteral("policy_data")) {
        return QStringLiteral("policy_data");
    }
    if (source == QStringLiteral("alternative") || source == QStringLiteral("alternative_data")) {
        return QStringLiteral("alternative_data");
    }
    if (source == QStringLiteral("derivatives") || source == QStringLiteral("derivatives_data")) {
        return QStringLiteral("derivatives_data");
    }
    if (source == QStringLiteral("social_media")
            || source == requirementFieldName(NewsFieldKeys::INVESTOR_SENTIMENT)
            || source == requirementFieldName(NewsFieldKeys::MARKET_SENTIMENT)
            || source == QStringLiteral("analyst_rating")
            || source == QString::fromUtf8("社交媒体")
            || source == QString::fromUtf8("分析师评级")
            || source == QString::fromUtf8("市场情绪")) {
        return QStringLiteral("news_sentiment");
    }
    return {};
}

inline QString inferRequirementSourceTable(const QVariantList& rawRequiredFields)
{
    if (rawRequiredFields.isEmpty()) {
        return {};
    }

    bool hasDailyBarField = false;
    bool hasFinancialField = false;
    bool hasSymbolInfoField = false;
    bool hasNewsField = false;
    bool hasPolicyField = false;
    bool hasAlternativeField = false;
    bool hasDerivativesField = false;
    bool hasUnknownField = false;

    for (const QVariant& fieldValue : rawRequiredFields) {
        const QString field = normalizeRequirementFieldName(fieldValue.toString());
        if (field.isEmpty()) {
            continue;
        }
        if (isDailyBarRequirementField(field)) {
            hasDailyBarField = true;
            continue;
        }
        if (isFinancialRequirementField(field)) {
            hasFinancialField = true;
            continue;
        }
        if (isSymbolInfoRequirementField(field)) {
            hasSymbolInfoField = true;
            continue;
        }
        if (isNewsRequirementField(field)) {
            hasNewsField = true;
            continue;
        }
        if (isPolicyRequirementField(field)) {
            hasPolicyField = true;
            continue;
        }
        if (isAlternativeRequirementField(field)) {
            hasAlternativeField = true;
            continue;
        }
        if (isDerivativesRequirementField(field)) {
            hasDerivativesField = true;
            continue;
        }
        hasUnknownField = true;
    }

    if (hasUnknownField) {
        return {};
    }

    if (hasFinancialField && !hasDailyBarField && !hasSymbolInfoField && !hasNewsField
            && !hasPolicyField && !hasAlternativeField && !hasDerivativesField) {
        return QStringLiteral("financial_indicator");
    }
    if (hasSymbolInfoField && !hasDailyBarField && !hasFinancialField && !hasNewsField
            && !hasPolicyField && !hasAlternativeField && !hasDerivativesField) {
        return QStringLiteral("symbol_info");
    }
    if (hasNewsField && !hasDailyBarField && !hasFinancialField && !hasSymbolInfoField
            && !hasPolicyField && !hasAlternativeField && !hasDerivativesField) {
        return QStringLiteral("news_sentiment");
    }
    if (hasPolicyField && !hasDailyBarField && !hasFinancialField && !hasSymbolInfoField
            && !hasNewsField && !hasAlternativeField && !hasDerivativesField) {
        return QStringLiteral("policy_data");
    }
    if (hasAlternativeField && !hasDailyBarField && !hasFinancialField && !hasSymbolInfoField
            && !hasNewsField && !hasPolicyField && !hasDerivativesField) {
        return QStringLiteral("alternative_data");
    }
    if (hasDerivativesField && !hasDailyBarField && !hasFinancialField && !hasSymbolInfoField
            && !hasNewsField && !hasPolicyField && !hasAlternativeField) {
        return QStringLiteral("derivatives_data");
    }
    if (hasDailyBarField && !hasFinancialField && !hasSymbolInfoField && !hasNewsField
            && !hasPolicyField && !hasAlternativeField && !hasDerivativesField) {
        return QStringLiteral("daily_bar");
    }
    return {};
}

}  // namespace factor::bridge