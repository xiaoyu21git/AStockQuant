#include "domain/factor/include/ConfigurableFactor.h"
#include "domain/factor/include/CustomExpressionUtils.h"
#include "domain/factor/include/FactorDataProvider.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"

#include <QDate>
#include <QDebug>
#include <QElapsedTimer>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <numeric>
#include <optional>
#include <stack>

namespace factor {

namespace {

std::map<QString, QVariant> makePositionalParams(std::initializer_list<QVariant> values)
{
    std::map<QString, QVariant> params;
    for (const QVariant& value : values) {
        params.emplace(QString(), value);
    }
    return params;
}

std::map<QString, QVariant> makeNamedParams(std::initializer_list<std::pair<QString, QVariant>> values)
{
    std::map<QString, QVariant> params;
    for (const auto& [key, value] : values) {
        params.emplace(key, value);
    }
    return params;
}

QString normalizeConfiguredTypeText(const QString& rawType)
{
    const QString normalized = rawType.trimmed().toLower();
    if (normalized == "growth" || normalized == QString::fromUtf8("成长因子")) {
        return "growth";
    }
    if (normalized == "dividend" || normalized == QString::fromUtf8("红利因子")) {
        return "dividend";
    }
    if (normalized == "technical" || normalized == QString::fromUtf8("技术因子")) {
        return "technical";
    }
    if (normalized == "liquidity" || normalized == QString::fromUtf8("流动性因子")) {
        return "liquidity";
    }
    if (normalized == "macro_sector" || normalized == QString::fromUtf8("宏观/行业因子") || normalized == QString::fromUtf8("宏观/行业")) {
        return "macro_sector";
    }
    if (normalized == "sentiment" || normalized == QString::fromUtf8("情绪因子")) {
        return "sentiment";
    }
    if (normalized == "custom" || normalized == QString::fromUtf8("自定义因子") || normalized == QString::fromUtf8("自定义")) {
        return "custom";
    }
    return normalized;
}

QString normalizeTechnicalIndicatorType(const QString& rawType)
{
    const QString normalized = rawType.trimmed().toLower();
    if (normalized == QStringLiteral("趋势指标") || normalized == QStringLiteral("trend")
            || normalized == QStringLiteral("trend_indicator")) {
        return QStringLiteral("trend");
    }
    if (normalized == QStringLiteral("动量指标") || normalized == QStringLiteral("momentum")
            || normalized == QStringLiteral("momentum_indicator")) {
        return QStringLiteral("momentum");
    }
    if (normalized == QStringLiteral("波动率指标") || normalized == QStringLiteral("volatility")
            || normalized == QStringLiteral("volatility_indicator")) {
        return QStringLiteral("volatility");
    }
    if (normalized == QStringLiteral("成交量指标") || normalized == QStringLiteral("volume")
            || normalized == QStringLiteral("volume_indicator")) {
        return QStringLiteral("volume");
    }
    return normalized;
}

QString normalizeSentimentSource(const QString& rawSource)
{
    const QString normalized = rawSource.trimmed().toLower();
    if (normalized == QStringLiteral("新闻情绪") || normalized == QStringLiteral("news")) {
        return QStringLiteral("news");
    }
    if (normalized == QStringLiteral("社交媒体") || normalized == QStringLiteral("social")
            || normalized == QStringLiteral("social_media")) {
        return QStringLiteral("social");
    }
    if (normalized == QStringLiteral("分析师评级") || normalized == QStringLiteral("analyst")
            || normalized == QStringLiteral("analyst_rating")) {
        return QStringLiteral("analyst");
    }
    if (normalized == QStringLiteral("市场情绪") || normalized == QStringLiteral("market")) {
        return QStringLiteral("market");
    }
    return normalized;
}

QString normalizeMacroFactor(const QString& rawFactor)
{
    const QString normalized = rawFactor.trimmed().toLower();
    if (normalized == QStringLiteral("利率敏感度") || normalized == QStringLiteral("interest_rate")
            || normalized == QStringLiteral("interest_rate_sensitivity")) {
        return QStringLiteral("interest_rate_sensitivity");
    }
    if (normalized == QStringLiteral("通胀敏感度") || normalized == QStringLiteral("inflation")
            || normalized == QStringLiteral("inflation_sensitivity")) {
        return QStringLiteral("inflation_sensitivity");
    }
    if (normalized == QStringLiteral("经济增长敏感度") || normalized == QStringLiteral("growth")
            || normalized == QStringLiteral("growth_sensitivity")) {
        return QStringLiteral("growth_sensitivity");
    }
    return normalized;
}

QString normalizeSectorType(const QString& rawSectorType)
{
    const QString normalized = rawSectorType.trimmed().toLower();
    if (normalized == QStringLiteral("申万一级") || normalized == QStringLiteral("sw_l1")) {
        return QStringLiteral("sw_l1");
    }
    if (normalized == QStringLiteral("申万二级") || normalized == QStringLiteral("sw_l2")) {
        return QStringLiteral("sw_l2");
    }
    if (normalized == QStringLiteral("中信一级") || normalized == QStringLiteral("citic_l1")) {
        return QStringLiteral("citic_l1");
    }
    if (normalized == QStringLiteral("中信二级") || normalized == QStringLiteral("citic_l2")) {
        return QStringLiteral("citic_l2");
    }
    return normalized;
}

QString normalizePriceField(const QString& rawPriceType)
{
    const QString normalized = rawPriceType.trimmed().toLower();
    if (normalized == QStringLiteral("adj_close") || normalized == QStringLiteral("adjusted_close")
            || normalized == QStringLiteral("后复权") || normalized == QStringLiteral("复权收盘价")) {
        return QStringLiteral("adj_close");
    }
    if (normalized == QStringLiteral("open") || normalized == QStringLiteral("开盘价")) {
        return QStringLiteral("open");
    }
    if (normalized == QStringLiteral("high") || normalized == QStringLiteral("最高价")) {
        return QStringLiteral("high");
    }
    if (normalized == QStringLiteral("low") || normalized == QStringLiteral("最低价")) {
        return QStringLiteral("low");
    }
    return QStringLiteral("close");
}

QString normalizeConfigurableFrequency(const std::string& frequency)
{
    const QString normalized = QString::fromStdString(frequency).trimmed().toLower();
    if (normalized == QStringLiteral("weekly") || normalized == QStringLiteral("周频")) {
        return QStringLiteral("weekly");
    }
    if (normalized == QStringLiteral("monthly") || normalized == QStringLiteral("月频")) {
        return QStringLiteral("monthly");
    }
    return QStringLiteral("daily");
}

QString normalizeConfigurableStandardization(const std::string& standardization)
{
    const QString normalized = QString::fromStdString(standardization).trimmed().toLower();
    if (normalized == QStringLiteral("zscore") || normalized == QStringLiteral("z_score")
            || normalized == QStringLiteral("z-score") || normalized == QStringLiteral("z score")) {
        return QStringLiteral("zscore");
    }
    if (normalized == QStringLiteral("minmax") || normalized == QStringLiteral("min_max")
            || normalized == QStringLiteral("min-max") || normalized == QStringLiteral("min max")) {
        return QStringLiteral("minmax");
    }
    if (normalized == QStringLiteral("percentile") || normalized == QStringLiteral("rank")) {
        return QStringLiteral("percentile");
    }
    return QStringLiteral("none");
}

QString normalizeConfigurableMetric(const QString& rawMetric, const QString& factorType)
{
    const QString normalized = rawMetric.trimmed().toLower();
    if (factorType == QStringLiteral("growth")) {
        if (normalized == QStringLiteral("收入增长") || normalized == QStringLiteral("营收增长")
                || normalized == QStringLiteral("revenue_growth")) {
            return QStringLiteral("revenue_growth");
        }
        if (normalized == QStringLiteral("盈利增长") || normalized == QStringLiteral("利润增长")
                || normalized == QStringLiteral("利润增长率") || normalized == QStringLiteral("earnings_growth")
                || normalized == QStringLiteral("profit_growth") || normalized == QStringLiteral("net_profit_growth")) {
            return QStringLiteral("net_profit_growth");
        }
        if (normalized == QStringLiteral("eps增长") || normalized == QStringLiteral("每股收益增长")
                || normalized == QStringLiteral("eps_growth")) {
            return QStringLiteral("eps_growth");
        }
    }
    if (factorType == QStringLiteral("liquidity")) {
        if (normalized == QStringLiteral("换手率")) {
            return QStringLiteral("turnover_rate");
        }
        if (normalized == QStringLiteral("成交量")) {
            return QStringLiteral("volume");
        }
        if (normalized == QStringLiteral("amihud非流动性") || normalized == QStringLiteral("amihud")
                || normalized == QStringLiteral("amihud_illiquidity")) {
            return QStringLiteral("amihud_illiquidity");
        }
        if (normalized == QStringLiteral("买卖价差") || normalized == QStringLiteral("bid_ask_spread")) {
            return QStringLiteral("amplitude");
        }
    }
    if (factorType == QStringLiteral("dividend")) {
        if (normalized == QStringLiteral("股息率")) {
            return QStringLiteral("dividend_yield");
        }
        if (normalized == QStringLiteral("派息率") || normalized == QStringLiteral("股息支付率")) {
            return QStringLiteral("payout_ratio");
        }
        if (normalized == QStringLiteral("分红稳定性") || normalized == QStringLiteral("股息稳定性")) {
            return QStringLiteral("dividend_stability");
        }
    }
    if (factorType == QStringLiteral("sentiment")) {
        if (normalized == QStringLiteral("新闻情绪")) {
            return QStringLiteral("sentiment_score");
        }
        if (normalized == QStringLiteral("社交媒体")) {
            return QStringLiteral("social_sentiment");
        }
        if (normalized == QStringLiteral("分析师评级")) {
            return QStringLiteral("investor_sentiment");
        }
        if (normalized == QStringLiteral("市场情绪")) {
            return QStringLiteral("market_sentiment");
        }
    }
    return normalized;
}

double configurableVolumeMultiplier(const std::vector<double>& volumeSeries)
{
    if (volumeSeries.size() < 2) {
        return 1.0;
    }

    const double latestVolume = volumeSeries.back();
    const std::vector<double> history(volumeSeries.begin(), volumeSeries.end() - 1);
    const double historyMean = history.empty()
        ? 0.0
        : std::accumulate(history.begin(), history.end(), 0.0) / static_cast<double>(history.size());
    if (!std::isfinite(latestVolume) || historyMean <= 1e-12) {
        return 1.0;
    }

    const double ratio = latestVolume / historyMean;
    return std::clamp(0.85 + 0.15 * ratio, 0.7, 1.35);
}

QString sentimentMetricForSource(const QString& source)
{
    if (source == QStringLiteral("social")) {
        return QStringLiteral("social_sentiment");
    }
    if (source == QStringLiteral("analyst")) {
        return QStringLiteral("investor_sentiment");
    }
    if (source == QStringLiteral("market")) {
        return QStringLiteral("market_sentiment");
    }
    return QStringLiteral("sentiment_score");
}

double sectorIndustryWeight(const QString& sectorType)
{
    if (sectorType == QStringLiteral("sw_l2")) {
        return 0.85;
    }
    if (sectorType == QStringLiteral("citic_l1")) {
        return 0.95;
    }
    if (sectorType == QStringLiteral("citic_l2")) {
        return 0.8;
    }
    return 1.0;
}

double safeMean(const std::vector<double>& values)
{
    if (values.empty()) {
        return 0.0;
    }
    return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

double safeStdDev(const std::vector<double>& values)
{
    if (values.size() < 2) {
        return 0.0;
    }
    const double mean = safeMean(values);
    double variance = 0.0;
    for (double value : values) {
        const double delta = value - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(values.size());
    return std::sqrt(variance);
}

double safeRatio(double numerator, double denominator)
{
    if (!std::isfinite(numerator) || !std::isfinite(denominator) || std::abs(denominator) < 1e-12) {
        return 0.0;
    }
    return numerator / denominator;
}

bool isFinancialMetricField(const QString& rawField)
{
    static const QSet<QString> financialFields = {
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
        QStringLiteral("payout_ratio")
    };
    return financialFields.contains(rawField.trimmed().toLower());
}

bool isNewsMetricField(const QString& rawField)
{
    static const QSet<QString> newsFields = {
        QStringLiteral("sentiment_score"),
        QStringLiteral("market_sentiment"),
        QStringLiteral("investor_sentiment"),
        QStringLiteral("sector_sentiment"),
        QStringLiteral("theme_sentiment"),
        QStringLiteral("social_sentiment"),
        QStringLiteral("news_count")
    };
    return newsFields.contains(rawField.trimmed().toLower());
}

bool isPolicyMetricField(const QString& rawField)
{
    static const QSet<QString> policyFields = {
        QStringLiteral("policy_score"),
        QStringLiteral("policy_strength"),
        QStringLiteral("policy_count")
    };
    return policyFields.contains(rawField.trimmed().toLower());
}

bool isAlternativeMetricField(const QString& rawField)
{
    static const QSet<QString> alternativeFields = {
        QStringLiteral("hot_rank"),
        QStringLiteral("popularity_score"),
        QStringLiteral("comment_count"),
        QStringLiteral("comment_sentiment")
    };
    return alternativeFields.contains(rawField.trimmed().toLower());
}

bool isDerivativesMetricField(const QString& rawField)
{
    static const QSet<QString> derivativesFields = {
        QStringLiteral("futures_close"),
        QStringLiteral("futures_volume"),
        QStringLiteral("open_interest"),
        QStringLiteral("basis"),
        QStringLiteral("basis_rate")
    };
    return derivativesFields.contains(rawField.trimmed().toLower());
}

bool tableExists(const std::shared_ptr<astock::database::QtMySQLDatabase>& db, const QString& tableName)
{
    if (!db || tableName.trimmed().isEmpty()) {
        return false;
    }

    const auto result = db->executeQuery(
        "SELECT COUNT(*) AS count FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table_name",
        makeNamedParams({{":table_name", tableName}}));
    return !result.isEmpty() && result.getRow(0).getInt("count") > 0;
}

bool tableHasColumn(const std::shared_ptr<astock::database::QtMySQLDatabase>& db,
                    const QString& tableName,
                    const QString& columnName)
{
    if (!db || tableName.trimmed().isEmpty() || columnName.trimmed().isEmpty()) {
        return false;
    }

    const auto result = db->executeQuery(
        "SELECT COUNT(*) AS count FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table_name AND COLUMN_NAME = :column_name",
        makeNamedParams({{":table_name", tableName}, {":column_name", columnName}}));
    return !result.isEmpty() && result.getRow(0).getInt("count") > 0;
}

QString resolveNewsTable(const std::shared_ptr<astock::database::QtMySQLDatabase>& db)
{
    const QStringList candidates = {
        QStringLiteral("news_sentiment"),
        QStringLiteral("stock_news"),
        QStringLiteral("news_data"),
        QStringLiteral("news")
    };
    for (const QString& candidate : candidates) {
        if (tableExists(db, candidate)) {
            return candidate;
        }
    }
    return {};
}

QString resolveNewsDateColumn(const std::shared_ptr<astock::database::QtMySQLDatabase>& db,
                              const QString& tableName)
{
    const QStringList candidates = {
        QStringLiteral("trade_date"),
        QStringLiteral("publish_time"),
        QStringLiteral("pub_time"),
        QStringLiteral("date"),
        QStringLiteral("created_at")
    };
    for (const QString& candidate : candidates) {
        if (tableHasColumn(db, tableName, candidate)) {
            return candidate;
        }
    }
    return {};
}

QString resolveNewsValueColumn(const std::shared_ptr<astock::database::QtMySQLDatabase>& db,
                               const QString& tableName,
                               const QString& requestedField)
{
    const QString normalizedField = requestedField.trimmed().toLower();
    if (tableHasColumn(db, tableName, normalizedField)) {
        return normalizedField;
    }

    if (normalizedField == QStringLiteral("sentiment_score")) {
        const QStringList candidates = {
            QStringLiteral("score"),
            QStringLiteral("sentiment"),
            QStringLiteral("sentiment_score")
        };
        for (const QString& candidate : candidates) {
            if (tableHasColumn(db, tableName, candidate)) {
                return candidate;
            }
        }
    }

    return {};
}

QString resolveGenericDateColumn(const std::shared_ptr<astock::database::QtMySQLDatabase>& db,
                                 const QString& tableName)
{
    const QStringList candidates = {
        QStringLiteral("trade_date"),
        QStringLiteral("publish_time"),
        QStringLiteral("pub_time"),
        QStringLiteral("date"),
        QStringLiteral("created_at")
    };
    for (const QString& candidate : candidates) {
        if (tableHasColumn(db, tableName, candidate)) {
            return candidate;
        }
    }
    return {};
}

QString resolveGenericSymbolColumn(const std::shared_ptr<astock::database::QtMySQLDatabase>& db,
                                   const QString& tableName)
{
    const QStringList candidates = {
        QStringLiteral("symbol"),
        QStringLiteral("underlying_symbol"),
        QStringLiteral("stock_code"),
        QStringLiteral("security_code")
    };
    for (const QString& candidate : candidates) {
        if (tableHasColumn(db, tableName, candidate)) {
            return candidate;
        }
    }
    return {};
}

QString resolveSupplementalTable(const std::shared_ptr<astock::database::QtMySQLDatabase>& db,
                                 const QString& requestedField)
{
    const QString normalizedField = requestedField.trimmed().toLower();
    if (isPolicyMetricField(normalizedField) && tableExists(db, QStringLiteral("policy_data"))) {
        return QStringLiteral("policy_data");
    }
    if (isAlternativeMetricField(normalizedField) && tableExists(db, QStringLiteral("alternative_data"))) {
        return QStringLiteral("alternative_data");
    }
    if (isDerivativesMetricField(normalizedField) && tableExists(db, QStringLiteral("derivatives_data"))) {
        return QStringLiteral("derivatives_data");
    }
    return {};
}

QString resolveSupplementalValueColumn(const std::shared_ptr<astock::database::QtMySQLDatabase>& db,
                                       const QString& tableName,
                                       const QString& requestedField)
{
    const QString normalizedField = requestedField.trimmed().toLower();
    if (tableHasColumn(db, tableName, normalizedField)) {
        return normalizedField;
    }
    if (normalizedField == QStringLiteral("futures_close") && tableHasColumn(db, tableName, QStringLiteral("close"))) {
        return QStringLiteral("close");
    }
    if (normalizedField == QStringLiteral("futures_volume") && tableHasColumn(db, tableName, QStringLiteral("volume"))) {
        return QStringLiteral("volume");
    }
    return {};
}

}

void ConfigurableFactor::Params::fromJson(const foundation::json::JsonFacade& json)
{
    variables.clear();
    if (json.has("factor_type")) configuredType = json.get("factor_type").asString();
    if (configuredType.empty() && json.has("factorType")) configuredType = json.get("factorType").asString();
    if (json.has("metric")) metric = json.get("metric").asString();
    if (metric.empty() && json.has("growthMetric")) metric = json.get("growthMetric").asString();
    if (metric.empty() && json.has("dividendMetric")) metric = json.get("dividendMetric").asString();
    if (metric.empty() && json.has("dividendType")) metric = json.get("dividendType").asString();
    if (metric.empty() && json.has("sentimentMetric")) metric = json.get("sentimentMetric").asString();
    if (metric.empty() && json.has("liquidity_metric")) metric = json.get("liquidity_metric").asString();
    if (metric.empty() && json.has("liquidityMetric")) metric = json.get("liquidityMetric").asString();
    if (metric.empty() && json.has("valuation_type")) metric = json.get("valuation_type").asString();
    if (metric.empty() && json.has("valuationType")) metric = json.get("valuationType").asString();
    if (metric.empty() && json.has("growthMetrics")) {
        const auto metrics = json.get("growthMetrics");
        if (metrics.isArray() && metrics.size() > 0) {
            metric = metrics.at(0).asString();
        }
    }
    if (json.has("timeframe")) timeframe = json.get("timeframe").asString();
    if (json.has("indicator_type")) indicatorType = json.get("indicator_type").asString();
    if (indicatorType.empty() && json.has("indicatorType")) indicatorType = json.get("indicatorType").asString();
    if (json.has("sentiment_source")) sentimentSource = json.get("sentiment_source").asString();
    if (sentimentSource.empty() && json.has("sentimentSource")) sentimentSource = json.get("sentimentSource").asString();
    if (json.has("expression")) expression = json.get("expression").asString();
    if (json.has("sector_type")) sectorType = json.get("sector_type").asString();
    if (sectorType.empty() && json.has("sectorType")) sectorType = json.get("sectorType").asString();
    if (json.has("macro_factor")) macroFactor = json.get("macro_factor").asString();
    if (macroFactor.empty() && json.has("macroFactor")) macroFactor = json.get("macroFactor").asString();
    if (json.has("price_type")) priceType = json.get("price_type").asString();
    if (priceType == "close" && json.has("priceType")) priceType = json.get("priceType").asString();
    if (json.has("use_volume")) useVolume = json.get("use_volume").asBool();
    if (!useVolume && json.has("useVolume")) useVolume = json.get("useVolume").asBool();
    if (json.has("frequency")) frequency = json.get("frequency").asString();
    if (json.has("laggedEnabled")) laggedEnabled = json.get("laggedEnabled").asBool();
    if (json.has("lagged_enabled")) laggedEnabled = json.get("lagged_enabled").asBool();
    if (json.has("standardization")) standardization = json.get("standardization").asString();
    if (json.has("neutralizationEnabled")) neutralizationEnabled = json.get("neutralizationEnabled").asBool();
    if (json.has("neutralization_enabled")) neutralizationEnabled = json.get("neutralization_enabled").asBool();
    if (json.has("variables")) {
        auto variableArray = json.get("variables");
        if (variableArray.isArray()) {
            for (size_t index = 0; index < variableArray.size(); ++index) {
                auto variable = variableArray.at(index);
                if (!variable.isObject() || !variable.has("name")) {
                    continue;
                }

                CustomVariableBinding binding;
                binding.name = variable.get("name").asString();
                if (binding.name.empty()) {
                    continue;
                }
                if (variable.has("field")) {
                    binding.field = variable.get("field").asString();
                }
                if (variable.has("defaultValue")) {
                    binding.hasDefaultValue = true;
                    binding.defaultValue = variable.get("defaultValue").asDouble();
                } else if (variable.has("default_value")) {
                    binding.hasDefaultValue = true;
                    binding.defaultValue = variable.get("default_value").asDouble();
                }
                variables.push_back(std::move(binding));
            }
        }
    }
    if (json.has("window")) window = json.get("window").asInt();
    if (json.has("indicatorWindow")) window = json.get("indicatorWindow").asInt();
    if (json.has("liquidityWindow")) window = json.get("liquidityWindow").asInt();
    if (json.has("sentimentWindow")) window = json.get("sentimentWindow").asInt();
    if (json.has("lookback_period")) lookbackPeriod = json.get("lookback_period").asInt();
    if (json.has("lookbackPeriod")) lookbackPeriod = json.get("lookbackPeriod").asInt();
    if (json.has("min_dividend_yield")) minDividendYield = json.get("min_dividend_yield").asDouble();
    if (json.has("minDividendYield")) minDividendYield = json.get("minDividendYield").asDouble();
    if (json.has("sentiment_weight")) sentimentWeight = json.get("sentiment_weight").asDouble();
    if (json.has("sentimentWeight")) sentimentWeight = json.get("sentimentWeight").asDouble();
}

ConfigurableFactor::ConfigurableFactor()
{
    factorType_ = "通用因子";
}

void ConfigurableFactor::initializeFromDatabase(const std::string& instanceId)
{
    BaseFactor::initializeFromDatabase(instanceId);
}

CalculationResult ConfigurableFactor::calculate(const CalculationContext& context)
{
    const QString type = normalizedType();
    if (type == "growth") return calculateGrowth(context);
    if (type == "liquidity") return calculateLiquidity(context);
    if (type == "technical") return calculateTechnical(context);
    if (type == "dividend") return calculateDividend(context);
    if (type == "macro_sector") return calculateMacroSector(context);
    if (type == "sentiment") return calculateSentiment(context);
    if (type == "custom") return calculateCustom(context);
    return CalculationResult::createError(QString::fromUtf8("未识别的通用因子类型: %1").arg(type).toStdString());
}

DataRequirements ConfigurableFactor::getDataRequirements() const
{
    return dataRequirements_;
}

BoundaryRules ConfigurableFactor::getBoundaryRules() const
{
    return boundaryRules_;
}

std::shared_ptr<ConfigurableFactor> ConfigurableFactor::create(
    const std::string& instanceId,
    std::shared_ptr<astock::database::QtMySQLDatabase> db,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<ConfigurableFactor>();
    factor->db_ = db;
    factor->dataChecker_ = dataChecker;
    factor->initializeFromDatabase(instanceId);
    return factor;
}

void ConfigurableFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    params_.configuredType = factorType_;
    if (config.has("factorType")) {
        params_.configuredType = config.get("factorType").asString();
    } else if (config.has("factor_type")) {
        params_.configuredType = config.get("factor_type").asString();
    }
    if (config.has("calculation")) {
        params_.fromJson(config.get("calculation"));
    }
    if (params_.configuredType.empty() && config.has("majorCategory")) {
        params_.configuredType = config.get("majorCategory").asString();
    }
    factorType_ = normalizedType().toStdString();
}

QString ConfigurableFactor::normalizedType() const
{
    return normalizeConfiguredTypeText(QString::fromStdString(params_.configuredType.empty() ? factorType_ : params_.configuredType));
}

QString ConfigurableFactor::normalizedMetric() const
{
    return normalizeConfigurableMetric(QString::fromStdString(params_.metric), normalizedType());
}

std::vector<std::string> ConfigurableFactor::effectiveSymbols(const CalculationContext& context) const
{
    if (!context.symbols.empty()) {
        return context.symbols;
    }
    if (context.dataProvider) {
        return context.dataProvider->getAvailableSymbols(context.date);
    }
    if (!db_) {
        return {};
    }
    auto queryResult = db_->executeQuery(
        "SELECT DISTINCT symbol FROM daily_bar WHERE trade_date = ? ORDER BY symbol",
        makePositionalParams({QString::fromStdString(context.date)})
    );
    std::vector<std::string> symbols;
    symbols.reserve(queryResult.rowCount());
    for (size_t i = 0; i < queryResult.rowCount(); ++i) {
        symbols.push_back(queryResult.getRow(i).getString("symbol").toStdString());
    }
    return symbols;
}

std::unordered_map<std::string, double> ConfigurableFactor::currentFieldCrossSection(
    const CalculationContext& context,
    const QString& field) const
{
    const QString normalizedField = field.trimmed().toLower();
    if (normalizedField.isEmpty()) {
        return {};
    }
    const std::vector<std::string> symbols = effectiveSymbols(context);
    if (context.dataProvider && context.dataProvider->hasField(normalizedField.toStdString())) {
        return context.dataProvider->getCrossSection(context.date, normalizedField.toStdString(), symbols);
    }

    if (isFinancialMetricField(normalizedField)) {
        return latestFinancialMetric(context, normalizedField, QString::fromStdString(context.date));
    }

    std::unordered_map<std::string, double> result;
    if (!db_) {
        return result;
    }

    const std::unordered_set<std::string> requested(symbols.begin(), symbols.end());

    if (isNewsMetricField(normalizedField)) {
        const QString newsTable = resolveNewsTable(db_);
        const QString dateColumn = resolveNewsDateColumn(db_, newsTable);
        if (!newsTable.isEmpty() && !dateColumn.isEmpty()) {
            QString sql;
            if (normalizedField == QStringLiteral("news_count")) {
                sql = QString(
                    "SELECT symbol, COUNT(*) AS field_value FROM %1 "
                    "WHERE DATE(%2) = :date GROUP BY symbol")
                    .arg(newsTable, dateColumn);
            } else {
                const QString valueColumn = resolveNewsValueColumn(db_, newsTable, normalizedField);
                if (!valueColumn.isEmpty()) {
                    sql = QString(
                        "SELECT symbol, AVG(%1) AS field_value FROM %2 "
                        "WHERE DATE(%3) = :date AND %1 IS NOT NULL GROUP BY symbol")
                        .arg(valueColumn, newsTable, dateColumn);
                }
            }

            if (!sql.isEmpty()) {
                auto queryResult = db_->executeQuery(sql, makeNamedParams({{":date", QString::fromStdString(context.date)}}));
                for (size_t i = 0; i < queryResult.rowCount(); ++i) {
                    const auto& row = queryResult.getRow(i);
                    const std::string symbol = row.getString("symbol").toStdString();
                    if (!requested.empty() && requested.find(symbol) == requested.end()) {
                        continue;
                    }
                    result[symbol] = row.getDouble("field_value");
                }
            }
        }

        if (!result.empty()) {
            return result;
        }
    }

    const QString supplementalTable = resolveSupplementalTable(db_, normalizedField);
    if (!supplementalTable.isEmpty()) {
        const QString dateColumn = resolveGenericDateColumn(db_, supplementalTable);
        const QString symbolColumn = resolveGenericSymbolColumn(db_, supplementalTable);
        const QString valueColumn = resolveSupplementalValueColumn(db_, supplementalTable, normalizedField);
        if (!dateColumn.isEmpty() && !valueColumn.isEmpty()) {
            QString sql;
            if (!symbolColumn.isEmpty()) {
                sql = QString(
                    "SELECT %1 AS metric_symbol, AVG(%2) AS field_value FROM %3 "
                    "WHERE DATE(%4) = :date AND %2 IS NOT NULL GROUP BY %1")
                    .arg(symbolColumn, valueColumn, supplementalTable, dateColumn);
            } else {
                sql = QString(
                    "SELECT 'MARKET' AS metric_symbol, AVG(%1) AS field_value FROM %2 "
                    "WHERE DATE(%3) = :date AND %1 IS NOT NULL")
                    .arg(valueColumn, supplementalTable, dateColumn);
            }

            auto queryResult = db_->executeQuery(sql, makeNamedParams({{":date", QString::fromStdString(context.date)}}));
            std::optional<double> marketFallback;
            for (size_t i = 0; i < queryResult.rowCount(); ++i) {
                const auto& row = queryResult.getRow(i);
                const std::string symbol = row.getString("metric_symbol").toStdString();
                const double value = row.getDouble("field_value");
                if (!std::isfinite(value)) {
                    continue;
                }
                if (symbol.empty() || symbol == "MARKET" || symbol == "ALL_MARKET" || symbol == "GLOBAL") {
                    marketFallback = value;
                    continue;
                }
                if (!requested.empty() && requested.find(symbol) == requested.end()) {
                    continue;
                }
                result[symbol] = value;
            }

            if (result.empty() && marketFallback.has_value()) {
                if (!requested.empty()) {
                    for (const auto& symbol : symbols) {
                        result[symbol] = *marketFallback;
                    }
                } else {
                    result["MARKET"] = *marketFallback;
                }
            }

            if (!result.empty()) {
                return result;
            }
        }
    }

    if (!tableHasColumn(db_, QStringLiteral("daily_bar"), normalizedField)) {
        return result;
    }

    const QString sql = QString("SELECT symbol, %1 AS field_value FROM daily_bar WHERE trade_date = :date AND %1 IS NOT NULL")
        .arg(normalizedField);
    auto queryResult = db_->executeQuery(sql, makeNamedParams({{":date", QString::fromStdString(context.date)}}));
    for (size_t i = 0; i < queryResult.rowCount(); ++i) {
        const auto& row = queryResult.getRow(i);
        const std::string symbol = row.getString("symbol").toStdString();
        if (!requested.empty() && requested.find(symbol) == requested.end()) {
            continue;
        }
        result[symbol] = row.getDouble("field_value");
    }
    return result;
}

std::vector<double> ConfigurableFactor::seriesForField(
    const CalculationContext& context,
    const std::string& symbol,
    const QString& field,
    int window) const
{
    std::vector<double> values;
    if (window <= 0) {
        return values;
    }

    const QDate endDate = QDate::fromString(QString::fromStdString(context.date), "yyyy-MM-dd");
    if (!endDate.isValid()) {
        return values;
    }
    const QDate startDate = endDate.addDays(-(window + 5));
    if (context.dataProvider && context.dataProvider->hasField(field.toStdString())) {
        const auto series = context.dataProvider->getSeries(
            symbol,
            startDate.toString("yyyy-MM-dd").toStdString(),
            endDate.toString("yyyy-MM-dd").toStdString(),
            field.toStdString()
        );
        for (const auto& point : series) {
            if (std::isfinite(point.value)) {
                values.push_back(point.value);
            }
        }
    } else if (db_) {
        const QString sql = QString(
            "SELECT %1 AS field_value FROM daily_bar WHERE symbol = :symbol AND trade_date BETWEEN :startDate AND :endDate AND %1 IS NOT NULL ORDER BY trade_date"
        ).arg(field);
        auto queryResult = db_->executeQuery(sql, makeNamedParams({
            {":symbol", QString::fromStdString(symbol)},
            {":startDate", startDate.toString("yyyy-MM-dd")},
            {":endDate", endDate.toString("yyyy-MM-dd")}
        }));
        for (size_t i = 0; i < queryResult.rowCount(); ++i) {
            values.push_back(queryResult.getRow(i).getDouble("field_value"));
        }
    }

    if (static_cast<int>(values.size()) > window) {
        values.erase(values.begin(), values.end() - window);
    }
    return values;
}

std::unordered_map<std::string, double> ConfigurableFactor::latestFinancialMetric(
    const CalculationContext& context,
    const QString& field,
    const QString& date) const
{
    std::unordered_map<std::string, double> result;
    if (!db_ || !tableHasColumn(db_, QStringLiteral("financial_indicator"), field.trimmed().toLower())) {
        return result;
    }
    const std::vector<std::string> symbols = effectiveSymbols(context);
    const std::unordered_set<std::string> requested(symbols.begin(), symbols.end());

    const QString sql = QString(
        "SELECT si.symbol, fi.report_date, fi.%1 AS field_value "
        "FROM financial_indicator fi "
        "JOIN symbol_info si ON si.symbol_id = fi.symbol_id "
        "WHERE fi.report_date <= :date AND fi.%1 IS NOT NULL "
        "ORDER BY si.symbol, fi.report_date DESC, fi.report_type DESC"
    ).arg(field);
    auto queryResult = db_->executeQuery(sql, makeNamedParams({{":date", date}}));
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < queryResult.rowCount(); ++i) {
        const auto& row = queryResult.getRow(i);
        const std::string symbol = row.getString("symbol").toStdString();
        if (!requested.empty() && requested.find(symbol) == requested.end()) {
            continue;
        }
        if (seen.find(symbol) != seen.end()) {
            continue;
        }
        seen.insert(symbol);
        result[symbol] = row.getDouble("field_value");
    }
    return result;
}

std::unordered_map<std::string, std::vector<double>> ConfigurableFactor::latestFinancialSeries(
    const CalculationContext& context,
    const QString& field,
    const QString& date,
    int limit) const
{
    std::unordered_map<std::string, std::vector<double>> result;
    if (!db_ || limit <= 0 || !tableHasColumn(db_, QStringLiteral("financial_indicator"), field.trimmed().toLower())) {
        return result;
    }
    const std::vector<std::string> symbols = effectiveSymbols(context);
    const std::unordered_set<std::string> requested(symbols.begin(), symbols.end());

    const QString sql = QString(
        "SELECT si.symbol, fi.report_date, fi.%1 AS field_value "
        "FROM financial_indicator fi "
        "JOIN symbol_info si ON si.symbol_id = fi.symbol_id "
        "WHERE fi.report_date <= :date AND fi.%1 IS NOT NULL "
        "ORDER BY si.symbol, fi.report_date DESC, fi.report_type DESC"
    ).arg(field);
    auto queryResult = db_->executeQuery(sql, makeNamedParams({{":date", date}}));
    for (size_t i = 0; i < queryResult.rowCount(); ++i) {
        const auto& row = queryResult.getRow(i);
        const std::string symbol = row.getString("symbol").toStdString();
        if (!requested.empty() && requested.find(symbol) == requested.end()) {
            continue;
        }
        auto& values = result[symbol];
        if (static_cast<int>(values.size()) >= limit) {
            continue;
        }
        values.push_back(row.getDouble("field_value"));
    }
    return result;
}

std::unordered_map<std::string, QString> ConfigurableFactor::industryBySymbol(const CalculationContext& context) const
{
    std::unordered_map<std::string, QString> result;
    if (!db_) {
        return result;
    }
    const std::vector<std::string> symbols = effectiveSymbols(context);
    const std::unordered_set<std::string> requested(symbols.begin(), symbols.end());
    auto queryResult = db_->executeQuery("SELECT symbol, industry FROM symbol_info WHERE industry IS NOT NULL");
    for (size_t i = 0; i < queryResult.rowCount(); ++i) {
        const auto& row = queryResult.getRow(i);
        const std::string symbol = row.getString("symbol").toStdString();
        if (!requested.empty() && requested.find(symbol) == requested.end()) {
            continue;
        }
        result[symbol] = row.getString("industry").trimmed();
    }
    return result;
}

const ConfigurableFactor::Params::CustomVariableBinding* ConfigurableFactor::findCustomVariableBinding(const QString& variableName) const
{
    const QString normalized = variableName.trimmed().toLower();
    for (const auto& binding : params_.variables) {
        if (QString::fromStdString(binding.name).trimmed().toLower() == normalized) {
            return &binding;
        }
    }
    return nullptr;
}

CalculationResult ConfigurableFactor::calculateGrowth(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus = checkDataAvailability(context.date);
    if (!result.dataStatus.isValid()) {
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    }

    const QString metric = normalizedMetric();
    QString field = "total_revenue";
    if (metric == "net_profit_growth" || metric == "earnings_growth") {
        field = "net_profit";
    } else if (metric == "eps_growth") {
        field = "eps";
    }

    const auto seriesMap = latestFinancialSeries(context, field, QString::fromStdString(context.date), 2);
    for (const auto& [symbol, values] : seriesMap) {
        if (values.size() < 2 || std::abs(values[1]) < 1e-12) {
            continue;
        }
        const double growth = safeRatio(values[0] - values[1], std::abs(values[1]));
        if (std::isfinite(growth)) {
            result.values[symbol] = growth;
        }
    }

    result.metadata.set("metric", json_helper::toJsonValue(field.toStdString()));
    return result;
}

CalculationResult ConfigurableFactor::calculateLiquidity(const CalculationContext& context) const
{
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();

    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    if (context.dataProvider) {
        result.dataStatus.availability = DataAvailability::AVAILABLE;
        result.dataStatus.coverage = 1.0;
        result.dataStatus.message = "使用缓存数据集";
    } else {
        result.dataStatus = checkDataAvailability(context.date);
    }
    if (!result.dataStatus.isValid()) {
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    }

    const QString metric = normalizedMetric();
    const QString frequency = normalizeConfigurableFrequency(params_.frequency);
    const QString standardization = normalizeConfigurableStandardization(params_.standardization);
    const int window = (std::max)(1, params_.window);
    auto resolvePreviousAvailableDate = [&](const QString& anchorDate, const QString& requiredField) {
        if (anchorDate.isEmpty()) {
            return QString::fromStdString(context.date);
        }

        const int maxOffset = (std::max)(45, params_.lookbackPeriod);
        if (context.dataProvider) {
            const std::vector<std::string> symbols = context.symbols.empty()
                ? context.dataProvider->getAvailableSymbols(context.date)
                : context.symbols;
            for (int offset = 1; offset <= maxOffset; ++offset) {
                const QString candidate = QDate::fromString(anchorDate, Qt::ISODate).addDays(-offset).toString(Qt::ISODate);
                if (context.dataProvider->getCrossSection(candidate.toStdString(), requiredField.toStdString(), symbols).empty()) {
                    continue;
                }
                return candidate;
            }
        }

        if (db_) {
            const QString sql = QString(
                "SELECT MAX(trade_date) AS trade_date FROM daily_bar "
                "WHERE trade_date < :date AND %1 IS NOT NULL"
            ).arg(requiredField);
            auto queryResult = db_->executeQuery(sql, {{":date", anchorDate}});
            if (!queryResult.isEmpty()) {
                const QString resolvedDate = queryResult.getRow(0).getString("trade_date");
                if (!resolvedDate.isEmpty()) {
                    return resolvedDate;
                }
            }
        }

        return anchorDate;
    };

    QString effectiveDate = QString::fromStdString(context.date);
    QDate anchorDate = QDate::fromString(effectiveDate, Qt::ISODate);
    if (anchorDate.isValid()) {
        if (frequency == QStringLiteral("weekly")) {
            const int shiftToPreviousFriday = anchorDate.dayOfWeek() >= 5 ? anchorDate.dayOfWeek() - 5 : anchorDate.dayOfWeek() + 2;
            anchorDate = anchorDate.addDays(-shiftToPreviousFriday);
        } else if (frequency == QStringLiteral("monthly")) {
            anchorDate = QDate(anchorDate.year(), anchorDate.month(), 1).addDays(-1);
        }
        effectiveDate = anchorDate.toString(Qt::ISODate);
    }

    if (params_.laggedEnabled) {
        QString requiredField = QStringLiteral("turnover_rate");
        if (metric == QStringLiteral("volume")) {
            requiredField = QStringLiteral("volume");
        } else if (metric == QStringLiteral("amplitude")) {
            requiredField = QStringLiteral("amplitude");
        } else if (metric == QStringLiteral("amihud_illiquidity")) {
            requiredField = QStringLiteral("close");
        }
        effectiveDate = resolvePreviousAvailableDate(effectiveDate, requiredField);
    }

    CalculationContext effectiveContext = context;
    effectiveContext.date = effectiveDate.toStdString();

    const auto symbols = effectiveSymbols(effectiveContext);
    size_t populatedSymbolCount = 0;
    for (const auto& symbol : symbols) {
        double score = std::numeric_limits<double>::quiet_NaN();
        if (metric == "volume") {
            const auto values = seriesForField(effectiveContext, symbol, "volume", window);
            score = safeMean(values);
        } else if (metric == "amplitude") {
            const auto values = seriesForField(effectiveContext, symbol, "amplitude", window);
            score = -safeMean(values);
        } else if (metric == "amihud_illiquidity") {
            const auto closes = seriesForField(effectiveContext, symbol, "close", window + 1);
            const auto volumes = seriesForField(effectiveContext, symbol, "volume", window + 1);
            std::vector<double> ratios;
            const size_t pairCount = (std::min)(closes.size(), volumes.size());
            for (size_t i = 1; i < pairCount; ++i) {
                if (closes[i - 1] <= 0.0 || volumes[i] <= 0.0) {
                    continue;
                }
                const double ret = std::abs((closes[i] - closes[i - 1]) / closes[i - 1]);
                ratios.push_back(ret / volumes[i]);
            }
            score = -safeMean(ratios);
        } else {
            const auto values = seriesForField(effectiveContext, symbol, "turnover_rate", window);
            score = safeMean(values);
        }

        if (std::isfinite(score) && score != 0.0) {
            result.values[symbol] = score;
            ++populatedSymbolCount;
        }
    }

    if (params_.neutralizationEnabled && db_ && !result.values.empty()) {
        const auto industryMap = industryBySymbol(effectiveContext);
        std::unordered_map<QString, std::vector<double>> groupedValues;
        for (const auto& [symbol, value] : result.values) {
            const auto industryIt = industryMap.find(symbol);
            if (industryIt != industryMap.end() && !industryIt->second.isEmpty() && std::isfinite(value)) {
                groupedValues[industryIt->second].push_back(value);
            }
        }

        std::unordered_map<QString, double> industryMean;
        for (const auto& [industry, values] : groupedValues) {
            industryMean[industry] = safeMean(values);
        }

        for (auto& [symbol, value] : result.values) {
            const auto industryIt = industryMap.find(symbol);
            if (industryIt == industryMap.end()) {
                continue;
            }
            const auto meanIt = industryMean.find(industryIt->second);
            if (meanIt != industryMean.end() && std::isfinite(meanIt->second)) {
                value -= meanIt->second;
            }
        }
    }

    if (!result.values.empty()) {
        auto applyPercentileScores = [&]() {
            std::vector<std::pair<std::string, double>> ranked(result.values.begin(), result.values.end());
            std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
                return left.second < right.second;
            });
            if (ranked.size() == 1) {
                result.values[ranked.front().first] = 1.0;
                return;
            }
            for (size_t index = 0; index < ranked.size(); ++index) {
                result.values[ranked[index].first] = static_cast<double>(index) / static_cast<double>(ranked.size() - 1);
            }
        };

        if (standardization == QStringLiteral("percentile")) {
            applyPercentileScores();
        } else {
            std::vector<double> values;
            values.reserve(result.values.size());
            for (const auto& [symbol, value] : result.values) {
                Q_UNUSED(symbol);
                if (std::isfinite(value)) {
                    values.push_back(value);
                }
            }

            if (!values.empty()) {
                if (standardization == QStringLiteral("zscore")) {
                    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
                    double variance = 0.0;
                    for (double value : values) {
                        const double delta = value - mean;
                        variance += delta * delta;
                    }
                    const double stdev = std::sqrt(variance / static_cast<double>(values.size()));
                    if (stdev > 1e-12) {
                        for (auto& [symbol, value] : result.values) {
                            Q_UNUSED(symbol);
                            value = (value - mean) / stdev;
                        }
                    }
                } else if (standardization == QStringLiteral("minmax")) {
                    const auto [minIt, maxIt] = std::minmax_element(values.begin(), values.end());
                    const double range = *maxIt - *minIt;
                    if (range > 1e-12) {
                        for (auto& [symbol, value] : result.values) {
                            Q_UNUSED(symbol);
                            value = (value - *minIt) / range;
                        }
                    }
                }
            }
        }
    }

    result.metadata.set("metric", json_helper::toJsonValue(metric.toStdString()));
    result.metadata.set("window", json_helper::toJsonValue(window));
    result.metadata.set("effective_date", json_helper::toJsonValue(effectiveDate.toStdString()));
    result.metadata.set("frequency", json_helper::toJsonValue(frequency.toStdString()));
    result.metadata.set("lagged_enabled", json_helper::toJsonValue(params_.laggedEnabled));
    result.metadata.set("lookback_period", json_helper::toJsonValue(params_.lookbackPeriod));
    result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
    result.metadata.set("neutralization_enabled", json_helper::toJsonValue(params_.neutralizationEnabled));
    result.metadata.set("symbol_count", json_helper::toJsonValue(static_cast<int>(result.values.size())));

    const qint64 elapsedMs = elapsedTimer.elapsed();
    if (elapsedMs >= 300) {
        qDebug() << "ConfigurableFactor(liquidity): 计算耗时较长"
                 << "date=" << QString::fromStdString(context.date)
                 << "metric=" << metric
                 << "window=" << window
                 << "symbolCount=" << static_cast<int>(symbols.size())
                 << "resultCount=" << static_cast<int>(populatedSymbolCount)
                 << "usingCacheProvider=" << static_cast<bool>(context.dataProvider)
                 << "elapsedMs=" << elapsedMs;
    }
    return result;
}

CalculationResult ConfigurableFactor::calculateTechnical(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    if (context.dataProvider) {
        result.dataStatus.availability = DataAvailability::AVAILABLE;
        result.dataStatus.coverage = 1.0;
        result.dataStatus.message = "使用缓存数据集";
    } else {
        result.dataStatus = checkDataAvailability(context.date);
    }
    if (!result.dataStatus.isValid()) {
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    }

    const QString indicatorType = normalizeTechnicalIndicatorType(QString::fromStdString(params_.indicatorType));
    const QString priceField = normalizePriceField(QString::fromStdString(params_.priceType));
    const int window = (std::max)(2, params_.window);
    const auto symbols = effectiveSymbols(context);
    for (const auto& symbol : symbols) {
        double score = std::numeric_limits<double>::quiet_NaN();
        const auto volumeSeries = seriesForField(context, symbol, "volume", window);
        const double volumeMultiplier = params_.useVolume ? configurableVolumeMultiplier(volumeSeries) : 1.0;
        if (indicatorType == QStringLiteral("volume")) {
            auto localVolumeSeries = volumeSeries;
            if (localVolumeSeries.size() >= 2) {
                const double latest = localVolumeSeries.back();
                localVolumeSeries.pop_back();
                score = safeRatio(latest - safeMean(localVolumeSeries), safeMean(localVolumeSeries));
            }
        } else if (indicatorType == QStringLiteral("volatility")) {
            const auto closes = seriesForField(context, symbol, priceField, window + 1);
            std::vector<double> returns;
            for (size_t i = 1; i < closes.size(); ++i) {
                if (closes[i - 1] <= 0.0) {
                    continue;
                }
                returns.push_back((closes[i] - closes[i - 1]) / closes[i - 1]);
            }
            score = -safeStdDev(returns);
        } else if (indicatorType == QStringLiteral("momentum")) {
            const auto closes = seriesForField(context, symbol, priceField, window + 1);
            if (closes.size() >= 2 && closes.front() > 0.0) {
                score = (closes.back() - closes.front()) / closes.front();
            }
        } else {
            const auto closes = seriesForField(context, symbol, priceField, window);
            if (closes.size() >= 2) {
                const double latest = closes.back();
                score = safeRatio(latest - safeMean(closes), safeMean(closes));
            }
        }

        if (std::isfinite(score)) {
            score *= volumeMultiplier;
        }

        if (std::isfinite(score) && score != 0.0) {
            result.values[symbol] = score;
        }
    }

    result.metadata.set("indicator_type", json_helper::toJsonValue(indicatorType.toStdString()));
    result.metadata.set("price_type", json_helper::toJsonValue(priceField.toStdString()));
    result.metadata.set("use_volume", json_helper::toJsonValue(params_.useVolume));
    result.metadata.set("window", json_helper::toJsonValue(window));
    return result;
}

CalculationResult ConfigurableFactor::calculateDividend(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用红利字段";

    const QString metric = normalizedMetric();
    const auto directMetricMap = currentFieldCrossSection(context, metric.isEmpty() ? QStringLiteral("dividend_yield") : metric);
    const auto peMap = currentFieldCrossSection(context, "pe_ratio");
    const auto pbMap = currentFieldCrossSection(context, "pb_ratio");
    const auto roeMap = latestFinancialMetric(context, "roe", QString::fromStdString(context.date));
    const auto marginMap = latestFinancialMetric(context, "profit_margin", QString::fromStdString(context.date));
    const auto stabilityMap = latestFinancialSeries(context, "net_profit", QString::fromStdString(context.date), 4);
    const auto symbols = effectiveSymbols(context);

    if (directMetricMap.empty()) {
        result.dataStatus = CalculationResult::createError("红利因子缺少真实底层字段，已禁止使用代理模型回测").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("红利因子缺少真实底层字段，已禁止使用代理模型回测"));
        result.metadata.set("metric", json_helper::toJsonValue(metric.toStdString()));
        return result;
    }

    for (const auto& symbol : symbols) {
        const auto directIt = directMetricMap.find(symbol);
        if (directIt != directMetricMap.end() && std::isfinite(directIt->second)) {
            double directScore = directIt->second;
            if (metric == QStringLiteral("dividend_yield") && params_.minDividendYield > 0.0
                    && directScore < params_.minDividendYield / 100.0) {
                continue;
            }
            result.values[symbol] = directScore;
            continue;
        }

        Q_UNUSED(peMap);
        Q_UNUSED(pbMap);
        Q_UNUSED(roeMap);
        Q_UNUSED(marginMap);
        Q_UNUSED(stabilityMap);
    }

    result.metadata.set("metric", json_helper::toJsonValue(metric.toStdString()));
    result.metadata.set("data_mode", json_helper::toJsonValue("direct"));
    return result;
}

CalculationResult ConfigurableFactor::calculateMacroSector(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    const QString macroFactor = normalizeMacroFactor(QString::fromStdString(params_.macroFactor));
    const QString sectorType = normalizeSectorType(QString::fromStdString(params_.sectorType));
    result.dataStatus = CalculationResult::createError("宏观/行业因子当前只有代理实现，已禁止进入回测").dataStatus;
    result.metadata.set("error", json_helper::toJsonValue("宏观/行业因子当前只有代理实现，已禁止进入回测"));
    result.metadata.set("macro_factor", json_helper::toJsonValue(macroFactor.toStdString()));
    result.metadata.set("sector_type", json_helper::toJsonValue(sectorType.toStdString()));
    return result;
}

CalculationResult ConfigurableFactor::calculateSentiment(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    if (context.dataProvider) {
        result.dataStatus.availability = DataAvailability::AVAILABLE;
        result.dataStatus.coverage = 1.0;
        result.dataStatus.message = "使用情绪字段/代理模型";
    } else {
        result.dataStatus = checkDataAvailability(context.date);
    }
    if (!result.dataStatus.isValid()) {
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    }

    const int window = (std::max)(5, params_.window);
    const auto symbols = effectiveSymbols(context);
    const QString source = normalizeSentimentSource(QString::fromStdString(params_.sentimentSource));
    const QString metric = normalizedMetric().isEmpty() ? sentimentMetricForSource(source) : normalizedMetric();
    const auto directMetricMap = currentFieldCrossSection(context, metric);
    if (!directMetricMap.empty()) {
        for (const auto& [symbol, value] : directMetricMap) {
            if (std::isfinite(value)) {
                result.values[symbol] = value;
            }
        }
        result.metadata.set("metric", json_helper::toJsonValue(metric.toStdString()));
        result.metadata.set("sentiment_source", json_helper::toJsonValue(source.toStdString()));
        result.metadata.set("data_mode", json_helper::toJsonValue("direct"));
        return result;
    }

    result.dataStatus = CalculationResult::createError("情绪因子缺少真实情绪字段，已禁止使用市场宽度代理回测").dataStatus;
    result.metadata.set("error", json_helper::toJsonValue("情绪因子缺少真实情绪字段，已禁止使用市场宽度代理回测"));
    result.metadata.set("metric", json_helper::toJsonValue(metric.toStdString()));
    result.metadata.set("sentiment_source", json_helper::toJsonValue(source.toStdString()));
    return result;

}

std::unordered_map<std::string, double> ConfigurableFactor::evaluateCustomExpression(
    const CalculationContext& context,
    const QString& expression,
    const std::vector<std::string>& symbols,
    QString* errorMessage) const
{
    std::unordered_map<std::string, double> results;
    const QString resolvedExpression = expression.trimmed().isEmpty() ? QStringLiteral("close / open - 1") : expression.trimmed();
    QString parseError;
    const QStringList rpn = factor::custom_expression::toRpn(resolvedExpression.toLower(), &parseError);
    if (rpn.isEmpty()) {
        if (errorMessage) {
            *errorMessage = parseError;
        }
        return results;
    }

    const QStringList variables = factor::custom_expression::extractVariables(resolvedExpression.toLower());
    std::unordered_map<std::string, std::unordered_map<std::string, double>> fieldValues;
    for (const QString& variable : variables) {
        const auto* binding = findCustomVariableBinding(variable);
        QString sourceField = variable;
        if (binding) {
            sourceField = QString::fromStdString(binding->field).trimmed();
            if (sourceField.isEmpty() && binding->hasDefaultValue) {
                continue;
            }
            if (sourceField.isEmpty()) {
                sourceField = variable;
            }
        }
        fieldValues[variable.toStdString()] = currentFieldCrossSection(context, sourceField);
    }

    for (const auto& symbol : symbols) {
        std::unordered_map<std::string, double> variableMap;
        bool missingVariable = false;
        for (const QString& variable : variables) {
            const auto* binding = findCustomVariableBinding(variable);
            const auto fieldIt = fieldValues.find(variable.toStdString());
            if (fieldIt == fieldValues.end()) {
                if (binding && binding->hasDefaultValue) {
                    variableMap[variable.toStdString()] = binding->defaultValue;
                    continue;
                }
                missingVariable = true;
                break;
            }
            const auto valueIt = fieldIt->second.find(symbol);
            if (valueIt == fieldIt->second.end()) {
                if (binding && binding->hasDefaultValue) {
                    variableMap[variable.toStdString()] = binding->defaultValue;
                    continue;
                }
                missingVariable = true;
                break;
            }
            variableMap[variable.toStdString()] = valueIt->second;
        }
        if (missingVariable) {
            continue;
        }

        QString evalError;
        const auto evaluated = factor::custom_expression::evaluateRpn(rpn, variableMap, &evalError);
        if (!evaluated.has_value() || !std::isfinite(*evaluated)) {
            if (errorMessage && errorMessage->isEmpty()) {
                *errorMessage = evalError;
            }
            continue;
        }
        results[symbol] = *evaluated;
    }
    return results;
}

CalculationResult ConfigurableFactor::calculateCustom(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    if (context.dataProvider) {
        result.dataStatus.availability = DataAvailability::AVAILABLE;
        result.dataStatus.coverage = 1.0;
        result.dataStatus.message = "使用自定义表达式";
    } else {
        result.dataStatus = checkDataAvailability(context.date);
    }
    if (!result.dataStatus.isValid()) {
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    }

    QString errorMessage;
    result.values = evaluateCustomExpression(context,
                                             QString::fromStdString(params_.expression),
                                             effectiveSymbols(context),
                                             &errorMessage);
    if (result.values.empty() && !errorMessage.isEmpty()) {
        result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
        result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
    }
    result.metadata.set("expression", json_helper::toJsonValue(params_.expression));
    result.metadata.set("variable_count", json_helper::toJsonValue(static_cast<int>(params_.variables.size())));
    result.metadata.set("symbol_count", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    return result;
}

} // namespace factor