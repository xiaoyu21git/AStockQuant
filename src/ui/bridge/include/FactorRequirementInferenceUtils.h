#pragma once

#include <QSet>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace factor::bridge {

inline QString normalizeRequirementFieldName(const QString& rawField)
{
    const QString field = rawField.trimmed().toLower();
    if (field == QStringLiteral("adj_factor")) {
        return {};
    }
    if (field == QStringLiteral("revenue_growth")) {
        return QStringLiteral("total_revenue");
    }
    return field;
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

struct FactorRequirementProfile
{
    QString factorType;
    QString metric;
    QVariantList requiredFields;
    QVariantList optionalFields;
    QString sourceTable;
    bool supported{false};
};

inline QString resolveSentimentSourceTable(const QVariantMap& calculation);

inline QString inferRequirementSourceTable(const QVariantList& requiredFields);

inline QString normalizeConfigurableFactorType(const QString& rawType)
{
    const QString normalized = rawType.trimmed().toLower();
    if (normalized == QStringLiteral("growth") || normalized == QString::fromUtf8("成长因子")) {
        return QStringLiteral("growth");
    }
    if (normalized == QStringLiteral("dividend") || normalized == QString::fromUtf8("红利因子")) {
        return QStringLiteral("dividend");
    }
    if (normalized == QStringLiteral("technical") || normalized == QString::fromUtf8("技术因子")) {
        return QStringLiteral("technical");
    }
    if (normalized == QStringLiteral("liquidity") || normalized == QString::fromUtf8("流动性因子")) {
        return QStringLiteral("liquidity");
    }
    if (normalized == QStringLiteral("macro") || normalized == QString::fromUtf8("宏观因子")) {
        return QStringLiteral("macro");
    }
    if (normalized == QStringLiteral("industry") || normalized == QString::fromUtf8("行业因子")) {
        return QStringLiteral("industry");
    }
    if (normalized == QStringLiteral("sentiment") || normalized == QString::fromUtf8("情绪因子")) {
        return QStringLiteral("sentiment");
    }
    if (normalized == QStringLiteral("custom")
            || normalized == QString::fromUtf8("自定义因子")
            || normalized == QString::fromUtf8("自定义")) {
        return QStringLiteral("custom");
    }
    if (normalized == QStringLiteral("value") || normalized == QString::fromUtf8("价值因子")) {
        return QStringLiteral("value");
    }
    if (normalized == QStringLiteral("momentum") || normalized == QString::fromUtf8("动量因子")) {
        return QStringLiteral("momentum");
    }
    if (normalized == QStringLiteral("size") || normalized == QString::fromUtf8("规模因子")) {
        return QStringLiteral("size");
    }
    if (normalized == QStringLiteral("quality") || normalized == QString::fromUtf8("质量因子")) {
        return QStringLiteral("quality");
    }
    if (normalized == QStringLiteral("low_volatility")|| normalized == QString::fromUtf8("低波因子")) {
        return QStringLiteral("low_volatility");
    }
    return normalized;
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
        QVariantMap{{QStringLiteral("sentiment_source"), sourceTable}});
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
        appendDimension(normalizeMacroRequirementDimension(calculation.value(QStringLiteral("macroMetric"), calculation.value(QStringLiteral("macro_metric"))).toString()));
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
        const QString legacyMetric = normalizeMacroRequirementDimension(calculation.value(QStringLiteral("macroMetric"), calculation.value(QStringLiteral("macro_metric"))).toString());
        const QStringList dimensions = legacyMetric.isEmpty() ? normalizeMacroRequirementDimensions(calculation) : QStringList{legacyMetric};
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
        if (calculation.value(QStringLiteral("use_volume"), calculation.value(QStringLiteral("useVolume"))).toBool()) {
            profile.optionalFields.append(QStringLiteral("volume"));
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("size")) {
        profile.metric = normalizeSizeRequirementMetric(
            calculation.value(QStringLiteral("size_metric"), calculation.value(QStringLiteral("sizeMetric"), QStringLiteral("market_cap"))).toString());
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
        profile.requiredFields.append(QStringLiteral("close"));
        const QString indicatorType = calculation.value(QStringLiteral("indicator_type"), calculation.value(QStringLiteral("indicatorType"))).toString().trimmed();
        if (indicatorType == QString::fromUtf8("趋势指标") || indicatorType == QString::fromUtf8("波动率指标")) {
            profile.optionalFields.append(QStringLiteral("high"));
            profile.optionalFields.append(QStringLiteral("low"));
        } else if (indicatorType == QString::fromUtf8("动量指标")) {
            profile.optionalFields.append(QStringLiteral("change_pct"));
        } else if (indicatorType == QString::fromUtf8("成交量指标")) {
            profile.requiredFields.append(QStringLiteral("volume"));
        }
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("liquidity")) {
        profile.metric = calculation.value(QStringLiteral("liquidity_metric"), calculation.value(QStringLiteral("liquidityMetric"), QStringLiteral("turnover_rate")))
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
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("macro")) {
        const QVariantList indicators = normalizeRequirementList(calculation.value(QStringLiteral("macroIndicators")));
        const QStringList normalizedIndicators = normalizeMacroRequirementIndicators(calculation);
        profile.metric = normalizedIndicators.isEmpty() ? QStringLiteral("industrial_added_value_yoy") : normalizedIndicators.first();
        profile.requiredFields = macroRequirementFieldsForIndicators(calculation);
        profile.sourceTable = QStringLiteral("daily_bar");
        Q_UNUSED(calculation.value(QStringLiteral("macroWindow"), calculation.value(QStringLiteral("macro_window"))));
        Q_UNUSED(indicators);
        profile.supported = true;
        return profile;
    }

    if (profile.factorType == QStringLiteral("industry")) {
        profile.metric = calculation.value(QStringLiteral("industryMetric"), calculation.value(QStringLiteral("industry_metric"))).toString().trimmed().toLower();
        profile.supported = false;
        return profile;
    }

    if (profile.factorType == QStringLiteral("sentiment")) {
        QString source = normalizeSentimentSourceText(
            calculation.value(QStringLiteral("sentiment_source"), calculation.value(QStringLiteral("sentimentSource"))).toString());
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
            QVariantMap{{QStringLiteral("sentiment_source"), source}});
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

inline bool isDailyBarRequirementField(const QString& rawField)
{
    static const QSet<QString> dailyBarFields{
        QStringLiteral("open"),
        QStringLiteral("high"),
        QStringLiteral("low"),
        QStringLiteral("close"),
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
    const QString source = calculation.value(QStringLiteral("sentiment_source"), calculation.value(QStringLiteral("sentimentSource")))
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
    return {};
}

}  // namespace factor::bridge