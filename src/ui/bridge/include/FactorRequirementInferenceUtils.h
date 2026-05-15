#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QMetaType>
#include <QVariant>
#include <QVariantMap>

#include <optional>
#include <vector>

#include "DataFetchFieldContractUtils.h"
#include "../../../foundation/include/foundation/json/json_facade.h"

#include "../../../domain/factor/include/FactorInstanceManager.h"
#include "../../../domain/factor/include/factor_enums.h"

namespace factor::bridge {

inline bool strictVariantInt(const QVariant& value, int& parsedValue)
{
    switch (value.userType()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
        parsedValue = value.toInt();
        return true;
    default:
        return false;
    }
}

template <typename EnumType>
inline bool strictVariantEnum(const QVariant& value,
                              const int minValue,
                              const int maxValue,
                              EnumType& parsedValue)
{
    int rawValue = 0;
    if (!strictVariantInt(value, rawValue)) {
        return false;
    }
    if (rawValue < minValue || rawValue > maxValue) {
        return false;
    }

    parsedValue = static_cast<EnumType>(rawValue);
    return true;
}

inline QStringList normalizeRequirementFieldNames(const QStringList& rawFields)
{
    QStringList result;
    QSet<QString> seenFields;
    for (const QString& field : rawFields) {
        const QString normalized = canonicalContractFieldName(field);
        if (normalized.isEmpty() || seenFields.contains(normalized)) {
            continue;
        }
        seenFields.insert(normalized);
        result.append(normalized);
    }
    return result;
}

class RequirementJsonFacadeQtAdapter final
{
public:
    static QVariant toVariant(const foundation::json::JsonFacade& value)
    {
        if (value.isNull()) {
            return {};
        }
        if (value.isBool()) {
            return value.asBool();
        }
        if (value.isNumber()) {
            return value.isInteger() ? QVariant(value.asInt()) : QVariant(value.asDouble());
        }
        if (value.isString()) {
            return QString::fromStdString(value.asString());
        }
        if (value.isArray()) {
            QVariantList result;
            result.reserve(static_cast<int>(value.size()));
            for (size_t index = 0; index < value.size(); ++index) {
                result.append(toVariant(value.at(index)));
            }
            return result;
        }
        if (value.isObject()) {
            QVariantMap result;
            for (const std::string& key : value.keys()) {
                result.insert(QString::fromStdString(key), toVariant(value.get(key)));
            }
            return result;
        }
        return {};
    }

    static QVariantMap toVariantMap(const foundation::json::JsonFacade& value)
    {
        return toVariant(value).toMap();
    }

private:
    RequirementJsonFacadeQtAdapter() = default;
};

class RequirementCalculationView final {
public:
    explicit RequirementCalculationView(const QVariantMap& calculation)
        : m_calculation(calculation)
    {
    }

    std::vector<factor::ValuationMetric> valuationMetrics() const
    {
        return parseEnumList<factor::ValuationMetric>(
            QStringLiteral("valuationMetrics"),
            static_cast<int>(factor::ValuationMetric::BP),
            static_cast<int>(factor::ValuationMetric::CFP));
    }

    std::vector<factor::GrowthMetric> growthMetrics() const
    {
        return parseEnumList<factor::GrowthMetric>(
            QStringLiteral("growthMetrics"),
            static_cast<int>(factor::GrowthMetric::REVENUE_GROWTH),
            static_cast<int>(factor::GrowthMetric::SUE));
    }

    int growthWeightsCount() const
    {
        return value(QStringLiteral("growthWeights")).toList().size();
    }

    std::optional<factor::QualityMetric> qualityMetric() const
    {
        return parseEnum<factor::QualityMetric>(
            QStringLiteral("metric"),
            static_cast<int>(factor::QualityMetric::ROE),
            static_cast<int>(factor::QualityMetric::EARNINGS_QUALITY));
    }

    std::optional<factor::DividendMetric> dividendMetric() const
    {
        return parseEnum<factor::DividendMetric>(
            QStringLiteral("metric"),
            static_cast<int>(factor::DividendMetric::DIVIDEND_YIELD),
            static_cast<int>(factor::DividendMetric::DIVIDEND_STABILITY));
    }

    std::vector<factor::DividendMetric> dividendMetrics() const
    {
        return parseEnumList<factor::DividendMetric>(
            QStringLiteral("dividendMetrics"),
            static_cast<int>(factor::DividendMetric::DIVIDEND_YIELD),
            static_cast<int>(factor::DividendMetric::DIVIDEND_STABILITY));
    }

    std::optional<factor::SentimentMetric> sentimentMetric() const
    {
        return parseEnum<factor::SentimentMetric>(
            QStringLiteral("metric"),
            static_cast<int>(factor::SentimentMetric::SENTIMENT_SCORE),
            static_cast<int>(factor::SentimentMetric::MARKET_SENTIMENT));
    }

    std::optional<factor::SentimentSource> sentimentSource() const
    {
        return parseEnum<factor::SentimentSource>(
            QStringLiteral("sentimentSource"),
            static_cast<int>(factor::SentimentSource::NEWS),
            static_cast<int>(factor::SentimentSource::DERIVATIVES));
    }

    std::optional<factor::SizeMetric> sizeMetric() const
    {
        return parseEnum<factor::SizeMetric>(
            QStringLiteral("sizeMetric"),
            static_cast<int>(factor::SizeMetric::MARKET_CAP),
            static_cast<int>(factor::SizeMetric::TOTAL_ASSETS));
    }

private:
    template <typename EnumType>
    std::optional<EnumType> parseEnum(const QString& key,
                                      const int minValue,
                                      const int maxValue) const
    {
        EnumType parsedValue = static_cast<EnumType>(maxValue + 1);
        if (!strictVariantEnum(value(key), minValue, maxValue, parsedValue)) {
            return std::nullopt;
        }
        return parsedValue;
    }

    template <typename EnumType>
    std::vector<EnumType> parseEnumList(const QString& key,
                                        const int minValue,
                                        const int maxValue) const
    {
        const QVariantList rawValues = value(key).toList();
        if (rawValues.isEmpty()) {
            return {};
        }

        std::vector<EnumType> parsedValues;
        parsedValues.reserve(rawValues.size());
        for (const QVariant& rawValue : rawValues) {
            EnumType parsedValue = static_cast<EnumType>(maxValue + 1);
            if (!strictVariantEnum(rawValue, minValue, maxValue, parsedValue)) {
                return {};
            }
            parsedValues.push_back(parsedValue);
        }
        return parsedValues;
    }

    QVariant value(const QString& key) const
    {
        return m_calculation.value(key);
    }

    const QVariantMap& m_calculation;
};

struct FactorRequirementProfile
{
    factor::FactorType factorType{factor::FactorType::UNKNOWN};
    QVariant metric;
    FieldKeySet requiredFields;
    FieldKeySet optionalFields;
    factor::SourceTable sourceTable{factor::SourceTable::UNKNOWN};
    bool supported{false};
    bool allowEmptyRequiredFields{false};
};

struct SupportMapRequirementResolution
{
    QStringList requiredFields;
    factor::SourceTable explicitSourceTable{factor::SourceTable::UNKNOWN};
    QString failureCategory;
    QString failureReason;
    bool allowEmptyRequiredFields{false};
};

class RequirementFieldMapper final
{
public:
    static const RequirementFieldMapper& instance()
    {
        static const RequirementFieldMapper mapper;
        return mapper;
    }

    FieldKey valuation(const factor::ValuationMetric metric) const
    {
        switch (metric) {
        case factor::ValuationMetric::BP:
            return MarketBarFieldKeys::PB_RATIO;
        case factor::ValuationMetric::EP:
            return MarketBarFieldKeys::PE_RATIO;
        case factor::ValuationMetric::DIVIDEND_YIELD:
            return FinancialFieldKeys::DIVIDEND_YIELD;
        case factor::ValuationMetric::CFP:
            return FinancialFieldKeys::OPERATING_CASH_FLOW;
        default:
            return FieldKey{""};
        }
    }

    FieldKey growth(const factor::GrowthMetric metric) const
    {
        switch (metric) {
        case factor::GrowthMetric::REVENUE_GROWTH:
            return FinancialFieldKeys::TOTAL_REVENUE;
        case factor::GrowthMetric::NET_PROFIT_GROWTH:
            return FinancialFieldKeys::NET_PROFIT;
        case factor::GrowthMetric::DELTA_ROE:
            return FinancialFieldKeys::ROE;
        case factor::GrowthMetric::SUE:
            return FinancialFieldKeys::EPS;
        default:
            return FieldKey{""};
        }
    }

    FieldKey quality(const factor::QualityMetric metric) const
    {
        switch (metric) {
        case factor::QualityMetric::ROE:
            return FinancialFieldKeys::ROE;
        case factor::QualityMetric::ROA:
            return FinancialFieldKeys::ROA;
        case factor::QualityMetric::GROSS_MARGIN:
            return FinancialFieldKeys::GROSS_MARGIN;
        case factor::QualityMetric::OPERATING_MARGIN:
            return FinancialFieldKeys::OPERATING_MARGIN;
        case factor::QualityMetric::EARNINGS_QUALITY:
            return RequirementAliasFieldKeys::EARNINGS_QUALITY;
        default:
            return FieldKey{""};
        }
    }

    FieldKey dividend(const factor::DividendMetric metric) const
    {
        switch (metric) {
        case factor::DividendMetric::DIVIDEND_YIELD:
            return FinancialFieldKeys::DIVIDEND_YIELD;
        case factor::DividendMetric::PAYOUT_RATIO:
            return FinancialFieldKeys::PAYOUT_RATIO;
        case factor::DividendMetric::DIVIDEND_STABILITY:
            return FinancialFieldKeys::DIVIDEND_STABILITY;
        default:
            return FieldKey{""};
        }
    }

    FieldKey size(const factor::SizeMetric metric) const
    {
        switch (metric) {
        case factor::SizeMetric::MARKET_CAP:
            return MarketBarFieldKeys::MARKET_CAP;
        case factor::SizeMetric::CIRCULATING_MARKET_CAP:
            return MarketBarFieldKeys::CIRCULATING_MARKET_CAP;
        case factor::SizeMetric::TOTAL_ASSETS:
            return FinancialFieldKeys::TOTAL_ASSETS;
        default:
            return FieldKey{""};
        }
    }

    FieldKey sentiment(const factor::SentimentMetric metric) const
    {
        switch (metric) {
        case factor::SentimentMetric::SENTIMENT_SCORE:
            return NewsFieldKeys::SENTIMENT_SCORE;
        case factor::SentimentMetric::SOCIAL_SENTIMENT:
            return NewsFieldKeys::SOCIAL_SENTIMENT;
        case factor::SentimentMetric::INVESTOR_SENTIMENT:
            return NewsFieldKeys::INVESTOR_SENTIMENT;
        case factor::SentimentMetric::MARKET_SENTIMENT:
            return NewsFieldKeys::MARKET_SENTIMENT;
        default:
            return FieldKey{""};
        }
    }

private:
    RequirementFieldMapper() = default;
};

class RequirementSourceTableResolver final
{
public:
    static const RequirementSourceTableResolver& instance()
    {
        static const RequirementSourceTableResolver resolver;
        return resolver;
    }

    factor::SourceTable fromSentimentSource(const factor::SentimentSource sentimentSource) const
    {
        switch (sentimentSource) {
        case factor::SentimentSource::NEWS:
        case factor::SentimentSource::SOCIAL_MEDIA:
        case factor::SentimentSource::ANALYST_RATING:
        case factor::SentimentSource::MARKET:
            return factor::SourceTable::NEWS_SENTIMENT;
        case factor::SentimentSource::POLICY:
            return factor::SourceTable::POLICY_DATA;
        case factor::SentimentSource::ALTERNATIVE:
            return factor::SourceTable::ALTERNATIVE_DATA;
        case factor::SentimentSource::DERIVATIVES:
            return factor::SourceTable::DERIVATIVES_DATA;
        default:
            return factor::SourceTable::UNKNOWN;
        }
    }

    factor::SourceTable fromRequirementField(const QString& rawField) const
    {
        const QString field = canonicalContractFieldName(rawField);
        if (field.isEmpty()) {
            return factor::SourceTable::UNKNOWN;
        }
        if (marketBarFields().contains(field)) {
            return factor::SourceTable::DAILY_BAR;
        }
        if (financialFields().contains(field)) {
            return factor::SourceTable::FINANCIAL_INDICATOR;
        }
        if (newsFields().contains(field)) {
            return factor::SourceTable::NEWS_SENTIMENT;
        }
        if (policyFields().contains(field)) {
            return factor::SourceTable::POLICY_DATA;
        }
        if (alternativeFields().contains(field)) {
            return factor::SourceTable::ALTERNATIVE_DATA;
        }
        if (derivativesFields().contains(field)) {
            return factor::SourceTable::DERIVATIVES_DATA;
        }
        if (symbolInfoFields().contains(field)) {
            return factor::SourceTable::SYMBOL_INFO;
        }
        return factor::SourceTable::UNKNOWN;
    }

    factor::SourceTable fromRequirementField(const FieldKey& field) const
    {
        return fromRequirementField(field.toQString());
    }

    factor::SourceTable resolveSentiment(const factor::SentimentSource sentimentSource,
                                         const factor::SentimentMetric sentimentMetric) const
    {
        const factor::SourceTable explicitSourceTable = fromSentimentSource(sentimentSource);
        if (explicitSourceTable != factor::SourceTable::UNKNOWN) {
            return explicitSourceTable;
        }

        return fromRequirementField(RequirementFieldMapper::instance().sentiment(sentimentMetric));
    }

    factor::SourceTable infer(const FieldKeySet& requiredFields) const
    {
        factor::SourceTable inferred = factor::SourceTable::UNKNOWN;
        for (const QString& field : requiredFields.orderedValues()) {
            const factor::SourceTable source = fromRequirementField(field);
            if (source == factor::SourceTable::UNKNOWN) {
                return factor::SourceTable::UNKNOWN;
            }
            if (inferred == factor::SourceTable::UNKNOWN) {
                inferred = source;
                continue;
            }
            if (inferred != source) {
                return factor::SourceTable::UNKNOWN;
            }
        }
        return inferred;
    }

    factor::SourceTable infer(const QStringList& requiredFields) const
    {
        factor::SourceTable inferred = factor::SourceTable::UNKNOWN;
        for (const QString& field : normalizeRequirementFieldNames(requiredFields)) {
            const factor::SourceTable source = fromRequirementField(field);
            if (source == factor::SourceTable::UNKNOWN) {
                return factor::SourceTable::UNKNOWN;
            }
            if (inferred == factor::SourceTable::UNKNOWN) {
                inferred = source;
                continue;
            }
            if (inferred != source) {
                return factor::SourceTable::UNKNOWN;
            }
        }
        return inferred;
    }

    factor::SentimentSource inferSentimentSource(const factor::SentimentMetric metric) const
    {
        switch (metric) {
        case factor::SentimentMetric::SOCIAL_SENTIMENT:
            return factor::SentimentSource::SOCIAL_MEDIA;
        case factor::SentimentMetric::INVESTOR_SENTIMENT:
            return factor::SentimentSource::ANALYST_RATING;
        case factor::SentimentMetric::SENTIMENT_SCORE:
        case factor::SentimentMetric::MARKET_SENTIMENT:
            return factor::SentimentSource::NEWS;
        default:
            return factor::SentimentSource::UNKNOWN;
        }
    }

private:
    RequirementSourceTableResolver() = default;
};

class RequirementInstanceConfigView final
{
public:
    explicit RequirementInstanceConfigView(const factor::FactorInstanceInfo& info)
        : m_info(info)
    {
    }

    QVariantMap calculationMap() const
    {
        if (!m_info.config.isObject() || !m_info.config.has("calculation")) {
            return {};
        }

        const foundation::json::JsonFacade calculation = m_info.config.get("calculation");
        if (!calculation.isObject()) {
            return {};
        }

        return RequirementJsonFacadeQtAdapter::toVariantMap(calculation);
    }

    factor::FactorType runtimeType() const
    {
        return m_info.factorType;
    }

    QStringList configuredFields() const
    {
        if (!m_info.config.isObject() || !m_info.config.has("dataRequirements")) {
            return {};
        }

        const foundation::json::JsonFacade dataRequirements = m_info.config.get("dataRequirements");
        if (!dataRequirements.isObject() || !dataRequirements.has("required")) {
            return {};
        }

        const foundation::json::JsonFacade required = dataRequirements.get("required");
        if (!required.isArray()) {
            return {};
        }

        QStringList fields;
        fields.reserve(static_cast<int>(required.size()));
        for (size_t index = 0; index < required.size(); ++index) {
            const foundation::json::JsonFacade value = required.at(index);
            if (!value.isString()) {
                continue;
            }
            fields.append(QString::fromStdString(value.asString()));
        }

        return normalizeRequirementFieldNames(fields);
    }

    factor::SourceTable configuredSourceTable() const
    {
        if (!m_info.config.isObject() || !m_info.config.has("dataRequirements")) {
            return factor::SourceTable::UNKNOWN;
        }

        const foundation::json::JsonFacade dataRequirements = m_info.config.get("dataRequirements");
        if (!dataRequirements.isObject() || !dataRequirements.has("sourceTable")) {
            return factor::SourceTable::UNKNOWN;
        }

        const foundation::json::JsonFacade sourceTableValue = dataRequirements.get("sourceTable");
        if (!sourceTableValue.isNumber()) {
            return factor::SourceTable::UNKNOWN;
        }

        switch (static_cast<factor::SourceTable>(sourceTableValue.asInt())) {
        case factor::SourceTable::DAILY_BAR:
        case factor::SourceTable::FINANCIAL_INDICATOR:
        case factor::SourceTable::SYMBOL_INFO:
        case factor::SourceTable::NEWS_SENTIMENT:
        case factor::SourceTable::POLICY_DATA:
        case factor::SourceTable::ALTERNATIVE_DATA:
        case factor::SourceTable::DERIVATIVES_DATA:
            return static_cast<factor::SourceTable>(sourceTableValue.asInt());
        default:
            return factor::SourceTable::UNKNOWN;
        }
    }

private:
    const factor::FactorInstanceInfo& m_info;
};

class FactorRequirementResolver final
{
public:
    static const FactorRequirementResolver& instance()
    {
        static const FactorRequirementResolver resolver;
        return resolver;
    }

    FactorRequirementProfile resolve(const factor::FactorType factorType,
                                     const QVariantMap& calculation) const
    {
        FactorRequirementProfile profile;
        profile.factorType = factorType;
        const RequirementCalculationView parsed(calculation);

        switch (factorType) {
        case factor::FactorType::VALUE:
            return resolveValue(profile, parsed);
        case factor::FactorType::QUALITY:
            return resolveQuality(profile, parsed);
        case factor::FactorType::GROWTH:
            return resolveGrowth(profile, parsed);
        case factor::FactorType::DIVIDEND:
            return resolveDividend(profile, parsed);
        case factor::FactorType::SENTIMENT:
            return resolveSentiment(profile, parsed);
        case factor::FactorType::SIZE:
            return resolveSize(profile, parsed);
        default:
            return profile;
        }
    }

    FactorRequirementProfile resolve(const factor::FactorInstanceInfo& info) const
    {
        const RequirementInstanceConfigView view(info);
        return resolve(view.runtimeType(), view.calculationMap());
    }

    SupportMapRequirementResolution resolveSupportMap(const factor::FactorType runtimeType,
                                                      const QVariantMap& calculation,
                                                      const QStringList& configuredFields) const
    {
        SupportMapRequirementResolution resolution;
        resolution.requiredFields = normalizeRequirementFieldNames(configuredFields);
        if (!configuredFields.isEmpty()) {
            for (const QString& field : configuredFields) {
                const QString normalized = canonicalContractFieldName(field);
                if (RequirementSourceTableResolver::instance().fromRequirementField(normalized)
                    == factor::SourceTable::UNKNOWN) {
                    resolution.failureCategory = QStringLiteral("unknown-field");
                    resolution.failureReason = QStringLiteral("未知字段: %1").arg(field);
                    return resolution;
                }
            }

            resolution.explicitSourceTable = RequirementSourceTableResolver::instance().infer(resolution.requiredFields);
            return resolution;
        }

        const FactorRequirementProfile profile = resolve(runtimeType, calculation);
        resolution.requiredFields = profile.requiredFields.orderedValues();
        resolution.explicitSourceTable = profile.sourceTable;
        resolution.allowEmptyRequiredFields = profile.allowEmptyRequiredFields;
        if (!profile.supported && resolution.requiredFields.isEmpty()) {
            resolution.failureCategory = QStringLiteral("unsupported-runtime");
            resolution.failureReason = QStringLiteral("未能解析因子运行时需求");
        }
        return resolution;
    }

    bool isConfigurableType(const factor::FactorType factorType) const
    {
        switch (factorType) {
        case factor::FactorType::GROWTH:
        case factor::FactorType::LIQUIDITY:
        case factor::FactorType::TECHNICAL:
        case factor::FactorType::DIVIDEND:
        case factor::FactorType::MACRO:
        case factor::FactorType::INDUSTRY:
        case factor::FactorType::SENTIMENT:
        case factor::FactorType::CUSTOM:
            return true;
        default:
            return false;
        }
    }

private:
    FactorRequirementResolver() = default;

    static void appendUniqueField(FieldKeySet& fields, const FieldKey& field)
    {
        if (field.toQString().isEmpty()) {
            return;
        }
        fields.unite(FieldKeySet{field});
    }

    FactorRequirementProfile resolveValue(FactorRequirementProfile profile,
                                          const RequirementCalculationView& parsed) const
    {
        const std::vector<factor::ValuationMetric> metrics = parsed.valuationMetrics();
        if (metrics.empty()) {
            return profile;
        }

        factor::ValuationMetric firstMetric = factor::ValuationMetric::UNKNOWN;
        FieldKeySet requiredFields;
        bool hasMetric = false;
        for (const factor::ValuationMetric metric : metrics) {
            if (!hasMetric) {
                firstMetric = metric;
                hasMetric = true;
            }
            if (metric == factor::ValuationMetric::CFP) {
                appendUniqueField(requiredFields, MarketBarFieldKeys::MARKET_CAP);
                appendUniqueField(requiredFields, FinancialFieldKeys::OPERATING_CASH_FLOW);
                continue;
            }
            appendUniqueField(requiredFields, RequirementFieldMapper::instance().valuation(metric));
        }
        if (!hasMetric) {
            return profile;
        }

        profile.metric = static_cast<int>(firstMetric);
        profile.requiredFields = requiredFields;
        profile.sourceTable = RequirementSourceTableResolver::instance().infer(profile.requiredFields);
        profile.supported = !profile.requiredFields.isEmpty();
        return profile;
    }

    FactorRequirementProfile resolveQuality(FactorRequirementProfile profile,
                                            const RequirementCalculationView& parsed) const
    {
        const std::optional<factor::QualityMetric> metric = parsed.qualityMetric();
        if (!metric.has_value()) {
            return profile;
        }

        profile.metric = static_cast<int>(*metric);
        appendUniqueField(profile.requiredFields, RequirementFieldMapper::instance().quality(*metric));
        profile.sourceTable = RequirementSourceTableResolver::instance().infer(profile.requiredFields);
        profile.supported = true;
        return profile;
    }

    FactorRequirementProfile resolveGrowth(FactorRequirementProfile profile,
                                           const RequirementCalculationView& parsed) const
    {
        const std::vector<factor::GrowthMetric> metrics = parsed.growthMetrics();
        if (metrics.empty()
            || parsed.growthWeightsCount() == 0
            || metrics.size() != static_cast<size_t>(parsed.growthWeightsCount())) {
            return profile;
        }

        factor::GrowthMetric firstMetric = factor::GrowthMetric::UNKNOWN;
        FieldKeySet requiredFields;
        bool hasMetric = false;
        for (const factor::GrowthMetric metric : metrics) {
            if (!hasMetric) {
                firstMetric = metric;
                hasMetric = true;
            }
            appendUniqueField(requiredFields, RequirementFieldMapper::instance().growth(metric));
        }
        if (!hasMetric) {
            return profile;
        }

        profile.metric = static_cast<int>(firstMetric);
        profile.requiredFields = requiredFields;
        profile.sourceTable = RequirementSourceTableResolver::instance().infer(profile.requiredFields);
        profile.supported = profile.metric.isValid() && !profile.requiredFields.isEmpty();
        return profile;
    }

    FactorRequirementProfile resolveDividend(FactorRequirementProfile profile,
                                             const RequirementCalculationView& parsed) const
    {
        const std::vector<factor::DividendMetric> metrics = parsed.dividendMetrics();
        if (metrics.empty()) {
            const std::optional<factor::DividendMetric> metric = parsed.dividendMetric();
            if (!metric.has_value()) {
                return profile;
            }
            profile.metric = static_cast<int>(*metric);
            appendUniqueField(profile.requiredFields, RequirementFieldMapper::instance().dividend(*metric));
        } else {
            factor::DividendMetric firstMetric = factor::DividendMetric::UNKNOWN;
            FieldKeySet requiredFields;
            bool hasMetric = false;
            for (const factor::DividendMetric metric : metrics) {
                if (!hasMetric) {
                    firstMetric = metric;
                    hasMetric = true;
                }
                appendUniqueField(requiredFields, RequirementFieldMapper::instance().dividend(metric));
            }
            if (!hasMetric) {
                return profile;
            }
            profile.metric = static_cast<int>(firstMetric);
            profile.requiredFields = requiredFields;
        }
        profile.sourceTable = RequirementSourceTableResolver::instance().infer(profile.requiredFields);
        profile.supported = !profile.requiredFields.isEmpty();
        return profile;
    }

    FactorRequirementProfile resolveSentiment(FactorRequirementProfile profile,
                                              const RequirementCalculationView& parsed) const
    {
        const std::optional<factor::SentimentMetric> metric = parsed.sentimentMetric();
        if (!metric.has_value()) {
            return profile;
        }
        const std::optional<factor::SentimentSource> source = parsed.sentimentSource();

        profile.metric = static_cast<int>(*metric);
        appendUniqueField(profile.requiredFields, RequirementFieldMapper::instance().sentiment(*metric));
        profile.sourceTable = RequirementSourceTableResolver::instance().resolveSentiment(
            source.value_or(factor::SentimentSource::UNKNOWN),
            *metric);
        profile.supported = profile.sourceTable != factor::SourceTable::UNKNOWN;
        return profile;
    }

    FactorRequirementProfile resolveSize(FactorRequirementProfile profile,
                                         const RequirementCalculationView& parsed) const
    {
        const std::optional<factor::SizeMetric> metric = parsed.sizeMetric();
        if (!metric.has_value()) {
            return profile;
        }

        profile.metric = static_cast<int>(*metric);
        appendUniqueField(profile.requiredFields, RequirementFieldMapper::instance().size(*metric));
        profile.sourceTable = RequirementSourceTableResolver::instance().infer(profile.requiredFields);
        profile.supported = true;
        return profile;
    }
};

inline FieldKey requirementFieldForValuationMetric(const factor::ValuationMetric metric)
{
    return RequirementFieldMapper::instance().valuation(metric);
}

inline FieldKey requirementFieldForGrowthMetric(const factor::GrowthMetric metric)
{
    return RequirementFieldMapper::instance().growth(metric);
}

inline FieldKey requirementFieldForQualityMetric(const factor::QualityMetric metric)
{
    return RequirementFieldMapper::instance().quality(metric);
}

inline FieldKey requirementFieldForDividendMetric(const factor::DividendMetric metric)
{
    return RequirementFieldMapper::instance().dividend(metric);
}

inline FieldKey requirementFieldForSizeMetric(const factor::SizeMetric metric)
{
    return RequirementFieldMapper::instance().size(metric);
}

inline FieldKey requirementFieldForSentimentMetric(const factor::SentimentMetric metric)
{
    return RequirementFieldMapper::instance().sentiment(metric);
}

inline void appendUniqueRequirementField(FieldKeySet& fields,
                                         const FieldKey& field)
{
    if (field.toQString().isEmpty()) {
        return;
    }
    fields.unite(FieldKeySet{field});
}

inline QStringList orderedRequirementFieldNames(const FieldKeySet& fields)
{
    return fields.orderedValues();
}

inline factor::SourceTable sourceTableFromSentimentSource(const factor::SentimentSource sentimentSource)
{
    return RequirementSourceTableResolver::instance().fromSentimentSource(sentimentSource);
}

inline factor::SourceTable sourceTableFromRequirementField(const QString& rawField)
{
    return RequirementSourceTableResolver::instance().fromRequirementField(rawField);
}

inline factor::SourceTable sourceTableFromRequirementField(const FieldKey& field)
{
    return RequirementSourceTableResolver::instance().fromRequirementField(field);
}

inline FactorRequirementProfile resolveFactorRequirementProfile(const factor::FactorType factorType,
                                                               const QVariantMap& calculation)
{
    return FactorRequirementResolver::instance().resolve(factorType, calculation);
}

inline QString requirementFieldName(const FieldKey& field)
{
    return QString(field);
}

inline bool isConfigurableFactorType(factor::FactorType factorType)
{
    return FactorRequirementResolver::instance().isConfigurableType(factorType);
}

inline bool requirementFieldSatisfiedByAvailableFields(const QString& rawField,
                                                       const QSet<QString>& availableFields)
{
    return availableFields.contains(canonicalContractFieldName(rawField));
}

inline QStringList requirementDiagnosticFields(const QString& rawField,
                                               const QSet<QString>& availableFields)
{
    Q_UNUSED(availableFields)
    const QString field = canonicalContractFieldName(rawField);
    return field.isEmpty() ? QStringList{} : QStringList{field};
}

inline QVariantMap extractRequirementCalculationMap(const factor::FactorInstanceInfo& info)
{
    return RequirementInstanceConfigView(info).calculationMap();
}

inline factor::FactorType configuredRequirementRuntimeType(const factor::FactorInstanceInfo& info)
{
    return RequirementInstanceConfigView(info).runtimeType();
}

inline QStringList configuredRequirementFields(const factor::FactorInstanceInfo& info)
{
    return RequirementInstanceConfigView(info).configuredFields();
}

inline factor::SourceTable resolveSentimentSourceTable(const factor::SentimentSource sentimentSource,
                                                       const factor::SentimentMetric sentimentMetric = factor::SentimentMetric::UNKNOWN)
{
    return RequirementSourceTableResolver::instance().resolveSentiment(sentimentSource, sentimentMetric);
}

inline factor::SourceTable inferRequirementSourceTable(const FieldKeySet& requiredFields)
{
    return RequirementSourceTableResolver::instance().infer(requiredFields);
}

inline factor::SourceTable inferRequirementSourceTable(const QStringList& requiredFields)
{
    return RequirementSourceTableResolver::instance().infer(requiredFields);
}

inline factor::SourceTable resolveRequirementSourceTable(const factor::FactorInstanceInfo& info,
                                                         const QStringList& requiredFields,
                                                         factor::SourceTable explicitSourceTable = factor::SourceTable::UNKNOWN)
{
    if (explicitSourceTable != factor::SourceTable::UNKNOWN) {
        return explicitSourceTable;
    }

    const RequirementInstanceConfigView view(info);
    const factor::SourceTable configuredSourceTable = view.configuredSourceTable();
    if (configuredSourceTable != factor::SourceTable::UNKNOWN) {
        return configuredSourceTable;
    }

    const QStringList effectiveRequiredFields = requiredFields.isEmpty()
        ? view.configuredFields()
        : requiredFields;
    return RequirementSourceTableResolver::instance().infer(effectiveRequiredFields);
}

inline FactorRequirementProfile resolveFactorRequirementProfile(const factor::FactorInstanceInfo& info)
{
    return FactorRequirementResolver::instance().resolve(info);
}

inline QStringList supportMapRequirementFieldsForProfile(const FactorRequirementProfile& profile)
{
    return orderedRequirementFieldNames(profile.requiredFields);
}

inline int supportMapRequirementSourceTableForProfile(const FactorRequirementProfile& profile)
{
    return static_cast<int>(profile.sourceTable);
}

inline bool isSupportMapConfigurableRuntimeType(const factor::FactorType runtimeType)
{
    return isConfigurableFactorType(runtimeType);
}

inline SupportMapRequirementResolution resolveSupportMapRequirementResolution(
    const factor::FactorType runtimeType,
    const QVariantMap& calculation,
    const QStringList& configuredFields = {})
{
    return FactorRequirementResolver::instance().resolveSupportMap(runtimeType,
                                                                   calculation,
                                                                   configuredFields);
}

inline bool isDailyBarRequirementField(const QString& rawField)
{
    return marketBarFields().contains(rawField);
}

inline bool isFinancialRequirementField(const QString& rawField)
{
    return financialFields().contains(rawField);
}

inline bool isSymbolInfoRequirementField(const QString& rawField)
{
    return symbolInfoFields().contains(rawField);
}

inline bool isNewsRequirementField(const QString& rawField)
{
    return newsFields().contains(rawField);
}

inline bool isPolicyRequirementField(const QString& rawField)
{
    return policyFields().contains(rawField);
}

inline bool isAlternativeRequirementField(const QString& rawField)
{
    return alternativeFields().contains(rawField);
}

inline bool isDerivativesRequirementField(const QString& rawField)
{
    return derivativesFields().contains(rawField);
}

inline factor::SentimentSource inferSentimentSourceFromMetric(const factor::SentimentMetric metric)
{
    return RequirementSourceTableResolver::instance().inferSentimentSource(metric);
}

} // namespace factor::bridge
