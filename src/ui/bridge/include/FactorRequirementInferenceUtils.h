#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

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
        return QStringLiteral("total_revenue");
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

struct FactorRequirementProfile
{
    QString factorType;
    QString metric;
    QVariantList requiredFields;
    QVariantList optionalFields;
    QString sourceTable;
    bool supported{false};
};

struct SupportMapRequirementResolution
{
    QStringList requiredFields;
    QString explicitSourceTable;
    QString failureCategory;
    QString failureReason;
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
    insertString(QStringLiteral("qualityMetric"), {"qualityMetric"});
    insertStringList(QStringLiteral("growthMetrics"), {"growthMetrics"});
    const QVariantList growthWeights = jsonNumberListRequirementValue(info.config, {"growthWeights"});
    if (!growthWeights.isEmpty()) {
        calculation.insert(QStringLiteral("growthWeights"), growthWeights);
    }
    insertString(QStringLiteral("dividendMetric"), {"dividendMetric"});
    insertStringList(QStringLiteral("dividendMetrics"), {"dividendMetrics"});
    insertString(QStringLiteral("macroMetric"), {"macroMetric"});
    insertStringList(QStringLiteral("macroDimensions"), {"macroDimensions"});
    insertStringList(QStringLiteral("macroIndicators"), {"macroIndicators"});
    insertString(QStringLiteral("macroFrequency"), {"macroFrequency"});
    insertString(QStringLiteral("macroWindow"), {"macroWindow"});
    insertString(QStringLiteral("sentimentSource"), {"sentimentSource"});
    insertString(QStringLiteral("sentimentMetric"), {"sentimentMetric"});
    insertStringList(QStringLiteral("technicalIndicators"), {"technicalIndicators"});
    insertString(QStringLiteral("technicalPriceType"), {"technicalPriceType"});
    insertString(QStringLiteral("turnoverStabilityMetric"), {"turnoverStabilityMetric"});
    insertString(QStringLiteral("liquidityMetric"), {"liquidityMetric"});
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
    if (normalized == QStringLiteral("close")) {
        return QStringLiteral("close");
    }
    if (normalized == QStringLiteral("adj_close")) {
        return QStringLiteral("adj_close");
    }
    if (normalized == QStringLiteral("open")) {
        return QStringLiteral("open");
    }
    if (normalized == QStringLiteral("high")) {
        return QStringLiteral("high");
    }
    if (normalized == QStringLiteral("low")) {
        return QStringLiteral("low");
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
    if (metric == QStringLiteral("dividend_yield")) {
        return QStringLiteral("dividend_yield");
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
    if (metric == QStringLiteral("market_cap") || rawMetric.startsWith(QString::fromUtf8("总市值"))) {
        return QStringLiteral("market_cap");
    }
    if (metric == QStringLiteral("circulating_market_cap") || rawMetric.startsWith(QString::fromUtf8("流通市值"))) {
        return QStringLiteral("circulating_market_cap");
    }
    if (metric == QStringLiteral("total_assets") || rawMetric.startsWith(QString::fromUtf8("总资产"))) {
        return QStringLiteral("total_assets");
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
    if (metric == QStringLiteral("roe") || rawMetric == QString::fromUtf8("净资产收益率")) {
        return QStringLiteral("roe");
    }
    if (metric == QStringLiteral("roa") || rawMetric == QString::fromUtf8("总资产收益率")) {
        return QStringLiteral("roa");
    }
    if (metric == QStringLiteral("gross_margin") || metric == QStringLiteral("operating_margin")
            || rawMetric == QString::fromUtf8("毛利率")
            || rawMetric == QString::fromUtf8("营业利润率")
            || rawMetric == QString::fromUtf8("利润率")) {
        return metric == QStringLiteral("operating_margin") ? QStringLiteral("operating_margin") : QStringLiteral("gross_margin");
    }
    if (metric == QStringLiteral("profit_margin")) {
        return QStringLiteral("gross_margin");
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
    return normalizeRequirementSourceTable(rawSource);
}

inline QString normalizeSentimentMetricText(const QString& rawMetric)
{
    const QString metric = rawMetric.trimmed().toLower();
    if (metric.isEmpty()) {
        return {};
    }
    if (metric == QStringLiteral("sentiment_score") || metric == QStringLiteral("news_sentiment") || metric == QString::fromUtf8("新闻情绪")) {
        return QStringLiteral("sentiment_score");
    }
    if (metric == QStringLiteral("social_sentiment") || metric == QStringLiteral("social_media") || metric == QString::fromUtf8("社交媒体")) {
        return QStringLiteral("social_sentiment");
    }
    if (metric == QStringLiteral("investor_sentiment") || metric == QStringLiteral("analyst_rating") || metric == QString::fromUtf8("分析师评级")) {
        return QStringLiteral("investor_sentiment");
    }
    if (metric == QStringLiteral("market_sentiment") || metric == QString::fromUtf8("市场情绪")) {
        return QStringLiteral("market_sentiment");
    }
    if (metric == QStringLiteral("policy_score") || metric == QStringLiteral("policy")) {
        return QStringLiteral("policy_score");
    }
    if (metric == QStringLiteral("hot_rank") || metric == QStringLiteral("alternative")) {
        return QStringLiteral("hot_rank");
    }
    if (metric == QStringLiteral("basis_rate") || metric == QStringLiteral("derivatives")) {
        return QStringLiteral("basis_rate");
    }
    return metric;
}

inline QString defaultSentimentMetricForSource(const QString& rawSource)
{
    const QString source = normalizeSentimentSourceText(rawSource);
    if (source == QStringLiteral("social_media")) {
        return QStringLiteral("social_sentiment");
    }
    if (source == QStringLiteral("investor_sentiment")) {
        return QStringLiteral("investor_sentiment");
    }
    if (source == QStringLiteral("market_sentiment")) {
        return QStringLiteral("market_sentiment");
    }
    if (source == QStringLiteral("policy_data")) {
        return QStringLiteral("policy_score");
    }
    if (source == QStringLiteral("alternative_data")) {
        return QStringLiteral("hot_rank");
    }
    if (source == QStringLiteral("derivatives_data")) {
        return QStringLiteral("basis_rate");
    }
    return QStringLiteral("sentiment_score");
}

inline QString inferSentimentSourceFromMetric(const QString& rawMetric)
{
    const QString metric = normalizeSentimentMetricText(rawMetric);
    if (metric == QStringLiteral("social_sentiment")) {
        return QStringLiteral("social_media");
    }
    if (metric == QStringLiteral("investor_sentiment")) {
        return QStringLiteral("investor_sentiment");
    }
    if (metric == QStringLiteral("market_sentiment")) {
        return QStringLiteral("market_sentiment");
    }
    if (metric == QStringLiteral("policy_score")) {
        return QStringLiteral("policy_data");
    }
    if (metric == QStringLiteral("hot_rank")) {
        return QStringLiteral("alternative_data");
    }
    if (metric == QStringLiteral("basis_rate")) {
        return QStringLiteral("derivatives_data");
    }
    if (metric == QStringLiteral("sentiment_score")) {
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
        if (metric == QStringLiteral("dividend_yield") || rawMetric.startsWith(QString::fromUtf8("股息率"))) {
            return QStringLiteral("dividend_yield");
        }
        if (metric == QStringLiteral("payout_ratio")
                || rawMetric.startsWith(QString::fromUtf8("股利支付率"))
                || rawMetric.startsWith(QString::fromUtf8("派息率"))) {
            return QStringLiteral("payout_ratio");
        }
        if (metric == QStringLiteral("dividend_stability")
                || rawMetric.startsWith(QString::fromUtf8("分红稳定性"))
                || rawMetric.startsWith(QString::fromUtf8("股息稳定性"))) {
            return QStringLiteral("dividend_stability");
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
        appendMetrics(calculation.value(QStringLiteral("dividendMetric")));
    }
    if (metrics.isEmpty()) {
        appendMetrics(calculation.value(QStringLiteral("metric")));
    }

    if (metrics.isEmpty()) {
        metrics.append(QStringLiteral("dividend_yield"));
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

    if (dimensions.isEmpty()) {
        appendDimension(normalizeMacroRequirementDimension(calculation.value(QStringLiteral("macroMetric")).toString()));
    }

    if (dimensions.isEmpty()) {
        appendDimension(QStringLiteral("growth"));
        appendDimension(QStringLiteral("inflation"));
        appendDimension(QStringLiteral("credit"));
        appendDimension(QStringLiteral("rates"));
        appendDimension(QStringLiteral("policy"));
        appendDimension(QStringLiteral("risk_appetite"));
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
        const QString canonicalMetric = normalizeMacroRequirementDimension(calculation.value(QStringLiteral("macroMetric")).toString());
        const QStringList dimensions = canonicalMetric.isEmpty() ? normalizeMacroRequirementDimensions(calculation) : QStringList{canonicalMetric};
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
        return {QStringLiteral("close")};
    }
    if (indicator == QStringLiteral("m2_yoy")
            || indicator == QStringLiteral("social_financing_stock_yoy")
            || indicator == QStringLiteral("m1_m2_spread")) {
        return {QStringLiteral("close"), QStringLiteral("turnover_rate")};
    }
    if (indicator == QStringLiteral("ten_year_bond_yield") || indicator == QStringLiteral("shibor_3m")) {
        return {QStringLiteral("close"), QStringLiteral("pe_ratio")};
    }
    if (indicator == QStringLiteral("lpr_1y") || indicator == QStringLiteral("reserve_requirement_ratio")) {
        return {QStringLiteral("close"), QStringLiteral("pb_ratio")};
    }
    if (indicator == QStringLiteral("aa_credit_spread") || indicator == QStringLiteral("vix_proxy")) {
        return {QStringLiteral("close"), QStringLiteral("volume")};
    }
    return {QStringLiteral("close")};
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
        fields.append(QStringLiteral("close"));
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
                if (!profile.requiredFields.contains(QStringLiteral("pb_ratio"))) {
                    profile.requiredFields.append(QStringLiteral("pb_ratio"));
                }
            } else if (metric == QStringLiteral("ep")) {
                if (!profile.requiredFields.contains(QStringLiteral("pe_ratio"))) {
                    profile.requiredFields.append(QStringLiteral("pe_ratio"));
                }
            } else if (metric == QStringLiteral("dividend_yield")) {
                if (!profile.requiredFields.contains(QStringLiteral("dividend_yield"))) {
                    profile.requiredFields.append(QStringLiteral("dividend_yield"));
                }
            } else if (metric == QStringLiteral("cf_p")) {
                if (!profile.requiredFields.contains(QStringLiteral("market_cap"))) {
                    profile.requiredFields.append(QStringLiteral("market_cap"));
                }
                if (!profile.requiredFields.contains(QStringLiteral("operating_cash_flow"))) {
                    profile.requiredFields.append(QStringLiteral("operating_cash_flow"));
                }
            } else {
                return profile;
            }
        }
        profile.metric = normalizedMetrics.join(QStringLiteral(","));
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("momentum")) {
        profile.requiredFields.append(QStringLiteral("close"));
        profile.optionalFields.append(QStringLiteral("adj_factor"));
        if (calculation.value(QStringLiteral("useVolume")).toBool()) {
            profile.optionalFields.append(QStringLiteral("volume"));
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("size")) {
        profile.metric = normalizeSizeRequirementMetric(
            calculation.value(QStringLiteral("sizeMetric"), QStringLiteral("market_cap")).toString());
        if (profile.metric.isEmpty()) {
            profile.metric = QStringLiteral("market_cap");
        }
        if (profile.metric == QStringLiteral("market_cap")) {
            profile.requiredFields.append(QStringLiteral("market_cap"));
        } else if (profile.metric == QStringLiteral("circulating_market_cap")) {
            profile.requiredFields.append(QStringLiteral("circulating_market_cap"));
        } else if (profile.metric == QStringLiteral("total_assets")) {
            profile.requiredFields.append(QStringLiteral("total_assets"));
        } else {
            return profile;
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("quality")) {
        profile.metric = normalizeQualityRequirementMetric(
            calculation.value(QStringLiteral("metric"), calculation.value(QStringLiteral("qualityMetric"), QStringLiteral("roe"))).toString());
        if (profile.metric == QStringLiteral("roe")) {
            profile.requiredFields.append(QStringLiteral("roe"));
        } else if (profile.metric == QStringLiteral("roa")) {
            profile.requiredFields.append(QStringLiteral("roa"));
        } else if (profile.metric == QStringLiteral("gross_margin") || profile.metric == QStringLiteral("operating_margin")) {
            profile.requiredFields.append(QStringLiteral("profit_margin"));
        } else {
            profile.requiredFields.append(QStringLiteral("net_profit"));
            profile.requiredFields.append(QStringLiteral("equity"));
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
                profile.requiredFields.append(QStringLiteral("total_revenue"));
            } else if (metric == QStringLiteral("net_profit_growth")) {
                profile.requiredFields.append(QStringLiteral("net_profit"));
            } else if (metric == QStringLiteral("delta_roe")) {
                profile.requiredFields.append(QStringLiteral("roe"));
            } else if (metric == QStringLiteral("sue")) {
                profile.requiredFields.append(QStringLiteral("eps"));
            } else {
                profile.requiredFields.append(QStringLiteral("total_revenue"));
            }
        }
        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            profile.requiredFields.append(QStringLiteral("industry_code"));
            profile.requiredFields.append(QStringLiteral("market_cap"));
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("dividend")) {
        const QStringList metrics = normalizeDividendRequirementMetrics(calculation);
        profile.metric = metrics.isEmpty() ? QStringLiteral("dividend_yield") : metrics.first();

        bool needsFinancialIndicator = false;
        for (const QString& metric : metrics) {
            if (metric == QStringLiteral("payout_ratio")) {
                profile.requiredFields.append(QStringLiteral("payout_ratio"));
                needsFinancialIndicator = true;
            } else if (metric == QStringLiteral("dividend_stability")) {
                profile.requiredFields.append(QStringLiteral("dividend_stability"));
            } else {
                profile.requiredFields.append(QStringLiteral("dividend_yield"));
            }
        }

        if (profile.requiredFields.isEmpty()) {
            profile.requiredFields.append(QStringLiteral("dividend_yield"));
            profile.metric = QStringLiteral("dividend_yield");
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
        if (configuredIndicators.isEmpty()) {
            return profile;
        }
        for (const QVariant& configuredIndicator : configuredIndicators) {
            const QString normalizedIndicator = normalizeTechnicalRequirementIndicator(configuredIndicator.toString());
            if (normalizedIndicator.isEmpty() || indicators.contains(normalizedIndicator)) {
                return profile;
            }
            indicators.append(normalizedIndicator);
        }

        const bool needHighLowSeries = indicators.contains(QStringLiteral("kdj"))
            || indicators.contains(QStringLiteral("atr"));
        const bool needVolumeSeries = indicators.contains(QStringLiteral("obv"))
            || indicators.contains(QStringLiteral("vwap"))
            || indicators.contains(QStringLiteral("volume_ratio"));
        const bool needPriceSeries = indicators.contains(QStringLiteral("rsi"))
            || indicators.contains(QStringLiteral("macd"))
            || indicators.contains(QStringLiteral("ma"))
            || indicators.contains(QStringLiteral("ema"))
            || indicators.contains(QStringLiteral("boll"))
            || indicators.contains(QStringLiteral("kdj"))
            || indicators.contains(QStringLiteral("atr"))
            || indicators.contains(QStringLiteral("obv"))
            || indicators.contains(QStringLiteral("vwap"));
        const bool needTurnoverSeries = indicators.contains(QStringLiteral("turnover_stability"));
        const QString turnoverMetric = calculation.value(QStringLiteral("turnoverStabilityMetric"), QStringLiteral("turnover_rate"))
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
            profile.requiredFields.append(QStringLiteral("high"));
            profile.requiredFields.append(QStringLiteral("low"));
        }
        if (needVolumeSeries) {
            profile.requiredFields.append(QStringLiteral("volume"));
        }
        if (needTurnoverSeries) {
            profile.requiredFields.append(turnoverMetric == QStringLiteral("volume")
                ? QStringLiteral("volume")
                : QStringLiteral("turnover_rate"));
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("liquidity")) {
        profile.metric = calculation.value(QStringLiteral("liquidityMetric"), QStringLiteral("turnover_rate"))
            .toString()
            .trimmed()
            .toLower();
        if (profile.metric == QStringLiteral("volume")) {
            profile.requiredFields.append(QStringLiteral("volume"));
        } else if (profile.metric == QStringLiteral("amplitude")) {
            profile.requiredFields.append(QStringLiteral("amplitude"));
        } else if (profile.metric == QStringLiteral("amihud_illiquidity")) {
            profile.requiredFields.append(QStringLiteral("close"));
            profile.requiredFields.append(QStringLiteral("volume"));
            profile.optionalFields.append(QStringLiteral("turnover"));
        } else {
            profile.metric = QStringLiteral("turnover_rate");
            profile.requiredFields.append(QStringLiteral("turnover_rate"));
        }
        if (calculation.value(QStringLiteral("neutralizationEnabled")).toBool()) {
            profile.requiredFields.append(QStringLiteral("industry_code"));
            profile.requiredFields.append(QStringLiteral("market_cap"));
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("macro")) {
        const QVariantList indicators = normalizeRequirementList(calculation.value(QStringLiteral("macroIndicators")));
        const QStringList normalizedIndicators = normalizeMacroRequirementIndicators(calculation);
        profile.metric = normalizedIndicators.isEmpty() ? QStringLiteral("industrial_added_value_yoy") : normalizedIndicators.first();
        profile.requiredFields = macroRequirementFieldsForIndicators(calculation);
        profile.sourceTable = QStringLiteral("daily_bar");
        Q_UNUSED(calculation.value(QStringLiteral("macroWindow")));
        Q_UNUSED(indicators);
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("industry")) {
        profile.metric = calculation.value(QStringLiteral("industryMetric")).toString().trimmed().toLower();
        profile.supported = false;
        return profile;
    }

    if (profile.factorType == QStringLiteral("sentiment")) {
        QString source = normalizeSentimentSourceText(
            calculation.value(QStringLiteral("sentimentSource")).toString());
        profile.metric = normalizeSentimentMetricText(
            calculation.value(QStringLiteral("metric"), calculation.value(QStringLiteral("sentimentMetric"))).toString());
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
            profile.metric = QStringLiteral("sentiment_score");
        }
        profile.requiredFields.append(normalizeRequirementFieldName(profile.metric));
        profile.sourceTable = resolveSentimentSourceTable(
            QVariantMap{{QStringLiteral("sentimentSource"), source}});
        if (profile.sourceTable.isEmpty()) {
            profile.sourceTable = inferRequirementSourceTable(profile.requiredFields);
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("low_volatility")) {
        profile.requiredFields.append(QStringLiteral("close"));
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
    QStringList requiredFields = normalizeRequirementFieldNames(profile.requiredFields);
    if (runtimeType == QStringLiteral("value")
        && profile.metric.split(QStringLiteral(","), Qt::SkipEmptyParts).contains(QStringLiteral("cf_p"))) {
        requiredFields.removeAll(QStringLiteral("operating_cash_flow"));
        if (!requiredFields.contains(QStringLiteral("total_revenue"))) {
            requiredFields.append(QStringLiteral("total_revenue"));
        }
    }
    return requiredFields;
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

    if (runtimeType == QStringLiteral("industry")) {
        resolution.failureCategory = QStringLiteral("proxy-only-runtime");
        resolution.failureReason = QStringLiteral("当前行业因子运行时尚未实现，已禁止进入回测");
        return resolution;
    }

    if (runtimeType == QStringLiteral("technical") && !profile.supported) {
        resolution.failureCategory = QStringLiteral("unsupported-metric");
        resolution.failureReason = QStringLiteral("技术因子缺少合法的 technicalIndicators 或 technicalPriceType 配置");
        return resolution;
    }

    if (runtimeType == QStringLiteral("technical") && profile.supported && !profile.requiredFields.isEmpty()) {
        resolution.requiredFields = supportMapRequirementFieldsForProfile(runtimeType, profile);
    } else {
        resolution.requiredFields = normalizeRequirementFieldNames(configuredFields);
        if (resolution.requiredFields.isEmpty() && profile.supported && !profile.requiredFields.isEmpty()) {
            resolution.requiredFields = supportMapRequirementFieldsForProfile(runtimeType, profile);
        }
    }

    if (resolution.requiredFields.isEmpty()) {
        resolution.failureCategory = QStringLiteral("missing-field");
        resolution.failureReason = QStringLiteral("因子配置缺少可用于支持校验的必需字段");
        return resolution;
    }

    resolution.explicitSourceTable = supportMapRequirementSourceTableForProfile(runtimeType, profile);
    return resolution;
}

inline bool isDailyBarRequirementField(const QString& rawField)
{
    static const QSet<QString> dailyBarFields{
        QStringLiteral("open"),
        QStringLiteral("high"),
        QStringLiteral("low"),
        QStringLiteral("close"),
        QStringLiteral("adj_close"),
        QStringLiteral("adj_factor"),
        QStringLiteral("pre_close"),
        QStringLiteral("volume"),
        QStringLiteral("turnover"),
        QStringLiteral("change_pct"),
        QStringLiteral("change_amt"),
        QStringLiteral("amplitude"),
        QStringLiteral("turnover_rate"),
        QStringLiteral("pe_ratio"),
        QStringLiteral("pb_ratio"),
        QStringLiteral("market_cap"),
        QStringLiteral("circulating_market_cap"),
        QStringLiteral("dividend_yield")
    };
    return dailyBarFields.contains(normalizeRequirementFieldName(rawField));
}

inline bool isFinancialRequirementField(const QString& rawField)
{
    static const QSet<QString> financialFields{
        QStringLiteral("bps"),
        QStringLiteral("roe"),
        QStringLiteral("roa"),
        QStringLiteral("profit_margin"),
        QStringLiteral("gross_margin"),
        QStringLiteral("operating_margin"),
        QStringLiteral("net_profit"),
        QStringLiteral("equity"),
        QStringLiteral("total_assets"),
        QStringLiteral("eps"),
        QStringLiteral("total_revenue"),
        QStringLiteral("total_liabilities"),
        QStringLiteral("debt_to_equity"),
        QStringLiteral("current_ratio"),
        QStringLiteral("quick_ratio"),
        QStringLiteral("operating_cash_flow"),
        QStringLiteral("investing_cash_flow"),
        QStringLiteral("financing_cash_flow"),
        QStringLiteral("payout_ratio")
    };
    return financialFields.contains(normalizeRequirementFieldName(rawField));
}

inline bool isSymbolInfoRequirementField(const QString& rawField)
{
    static const QSet<QString> symbolInfoFields{
        QStringLiteral("industry"),
        QStringLiteral("industry_code"),
        QStringLiteral("exchange"),
        QStringLiteral("asset_class"),
        QStringLiteral("status"),
        QStringLiteral("list_date"),
        QStringLiteral("name")
    };
    return symbolInfoFields.contains(normalizeRequirementFieldName(rawField));
}

inline bool isNewsRequirementField(const QString& rawField)
{
    static const QSet<QString> newsFields{
        QStringLiteral("sentiment_score"),
        QStringLiteral("market_sentiment"),
        QStringLiteral("investor_sentiment"),
        QStringLiteral("sector_sentiment"),
        QStringLiteral("theme_sentiment"),
        QStringLiteral("social_sentiment"),
        QStringLiteral("news_count")
    };
    return newsFields.contains(normalizeRequirementFieldName(rawField));
}

inline bool isPolicyRequirementField(const QString& rawField)
{
    static const QSet<QString> policyFields{
        QStringLiteral("policy_score"),
        QStringLiteral("policy_strength"),
        QStringLiteral("policy_count")
    };
    return policyFields.contains(normalizeRequirementFieldName(rawField));
}

inline bool isAlternativeRequirementField(const QString& rawField)
{
    static const QSet<QString> alternativeFields{
        QStringLiteral("hot_rank"),
        QStringLiteral("popularity_score"),
        QStringLiteral("comment_count"),
        QStringLiteral("comment_sentiment")
    };
    return alternativeFields.contains(normalizeRequirementFieldName(rawField));
}

inline bool isDerivativesRequirementField(const QString& rawField)
{
    static const QSet<QString> derivativesFields{
        QStringLiteral("futures_close"),
        QStringLiteral("futures_volume"),
        QStringLiteral("open_interest"),
        QStringLiteral("basis"),
        QStringLiteral("basis_rate")
    };
    return derivativesFields.contains(normalizeRequirementFieldName(rawField));
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
            || source == QStringLiteral("investor_sentiment")
            || source == QStringLiteral("market_sentiment")
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
        hasDailyBarField = true;
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