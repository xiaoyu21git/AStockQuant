// FactorService.cpp
// 因子服务层实现 - 负责业务逻辑

#include "../../ui/bridge/include/FactorService.h"
#include "../../ui/bridge/include/FactorViewModel.h"
#include "../../ui/bridge/include/FactorDomainSyncUtils.h"
#include "../../ui/bridge/include/FactorDomainSyncRetryUtils.h"
#include "../../ui/bridge/include/DatabaseConnectionManager.h"
#include "../../infrastructure/include/database/FactorRepository.h"
#include "../../infrastructure/include/database/DatabaseConfig.h"
#include "../../ui/bridge/include/DataServiceCache.h"
#include "../../../cache/include/cache_facade.h"
#include "../../../domain/factor/include/DataAvailabilityChecker.h"
#include "../../../domain/factor/include/CustomExpressionUtils.h"
#include "../../../domain/factor/include/FactorCacheManager.h"
#include "../../../domain/factor/include/FactorInstanceManager.h"
#include "../../ui/bridge/include/FactorInstanceResolutionUtils.h"
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

using namespace astock::database;

namespace {

constexpr int kMaxRecentFactorOperationReports = 8;

QStringList variantToStringList(const QVariant& value)
{
    QStringList result;

    if (!value.isValid() || value.isNull()) {
        return result;
    }

    if (value.type() == QVariant::String) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            result.append(text);
        }
        return result;
    }

    if (value.type() == QVariant::StringList) {
        const QStringList list = value.toStringList();
        for (const QString& item : list) {
            const QString text = item.trimmed();
            if (!text.isEmpty()) {
                result.append(text);
            }
        }
        return result;
    }

    const QVariantList list = value.toList();
    for (const QVariant& item : list) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty()) {
            result.append(text);
        }
    }

    if (result.isEmpty()) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            result.append(text);
        }
    }

    return result;
}

QString firstNonEmptyText(const QVariantMap& map, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QString text = map.value(QString::fromUtf8(key)).toString().trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}

QStringList factorTagList(const QVariantMap& factor)
{
    QStringList tags = variantToStringList(factor.value(QStringLiteral("tags")));
    tags.removeDuplicates();
    return tags;
}

bool containsTextCaseInsensitive(const QString& haystack, const QString& needle)
{
    return haystack.contains(needle, Qt::CaseInsensitive);
}

QString normalizeFactorType(const QString& majorCategory)
{
    const QString normalized = majorCategory.trimmed().toLower();
    if (majorCategory == "价值因子") {
        return "value";
    }
    if (majorCategory == "动量因子") {
        return "momentum";
    }
    if (majorCategory == "规模因子") {
        return "size";
    }
    if (majorCategory == "质量因子") {
        return "quality";
    }
    if (majorCategory == "成长因子") {
        return "growth";
    }
    if (majorCategory == QString::fromUtf8("红利因子")) {
        return "dividend";
    }
    if (majorCategory == QString::fromUtf8("技术因子")) {
        return "technical";
    }
    if (majorCategory == QString::fromUtf8("流动性因子")) {
        return "liquidity";
    }
    if (majorCategory == QString::fromUtf8("宏观/行业因子") || majorCategory == QString::fromUtf8("宏观/行业")) {
        return "macro_sector";
    }
    if (majorCategory == QString::fromUtf8("情绪因子")) {
        return "sentiment";
    }
    if (majorCategory == QString::fromUtf8("自定义因子") || majorCategory == QString::fromUtf8("自定义")) {
        return "custom";
    }
    if (majorCategory == QString::fromUtf8("低波因子") || majorCategory == QString::fromUtf8("低波动因子")) {
        return "lowvol";
    }
    if (normalized == "low_volatility") {
        return "lowvol";
    }

    return normalized;
}

QString makeUnsupportedMetricError(const QString& factorType, const QString& metric)
{
    return QString("因子类型 %1 暂不支持参数: %2").arg(factorType, metric);
}

QString normalizeDisplayCategory(const QString& rawType)
{
    const QString normalized = rawType.trimmed().toLower();
    if (normalized == QString::fromUtf8("价值因子") || normalized == "value") {
        return QString::fromUtf8("价值因子");
    }
    if (normalized == QString::fromUtf8("动量因子") || normalized == "momentum") {
        return QString::fromUtf8("动量因子");
    }
    if (normalized == QString::fromUtf8("规模因子") || normalized == "size") {
        return QString::fromUtf8("规模因子");
    }
    if (normalized == QString::fromUtf8("质量因子") || normalized == "quality") {
        return QString::fromUtf8("质量因子");
    }
    if (normalized == QString::fromUtf8("成长因子") || normalized == "growth") {
        return QString::fromUtf8("成长因子");
    }
    if (normalized == QString::fromUtf8("红利因子") || normalized == "dividend") {
        return QString::fromUtf8("红利因子");
    }
    if (normalized == QString::fromUtf8("技术因子") || normalized == "technical") {
        return QString::fromUtf8("技术因子");
    }
    if (normalized == QString::fromUtf8("流动性因子") || normalized == "liquidity") {
        return QString::fromUtf8("流动性因子");
    }
    if (normalized == QString::fromUtf8("宏观/行业因子") || normalized == QString::fromUtf8("宏观/行业") || normalized == "macro_sector") {
        return QString::fromUtf8("宏观/行业因子");
    }
    if (normalized == QString::fromUtf8("情绪因子") || normalized == "sentiment") {
        return QString::fromUtf8("情绪因子");
    }
    if (normalized == QString::fromUtf8("自定义因子") || normalized == QString::fromUtf8("自定义") || normalized == "custom") {
        return QString::fromUtf8("自定义因子");
    }
    if (normalized == QString::fromUtf8("低波因子") || normalized == "low_vol" || normalized == "lowvol" || normalized == "low_volatility") {
        return QString::fromUtf8("低波因子");
    }

    return rawType.trimmed();
}

QString resolveFactorTypeId(const QVariantMap& factorData)
{
    const QString explicitFactorType = factorData.value("factorType").toString().trimmed();
    if (!explicitFactorType.isEmpty()) {
        return normalizeFactorType(explicitFactorType);
    }

    return normalizeFactorType(factorData.value("majorCategory").toString().trimmed());
}

QString resolveDisplayCategory(const QVariantMap& factorData)
{
    const QString explicitDisplayCategory = factorData.value("majorCategory").toString().trimmed();
    if (!explicitDisplayCategory.isEmpty()) {
        return normalizeDisplayCategory(explicitDisplayCategory);
    }

    return normalizeDisplayCategory(factorData.value("factorType").toString().trimmed());
}

QStringList extractCustomExpressionFields(const QVariantMap& calculation)
{
    const QString expression = calculation.value("expression").toString().trimmed().toLower();
    if (expression.isEmpty()) {
        return {"close", "open"};
    }

    std::vector<factor::custom_expression::VariableBinding> bindings;
    const QVariantList variableList = calculation.value("variables").toList();
    bindings.reserve(static_cast<size_t>(variableList.size()));
    for (const QVariant& variableValue : variableList) {
        const QVariantMap variableMap = variableValue.toMap();
        const QString name = variableMap.value("name").toString().trimmed();
        if (name.isEmpty()) {
            continue;
        }

        factor::custom_expression::VariableBinding binding;
        binding.name = name;
        binding.field = variableMap.value("field").toString().trimmed();
        if (variableMap.contains("defaultValue")) {
            binding.hasDefaultValue = true;
            binding.defaultValue = variableMap.value("defaultValue").toDouble();
        }
        bindings.push_back(std::move(binding));
    }

    const auto requirements = factor::custom_expression::resolveFieldRequirements(expression, bindings);
    QStringList fields = requirements.requiredFields;
    for (const QString& field : requirements.optionalFields) {
        if (!fields.contains(field)) {
            fields.append(field);
        }
    }
    return fields.isEmpty() ? QStringList{"close", "open"} : fields;
}

QString normalizeValuationMetric(const QString& rawMetric)
{
    const QString metric = rawMetric.trimmed().toLower();
    if (metric.isEmpty()) {
        return {};
    }

    if (metric == "pe" || metric == "pe_ttm"
        || rawMetric.startsWith(QString::fromUtf8("市盈率"))) {
        return "pe";
    }
    if (metric == "pb" || rawMetric.startsWith(QString::fromUtf8("市净率"))) {
        return "pb";
    }
    if (metric == "ps" || rawMetric.startsWith(QString::fromUtf8("市销率"))) {
        return "ps";
    }
    if (metric == "dividend_yield" || rawMetric.startsWith(QString::fromUtf8("股息率"))) {
        return "dividend_yield";
    }
    if (metric == "market_cap" || rawMetric.startsWith(QString::fromUtf8("总市值"))) {
        return "market_cap";
    }

    return metric;
}

QString normalizeSizeMetric(const QString& rawMetric)
{
    const QString metric = rawMetric.trimmed().toLower();
    if (metric.isEmpty()) {
        return {};
    }

    if (metric == "market_cap" || rawMetric.startsWith(QString::fromUtf8("总市值"))) {
        return "market_cap";
    }
    if (metric == "circulating_market_cap" || rawMetric.startsWith(QString::fromUtf8("流通市值"))) {
        return "circulating_market_cap";
    }
    if (metric == "total_assets" || rawMetric.startsWith(QString::fromUtf8("总资产"))) {
        return "total_assets";
    }

    return metric;
}

QString normalizeGrowthMetric(const QString& rawMetric)
{
    const QString metric = rawMetric.trimmed().toLower();
    if (metric.isEmpty()) {
        return {};
    }

    if (metric == "revenue_growth"
        || metric == QString::fromUtf8("收入增长")
        || metric == QString::fromUtf8("营收增长")) {
        return "revenue_growth";
    }
    if (metric == "net_profit_growth"
        || metric == "earnings_growth"
        || metric == "profit_growth"
        || metric == QString::fromUtf8("盈利增长")
        || metric == QString::fromUtf8("利润增长")
        || metric == QString::fromUtf8("利润增长率")) {
        return "net_profit_growth";
    }
    if (metric == "eps_growth" || metric == QString::fromUtf8("eps增长")) {
        return "eps_growth";
    }

    return metric;
}

QString normalizeQualityMetric(const QString& rawMetric)
{
    const QString metric = rawMetric.trimmed().toLower();
    if (metric.isEmpty()) {
        return {};
    }

    if (metric == "roe" || rawMetric == QString::fromUtf8("净资产收益率")) {
        return "roe";
    }
    if (metric == "roa" || rawMetric == QString::fromUtf8("总资产收益率")) {
        return "roa";
    }
    if (metric == "gross_margin" || metric == "operating_margin"
        || rawMetric == QString::fromUtf8("毛利率")
        || rawMetric == QString::fromUtf8("营业利润率")
        || rawMetric == QString::fromUtf8("利润率")) {
        return metric == "operating_margin" ? "operating_margin" : "gross_margin";
    }
    if (metric == "profit_margin") {
        return "gross_margin";
    }
    if (metric == "net_profit_to_equity" || metric == "earnings_quality"
        || rawMetric == QString::fromUtf8("盈利质量")) {
        return "earnings_quality";
    }

    return metric;
}

QString buildFinancialReportTypeClause(const QString& timeframe, const QString& alias)
{
    const QString normalizedTimeframe = timeframe.trimmed().toLower();
    if (normalizedTimeframe == "annual") {
        return QString(" AND %1.report_type = 'FY'").arg(alias);
    }
    if (normalizedTimeframe == "quarterly" || normalizedTimeframe == "ttm") {
        return QString(" AND %1.report_type IN ('Q1', 'Q2', 'Q3', 'Q4')").arg(alias);
    }
    return {};
}

QVariant jsonValueToVariant(const QJsonValue& value)
{
    if (value.isObject()) {
        return value.toObject().toVariantMap();
    }
    if (value.isArray()) {
        return value.toArray().toVariantList();
    }
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isString()) {
        return value.toString();
    }
    return QVariant();
}

QVariantMap parseJsonObject(const QString& jsonText)
{
    if (jsonText.trimmed().isEmpty()) {
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    if (!doc.isObject()) {
        return {};
    }

    return doc.object().toVariantMap();
}

QString chooseFirstNonEmpty(const QVariantMap& map, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QString value = map.value(QString::fromUtf8(key)).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QVariantMap extractParametersFromConfig(const QVariantMap& config)
{
    const QVariantMap directParameters = config.value("parameters").toMap();
    if (!directParameters.isEmpty()) {
        return directParameters;
    }

    const QVariantMap calculation = config.value("calculation").toMap();
    if (calculation.isEmpty()) {
        return {};
    }

    const QVariantMap nestedParameters = calculation.value("params").toMap();
    if (!nestedParameters.isEmpty()) {
        return nestedParameters;
    }

    QVariantMap extracted = calculation;
    extracted.remove("type");
    extracted.remove("method");
    extracted.remove("formula");
    extracted.remove("expression");
    return extracted;
}

QVariantMap normalizeCalculationParameters(const QString& majorCategory, const QVariantMap& parameters)
{
    QVariantMap normalized = parameters;
    const QString factorType = normalizeFactorType(majorCategory);

    if (factorType == "value") {
        QString valuationType = normalizeValuationMetric(normalized.value("valuation_type").toString());
        if (valuationType.isEmpty()) {
            valuationType = normalizeValuationMetric(normalized.value("valuationType").toString());
        }
        if (valuationType.isEmpty()) {
            valuationType = normalizeValuationMetric(normalized.value("valuationMetric").toString());
        }
        if (valuationType.isEmpty()) {
            const QStringList metrics = variantToStringList(normalized.value("valuationMetrics"));
            if (!metrics.isEmpty()) {
                valuationType = normalizeValuationMetric(metrics.first());
            }
        }
        if (valuationType.isEmpty()) {
            valuationType = "pe";
        }
        normalized["valuation_type"] = valuationType;
        normalized["use_percentile"] = normalized.value("use_percentile", normalized.value("usePercentile", false));
        normalized["industry_neutral"] = normalized.value("industry_neutral", normalized.value("industryNeutral", false));
    } else if (factorType == "momentum") {
            const QVariant resolvedWindow = normalized.contains("window")
                ? normalized.value("window")
                : normalized.value("lookback_window", normalized.value("lookbackWindow", 60));
            normalized["window"] = resolvedWindow;
            normalized["lookback_window"] = normalized.value("lookback_window", normalized.value("lookbackWindow", resolvedWindow));
        normalized["type"] = normalized.value("type", normalized.value("momentumType", "simple")).toString();
        normalized["price_type"] = normalized.value("price_type", normalized.value("priceType", "adj_close"));
        normalized["use_volume"] = normalized.value("use_volume", normalized.value("useVolume", false));
        normalized["skip_recent"] = normalized.value("skip_recent", normalized.value("skipRecent", 0));
    } else if (factorType == "size") {
        QString sizeMetric = normalizeSizeMetric(normalized.value("size_metric").toString());
        if (sizeMetric.isEmpty()) {
            sizeMetric = normalizeSizeMetric(normalized.value("sizeMetric").toString());
        }
        if (sizeMetric.isEmpty()) {
            sizeMetric = "market_cap";
        }
        normalized["size_metric"] = sizeMetric;
        normalized["log_transform"] = normalized.value("log_transform", normalized.value("logTransform", true));
    } else if (factorType == "growth") {
        QString metric = normalized.value("metric").toString().trimmed().toLower();
        if (metric.isEmpty()) {
            const QString growthMetric = normalized.value("growthMetrics").toString().trimmed();
            if (growthMetric == QString::fromUtf8("收入增长") || growthMetric == QString::fromUtf8("营收增长")) {
                metric = "revenue_growth";
            } else if (growthMetric == QString::fromUtf8("利润增长") || growthMetric == QString::fromUtf8("利润增长率") || growthMetric == QString::fromUtf8("盈利增长")) {
                metric = "net_profit_growth";
            } else if (growthMetric == "eps_growth" || growthMetric == QString::fromUtf8("EPS增长")) {
                metric = "eps_growth";
            }
        }
        if (metric.isEmpty()) {
            metric = "revenue_growth";
        }
        normalized["metric"] = metric;
        normalized["timeframe"] = normalized.value("timeframe", "quarterly");
        normalized["lookback_period"] = normalized.value("lookback_period", normalized.value("lookbackPeriod", 252));
    } else if (factorType == "quality") {
        QString metric = normalizeQualityMetric(normalized.value("metric").toString());
        if (metric.isEmpty()) {
            metric = normalizeQualityMetric(normalized.value("qualityMetric").toString());
        }
        if (metric.isEmpty()) {
            metric = "roe";
        }
        normalized["metric"] = metric;
        normalized["timeframe"] = normalized.value("timeframe", "quarterly");
        normalized["quality_threshold"] = normalized.value("quality_threshold", normalized.value("qualityThreshold", 0.1));
    } else if (majorCategory == QString::fromUtf8("红利因子") || factorType == "dividend") {
        QString metric = normalized.value("metric").toString().trimmed().toLower();
        if (metric.isEmpty()) {
            const QString dividendType = normalized.value("dividendType").toString().trimmed();
            if (dividendType == QString::fromUtf8("股息支付率")) {
                metric = "payout_ratio";
            } else if (dividendType == QString::fromUtf8("股息稳定性")) {
                metric = "dividend_stability";
            } else {
                metric = "dividend_yield";
            }
        }
        normalized["metric"] = metric;
        normalized["min_dividend_yield"] = normalized.value("min_dividend_yield", normalized.value("minDividendYield", 0));
        normalized["timeframe"] = normalized.value("timeframe", "annual");
    } else if (factorType == "technical") {
        QString indicatorType = normalized.value("indicator_type").toString().trimmed();
        if (indicatorType.isEmpty()) {
            indicatorType = normalized.value("indicatorType").toString().trimmed();
        }
        if (indicatorType.isEmpty()) {
            indicatorType = QString::fromUtf8("趋势指标");
        }

        normalized["indicator_type"] = indicatorType;
        normalized["window"] = normalized.value("window", normalized.value("indicatorWindow", 20));
        normalized["lookback_period"] = normalized.value("lookback_period", normalized.value("lookbackPeriod", 252));
        normalized["frequency"] = normalized.value("frequency", QString::fromUtf8("日频"));
        normalized["price_type"] = normalized.value("price_type", normalized.value("priceType", "close"));
        normalized["use_volume"] = indicatorType == QString::fromUtf8("成交量指标");
    } else if (factorType == "liquidity") {
        QString metric = normalized.value("liquidity_metric").toString().trimmed().toLower();
        if (metric.isEmpty()) {
            const QString liquidityMetric = normalized.value("liquidityMetric").toString().trimmed();
            if (liquidityMetric == QString::fromUtf8("成交量")) {
                metric = "volume";
            } else if (liquidityMetric == QString::fromUtf8("买卖价差")) {
                metric = "amplitude";
            } else if (liquidityMetric == "amihud" || liquidityMetric == QString::fromUtf8("Amihud非流动性")) {
                metric = "amihud_illiquidity";
            } else {
                metric = "turnover_rate";
            }
        }

        normalized["liquidity_metric"] = metric;
        normalized["window"] = normalized.value("window", normalized.value("liquidityWindow", 20));
        normalized["lookback_period"] = normalized.value("lookback_period", normalized.value("lookbackPeriod", 252));
        normalized["frequency"] = normalized.value("frequency", QString::fromUtf8("日频"));
    } else if (factorType == "macro_sector") {
        normalized["sector_type"] = normalized.value("sector_type", normalized.value("sectorType", QString::fromUtf8("申万一级")));
        normalized["macro_factor"] = normalized.value("macro_factor", normalized.value("macroFactor", QString::fromUtf8("经济增长敏感度")));
        normalized["window"] = normalized.value("window", normalized.value("lookbackPeriod", 20));
    } else if (factorType == "sentiment") {
        normalized["sentiment_source"] = normalized.value("sentiment_source", normalized.value("sentimentSource", "market_sentiment"));
        normalized["metric"] = normalized.value("metric", normalized.value("sentimentMetric", "sentiment_score")).toString();
        normalized["window"] = normalized.value("window", normalized.value("sentimentWindow", normalized.value("lookbackDays", 20)));
        normalized["sentiment_weight"] = normalized.value("sentiment_weight", normalized.value("sentimentWeight", 0.3));
    } else if (factorType == "custom") {
        normalized["expression"] = normalized.value("expression", "close / open - 1").toString();
        normalized["variables"] = normalized.value("variables", QVariantList{}).toList();
    } else if (factorType == "lowvol") {
        normalized["window"] = normalized.value("window", normalized.value("volatilityWindow", 20));
        normalized["volatility_type"] = normalized.value("volatility_type", normalized.value("volatilityType", "standard"));
    }

    return normalized;
}

QVariantMap buildDataRequirementsConfig(const QString& majorCategory, const QVariantMap& calculation)
{
    const QString factorType = normalizeFactorType(majorCategory);
    QVariantList required;
    QVariantList optional;

    if (factorType == "value") {
        const QString valuationType = calculation.value("valuation_type", "pe").toString().trimmed().toLower();
        if (valuationType == "pb") {
            required.append("pb_ratio");
        } else if (valuationType == "dividend_yield") {
            required.append("dividend_yield");
        } else if (valuationType == "market_cap") {
            required.append("market_cap");
        } else {
            required.append("pe_ratio");
        }
    } else if (factorType == "momentum") {
        required.append("close");
        optional.append("adj_factor");
        if (calculation.value("use_volume").toBool()) {
            optional.append("volume");
        }
    } else if (factorType == "size") {
        const QString sizeMetric = normalizeSizeMetric(calculation.value("size_metric", "market_cap").toString());
        required.append(sizeMetric == "circulating_market_cap" ? "circulating_market_cap" : "market_cap");
    } else if (factorType == "quality") {
        const QString metric = calculation.value("metric", "roe").toString().trimmed().toLower();
        if (metric == "roe") {
            required.append("roe");
        } else if (metric == "roa") {
            required.append("roa");
        } else if (metric == "gross_margin" || metric == "operating_margin") {
            required.append("profit_margin");
        } else {
            required.append("net_profit");
            required.append("equity");
        }
    } else if (factorType == "growth") {
        const QString metric = calculation.value("metric", "revenue_growth").toString().trimmed().toLower();
        if (metric == "net_profit_growth") {
            required.append("net_profit");
        } else if (metric == "eps_growth") {
            required.append("eps");
            optional.append("net_profit");
        } else {
            required.append("total_revenue");
        }
    } else if (majorCategory == QString::fromUtf8("红利因子") || factorType == "dividend") {
        required.append("pe_ratio");
        optional.append("pb_ratio");
        optional.append("roe");
        optional.append("profit_margin");
    } else if (factorType == "technical") {
        required.append("close");

        const QString indicatorType = calculation.value("indicator_type", calculation.value("indicatorType")).toString().trimmed();
        if (indicatorType == QString::fromUtf8("趋势指标")) {
            optional.append("high");
            optional.append("low");
        } else if (indicatorType == QString::fromUtf8("动量指标")) {
            optional.append("change_pct");
        } else if (indicatorType == QString::fromUtf8("波动率指标")) {
            optional.append("high");
            optional.append("low");
        } else if (indicatorType == QString::fromUtf8("成交量指标")) {
            required.append("volume");
        }
    } else if (factorType == "liquidity") {
        const QString metric = calculation.value("liquidity_metric", calculation.value("liquidityMetric", "turnover_rate")).toString().trimmed().toLower();
        if (metric == "volume") {
            required.append("volume");
        } else if (metric == "amplitude") {
            required.append("amplitude");
        } else if (metric == "amihud_illiquidity") {
            required.append("close");
            required.append("volume");
            optional.append("turnover");
        } else {
            required.append("turnover_rate");
        }
    } else if (factorType == "macro_sector") {
        required.append("close");
    } else if (factorType == "sentiment") {
        required.append("change_pct");
        required.append("close");
        optional.append("turnover_rate");
        optional.append("volume");
    } else if (factorType == "custom") {
        std::vector<factor::custom_expression::VariableBinding> bindings;
        const QVariantList variableList = calculation.value("variables").toList();
        bindings.reserve(static_cast<size_t>(variableList.size()));
        for (const QVariant& variableValue : variableList) {
            const QVariantMap variableMap = variableValue.toMap();
            const QString name = variableMap.value("name").toString().trimmed();
            if (name.isEmpty()) {
                continue;
            }

            factor::custom_expression::VariableBinding binding;
            binding.name = name;
            binding.field = variableMap.value("field").toString().trimmed();
            if (variableMap.contains("defaultValue")) {
                binding.hasDefaultValue = true;
                binding.defaultValue = variableMap.value("defaultValue").toDouble();
            }
            bindings.push_back(std::move(binding));
        }

        const auto fieldRequirements = factor::custom_expression::resolveFieldRequirements(
            calculation.value("expression").toString().trimmed().toLower(),
            bindings
        );
        for (const QString& field : fieldRequirements.requiredFields) {
            required.append(field);
        }
        for (const QString& field : fieldRequirements.optionalFields) {
            optional.append(field);
        }
    } else if (factorType == "lowvol") {
        required.append("close");
    }

    QVariantMap result;
    result["required"] = required;
    if (!optional.isEmpty()) {
        result["optional"] = optional;
    }
    return result;
}

QVariantMap buildBoundaryRulesConfig(const QString& majorCategory, const QVariantMap& calculation)
{
    const QString factorType = normalizeFactorType(majorCategory);
    QVariantMap rules;

    if (factorType == "momentum") {
        const int window = (std::max)(1, calculation.value("window", 20).toInt());
        rules["min_data_points"] = window + 1;
        rules["handle_new_stock"] = "exclude_if_lt_60d";
        rules["handle_suspended"] = "forward_fill";
        rules["handle_delisted"] = "keep_until_delist";
        rules["handle_outliers"] = "winsorize_3sigma";
    } else if (factorType == "technical") {
        const int window = (std::max)(1, calculation.value("window", calculation.value("indicatorWindow", 20)).toInt());
        const QString indicatorType = calculation.value("indicator_type", calculation.value("indicatorType")).toString().trimmed();
        rules["min_data_points"] = (indicatorType == QString::fromUtf8("动量指标")
            || indicatorType == QString::fromUtf8("波动率指标")) ? (window + 1) : window;
        rules["handle_new_stock"] = "exclude_if_lt_60d";
        rules["handle_suspended"] = "forward_fill";
        rules["handle_outliers"] = "winsorize_3sigma";
    } else if (factorType == "liquidity") {
        const int window = (std::max)(1, calculation.value("window", calculation.value("liquidityWindow", 20)).toInt());
        const QString metric = calculation.value("liquidity_metric", calculation.value("liquidityMetric", "turnover_rate")).toString().trimmed().toLower();
        rules["min_data_points"] = metric == "amihud_illiquidity" ? (window + 1) : window;
        rules["handle_new_stock"] = "exclude_if_lt_20d";
        rules["handle_suspended"] = "exclude";
        rules["handle_outliers"] = "winsorize_3sigma";
    } else if (factorType == "growth") {
        rules["min_data_points"] = 2;
    } else if (factorType == "dividend") {
        const QString metric = calculation.value("metric", "dividend_yield").toString().trimmed().toLower();
        rules["min_data_points"] = metric == "dividend_stability" ? 2 : 1;
        rules["handle_outliers"] = "winsorize_3sigma";
    } else if (factorType == "macro_sector" || factorType == "sentiment") {
        const int window = (std::max)(5, calculation.value("window", 1).toInt());
        rules["min_data_points"] = window + 1;
        rules["handle_outliers"] = "winsorize_3sigma";
    } else if (factorType == "custom") {
        rules["min_data_points"] = 1;
        rules["handle_outliers"] = "winsorize_3sigma";
    } else if (factorType == "lowvol") {
        rules["min_data_points"] = (std::max)(1, calculation.value("window", calculation.value("volatilityWindow", 20)).toInt());
        rules["handle_outliers"] = "winsorize_3sigma";
    } else {
        rules["min_data_points"] = 1;
        if (factorType == "value" || factorType == "size") {
            rules["handle_outliers"] = "winsorize_3sigma";
        }
    }

    return rules;
}

QJsonObject buildDomainConfigObject(const QVariantMap& factorData)
{
    const QString factorType = resolveFactorTypeId(factorData);
    const QString majorCategory = resolveDisplayCategory(factorData);
    const QVariantMap rawParameters = factorData.value("parameters").toMap();
    const QVariantMap calculation = normalizeCalculationParameters(factorType, rawParameters);
    const QStringList tags = variantToStringList(factorData.value("tags"));
    const QStringList expressionFields = factorType == "custom"
        ? extractCustomExpressionFields(calculation)
        : QStringList{};

    QJsonObject config;
    config.insert("factor_type", factorType);
    config.insert("factorType", factorType);
    config.insert("majorCategory", majorCategory);
    config.insert("factorName", factorData.value("factorName").toString());
    config.insert("displayName", factorData.value("displayName").toString());
    config.insert("description", factorData.value("description").toString());
    config.insert("tags", QJsonArray::fromStringList(tags));
    config.insert("parameters", QJsonObject::fromVariantMap(rawParameters));
    config.insert("calculation", QJsonObject::fromVariantMap(calculation));
    config.insert("data_requirements", QJsonObject::fromVariantMap(buildDataRequirementsConfig(factorType, calculation)));
    config.insert("boundary_rules", QJsonObject::fromVariantMap(buildBoundaryRulesConfig(factorType, calculation)));

    QJsonObject metadata;
    metadata.insert("factorId", factorData.value("factorId").toString());
    metadata.insert("creator", factorData.value("creator").toString());
    metadata.insert("tags", QJsonArray::fromStringList(tags));
    if (factorType == "custom") {
        metadata.insert("custom_variable_mode", QStringLiteral("runtime_bindings"));
        metadata.insert("expression_fields", QJsonArray::fromStringList(expressionFields));
        metadata.insert("custom_variables", QJsonArray::fromVariantList(rawParameters.value("variables").toList()));
    }
    config.insert("metadata", metadata);

    return config;
}

QStringList extractTagsFromConfig(const QVariantMap& config, const QString& majorCategory)
{
    QStringList tags = variantToStringList(config.value("tags"));

    const QVariantMap metadata = config.value("metadata").toMap();
    if (!metadata.isEmpty()) {
        tags.append(variantToStringList(metadata.value("tags")));
    }

    if (!majorCategory.isEmpty()) {
        tags.append(majorCategory);
    }

    tags.removeDuplicates();
    return tags;
}

QString buildFactorName(const QString& explicitFactorName,
                        const QString& instanceName,
                        const QVariantMap& config)
{
    if (!explicitFactorName.trimmed().isEmpty()) {
        return explicitFactorName.trimmed();
    }

    const QString configuredName = chooseFirstNonEmpty(config, {"factorName", "factor_name", "name", "id"});
    if (!configuredName.isEmpty()) {
        return configuredName;
    }

    QString normalizedInstanceName = instanceName.trimmed().toLower();
    normalizedInstanceName.replace(QRegularExpression("\\s+"), "_");
    return normalizedInstanceName;
}

QVariantMap buildDomainFactorMap(const astock::database::QueryResultRow& row)
{
    const QString legacyFactorId = row.getString("factor_id").trimmed();
    const QString instanceId = row.getString("instance_id").trimmed();
    const QString instanceName = row.getString("instance_name").trimmed();
    const QString configText = row.getString("full_config");
    const QVariantMap config = parseJsonObject(configText);

    QString majorCategory = row.getString("major_category").trimmed();
    if (majorCategory.isEmpty()) {
        majorCategory = normalizeDisplayCategory(
            chooseFirstNonEmpty(config, {"majorCategory", "factorType", "factor_type"})
        );

        if (majorCategory.isEmpty()) {
            majorCategory = normalizeDisplayCategory(config.value("calculation").toMap().value("type").toString());
        }
    }

    QString factorType = chooseFirstNonEmpty(config, {"factorType", "factor_type"});
    if (factorType.isEmpty()) {
        factorType = normalizeFactorType(majorCategory);
    }

    QVariantMap factorMap;
    factorMap["factorId"] = legacyFactorId.isEmpty() ? instanceId : legacyFactorId;
    factorMap["instanceId"] = instanceId;
    factorMap["legacyFactorId"] = legacyFactorId;
    factorMap["factorName"] = buildFactorName(row.getString("factor_name"), instanceName, config);
    factorMap["factorType"] = factorType;
    factorMap["displayName"] = chooseFirstNonEmpty(config, {"displayName", "display_name", "name"});
    if (factorMap["displayName"].toString().isEmpty()) {
        factorMap["displayName"] = !row.getString("display_name").trimmed().isEmpty()
            ? row.getString("display_name").trimmed()
            : instanceName;
    }
    factorMap["majorCategory"] = majorCategory;
    factorMap["subCategory"] = !row.getString("sub_category").trimmed().isEmpty()
        ? row.getString("sub_category").trimmed()
        : chooseFirstNonEmpty(config, {"subCategory", "sub_category"});
    factorMap["description"] = !row.getString("factor_description").trimmed().isEmpty()
        ? row.getString("factor_description").trimmed()
        : (!row.getString("instance_description").trimmed().isEmpty()
            ? row.getString("instance_description").trimmed()
            : chooseFirstNonEmpty(config, {"description", "summary"}));
    factorMap["icValue"] = row.getDouble("ic_value", 0.0);
    factorMap["irValue"] = row.getDouble("ir_value", 0.0);
    factorMap["validityDays"] = row.getInt("validity_days", 0);
    factorMap["turnoverRate"] = row.getDouble("turnover_rate", 0.0);
    factorMap["isRecommended"] = row.getValueAs<bool>("is_recommended", false);
    factorMap["isFavorite"] = row.getValueAs<bool>("is_favorite", false);
    factorMap["status"] = !row.getString("factor_status").trimmed().isEmpty()
        ? row.getString("factor_status").trimmed()
        : row.getString("instance_status").trimmed();
    factorMap["creator"] = row.getString("creator").trimmed();
    factorMap["createDate"] = row.getString("create_date").trimmed();
    factorMap["parameters"] = extractParametersFromConfig(config);
    factorMap["tags"] = extractTagsFromConfig(config, majorCategory);
    factorMap["config"] = config;
    factorMap["isAvailable"] = row.getValueAs<bool>("is_available", false);

    const QString availabilityMessage = row.getString("availability_message").trimmed();
    if (!availabilityMessage.isEmpty()) {
        factorMap["availabilityMessage"] = availabilityMessage;
    }

    return factorMap;
}

} // namespace

// 单例实例定义
FactorService* FactorService::m_instance = nullptr;
QMutex FactorService::m_instanceMutex;

// 单例访问方法
FactorService* FactorService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new FactorService();
        m_instance->initialize();
    }
    return m_instance;
}

FactorService::FactorService(QObject* parent)
    : QObject(parent)
    , m_repository(nullptr)
    , m_database(nullptr)
    , m_dataChecker(nullptr)
    , m_factorCacheManager(nullptr)
    , m_factorInstanceManager(nullptr)
    , m_initialized(false)
    , m_isLoading(false)
    , m_cacheLoaded(false)
    , m_autoInitialize(true)
    , m_viewModel(new FactorViewModel(this))
    , m_mutationInProgress(false)
    , m_lastOperationReport()
    , m_recentOperationReports()
    , m_syncFactorDefinitionOverrideForTests()
    , m_removeFactorDefinitionOverrideForTests()
{
    connect(this, &FactorService::factorsLoaded, this, [this](const QVariantList& factors) {
        if (m_viewModel) {
            m_viewModel->updateData(factors);
        } else {
            qWarning() << "FactorService: 视图模型为空，无法更新数据";
        }
    });
}

FactorService::~FactorService()
{}

void FactorService::initialize()
{
    QMutexLocker locker(&m_initMutex);
    if (m_initialized) {
        return;
    }

    try {
        initializeRepository();

        if (!m_repository) {
            qWarning() << "FactorService::initialize: 仓储初始化失败";
            return;
        }

        m_initialized = true;

        if (!initializeFactorDomainRuntime()) {
            qWarning() << "FactorService::initialize: domain/factor 运行时未完全就绪，将保留旧逻辑兜底";
        }

        QTimer::singleShot(0, this, [this]() {
            if (!m_isLoading) {
                m_isLoading = true;
                loadFactorsFromDatabase();
                m_isLoading = false;
            }
        });

    } catch (const std::exception& e) {
        qWarning() << "FactorService::initialize: Error:" << e.what();
    }
}

bool FactorService::mutationInProgress() const
{
    QMutexLocker locker(&m_observabilityMutex);
    return m_mutationInProgress;
}

QVariantMap FactorService::lastOperationReport() const
{
    QMutexLocker locker(&m_observabilityMutex);
    return m_lastOperationReport;
}

QVariantList FactorService::recentOperationReports() const
{
    QMutexLocker locker(&m_observabilityMutex);
    return m_recentOperationReports;
}

void FactorService::setMutationInProgress(bool inProgress)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_observabilityMutex);
        if (m_mutationInProgress != inProgress) {
            m_mutationInProgress = inProgress;
            changed = true;
        }
    }

    if (changed) {
        emit mutationInProgressChanged();
    }
}

void FactorService::publishOperationReport(const QString& operation,
                                           const QString& factorId,
                                           bool success,
                                           const QString& stage,
                                           const QString& message)
{
    QVariantMap report;
    report["operation"] = operation.trimmed();
    report["factorId"] = factorId.trimmed();
    report["success"] = success;
    report["stage"] = stage.trimmed();
    report["message"] = message.trimmed();
    report["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    {
        QMutexLocker locker(&m_observabilityMutex);
        m_lastOperationReport = report;
        m_recentOperationReports.prepend(report);
        while (m_recentOperationReports.size() > kMaxRecentFactorOperationReports) {
            m_recentOperationReports.removeLast();
        }
    }

    emit lastOperationReportChanged();
    emit recentOperationReportsChanged();
}

QString FactorService::addFactor(const QVariantMap& factorData)
{
    QMutexLocker mutationLocker(&m_mutationMutex);
    setMutationInProgress(true);
    const auto mutationGuard = std::shared_ptr<void>(nullptr, [this](void*) {
        setMutationInProgress(false);
    });

    qDebug() << "FactorService::addFactor 开始，数据:" << factorData;

    QString errorMessage;
    if (!validateFactorData(factorData, errorMessage)) {
        qWarning() << "因子数据验证失败:" << errorMessage;
        publishOperationReport("addFactor",
                               factorData.value("factorId").toString(),
                               false,
                               "validation_failed",
                               errorMessage);
        return QString();
    }

    QVariantMap dataToSave = factorData;
    if (!dataToSave.contains("factorId") || dataToSave["factorId"].toString().isEmpty()) {
        QString factorName = dataToSave["factorName"].toString();
        QString factorId = generateFactorId(factorName);
        dataToSave["factorId"] = factorId;
    }

    QString factorId = dataToSave["factorId"].toString();

    bool dbSuccess = saveFactorToDatabase(dataToSave);
    if (!dbSuccess) {
        QString errorMsg = QString("因子保存到数据库失败: %1").arg(factorId);
        qWarning() << errorMsg;
        publishOperationReport("addFactor", factorId, false, "save_database_failed", errorMsg);
        return QString();
    }

    if (!syncFactorDefinitionToDomain(dataToSave)) {
        qWarning() << "FactorService::addFactor: 同步 factor_instance 失败，回滚 factors 表:" << factorId;
        deleteFactorFromDatabase(factorId);
        publishOperationReport("addFactor",
                               factorId,
                               false,
                               "sync_domain_failed_rolled_back",
                               QString("同步 factor_instance 失败，已回滚 factors 表: %1").arg(factorId));
        return QString();
    }

    const QVariantMap syncedFactor = getFactorDefinitionFromDomain(factorId);
    if (!syncedFactor.isEmpty()) {
        dataToSave = syncedFactor;
    }

    {
        QWriteLocker locker(&m_rwLock);
        m_memoryCache[factorId] = dataToSave;

        QString cacheKey = QString("factor_%1").arg(factorId);
        QVariantList factorList;
        factorList.append(dataToSave);
        DataServiceCache::getInstance().storeData(cacheKey, factorList);
    }

    if (m_viewModel) {
        m_viewModel->appendData(dataToSave);
    }

    emit factorAdded(factorId, dataToSave);
    emit dataChanged();

    publishOperationReport("addFactor", factorId, true, "completed", QStringLiteral("新增因子成功"));

    qDebug() << "FactorService::addFactor 结束，新增因子ID:" << factorId;
    return factorId;
}

bool FactorService::updateFactor(const QString& factorId, const QVariantMap& factorData)
{
    QMutexLocker mutationLocker(&m_mutationMutex);
    setMutationInProgress(true);
    const auto mutationGuard = std::shared_ptr<void>(nullptr, [this](void*) {
        setMutationInProgress(false);
    });

    qDebug() << "FactorService::updateFactor 开始，因子ID:" << factorId;

    const QString effectiveFactorId = resolveRepositoryFactorId(factorId);
    const QString targetFactorId = effectiveFactorId.isEmpty() ? factorId : effectiveFactorId;

    QString errorMessage;
    if (!validateFactorData(factorData, errorMessage)) {
        qWarning() << "因子数据验证失败:" << errorMessage;
        publishOperationReport("updateFactor", targetFactorId, false, "validation_failed", errorMessage);
        return false;
    }

    QVariantMap dataToUpdate = factorData;
    dataToUpdate["factorId"] = targetFactorId;

    QVariantMap previousFactor;
    if (m_repository) {
        previousFactor = m_repository->findById(targetFactorId);
    }

    bool dbSuccess = updateFactorInDatabase(targetFactorId, dataToUpdate);
    if (!dbSuccess) {
        QString errorMsg = QString("因子更新到数据库失败: %1").arg(targetFactorId);
        qWarning() << errorMsg;
        publishOperationReport("updateFactor", targetFactorId, false, "update_database_failed", errorMsg);
        return false;
    }

    if (!syncFactorDefinitionToDomain(dataToUpdate)) {
        qWarning() << "FactorService::updateFactor: 同步 factor_instance 失败，尝试回滚 factors 表:" << targetFactorId;
        if (!previousFactor.isEmpty()) {
            updateFactorInDatabase(targetFactorId, previousFactor);
        }
        publishOperationReport("updateFactor",
                               targetFactorId,
                               false,
                               "sync_domain_failed_rolled_back",
                               QString("同步 factor_instance 失败，已回滚 factors 表: %1").arg(targetFactorId));
        return false;
    }

    const QVariantMap syncedFactor = getFactorDefinitionFromDomain(targetFactorId);
    if (!syncedFactor.isEmpty()) {
        dataToUpdate = syncedFactor;
    }

    saveFactorToCache(targetFactorId, dataToUpdate);

    if (m_viewModel) {
        m_viewModel->updateFactor(targetFactorId, dataToUpdate);
    }

    emit factorUpdated(targetFactorId, dataToUpdate);
    emit dataChanged();

    publishOperationReport("updateFactor", targetFactorId, true, "completed", QStringLiteral("更新因子成功"));

    qDebug() << "FactorService::updateFactor 结束，更新因子ID:" << targetFactorId;
    return true;
}

bool FactorService::deleteFactor(const QString& factorId)
{
    QMutexLocker mutationLocker(&m_mutationMutex);
    setMutationInProgress(true);
    const auto mutationGuard = std::shared_ptr<void>(nullptr, [this](void*) {
        setMutationInProgress(false);
    });

    qDebug() << "FactorService::deleteFactor 开始，因子ID:" << factorId;

    const QString effectiveFactorId = resolveRepositoryFactorId(factorId);
    const QString targetFactorId = effectiveFactorId.isEmpty() ? factorId : effectiveFactorId;

    const bool domainDeleted = removeFactorDefinitionFromDomain(targetFactorId);
    if (!domainDeleted) {
        QString errorMsg = QString("因子从 factor_instance 删除失败: %1").arg(targetFactorId);
        qWarning() << errorMsg;
        publishOperationReport("deleteFactor", targetFactorId, false, "delete_domain_failed", errorMsg);
        return false;
    }

    bool dbSuccess = deleteFactorFromDatabase(targetFactorId);
    if (!dbSuccess) {
        QString errorMsg = QString("因子从数据库删除失败: %1").arg(targetFactorId);
        qWarning() << errorMsg;
        publishOperationReport("deleteFactor", targetFactorId, false, "delete_database_failed", errorMsg);
        return false;
    }

    removeFactorFromCache(targetFactorId);
    if (targetFactorId != factorId) {
        removeFactorFromCache(factorId);
    }

    if (m_viewModel) {
        m_viewModel->removeFactor(targetFactorId);
    }

    emit factorDeleted(targetFactorId);
    emit dataChanged();

    publishOperationReport("deleteFactor", targetFactorId, true, "completed", QStringLiteral("删除因子成功"));

    qDebug() << "FactorService::deleteFactor 结束，删除因子ID:" << targetFactorId;
    return true;
}

QVariantMap FactorService::getFactorById(const QString& factorId)
{
    qDebug() << "FactorService::getFactorById 开始，因子ID:" << factorId;

    QVariantMap cachedFactor = loadFactorFromCache(factorId);
    if (!cachedFactor.isEmpty() && cachedFactor.contains("parameters")) {
        qDebug() << "从缓存获取因子:" << factorId;
        return cachedFactor;
    }

    if (!cachedFactor.isEmpty()) {
        qDebug() << "缓存中的因子缺少参数信息，重新从数据库加载:" << factorId;
    }

    const QVariantMap domainFactor = getFactorDefinitionFromDomain(factorId);
    if (!domainFactor.isEmpty()) {
        const QString canonicalFactorId = domainFactor.value("factorId").toString();
        if (!canonicalFactorId.isEmpty()) {
            saveFactorToCache(canonicalFactorId, domainFactor);
        }
        if (!factorId.isEmpty() && factorId != canonicalFactorId) {
            saveFactorToCache(factorId, domainFactor);
        }
        qDebug() << "从 factor_instance 获取因子:" << factorId;
        return domainFactor;
    }

    if (!m_repository) {
        qWarning() << "FactorService::getFactorById: Repository not initialized";
        return QVariantMap();
    }

    try {
        QVariantMap factorMap = m_repository->findById(factorId);
        if (!factorMap.isEmpty()) {
            saveFactorToCache(factorId, factorMap);
            qDebug() << "从数据库获取因子:" << factorId;
        }

        return factorMap;

    } catch (const std::exception& e) {
        qWarning() << "FactorService::getFactorById: Error:" << e.what();
        return QVariantMap();
    }
}

QVariantList FactorService::getAllFactors()
{
    qDebug() << "FactorService::getAllFactors 开始";
    
    // 首先检查缓存是否已加载
    if (m_cacheLoaded) {
        // 从内存缓存获取所有因子
        QReadLocker locker(&m_rwLock);
        if (!m_memoryCache.isEmpty()) {
            QVariantList factors;
            for (const auto& factor : m_memoryCache) {
                factors.append(factor);
            }
            qDebug() << "FactorService::getAllFactors 从因子定义缓存获取，数量:" << factors.size();
            return factors;
        }
    }
    
    const QVariantList domainFactors = getAllFactorDefinitionsFromDomain();
    if (!domainFactors.isEmpty()) {
        {
            QWriteLocker locker(&m_rwLock);
            m_memoryCache.clear();
            for (const QVariant& factorVariant : domainFactors) {
                const QVariantMap factorMap = factorVariant.toMap();
                const QString cachedFactorId = factorMap.value("factorId").toString();
                if (!cachedFactorId.isEmpty()) {
                    m_memoryCache[cachedFactorId] = factorMap;
                }
            }
            m_cacheLoaded = true;
        }

        if (m_viewModel) {
            m_viewModel->updateData(domainFactors);
        }
        emit factorsLoaded(domainFactors);
        qDebug() << "FactorService::getAllFactors 从 factor_instance 获取，数量:" << domainFactors.size();
        return domainFactors;
    }

    // 缓存未加载或为空，从数据库加载
    QVariantList factors = loadFactorsFromDatabase();
    
    // 更新缓存加载标志
    if (!factors.isEmpty()) {
        m_cacheLoaded = true;
    }
    
    qDebug() << "FactorService::getAllFactors 结束，获取因子数量:" << factors.size();
    return factors;
}

QVariantList FactorService::searchFactors(const QString& keyword)
{
    const QString normalizedKeyword = keyword.trimmed();
    if (normalizedKeyword.isEmpty()) {
        return getAllFactors();
    }

    const QVariantList factors = getAllFactors();
    QVariantList result;
    for (const QVariant& factorVariant : factors) {
        const QVariantMap factor = factorVariant.toMap();
        const QStringList searchableTexts = {
            firstNonEmptyText(factor, {"factorName", "name"}),
            firstNonEmptyText(factor, {"displayName", "display_name"}),
            firstNonEmptyText(factor, {"description"}),
            firstNonEmptyText(factor, {"majorCategory", "major_category", "factorType", "factor_type"}),
            firstNonEmptyText(factor, {"subCategory", "sub_category"})
        };

        bool matched = false;
        for (const QString& text : searchableTexts) {
            if (!text.isEmpty() && containsTextCaseInsensitive(text, normalizedKeyword)) {
                matched = true;
                break;
            }
        }

        if (!matched) {
            const QStringList tags = factorTagList(factor);
            for (const QString& tag : tags) {
                if (containsTextCaseInsensitive(tag, normalizedKeyword)) {
                    matched = true;
                    break;
                }
            }
        }

        if (matched) {
            result.append(factor);
        }
    }

    return result;
}

QVariantList FactorService::filterFactorsByCategory(const QString& category)
{
    const QString normalizedCategory = category.trimmed();
    if (normalizedCategory.isEmpty() || normalizedCategory.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) {
        return getAllFactors();
    }

    const QVariantList factors = getAllFactors();
    QVariantList result;
    for (const QVariant& factorVariant : factors) {
        const QVariantMap factor = factorVariant.toMap();
        const QString majorCategory = firstNonEmptyText(factor, {"majorCategory", "major_category", "factorType", "factor_type"});
        const QString subCategory = firstNonEmptyText(factor, {"subCategory", "sub_category"});
        if (majorCategory == normalizedCategory || subCategory == normalizedCategory) {
            result.append(factor);
        }
    }

    return result;
}

QVariantList FactorService::filterFactorsByTags(const QStringList& tags)
{
    QStringList normalizedTags;
    for (const QString& tag : tags) {
        const QString normalized = tag.trimmed();
        if (!normalized.isEmpty()) {
            normalizedTags.append(normalized);
        }
    }
    normalizedTags.removeDuplicates();

    if (normalizedTags.isEmpty()) {
        return {};
    }

    const QVariantList factors = getAllFactors();
    QVariantList result;
    for (const QVariant& factorVariant : factors) {
        const QVariantMap factor = factorVariant.toMap();
        const QStringList factorTags = factorTagList(factor);

        bool matchesAll = true;
        for (const QString& tag : normalizedTags) {
            if (!factorTags.contains(tag)) {
                matchesAll = false;
                break;
            }
        }

        if (matchesAll) {
            result.append(factor);
        }
    }

    return result;
}

// 私有方法实现

void FactorService::initializeRepository()
{
    try {
        // 首先确保数据库连接管理器已初始化
        // 这会配置ConnectionPool，确保FactorRepository使用的连接池有正确的配置
        auto& dbManager = astock::database::DatabaseConnectionManager::instance();
        if (!dbManager.initialize()) {
            qWarning() << "FactorService::initializeRepository: Database connection manager initialization failed";
            //emit errorOccurred("数据库连接初始化失败");
            return;
        }
        
        qDebug() << "✅ FactorService::initializeRepository: Database connection manager initialized";
        
        // 创建因子仓储实例（使用新的无参数构造函数）
        auto repository = std::make_shared<astock::database::FactorRepository>();
        if (!repository) {
            qWarning() << "FactorService::initializeRepository: Failed to create repository";
            return;
        }
        
        // 初始化数据库表
        if (!repository->initialize()) {
            qWarning() << "FactorService::initializeRepository: Failed to initialize database tables";
            return;
        }
        
        m_repository = repository;
        qDebug() << "✅ FactorService::initializeRepository: Repository initialized successfully";
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::initializeRepository: Error:" << e.what();
            //emit errorOccurred(QString("仓储初始化失败: %1").arg(e.what()));
    }
}

bool FactorService::saveFactorToDatabase(const QVariantMap& factorData)
{
    qDebug() << "FactorService::saveFactorToDatabase 开始，因子ID:" << factorData.value("factorId").toString();
    
    if (!m_repository) {
        qWarning() << "FactorService::saveFactorToDatabase: Repository not initialized";
        return false;
    }
    
    try {
        bool success = m_repository->save(factorData);
        qDebug() << "FactorService::saveFactorToDatabase 结果:" << success;
        
        return success;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::saveFactorToDatabase: Error:" << e.what();
        return false;
    }
}

bool FactorService::updateFactorInDatabase(const QString& factorId, const QVariantMap& factorData)
{
    if (!m_repository) {
        qWarning() << "FactorService::updateFactorInDatabase: Repository not initialized";
        return false;
    }
    
    try {
        bool success = m_repository->update(factorId, factorData);
        return success;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::updateFactorInDatabase: Error:" << e.what();
        return false;
    }
}

bool FactorService::deleteFactorFromDatabase(const QString& factorId)
{
    if (!m_repository) {
        qWarning() << "FactorService::deleteFactorFromDatabase: Repository not initialized";
        return false;
    }
    
    try {
        bool success = m_repository->remove(factorId);
        return success;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::deleteFactorFromDatabase: Error:" << e.what();
        return false;
    }
}

QVariantList FactorService::loadFactorsFromDatabase()
{
   // qDebug() << "FactorService::loadFactorsFromDatabase: 开始加载因子数据";
    try {
        const QVariantList domainFactors = getAllFactorDefinitionsFromDomain();
        if (!domainFactors.isEmpty()) {
            {
                QWriteLocker locker(&m_rwLock);
                m_memoryCache.clear();

                for (const QVariant& factorVariant : domainFactors) {
                    const QVariantMap factorMap = factorVariant.toMap();
                    const QString factorId = factorMap.value("factorId").toString();
                    if (!factorId.isEmpty()) {
                        m_memoryCache[factorId] = factorMap;
                    }
                }

                m_cacheLoaded = true;
            }

            if (m_viewModel) {
                m_viewModel->updateData(domainFactors);
            } else {
                qWarning() << "FactorService::loadFactorsFromDatabase: 视图模型为空，无法更新";
            }

            emit factorsLoaded(domainFactors);
            return domainFactors;
        }

        qDebug() << "FactorService::loadFactorsFromDatabase: 调用 m_repository->findAll()...";
        // 从数据库加载所有因子
        auto factorMaps = m_repository->findAll();
        //qDebug() << "FactorService::loadFactorsFromDatabase: 数据库查询返回" << factorMaps.size() << "个因子";
        
        // 转换为QVariantList
        QVariantList factors;
        
        // 保存到内存缓存
        {
            QWriteLocker locker(&m_rwLock);
            m_memoryCache.clear(); // 清空现有缓存
            
            for (const auto& factorMap : factorMaps) {
                QString factorId = factorMap["factorId"].toString();
                if (!factorId.isEmpty()) {
                    m_memoryCache[factorId] = factorMap;
                }
                factors.append(factorMap);
            }
            
            // 设置缓存已加载标志
            m_cacheLoaded = true;
        }
        
        // 直接更新视图模型

        if (m_viewModel) {
           // qDebug() << "FactorService::loadFactorsFromDatabase: 更新视图模型，因子数量:" << factors.size();
            m_viewModel->updateData(factors);
        } else {
            qWarning() << "FactorService::loadFactorsFromDatabase: 视图模型为空，无法更新";
        }
        
        // 发出加载完成信号
        emit factorsLoaded(factors);
        
        //qDebug() << "FactorService::loadFactorsFromDatabase: 加载完成，缓存因子数量:" << m_memoryCache.size();
        return factors;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::loadFactorsFromDatabase: Error:" << e.what();
        return QVariantList();
    }
}

void FactorService::saveFactorToCache(const QString& factorId, const QVariantMap& factorData)
{
    try {
        // 保存到内存缓存 - 使用写锁保护整个操作
        QWriteLocker locker(&m_rwLock);
        m_memoryCache[factorId] = factorData;
        
        // 保存到全局缓存
        QString cacheKey = QString("factor_%1").arg(factorId);
        QVariantList factorList;
        factorList.append(factorData);
        
        DataServiceCache::getInstance().storeData(cacheKey, factorList);
        
        qDebug() << "FactorService::saveFactorToCache: Saved factor to cache:" << factorId;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::saveFactorToCache: Error:" << e.what();
    }
}

QVariantMap FactorService::loadFactorFromCache(const QString& factorId)
{
    try {
        // 首先尝试从内存缓存获取 - 使用读锁
        {
            QReadLocker locker(&m_rwLock);
            if (m_memoryCache.contains(factorId)) {
                qDebug() << "FactorService::loadFactorFromCache: Loaded from memory cache:" << factorId;
                return m_memoryCache[factorId];
            }
        }
        
        // 从全局缓存获取
        QString cacheKey = QString("factor_%1").arg(factorId);
        QVariantList cachedData = DataServiceCache::getInstance().getData(cacheKey);
        
        if (!cachedData.isEmpty() && cachedData[0].canConvert<QVariantMap>()) {
            QVariantMap factorData = cachedData[0].toMap();
            
            // 保存到内存缓存 - 使用写锁
            QWriteLocker locker(&m_rwLock);
            m_memoryCache[factorId] = factorData;
            
            //qDebug() << "FactorService::loadFactorFromCache: Loaded from global cache:" << factorId;
            return factorData;
        }
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::loadFactorFromCache: Error:" << e.what();
    }
    
    return QVariantMap();
}

void FactorService::removeFactorFromCache(const QString& factorId)
{
    try {
        // 从内存缓存删除 - 使用写锁
        {
            QWriteLocker locker(&m_rwLock);
            m_memoryCache.remove(factorId);
        }
        
        // 从全局缓存删除
        QString cacheKey = QString("factor_%1").arg(factorId);
        DataServiceCache::getInstance().removeData(cacheKey);
        
        //qDebug() << "FactorService::removeFactorFromCache: Removed factor from cache:" << factorId;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::removeFactorFromCache: Error:" << e.what();
    }
}

void FactorService::clearAllCache()
{
    try {
        // 清空内存缓存 - 使用写锁
        {
            QWriteLocker locker(&m_rwLock);
            m_memoryCache.clear();
            m_cacheLoaded = false;  // 重置缓存加载标志
        }
        
        // 清空所有因子相关的全局缓存
        // 这里可以添加更精确的缓存清理逻辑
        
        qDebug() << "FactorService::clearAllCache: Cleared all cache, cacheLoaded reset to false";
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::clearAllCache: Error:" << e.what();
    }
}

bool FactorService::validateFactorData(const QVariantMap& factorData, QString& errorMessage)
{
    // 检查必要字段
    if (!factorData.contains("factorName") || factorData["factorName"].toString().isEmpty()) {
        errorMessage = "因子名称不能为空";
        return false;
    }
    
    if (!factorData.contains("displayName") || factorData["displayName"].toString().isEmpty()) {
        errorMessage = "显示名称不能为空";
        return false;
    }
    
    if (!factorData.contains("majorCategory") || factorData["majorCategory"].toString().isEmpty()) {
        errorMessage = "主类别不能为空";
        return false;
    }
    
    // 检查数值范围
    if (factorData.contains("icValue")) {
        double icValue = factorData["icValue"].toDouble();
        if (icValue < -1.0 || icValue > 1.0) {
            errorMessage = "IC值必须在-1.0到1.0之间";
            return false;
        }
    }
    
    if (factorData.contains("irValue")) {
        double irValue = factorData["irValue"].toDouble();
        if (irValue < 0.0) {
            errorMessage = "IR值不能为负数";
            return false;
        }
    }
    
    if (factorData.contains("validityDays")) {
        int validityDays = factorData["validityDays"].toInt();
        if (validityDays < 1 || validityDays > 365) {
            errorMessage = "有效天数必须在1到365之间";
            return false;
        }
    }
    
    if (factorData.contains("turnoverRate")) {
        double turnoverRate = factorData["turnoverRate"].toDouble();
        if (turnoverRate < 0.0 || turnoverRate > 1.0) {
            errorMessage = "换手率必须在0.0到1.0之间";
            return false;
        }
    }
    
    return true;
}

QString FactorService::generateFactorId(const QString& factorName)
{
    // 生成唯一的因子ID：因子名称_时间戳
    QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString sanitizedName = factorName.toLower().replace(QRegularExpression("[^a-z0-9_]"), "_");
    return QString("%1_%2").arg(sanitizedName).arg(timestamp);
}

QString FactorService::resolveRepositoryFactorId(const QString& factorId) const
{
    if (!m_database || factorId.trimmed().isEmpty()) {
        return {};
    }

    try {
        const auto result = m_database->executeQuery(
            "SELECT factor_id FROM factor_instance WHERE instance_id = :id LIMIT 1",
            {{":id", factorId.trimmed()}}
        );

        if (!result.isEmpty()) {
            return result.getRow(0).getString("factor_id").trimmed();
        }
    } catch (const std::exception& e) {
        qWarning() << "FactorService::resolveRepositoryFactorId failed:" << e.what();
    }

    return {};
}

QVariantMap FactorService::getFactorDefinitionFromDomain(const QString& factorId) const
{
    if (!m_database || factorId.trimmed().isEmpty()) {
        return {};
    }

    try {
        const auto result = m_database->executeQuery(
            "SELECT fi.instance_id, fi.factor_id, fi.instance_name, fi.description AS instance_description, "
            "CAST(fi.full_config AS CHAR) AS full_config, fi.status AS instance_status, "
            "f.factor_name, f.display_name, f.major_category, f.sub_category, "
            "f.description AS factor_description, f.ic_value, f.ir_value, f.validity_days, f.turnover_rate, "
            "f.is_recommended, f.is_favorite, f.status AS factor_status, "
            "f.creator, f.create_date "
            "FROM factor_instance fi "
            "LEFT JOIN factors f ON fi.factor_id = f.factor_id "
            "WHERE fi.status = 'ACTIVE' AND (fi.instance_id = :id OR fi.factor_id = :id) "
            "ORDER BY CASE WHEN fi.instance_id = :id THEN 0 ELSE 1 END, fi.updated_at DESC, fi.created_at DESC "
            "LIMIT 1",
            {{":id", factorId.trimmed()}}
        );

        if (!result.isEmpty()) {
            return buildDomainFactorMap(result.getRow(0));
        }
    } catch (const std::exception& e) {
        qWarning() << "FactorService::getFactorDefinitionFromDomain failed:" << e.what();
    }

    return {};
}

QVariantList FactorService::getAllFactorDefinitionsFromDomain() const
{
    QVariantList factors;
    if (!m_database) {
        return factors;
    }

    try {
        const auto result = m_database->executeQuery(
            "SELECT fi.instance_id, fi.factor_id, fi.instance_name, fi.description AS instance_description, "
            "CAST(fi.full_config AS CHAR) AS full_config, fi.status AS instance_status, "
            "f.factor_name, f.display_name, f.major_category, f.sub_category, "
            "f.description AS factor_description, f.ic_value, f.ir_value, f.validity_days, f.turnover_rate, "
            "f.is_recommended, f.is_favorite, f.status AS factor_status, "
            "f.creator, f.create_date "
            "FROM factor_instance fi "
            "LEFT JOIN factors f ON fi.factor_id = f.factor_id "
            "WHERE fi.status = 'ACTIVE' "
            "ORDER BY COALESCE(fi.factor_id, fi.instance_id), fi.updated_at DESC, fi.created_at DESC"
        );

        QSet<QString> seenFactorIds;
        for (size_t i = 0; i < result.rowCount(); ++i) {
            const QVariantMap factorMap = buildDomainFactorMap(result.getRow(i));
            const QString factorKey = factorMap.value("factorId").toString();
            if (factorKey.isEmpty() || seenFactorIds.contains(factorKey)) {
                continue;
            }

            seenFactorIds.insert(factorKey);
            factors.append(factorMap);
        }
    } catch (const std::exception& e) {
        qWarning() << "FactorService::getAllFactorDefinitionsFromDomain failed:" << e.what();
    }

    return factors;
}

bool FactorService::verifyDomainInstanceReady(const QString& instanceId, QString* errorMessage)
{
    if (!initializeFactorDomainRuntime() || !m_factorInstanceManager) {
        if (errorMessage) {
            *errorMessage = "domain/factor 运行时未初始化";
        }
        return false;
    }

    const QString trimmedInstanceId = instanceId.trimmed();
    if (trimmedInstanceId.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "instanceId 为空";
        }
        return false;
    }

    m_factorInstanceManager->refreshCache();
    const auto instance = m_factorInstanceManager->createInstance(trimmedInstanceId.toStdString());
    if (!instance) {
        if (errorMessage) {
            *errorMessage = QString("无法创建运行时因子实例: %1").arg(trimmedInstanceId);
        }
        return false;
    }

    return true;
}

bool FactorService::syncFactorDefinitionToDomain(const QVariantMap& factorData)
{
    if (m_syncFactorDefinitionOverrideForTests) {
        return m_syncFactorDefinitionOverrideForTests(factorData);
    }

    if (!initializeFactorDomainRuntime()) {
        qWarning() << "FactorService::syncFactorDefinitionToDomain: domain runtime 未初始化";
        return false;
    }

    const QString factorId = factorData.value("factorId").toString().trimmed();
    if (factorId.isEmpty()) {
        qWarning() << "FactorService::syncFactorDefinitionToDomain: factorId 为空";
        return false;
    }

    QString instanceId = determineDomainInstanceId(factorData);
    if (instanceId.isEmpty()) {
        qWarning() << "FactorService::syncFactorDefinitionToDomain: instanceId 为空";
        return false;
    }

    QString instanceName = factorData.value("displayName").toString().trimmed();
    if (instanceName.isEmpty()) {
        instanceName = factorData.value("factorName").toString().trimmed();
    }
    if (instanceName.isEmpty()) {
        instanceName = factorId;
    }

    QString status = factorData.value("status").toString().trimmed();
    if (status.isEmpty()) {
        status = "ACTIVE";
    } else {
        status = status.toUpper();
    }

    const QString fullConfig = QString::fromUtf8(
        QJsonDocument(buildDomainConfigObject(factorData)).toJson(QJsonDocument::Compact)
    );

    try {
        auto writeDomainRecord = [&](QString* persistedInstanceId, bool forceRequestedInstanceId) -> bool {
            const auto existingResult = m_database->executeQuery(
                "SELECT instance_id, factor_id FROM factor_instance WHERE instance_id = :instanceId OR factor_id = :factorId "
                "ORDER BY CASE WHEN instance_id = :instanceId THEN 0 ELSE 1 END, updated_at DESC, created_at DESC",
                {
                    {":instanceId", instanceId},
                    {":factorId", factorId}
                }
            );

            QVector<factor::bridge::FactorDomainExistingRecord> existingRecords;
            existingRecords.reserve(static_cast<int>(existingResult.rowCount()));
            for (const auto& row : existingResult.getRows()) {
                factor::bridge::FactorDomainExistingRecord record;
                record.instanceId = row.getString("instance_id").trimmed();
                record.factorId = row.getString("factor_id").trimmed();
                existingRecords.append(record);
            }

            const factor::bridge::FactorDomainSyncWritePlan writePlan =
                factor::bridge::planFactorDomainSyncWrite(instanceId, factorId, existingRecords, forceRequestedInstanceId);
            const QString actualInstanceId = writePlan.persistedInstanceId;
            if (actualInstanceId.isEmpty()) {
                return false;
            }

            bool writeOk = false;
            if (writePlan.updateExisting) {
                writeOk = m_database->executeUpdate(
                    "UPDATE factor_instance SET factor_id = :factorId, instance_name = :instanceName, "
                    "description = :description, full_config = :fullConfig, status = :status, updated_at = CURRENT_TIMESTAMP "
                    "WHERE instance_id = :instanceId",
                    {
                        {":factorId", factorId},
                        {":instanceName", instanceName},
                        {":description", factorData.value("description").toString()},
                        {":fullConfig", fullConfig},
                        {":status", status},
                        {":instanceId", actualInstanceId}
                    }
                ) > 0;
            } else {
                writeOk = m_database->executeUpdate(
                    "INSERT INTO factor_instance (instance_id, factor_id, instance_name, description, full_config, status) "
                    "VALUES (:instanceId, :factorId, :instanceName, :description, :fullConfig, :status)",
                    {
                        {":instanceId", actualInstanceId},
                        {":factorId", factorId},
                        {":instanceName", instanceName},
                        {":description", factorData.value("description").toString()},
                        {":fullConfig", fullConfig},
                        {":status", status}
                    }
                ) > 0;
            }

            if (!writeOk) {
                return false;
            }

            if (!writePlan.duplicateInstanceIds.isEmpty()) {
                m_database->executeUpdate(
                    "DELETE FROM factor_instance WHERE factor_id = :factorId AND instance_id <> :instanceId",
                    {
                        {":factorId", factorId},
                        {":instanceId", actualInstanceId}
                    }
                );
            }

            if (persistedInstanceId) {
                *persistedInstanceId = actualInstanceId;
            }
            return true;
        };

        return factor::bridge::executeDomainSyncWithRetry(
            instanceId,
            factorId,
            writeDomainRecord,
            [this](const QString& candidateInstanceId, QString* errorMessage) {
                return verifyDomainInstanceReady(candidateInstanceId, errorMessage);
            },
            [this](const QString& candidateFactorId, const QString& candidateInstanceId) {
                m_database->executeUpdate(
                    "DELETE FROM factor_instance WHERE factor_id = :factorId OR instance_id = :instanceId",
                    {
                        {":factorId", candidateFactorId},
                        {":instanceId", candidateInstanceId}
                    }
                );
            },
            nullptr,
            [](const QString&, const QString& errorMessage) {
                qWarning() << "FactorService::syncFactorDefinitionToDomain: 首次实例验证失败，尝试重建记录:" << errorMessage;
            },
            [](const QString&, const QString& errorMessage) {
                qWarning() << "FactorService::syncFactorDefinitionToDomain: 重建后实例验证仍失败:" << errorMessage;
            }
        );
    } catch (const std::exception& e) {
        qWarning() << "FactorService::syncFactorDefinitionToDomain failed:" << e.what();
        return false;
    }
}

bool FactorService::removeFactorDefinitionFromDomain(const QString& factorId)
{
    if (m_removeFactorDefinitionOverrideForTests) {
        return m_removeFactorDefinitionOverrideForTests(factorId);
    }

    if (!initializeFactorDomainRuntime()) {
        qWarning() << "FactorService::removeFactorDefinitionFromDomain: domain runtime 未初始化";
        return false;
    }

    const QString trimmedId = factorId.trimmed();
    if (trimmedId.isEmpty()) {
        return false;
    }

    try {
        const auto existingResult = m_database->executeQuery(
            "SELECT instance_id FROM factor_instance WHERE instance_id = :id OR factor_id = :id LIMIT 1",
            {{":id", trimmedId}}
        );
        if (existingResult.isEmpty()) {
            return true;
        }

        const int affectedRows = m_database->executeUpdate(
            "DELETE FROM factor_instance WHERE instance_id = :id OR factor_id = :id",
            {{":id", trimmedId}}
        );
        return affectedRows > 0;
    } catch (const std::exception& e) {
        qWarning() << "FactorService::removeFactorDefinitionFromDomain failed:" << e.what();
        return false;
    }
}

bool FactorService::initializeFactorDomainRuntime()
{
    if (m_factorInstanceManager && m_database && m_dataChecker && m_factorCacheManager) {
        return true;
    }

    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        qWarning() << "FactorService::initializeFactorDomainRuntime: 无法获取数据库连接";
        return false;
    }

    m_database = std::move(database);
    m_dataChecker = std::make_shared<factor::DataAvailabilityChecker>(m_database);
    m_factorCacheManager = std::make_shared<factor::FactorCacheManager>();
    m_factorInstanceManager = std::make_shared<factor::FactorInstanceManager>(m_database, m_dataChecker);

    auto& cacheFacade = AStockQuantEngine::Cache::CacheFacade::getInstance();
    if (!cacheFacade.isEnabled()) {
        AStockQuantEngine::Cache::CacheConfig cacheConfig;
        cacheFacade.initialize(cacheConfig);
    }

    m_factorCacheManager->setCacheFacade(
        std::shared_ptr<AStockQuantEngine::Cache::CacheFacade>(&cacheFacade, [](AStockQuantEngine::Cache::CacheFacade*) {})
    );

    return static_cast<bool>(m_factorInstanceManager);
}

QString FactorService::resolveDomainInstanceId(const QString& factorId) const
{
    if (!m_database) {
        return {};
    }

    const QString rawId = factorId.trimmed();
    if (rawId.isEmpty()) {
        return {};
    }
    const auto candidates = factor::bridge::buildFactorInstanceLookupCandidates(rawId);
    const auto result = m_database->executeQuery(
        QString(
            "SELECT instance_id, factor_id, status FROM factor_instance "
            "WHERE instance_id = :instanceIdPrimary OR factor_id = :factorIdPrimary "
            "OR instance_id = :instanceIdSecondary OR factor_id = :factorIdSecondary "
            "ORDER BY updated_at DESC, created_at DESC"
        ),
        {
            {":instanceIdPrimary", candidates.primaryId},
            {":factorIdPrimary", candidates.primaryId},
            {":instanceIdSecondary", candidates.secondaryId},
            {":factorIdSecondary", candidates.secondaryId}
        }
    );

    QVector<factor::bridge::FactorInstanceLookupRecord> records;
    records.reserve(static_cast<int>(result.rowCount()));
    for (const auto& row : result.getRows()) {
        factor::bridge::FactorInstanceLookupRecord record;
        record.instanceId = row.getString("instance_id").trimmed();
        record.factorId = row.getString("factor_id").trimmed();
        record.status = row.getString("status").trimmed();
        records.append(record);
    }

    return factor::bridge::resolveFactorInstanceId(rawId, records);
}

QString FactorService::determineDomainInstanceId(const QVariantMap& factorData) const
{
    const QString explicitInstanceId = factorData.value("instanceId").toString().trimmed();
    if (!explicitInstanceId.isEmpty()) {
        return explicitInstanceId;
    }

    const QString factorId = factorData.value("factorId").toString().trimmed();
    if (factorId.isEmpty()) {
        return {};
    }

    const QString resolvedInstanceId = resolveDomainInstanceId(factorId);
    if (!resolvedInstanceId.isEmpty()) {
        return resolvedInstanceId;
    }

    return factorId;
}

QVariantMap FactorService::getFactorValuesFromDomain(const QString& factorId,
                                                     const QString& resolvedInstanceId,
                                                     const QString& date)
{
    QVariantMap result;
    result["factorId"] = factorId;
    result["instanceId"] = resolvedInstanceId;
    result["date"] = date;

    if (!m_factorInstanceManager) {
        result["stockValues"] = QVariantMap();
        result["count"] = 0;
        result["status"] = "error";
        result["error"] = "domain/factor 运行时未初始化";
        return result;
    }

    const auto factorInstance = m_factorInstanceManager->createInstance(resolvedInstanceId.toStdString());
    if (!factorInstance) {
        result["stockValues"] = QVariantMap();
        result["count"] = 0;
        result["status"] = "error";
        result["error"] = QString("未能创建因子实例: %1").arg(resolvedInstanceId);
        return result;
    }

    factor::CalculationContext context;
    context.date = date.toStdString();

    const factor::CalculationResult calculation = factorInstance->calculate(context);
    if (!calculation.dataStatus.isValid()) {
        result["stockValues"] = QVariantMap();
        result["count"] = 0;
        result["status"] = "error";
        result["error"] = QString::fromStdString(calculation.dataStatus.message);
        return result;
    }

    QVariantMap stockValues;
    for (const auto& [symbol, value] : calculation.values) {
        stockValues[QString::fromStdString(symbol)] = value;
    }

    result["stockValues"] = stockValues;
    result["count"] = stockValues.size();
    result["status"] = "success";
    return result;
}

QVariantMap FactorService::getFactorValuesBatchFromDomain(const QString& factorId,
                                                          const QString& resolvedInstanceId,
                                                          const QStringList& dates)
{
    QVariantMap result;
    result["factorId"] = factorId;
    result["dates"] = dates;

    if (!m_factorInstanceManager) {
        result["batchResults"] = QVariantMap();
        result["totalCount"] = 0;
        result["status"] = "error";
        result["error"] = "domain/factor 运行时未初始化";
        return result;
    }

    const auto factorInstance = m_factorInstanceManager->createInstance(resolvedInstanceId.toStdString());
    if (!factorInstance) {
        result["batchResults"] = QVariantMap();
        result["totalCount"] = 0;
        result["status"] = "error";
        result["error"] = QString("未能创建因子实例: %1").arg(resolvedInstanceId);
        return result;
    }

    std::vector<factor::CalculationContext> contexts;
    contexts.reserve(static_cast<size_t>(dates.size()));
    for (const QString& date : dates) {
        factor::CalculationContext context;
        context.date = date.toStdString();
        contexts.push_back(std::move(context));
    }

    const auto calculations = factorInstance->calculateBatch(contexts);

    QVariantMap batchResults;
    int totalCount = 0;
    for (size_t i = 0; i < calculations.size() && i < static_cast<size_t>(dates.size()); ++i) {
        const QString date = dates.at(static_cast<int>(i));
        const auto& calculation = calculations[i];

        QVariantMap dayResult;
        dayResult["factorId"] = factorId;
        dayResult["instanceId"] = resolvedInstanceId;
        dayResult["date"] = date;

        if (!calculation.dataStatus.isValid()) {
            dayResult["stockValues"] = QVariantMap();
            dayResult["count"] = 0;
            dayResult["status"] = "error";
            dayResult["error"] = QString::fromStdString(calculation.dataStatus.message);
            batchResults[date] = dayResult;
            continue;
        }

        QVariantMap stockValues;
        for (const auto& [symbol, value] : calculation.values) {
            stockValues[QString::fromStdString(symbol)] = value;
        }

        dayResult["stockValues"] = stockValues;
        dayResult["count"] = stockValues.size();
        dayResult["status"] = "success";
        totalCount += stockValues.size();
        batchResults[date] = dayResult;

        QString cacheKey = QString("factor_values_%1_%2").arg(factorId, date);
        QVariantList cacheData;
        cacheData.append(dayResult);
        DataServiceCache::getInstance().storeData(cacheKey, cacheData);
    }

    result["batchResults"] = batchResults;
    result["totalCount"] = totalCount;
    result["status"] = "success";
    return result;
}

// 新增方法实现：获取因子值（带缓存）
QVariantMap FactorService::getFactorValues(const QString& factorId, const QString& date)
{
    qDebug() << "FactorService::getFactorValues 开始，因子ID:" << factorId << "日期:" << date;
    
    // 生成缓存键
    QString cacheKey = QString("factor_values_%1_%2").arg(factorId).arg(date);
    
    // 首先尝试从缓存获取
    QVariantList cachedData = DataServiceCache::getInstance().getData(cacheKey);
    if (!cachedData.isEmpty() && cachedData[0].canConvert<QVariantMap>()) {
        qDebug() << "FactorService::getFactorValues: 从缓存获取数据，因子ID:" << factorId << "日期:" << date;
        return cachedData[0].toMap();
    }

    QString domainFallbackError;

    if (initializeFactorDomainRuntime()) {
        const QString resolvedInstanceId = resolveDomainInstanceId(factorId);
        if (!resolvedInstanceId.isEmpty()) {
            QVariantMap domainResult = getFactorValuesFromDomain(factorId, resolvedInstanceId, date);
            if (domainResult["status"].toString() == "success") {
                QVariantList cacheData;
                cacheData.append(domainResult);
                DataServiceCache::getInstance().storeData(cacheKey, cacheData);
                return domainResult;
            }
            qDebug() << "FactorService::getFactorValues: domain 计算失败，回退桥接实现，因子ID:" << factorId
                     << "错误:" << domainResult.value("error").toString();
            domainFallbackError = domainResult.value("error").toString();
        }
    }
    
    QVariantMap result;
    
    // 获取因子信息
    QVariantMap factorInfo = getFactorById(factorId);
    if (factorInfo.isEmpty()) {
        qWarning() << "FactorService::getFactorValues: 未找到因子:" << factorId;
        result["status"] = "error";
        result["error"] = "未找到因子";
        return result;
    }
    
    // 连接到数据库
    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    auto database = dbManager.getDatabase();
    if (!database) {
        qWarning() << "FactorService::getFactorValues: 无法获取数据库连接";
        result["status"] = "error";
        result["error"] = "数据库连接失败";
        return result;
    }
    
    try {
        QString factorName = factorInfo["factorName"].toString();
        QString majorCategory = factorInfo["majorCategory"].toString();
        QString factorType = normalizeFactorType(majorCategory);
        QVariantMap parameters = factorInfo.value("parameters").toMap();

        auto buildErrorResult = [&](const QString& errorMessage, const QVariantMap& diagnostics) {
            result["factorId"] = factorId;
            result["date"] = date;
            result["stockValues"] = QVariantMap();
            result["count"] = 0;
            result["status"] = "error";
            result["error"] = errorMessage;
            if (!diagnostics.isEmpty()) {
                result["diagnostics"] = diagnostics;
            } else {
                result.remove("diagnostics");
            }
        };

        auto buildSuccessResult = [&](const QVariantMap& stockValues, const QString& message, const QVariantMap& diagnostics) {
            result["factorId"] = factorId;
            result["date"] = date;
            result["stockValues"] = stockValues;
            result["count"] = stockValues.size();
            result["status"] = "success";
            if (!message.isEmpty()) {
                result["message"] = message;
            }
            if (!diagnostics.isEmpty()) {
                result["diagnostics"] = diagnostics;
            } else {
                result.remove("diagnostics");
            }
        };
        
        // 根据因子类型计算因子值
        if (factorName == "pe_ttm_factor" || factorType == "value") {
            QString selectedMetric = normalizeValuationMetric(parameters.value("valuation_type").toString());
            if (selectedMetric.isEmpty()) {
                selectedMetric = normalizeValuationMetric(parameters.value("valuationType").toString());
            }
            if (selectedMetric.isEmpty()) {
                selectedMetric = normalizeValuationMetric(parameters.value("valuationMetric").toString());
            }
            if (selectedMetric.isEmpty()) {
                const QStringList requestedMetrics = variantToStringList(parameters.value("valuationMetrics"));
                if (!requestedMetrics.isEmpty()) {
                    selectedMetric = normalizeValuationMetric(requestedMetrics.first());
                }
            }
            if (selectedMetric.isEmpty() && factorName == "pe_ttm_factor") {
                selectedMetric = "pe";
            }

            QString sql;
            QString columnName;
            bool useLogInverseScore = false;

            if (selectedMetric == "pe" || selectedMetric == "pe_ttm") {
                sql = "SELECT symbol, pe_ratio FROM daily_bar WHERE trade_date = :date AND pe_ratio IS NOT NULL";
                columnName = "pe_ratio";
            } else if (selectedMetric == "pb") {
                sql = "SELECT symbol, pb_ratio FROM daily_bar WHERE trade_date = :date AND pb_ratio IS NOT NULL";
                columnName = "pb_ratio";
            } else if (selectedMetric == "market_cap") {
                sql = "SELECT symbol, market_cap FROM daily_bar WHERE trade_date = :date AND market_cap IS NOT NULL AND market_cap > 0";
                columnName = "market_cap";
                useLogInverseScore = true;
            } else {
                buildErrorResult(QString("当前运行时暂不支持计算 value 指标: %1").arg(selectedMetric.isEmpty() ? QString("unknown") : selectedMetric),
                                 {{"factorType", factorType}, {"metric", selectedMetric}, {"domainFallbackError", domainFallbackError}});
                return result;
            }

            std::map<QString, QVariant> params;
            params[":date"] = date;
            
            auto queryResult = database->executeQuery(sql, params);
            
            QVariantMap stockValues;
            int validRows = 0;
            for (size_t i = 0; i < queryResult.rowCount(); i++) {
                const auto& row = queryResult.getRow(i);
                QString symbol = row.getString("symbol");
                double rawValue = row.getDouble(columnName);
                if (rawValue > 0) {
                    validRows += 1;
                    stockValues[symbol] = useLogInverseScore
                        ? (1.0 / std::log(rawValue + 1.0))
                        : (1.0 / rawValue);
                }
            }

            QVariantMap diagnostics;
            diagnostics["factorType"] = factorType;
            diagnostics["metric"] = selectedMetric;
            diagnostics["sourceTable"] = "daily_bar";
            diagnostics["queriedRows"] = static_cast<int>(queryResult.rowCount());
            diagnostics["validRows"] = validRows;
            diagnostics["filteredZeroOrNegativeRows"] = static_cast<int>(queryResult.rowCount()) - validRows;
            if (!domainFallbackError.isEmpty()) {
                diagnostics["domainFallbackError"] = domainFallbackError;
            }

            buildSuccessResult(stockValues, QString(), diagnostics);

        } else if (factorName == "momentum_60d" || factorType == "momentum") {
            const int window = (std::max)(1, parameters.value("window",
                                                               parameters.value("lookback_window",
                                                                                parameters.value("lookbackWindow", 60))).toInt());
            const int skipRecent = (std::max)(0, parameters.value("skipRecent", parameters.value("skip_recent", 0)).toInt());
            const QString momentumType = parameters.value("type", "simple").toString().trimmed().toLower();

            if (momentumType != "simple" && momentumType != "rank") {
                buildErrorResult(makeUnsupportedMetricError("momentum", momentumType),
                                 {{"factorType", factorType}, {"metric", momentumType}, {"domainFallbackError", domainFallbackError}});
                return result;
            }

            const QDate currentDate = QDate::fromString(date, "yyyy-MM-dd");
            if (!currentDate.isValid()) {
                buildErrorResult(QString("非法日期: %1").arg(date), {{"factorType", factorType}});
                return result;
            }

            const QDate endDate = currentDate.addDays(-skipRecent);
            const QDate startDate = endDate.addDays(-window);
            QString sql = "SELECT curr.symbol, curr.close AS current_close, prev.close AS previous_close "
                          "FROM cleaned_daily_bar curr "
                          "JOIN cleaned_daily_bar prev ON curr.symbol = prev.symbol "
                          "WHERE curr.trade_date = :end_date AND prev.trade_date = :start_date";
            std::map<QString, QVariant> params;
            params[":end_date"] = endDate.toString("yyyy-MM-dd");
            params[":start_date"] = startDate.toString("yyyy-MM-dd");
            
            auto queryResult = database->executeQuery(sql, params);
            
            QVariantMap stockValues;
            std::vector<std::pair<QString, double>> momentumValues;
            for (size_t i = 0; i < queryResult.rowCount(); i++) {
                const auto& row = queryResult.getRow(i);
                QString symbol = row.getString("symbol");
                double currentClose = row.getDouble("current_close");
                double previousClose = row.getDouble("previous_close");
                if (currentClose > 0 && previousClose > 0) {
                    double momentum = (currentClose - previousClose) / previousClose;
                    momentumValues.emplace_back(symbol, momentum);
                    stockValues[symbol] = momentum;
                }
            }

            if (momentumType == "rank" && momentumValues.size() > 1) {
                std::sort(momentumValues.begin(), momentumValues.end(), [](const auto& left, const auto& right) {
                    return left.second < right.second;
                });

                QVariantMap rankedValues;
                const double denominator = static_cast<double>(momentumValues.size() - 1);
                for (size_t index = 0; index < momentumValues.size(); ++index) {
                    rankedValues[momentumValues[index].first] = static_cast<double>(index) / denominator;
                }
                QVariantMap diagnostics;
                diagnostics["factorType"] = factorType;
                diagnostics["metric"] = momentumType;
                diagnostics["sourceTable"] = "cleaned_daily_bar";
                diagnostics["queriedRows"] = static_cast<int>(queryResult.rowCount());
                diagnostics["validRows"] = static_cast<int>(momentumValues.size());
                diagnostics["window"] = window;
                diagnostics["skipRecent"] = skipRecent;
                if (!domainFallbackError.isEmpty()) {
                    diagnostics["domainFallbackError"] = domainFallbackError;
                }
                buildSuccessResult(rankedValues, QString(), diagnostics);
            } else {
                QVariantMap diagnostics;
                diagnostics["factorType"] = factorType;
                diagnostics["metric"] = momentumType;
                diagnostics["sourceTable"] = "cleaned_daily_bar";
                diagnostics["queriedRows"] = static_cast<int>(queryResult.rowCount());
                diagnostics["validRows"] = static_cast<int>(momentumValues.size());
                diagnostics["window"] = window;
                diagnostics["skipRecent"] = skipRecent;
                if (!domainFallbackError.isEmpty()) {
                    diagnostics["domainFallbackError"] = domainFallbackError;
                }
                buildSuccessResult(stockValues, QString(), diagnostics);
            }

        } else if (factorType == "size") {
            QString sizeMetric = normalizeSizeMetric(parameters.value("size_metric").toString());
            if (sizeMetric.isEmpty()) {
                sizeMetric = normalizeSizeMetric(parameters.value("sizeMetric", "market_cap").toString());
            }
            if (sizeMetric.isEmpty()) {
                sizeMetric = "market_cap";
            }

            QString columnName;
            if (sizeMetric == "market_cap") {
                columnName = "market_cap";
            } else if (sizeMetric == "circulating_market_cap") {
                columnName = "circulating_market_cap";
            } else {
                buildErrorResult(makeUnsupportedMetricError("size", sizeMetric),
                                 {{"factorType", factorType}, {"metric", sizeMetric}, {"domainFallbackError", domainFallbackError}});
                return result;
            }

            QString sql = QString("SELECT symbol, %1 AS factor_raw FROM daily_bar WHERE trade_date = :date AND %1 IS NOT NULL AND %1 > 0")
                .arg(columnName);
            std::map<QString, QVariant> params;
            params[":date"] = date;

            auto queryResult = database->executeQuery(sql, params);

            QVariantMap stockValues;
            for (size_t i = 0; i < queryResult.rowCount(); i++) {
                const auto& row = queryResult.getRow(i);
                QString symbol = row.getString("symbol");
                double metricValue = row.getDouble("factor_raw");
                if (metricValue > 0) {
                    stockValues[symbol] = -std::log(metricValue);
                }
            }

            QVariantMap diagnostics;
            diagnostics["factorType"] = factorType;
            diagnostics["metric"] = sizeMetric;
            diagnostics["sourceTable"] = "daily_bar";
            diagnostics["queriedRows"] = static_cast<int>(queryResult.rowCount());
            diagnostics["validRows"] = stockValues.size();
            if (!domainFallbackError.isEmpty()) {
                diagnostics["domainFallbackError"] = domainFallbackError;
            }
            buildSuccessResult(stockValues, QString(), diagnostics);

        } else if (factorType == "quality") {
            const QVariantMap config = factorInfo.value("config").toMap();
            const QVariantMap calculation = config.value("calculation").toMap();

            QString qualityMetric = normalizeQualityMetric(calculation.value("metric").toString());
            if (qualityMetric.isEmpty()) {
                qualityMetric = normalizeQualityMetric(parameters.value("metric").toString());
            }
            if (qualityMetric.isEmpty()) {
                qualityMetric = normalizeQualityMetric(parameters.value("qualityMetric").toString());
            }
            if (qualityMetric.isEmpty()) {
                qualityMetric = "roe";
            }

            const QString timeframe = calculation.value("timeframe", parameters.value("timeframe", "quarterly")).toString();
            double qualityThreshold = calculation.value("quality_threshold", parameters.value("quality_threshold", parameters.value("qualityThreshold", 0.1))).toDouble();
            if (qualityThreshold > 1.0) {
                qualityThreshold /= 100.0;
            }

            QString directColumn;
            if (qualityMetric == "roe") {
                directColumn = "roe";
            } else if (qualityMetric == "roa") {
                directColumn = "roa";
            } else if (qualityMetric == "gross_margin" || qualityMetric == "operating_margin") {
                directColumn = "profit_margin";
            } else if (qualityMetric == "earnings_quality") {
                directColumn = QString();
            } else {
                buildErrorResult(makeUnsupportedMetricError("quality", qualityMetric),
                                 {{"factorType", factorType}, {"metric", qualityMetric}, {"domainFallbackError", domainFallbackError}});
                return result;
            }

            const QString latestClause = buildFinancialReportTypeClause(timeframe, "base");
            const QString outerClause = buildFinancialReportTypeClause(timeframe, "fi");
            const QString sql = QString(
                "SELECT si.symbol, fi.roe, fi.roa, fi.profit_margin, fi.net_profit, fi.equity "
                "FROM financial_indicator fi "
                "JOIN symbol_info si ON si.symbol_id = fi.symbol_id "
                "JOIN ("
                "    SELECT base.symbol_id, MAX(base.report_date) AS latest_report_date "
                "    FROM financial_indicator base "
                "    WHERE base.report_date <= :date%1 "
                "    GROUP BY base.symbol_id"
                ") latest ON latest.symbol_id = fi.symbol_id AND latest.latest_report_date = fi.report_date "
                "WHERE 1=1%2 "
                "ORDER BY si.symbol")
                .arg(latestClause, outerClause);

            auto queryResult = database->executeQuery(sql, {{":date", date}});

            QVariantMap stockValues;
            int validRows = 0;
            int filteredRows = 0;
            for (size_t i = 0; i < queryResult.rowCount(); i++) {
                const auto& row = queryResult.getRow(i);
                double factorValue = 0.0;

                if (!directColumn.isEmpty()) {
                    factorValue = row.getDouble(directColumn);
                } else {
                    const double netProfit = row.getDouble("net_profit");
                    const double equity = row.getDouble("equity");
                    if (netProfit > 0.0 && equity > 0.0) {
                        factorValue = netProfit / equity;
                    }
                }

                if (factorValue <= 0.0 || factorValue < qualityThreshold) {
                    filteredRows += 1;
                    continue;
                }

                validRows += 1;
                stockValues[row.getString("symbol")] = factorValue;
            }

            QVariantMap diagnostics;
            diagnostics["factorType"] = factorType;
            diagnostics["metric"] = qualityMetric;
            diagnostics["sourceTable"] = "financial_indicator";
            diagnostics["queriedRows"] = static_cast<int>(queryResult.rowCount());
            diagnostics["validRows"] = validRows;
            diagnostics["filteredRows"] = filteredRows;
            diagnostics["timeframe"] = timeframe;
            diagnostics["qualityThreshold"] = qualityThreshold;
            if (!directColumn.isEmpty()) {
                diagnostics["metricColumn"] = directColumn;
            } else {
                diagnostics["metricColumn"] = "net_profit/equity";
            }
            if (!domainFallbackError.isEmpty()) {
                diagnostics["domainFallbackError"] = domainFallbackError;
            }

            buildSuccessResult(stockValues, QString(), diagnostics);

        } else if (factorType == "growth") {
            const QVariantMap config = factorInfo.value("config").toMap();
            const QVariantMap calculation = config.value("calculation").toMap();

            QString growthMetric = normalizeGrowthMetric(calculation.value("metric").toString());
            if (growthMetric.isEmpty()) {
                growthMetric = normalizeGrowthMetric(parameters.value("metric").toString());
            }
            if (growthMetric.isEmpty()) {
                const QStringList growthMetrics = variantToStringList(parameters.value("growthMetrics", calculation.value("growthMetrics")));
                for (const QString& rawMetric : growthMetrics) {
                    growthMetric = normalizeGrowthMetric(rawMetric);
                    if (!growthMetric.isEmpty()) {
                        break;
                    }
                }
            }
            if (growthMetric.isEmpty()) {
                growthMetric = "revenue_growth";
            }

            QString columnName;
            if (growthMetric == "revenue_growth") {
                columnName = "total_revenue";
            } else if (growthMetric == "net_profit_growth") {
                columnName = "net_profit";
            } else if (growthMetric == "eps_growth") {
                columnName = "eps";
            } else {
                buildErrorResult(makeUnsupportedMetricError("growth", growthMetric),
                                 {{"factorType", factorType}, {"metric", growthMetric}, {"domainFallbackError", domainFallbackError}});
                return result;
            }

            const QString timeframe = calculation.value("timeframe", "quarterly").toString();
            const QString latestClause = buildFinancialReportTypeClause(timeframe, "base");
            const QString currentClause = buildFinancialReportTypeClause(timeframe, "curr");

            const QString sql = QString(
                "SELECT si.symbol, curr.%1 AS current_value, prev.%1 AS previous_value "
                "FROM financial_indicator curr "
                "JOIN symbol_info si ON si.symbol_id = curr.symbol_id "
                "JOIN ("
                "    SELECT base.symbol_id, MAX(base.report_date) AS latest_report_date "
                "    FROM financial_indicator base "
                "    WHERE base.report_date <= :date%2 "
                "    GROUP BY base.symbol_id"
                ") latest ON latest.symbol_id = curr.symbol_id AND latest.latest_report_date = curr.report_date "
                "LEFT JOIN financial_indicator prev ON prev.symbol_id = curr.symbol_id "
                "    AND prev.report_type = curr.report_type "
                "    AND prev.report_date = ("
                "        SELECT MAX(prev2.report_date) FROM financial_indicator prev2 "
                "        WHERE prev2.symbol_id = curr.symbol_id "
                "          AND prev2.report_type = curr.report_type "
                "          AND prev2.report_date < curr.report_date"
                "    ) "
                "WHERE curr.%1 IS NOT NULL AND prev.%1 IS NOT NULL AND prev.%1 != 0%3 "
                "ORDER BY si.symbol")
                .arg(columnName, latestClause, currentClause);

            auto queryResult = database->executeQuery(sql, {{":date", date}});

            QVariantMap stockValues;
            for (size_t i = 0; i < queryResult.rowCount(); i++) {
                const auto& row = queryResult.getRow(i);
                const double currentValue = row.getDouble("current_value");
                const double previousValue = row.getDouble("previous_value");
                if (previousValue == 0.0) {
                    continue;
                }
                stockValues[row.getString("symbol")] = (currentValue - previousValue) / std::abs(previousValue);
            }

            QVariantMap diagnostics;
            diagnostics["factorType"] = factorType;
            diagnostics["metric"] = growthMetric;
            diagnostics["sourceTable"] = "financial_indicator";
            diagnostics["queriedRows"] = static_cast<int>(queryResult.rowCount());
            diagnostics["validRows"] = stockValues.size();
            diagnostics["timeframe"] = timeframe;
            if (!domainFallbackError.isEmpty()) {
                diagnostics["domainFallbackError"] = domainFallbackError;
            }
            buildSuccessResult(stockValues, QString(), diagnostics);

        } else if (factorType == "dividend") {
            buildErrorResult(QString::fromUtf8("红利因子当前缺少 dividend_yield / payout_ratio 底层字段接入，暂不支持实际计算"),
                             {{"factorType", factorType},
                              {"requiredFields", QStringList{"dividend_yield", "payout_ratio"}},
                              {"sourceTable", "daily_bar/financial_indicator"},
                              {"domainFallbackError", domainFallbackError}});
            return result;

        } else {
            // 其他因子：返回空结果，表示需要外部计算
            buildSuccessResult(QVariantMap(), "因子需要外部计算",
                               {{"factorType", factorType}, {"domainFallbackError", domainFallbackError}});
        }
        
        // 将结果保存到缓存
        if (result["status"].toString() == "success") {
            QVariantList cacheData;
            cacheData.append(result);
            DataServiceCache::getInstance().storeData(cacheKey, cacheData);
            qDebug() << "FactorService::getFactorValues: 数据已缓存，因子ID:" << factorId << "日期:" << date;
        }
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::getFactorValues: 数据库错误:" << e.what();
        result["status"] = "error";
        result["error"] = QString::fromStdString(e.what());
    }
    
    qDebug() << "FactorService::getFactorValues 结束，返回股票数量:" << result["count"].toInt();
    return result;
}

QString FactorService::getLatestAvailableTradeDate()
{
    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    auto database = dbManager.getDatabase();
    if (!database) {
        qWarning() << "FactorService::getLatestAvailableTradeDate: 无法获取数据库连接";
        return {};
    }

    QString latestDate;

    auto tryQueryLatestDate = [&](const QString& sql, const QString& columnName) {
        try {
            const auto queryResult = database->executeQuery(sql, {});
            if (queryResult.rowCount() == 0) {
                return;
            }

            const auto& row = queryResult.getRow(0);
            const QString candidateDate = row.getString(columnName).trimmed();
            if (!candidateDate.isEmpty() && (latestDate.isEmpty() || candidateDate > latestDate)) {
                latestDate = candidateDate;
            }
        } catch (const std::exception& e) {
            qWarning() << "FactorService::getLatestAvailableTradeDate: 查询失败:" << sql << e.what();
        }
    };

    tryQueryLatestDate("SELECT MAX(trade_date) AS latest_date FROM daily_bar", "latest_date");
    tryQueryLatestDate("SELECT MAX(trade_date) AS latest_date FROM cleaned_daily_bar", "latest_date");

    qDebug() << "FactorService::getLatestAvailableTradeDate:" << latestDate;
    return latestDate;
}

// 新增方法实现：批量获取因子值（简化版：先保证回测流程跑通）
QVariantMap FactorService::getFactorValuesBatch(const QString& factorId, const QStringList& dates)
{
    qDebug() << "FactorService::getFactorValuesBatch 开始，因子ID:" << factorId << "日期数量:" << dates.size();

    if (initializeFactorDomainRuntime()) {
        const QString resolvedInstanceId = resolveDomainInstanceId(factorId);
        if (!resolvedInstanceId.isEmpty()) {
            QVariantMap domainBatchResult = getFactorValuesBatchFromDomain(factorId, resolvedInstanceId, dates);
            if (domainBatchResult["status"].toString() == "success") {
                return domainBatchResult;
            }
            qDebug() << "FactorService::getFactorValuesBatch: domain 批量计算失败，回退桥接实现，因子ID:" << factorId
                     << "错误:" << domainBatchResult.value("error").toString();
        }
    }
    
    QVariantMap result;
    
    try {
        QVariantMap batchResult;
        int totalRows = 0;

        for (const QString& date : dates) {
            QString cacheKey = QString("factor_values_%1_%2").arg(factorId).arg(date);
            QVariantList cachedData = DataServiceCache::getInstance().getData(cacheKey);

            if (!cachedData.isEmpty() && cachedData[0].canConvert<QVariantMap>()) {
                QVariantMap cachedResult = cachedData[0].toMap();
                if (cachedResult["status"].toString() == "success") {
                    batchResult[date] = cachedResult;
                    totalRows += cachedResult["count"].toInt();
                    qDebug() << "FactorService::getFactorValuesBatch: 从缓存获取数据，日期:" << date;
                    continue;
                }
            }

            QVariantMap dayResult = getFactorValues(factorId, date);
            if (dayResult.isEmpty()) {
                dayResult["factorId"] = factorId;
                dayResult["date"] = date;
                dayResult["stockValues"] = QVariantMap();
                dayResult["count"] = 0;
                dayResult["status"] = "error";
                dayResult["error"] = "因子值计算失败";
            }

            batchResult[date] = dayResult;
            if (dayResult["status"].toString() == "success") {
                totalRows += dayResult["count"].toInt();
            }
        }
        
        result["factorId"] = factorId;
        result["dates"] = dates;
        result["batchResults"] = batchResult;
        result["totalCount"] = totalRows;
        result["status"] = "success";
        
        qDebug() << "FactorService::getFactorValuesBatch: 批量查询完成，总行数:" << totalRows;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::getFactorValuesBatch: 数据库错误:" << e.what();
        result["status"] = "error";
        result["error"] = QString::fromStdString(e.what());
    }
    
    qDebug() << "FactorService::getFactorValuesBatch 结束";
    return result;
}

// 私有方法：查询数据库数据
QVariantList FactorService::queryDatabaseData(const QString& minDate, const QString& maxDate)
{
    qDebug() << "FactorService::queryDatabaseData 开始，日期范围:" << minDate << "到" << maxDate;
    
    QVariantList result;
    
    // 连接到数据库
    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    auto database = dbManager.getDatabase();
    if (!database) {
        qWarning() << "FactorService::queryDatabaseData: 无法获取数据库连接";
        return result;
    }
    
    try {
        // 从cleaned_daily_bar表查询指定日期范围内的所有股票数据
        QString sql = "SELECT trade_date, symbol, close FROM cleaned_daily_bar "
                     "WHERE trade_date BETWEEN :start_date AND :end_date "
                     "ORDER BY trade_date, symbol";
        std::map<QString, QVariant> params;
        params[":start_date"] = minDate;
        params[":end_date"] = maxDate;
        
        auto queryResult = database->executeQuery(sql, params);
        
        for (size_t i = 0; i < queryResult.rowCount(); i++) {
            const auto& row = queryResult.getRow(i);
            QVariantMap dataMap;
            dataMap["trade_date"] = row.getString("trade_date");
            dataMap["symbol"] = row.getString("symbol");
            dataMap["close"] = row.getDouble("close");
            result.append(dataMap);
        }
        
        qDebug() << "FactorService::queryDatabaseData: 从数据库获取到" << result.size() << "条数据";
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::queryDatabaseData: 数据库错误:" << e.what();
    }
    
    return result;
}

// 新增辅助方法：从数据映射中提取日期
QString FactorService::extractDateFromDataMap(const QVariantMap& dataMap, int itemIndex)
{
    // 检查所有可能的日期字段名称
    QString date;
    
    // 主要字段名（按优先级）
    if (dataMap.contains("trade_date")) {
        date = dataMap.value("trade_date").toString();
    } else if (dataMap.contains("date")) {
        date = dataMap.value("date").toString();
    } else if (dataMap.contains("Date")) {
        date = dataMap.value("Date").toString();
    } else if (dataMap.contains("TRADE_DATE")) {
        date = dataMap.value("TRADE_DATE").toString();
    } else if (dataMap.contains("DATE")) {
        date = dataMap.value("DATE").toString();
    } else if (dataMap.contains("tradeDate")) {
        date = dataMap.value("tradeDate").toString();
    } else if (dataMap.contains("tradeDateStr")) {
        date = dataMap.value("tradeDateStr").toString();
    } else if (dataMap.contains("交易日期")) {
        date = dataMap.value("交易日期").toString();
    } else if (dataMap.contains("交易日")) {
        date = dataMap.value("交易日").toString();
    } else {
        // 尝试查找任何看起来像日期的字段
        for (const QString& key : dataMap.keys()) {
            if (key.contains("date", Qt::CaseInsensitive) || 
                key.contains("time", Qt::CaseInsensitive) ||
                key.contains("日期", Qt::CaseSensitive) ||
                key.contains("天", Qt::CaseSensitive)) {
                QVariant possibleDate = dataMap.value(key);
                if (possibleDate.canConvert<QString>()) {
                    QString dateStr = possibleDate.toString();
                    // 简单的日期格式验证
                    if (dateStr.length() >= 8 && (dateStr.contains("-") || dateStr.length() == 8)) {
                        date = dateStr;
                        qDebug() << "FactorService::extractDateFromDataMap: 第" << itemIndex 
                                 << "项从字段" << key << "提取到日期:" << date;
                        break;
                    }
                }
            }
        }
    }
    
    return date;
}

// 新增辅助方法：从数据映射中提取股票代码
QString FactorService::extractSymbolFromDataMap(const QVariantMap& dataMap, int itemIndex)
{
    // 检查所有可能的股票代码字段名称
    QString symbol;
    
    // 主要字段名（按优先级）
    if (dataMap.contains("symbol")) {
        symbol = dataMap.value("symbol").toString();
    } else if (dataMap.contains("code")) {
        symbol = dataMap.value("code").toString();
    } else if (dataMap.contains("stock_code")) {
        symbol = dataMap.value("stock_code").toString();
    } else if (dataMap.contains("stockCode")) {
        symbol = dataMap.value("stockCode").toString();
    } else if (dataMap.contains("SYMBOL")) {
        symbol = dataMap.value("SYMBOL").toString();
    } else if (dataMap.contains("CODE")) {
        symbol = dataMap.value("CODE").toString();
    } else if (dataMap.contains("股票代码")) {
        symbol = dataMap.value("股票代码").toString();
    } else if (dataMap.contains("代码")) {
        symbol = dataMap.value("代码").toString();
    } else if (dataMap.contains("ticker")) {
        symbol = dataMap.value("ticker").toString();
    } else {
        // 尝试查找任何看起来像股票代码的字段
        for (const QString& key : dataMap.keys()) {
            if (key.contains("symbol", Qt::CaseInsensitive) || 
                key.contains("code", Qt::CaseInsensitive) ||
                key.contains("股票", Qt::CaseSensitive) ||
                key.contains("代码", Qt::CaseSensitive)) {
                QVariant possibleSymbol = dataMap.value(key);
                if (possibleSymbol.canConvert<QString>()) {
                    QString symbolStr = possibleSymbol.toString();
                    // 简单的股票代码格式验证（6位数字或带后缀）
                    if (symbolStr.length() >= 4) {
                        symbol = symbolStr;
                        qDebug() << "FactorService::extractSymbolFromDataMap: 第" << itemIndex 
                                 << "项从字段" << key << "提取到股票代码:" << symbol;
                        break;
                    }
                }
            }
        }
    }
    
    return symbol;
}

// 新增辅助方法：从数据映射中提取收盘价
double FactorService::extractClosePriceFromDataMap(const QVariantMap& dataMap, int itemIndex)
{
    // 检查所有可能的收盘价字段名称
    double closePrice = -1.0;
    
    // 主要字段名（按优先级）
    if (dataMap.contains("close")) {
        QVariant closeValue = dataMap.value("close");
        if (closeValue.isValid() && closeValue.canConvert<double>()) {
            closePrice = closeValue.toDouble();
        }
    } else if (dataMap.contains("Close")) {
        QVariant closeValue = dataMap.value("Close");
        if (closeValue.isValid() && closeValue.canConvert<double>()) {
            closePrice = closeValue.toDouble();
        }
    } else if (dataMap.contains("closing_price")) {
        QVariant closeValue = dataMap.value("closing_price");
        if (closeValue.isValid() && closeValue.canConvert<double>()) {
            closePrice = closeValue.toDouble();
        }
    } else if (dataMap.contains("CLOSE")) {
        QVariant closeValue = dataMap.value("CLOSE");
        if (closeValue.isValid() && closeValue.canConvert<double>()) {
            closePrice = closeValue.toDouble();
        }
    } else if (dataMap.contains("收盘价")) {
        QVariant closeValue = dataMap.value("收盘价");
        if (closeValue.isValid() && closeValue.canConvert<double>()) {
            closePrice = closeValue.toDouble();
        }
    } else if (dataMap.contains("收盘")) {
        QVariant closeValue = dataMap.value("收盘");
        if (closeValue.isValid() && closeValue.canConvert<double>()) {
            closePrice = closeValue.toDouble();
        }
    } else {
        // 尝试查找任何看起来像价格的字段
        for (const QString& key : dataMap.keys()) {
            if (key.contains("close", Qt::CaseInsensitive) || 
                key.contains("price", Qt::CaseInsensitive) ||
                key.contains("收盘", Qt::CaseSensitive) ||
                key.contains("价", Qt::CaseSensitive)) {
                QVariant possiblePrice = dataMap.value(key);
                if (possiblePrice.isValid() && possiblePrice.canConvert<double>()) {
                    double price = possiblePrice.toDouble();
                    // 简单的价格验证（正数）
                    if (price > 0) {
                        closePrice = price;
                        qDebug() << "FactorService::extractClosePriceFromDataMap: 第" << itemIndex 
                                 << "项从字段" << key << "提取到收盘价:" << closePrice;
                        break;
                    }
                }
            }
        }
    }
    
    return closePrice;
}

// 新增辅助方法：记录数据提取调试信息
void FactorService::logDataExtractionDebugInfo(const QVariantMap& dataMap, int itemIndex, 
                                              const QString& extractedDate, 
                                              const QString& extractedSymbol, 
                                              double extractedClosePrice)
{
    if (extractedDate.isEmpty()) {
        qDebug() << "FactorService::getFactorValuesBatch: 第" << itemIndex << "项缺少日期字段";
        qDebug() << "可用字段:" << dataMap.keys();
        
        // 尝试查找任何看起来像日期的字段
        for (const QString& key : dataMap.keys()) {
            if (key.contains("date", Qt::CaseInsensitive) || 
                key.contains("time", Qt::CaseInsensitive) ||
                key.contains("日期", Qt::CaseSensitive)) {
                QVariant possibleDate = dataMap.value(key);
                qDebug() << "可能包含日期的字段:" << key << "=" << possibleDate;
            }
        }
    }
    
    if (extractedSymbol.isEmpty()) {
        qDebug() << "FactorService::getFactorValuesBatch: 第" << itemIndex << "项缺少股票代码字段";
        qDebug() << "可用字段:" << dataMap.keys();
        
        // 尝试查找任何看起来像股票代码的字段
        for (const QString& key : dataMap.keys()) {
            if (key.contains("symbol", Qt::CaseInsensitive) || 
                key.contains("code", Qt::CaseInsensitive) ||
                key.contains("股票", Qt::CaseSensitive) ||
                key.contains("代码", Qt::CaseSensitive)) {
                QVariant possibleSymbol = dataMap.value(key);
                qDebug() << "可能包含股票代码的字段:" << key << "=" << possibleSymbol;
            }
        }
    }
    
    if (extractedClosePrice < 0) {
        qDebug() << "FactorService::getFactorValuesBatch: 第" << itemIndex << "项收盘价字段无效或缺失";
        qDebug() << "可用字段:" << dataMap.keys();
        
        // 尝试查找任何看起来像价格的字段
        for (const QString& key : dataMap.keys()) {
            if (key.contains("close", Qt::CaseInsensitive) || 
                key.contains("price", Qt::CaseInsensitive) ||
                key.contains("收盘", Qt::CaseSensitive) ||
                key.contains("价", Qt::CaseSensitive)) {
                QVariant possiblePrice = dataMap.value(key);
                qDebug() << "可能包含价格的字段:" << key << "=" << possiblePrice;
            }
        }
    }
}
