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
#include "../../ui/bridge/include/FactorRequirementInferenceUtils.h"
#include "../../../cache/include/cache_facade.h"
#include "../../../domain/factor/include/DataAvailabilityChecker.h"
#include "../../../domain/factor/include/CustomExpressionUtils.h"
#include "../../../domain/factor/include/FactorCacheManager.h"
#include "../../../domain/factor/include/FactorInstanceManager.h"
#include "../../ui/bridge/include/FactorInstanceResolutionUtils.h"
#include "../../ui/bridge/include/RiskConfigService.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <vector>
#include <QDebug>
#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUuid>

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

bool isNumericLikeValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return false;
    }

    if (value.typeId() == QMetaType::Int
        || value.typeId() == QMetaType::UInt
        || value.typeId() == QMetaType::LongLong
        || value.typeId() == QMetaType::ULongLong
        || value.typeId() == QMetaType::Float
        || value.typeId() == QMetaType::Double) {
        return std::isfinite(value.toDouble());
    }

    if (value.typeId() != QMetaType::QString) {
        return false;
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return false;
    }

    static const QRegularExpression numericPattern(
        QStringLiteral("^[+-]?(?:\\d+(?:\\.\\d+)?|\\.\\d+)(?:[eE][+-]?\\d+)?$"));
    if (!numericPattern.match(text).hasMatch()) {
        return false;
    }

    bool ok = false;
    const double numeric = text.toDouble(&ok);
    return ok && std::isfinite(numeric);
}

bool isNumericParameterKey(const QString& key)
{
    static const QSet<QString> kNumericKeys = {
        QStringLiteral("window"),
        QStringLiteral("liquidityWindow"),
        QStringLiteral("sentimentWindow"),
        QStringLiteral("lookback"),
        QStringLiteral("lookback_window"),
        QStringLiteral("lookbackWindow"),
        QStringLiteral("lookback_period"),
        QStringLiteral("lookbackPeriod"),
        QStringLiteral("period"),
        QStringLiteral("skip_recent"),
        QStringLiteral("skipRecent"),
        QStringLiteral("min_dividend_yield"),
        QStringLiteral("minDividendYield"),
        QStringLiteral("quality_threshold"),
        QStringLiteral("qualityThreshold"),
        QStringLiteral("sentiment_weight"),
        QStringLiteral("sentimentWeight"),
        QStringLiteral("transactionCost"),
        QStringLiteral("commissionRate"),
        QStringLiteral("commission"),
        QStringLiteral("slippageRate"),
        QStringLiteral("slippage"),
        QStringLiteral("riskFreeRate"),
        QStringLiteral("risk_free_rate"),
        QStringLiteral("maxDrawdownLimit"),
        QStringLiteral("maxDailyLoss"),
        QStringLiteral("maxPositionPercent"),
        QStringLiteral("maxTotalExposure"),
        QStringLiteral("stopLossPercent"),
        QStringLiteral("takeProfitPercent"),
        QStringLiteral("varWarningPercent"),
        QStringLiteral("maxCorrelation"),
        QStringLiteral("maxIndustryExposure"),
        QStringLiteral("maxPositions"),
        QStringLiteral("level1Breaker"),
        QStringLiteral("level2Breaker"),
        QStringLiteral("level3Breaker")
    };

    if (kNumericKeys.contains(key)) {
        return true;
    }

    const QString lower = key.trimmed().toLower();
    return lower.endsWith(QStringLiteral("rate"))
        || lower.endsWith(QStringLiteral("ratio"))
        || lower.endsWith(QStringLiteral("weight"))
        || lower.endsWith(QStringLiteral("window"))
        || lower.endsWith(QStringLiteral("period"))
        || lower.endsWith(QStringLiteral("days"))
        || lower.endsWith(QStringLiteral("percent"))
        || lower.endsWith(QStringLiteral("threshold"))
        || lower.endsWith(QStringLiteral("limit"));
}

bool isBooleanLikeValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return false;
    }

    if (value.typeId() == QMetaType::Bool) {
        return true;
    }

    if (value.typeId() == QMetaType::Int
        || value.typeId() == QMetaType::UInt
        || value.typeId() == QMetaType::LongLong
        || value.typeId() == QMetaType::ULongLong) {
        const qlonglong integerValue = value.toLongLong();
        return integerValue == 0 || integerValue == 1;
    }

    if (value.typeId() != QMetaType::QString) {
        return false;
    }

    const QString text = value.toString().trimmed().toLower();
    return text == QStringLiteral("true")
        || text == QStringLiteral("false")
        || text == QStringLiteral("0")
        || text == QStringLiteral("1");
}

bool isBooleanParameterKey(const QString& key)
{
    static const QSet<QString> kBooleanKeys = {
        QStringLiteral("laggedEnabled"),
        QStringLiteral("neutralizationEnabled"),
        QStringLiteral("industry_neutral"),
        QStringLiteral("industryNeutral"),
        QStringLiteral("use_percentile"),
        QStringLiteral("usePercentile"),
        QStringLiteral("autoStopEnabled"),
        QStringLiteral("log_transform"),
        QStringLiteral("logTransform"),
        QStringLiteral("use_volume"),
        QStringLiteral("useVolume")
    };

    if (kBooleanKeys.contains(key)) {
        return true;
    }

    const QString lower = key.trimmed().toLower();
    return lower.endsWith(QStringLiteral("enabled"))
        || lower.startsWith(QStringLiteral("is"))
        || lower.startsWith(QStringLiteral("use_"));
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
    if (majorCategory == QString::fromUtf8("宏观因子")) {
        return "macro";
    }
    if (majorCategory == QString::fromUtf8("行业因子")) {
        return "industry";
    }
    if (majorCategory == QString::fromUtf8("情绪因子")) {
        return "sentiment";
    }
    if (majorCategory == QString::fromUtf8("自定义因子") || majorCategory == QString::fromUtf8("自定义")) {
        return "custom";
    }
    if (majorCategory == QString::fromUtf8("低波因子") || majorCategory == QString::fromUtf8("低波动因子")) {
        return "low_volatility";
    }
    return normalized;
}

QString normalizeTechnicalIndicatorTypeId(const QString& rawType)
{
    const QString normalized = rawType.trimmed().toLower();
    if (normalized.isEmpty()) {
        return QStringLiteral("rsi");
    }
    if (normalized == QStringLiteral("rsi")
            || normalized == QStringLiteral("relative_strength_index")
            || normalized == QString::fromUtf8("相对强弱指数")
            || normalized == QString::fromUtf8("趋势指标")) {
        return QStringLiteral("rsi");
    }
    if (normalized == QStringLiteral("macd")
            || normalized == QStringLiteral("macd_indicator")
            || normalized == QString::fromUtf8("指数平滑异同平均线")
            || normalized == QString::fromUtf8("动量指标")) {
        return QStringLiteral("macd");
    }
    if (normalized == QStringLiteral("ma") || normalized == QStringLiteral("moving_average")
            || normalized == QStringLiteral("sma") || normalized == QString::fromUtf8("移动平均")
            || normalized == QString::fromUtf8("均线")) {
        return QStringLiteral("ma");
    }
    if (normalized == QStringLiteral("ema") || normalized == QStringLiteral("exponential_moving_average")
            || normalized == QStringLiteral("ema_indicator") || normalized == QString::fromUtf8("指数移动平均")) {
        return QStringLiteral("ema");
    }
    if (normalized == QStringLiteral("boll") || normalized == QStringLiteral("bollinger")
            || normalized == QStringLiteral("bollinger_bands") || normalized == QString::fromUtf8("布林带")) {
        return QStringLiteral("boll");
    }
    if (normalized == QStringLiteral("kdj") || normalized == QStringLiteral("stochastic")
            || normalized == QString::fromUtf8("随机指标")) {
        return QStringLiteral("kdj");
    }
    if (normalized == QStringLiteral("atr") || normalized == QStringLiteral("average_true_range")
            || normalized == QString::fromUtf8("真实波幅")) {
        return QStringLiteral("atr");
    }
    if (normalized == QStringLiteral("obv")
            || normalized == QStringLiteral("obv_indicator")
            || normalized == QString::fromUtf8("能量潮")
            || normalized == QString::fromUtf8("成交量指标")) {
        return QStringLiteral("obv");
    }
    if (normalized == QStringLiteral("vwap") || normalized == QStringLiteral("volume_weighted_average_price")
            || normalized == QString::fromUtf8("成交量加权平均价")) {
        return QStringLiteral("vwap");
    }
    if (normalized == QStringLiteral("volume_ratio") || normalized == QStringLiteral("volume_ratio_indicator")
            || normalized == QString::fromUtf8("量比")) {
        return QStringLiteral("volume_ratio");
    }
    if (normalized == QStringLiteral("turnover_stability")
            || normalized == QStringLiteral("turnover_stability_indicator")
            || normalized == QString::fromUtf8("换手率稳定性")
            || normalized == QString::fromUtf8("波动率指标")) {
        return QStringLiteral("turnover_stability");
    }
    if (normalized == QString::fromUtf8("成交量指标")) {
        return QStringLiteral("obv");
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
    if (normalized == QString::fromUtf8("宏观因子") || normalized == "macro") {
        return QString::fromUtf8("宏观因子");
    }
    if (normalized == QString::fromUtf8("行业因子") || normalized == "industry") {
        return QString::fromUtf8("行业因子");
    }
    if (normalized == QString::fromUtf8("情绪因子") || normalized == "sentiment") {
        return QString::fromUtf8("情绪因子");
    }
    if (normalized == QString::fromUtf8("自定义因子") || normalized == QString::fromUtf8("自定义") || normalized == "custom") {
        return QString::fromUtf8("自定义因子");
    }
    if (normalized == QString::fromUtf8("低波因子") ||normalized == "low_volatility") {
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

QString normalizeDividendMetric(const QString& rawMetric)
{
    const QString metric = rawMetric.trimmed().toLower();
    if (metric.isEmpty()) {
        return {};
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
}

QString normalizeMacroDimension(const QString& rawDimension)
{
    const QString dimension = rawDimension.trimmed().toLower();
    if (dimension.isEmpty()) {
        return {};
    }

    if (dimension == QStringLiteral("growth")
            || rawDimension == QString::fromUtf8("经济增长")
            || dimension == QStringLiteral("economic_growth")
            || dimension == QStringLiteral("growth_sensitivity")) {
        return QStringLiteral("growth");
    }
    if (dimension == QStringLiteral("inflation")
            || rawDimension == QString::fromUtf8("通货膨胀")
            || dimension == QStringLiteral("inflation_sensitivity")) {
        return QStringLiteral("inflation");
    }
    if (dimension == QStringLiteral("credit")
            || rawDimension == QString::fromUtf8("货币信用")
            || dimension == QStringLiteral("credit_sensitivity")) {
        return QStringLiteral("credit");
    }
    if (dimension == QStringLiteral("rates")
            || rawDimension == QString::fromUtf8("利率水平")
            || dimension == QStringLiteral("interest_rate_sensitivity")) {
        return QStringLiteral("rates");
    }
    if (dimension == QStringLiteral("policy")
            || rawDimension == QString::fromUtf8("政策环境")
            || dimension == QStringLiteral("policy_environment")) {
        return QStringLiteral("policy");
    }
    if (dimension == QStringLiteral("risk_appetite")
            || rawDimension == QString::fromUtf8("风险偏好")
            || dimension == QStringLiteral("risk_preference")) {
        return QStringLiteral("risk_appetite");
    }

    return {};
}

QString normalizeMacroIndicator(const QString& rawIndicator)
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

QStringList defaultMacroDimensions()
{
    return {QStringLiteral("growth"), QStringLiteral("inflation"), QStringLiteral("credit"), QStringLiteral("rates"), QStringLiteral("policy"), QStringLiteral("risk_appetite")};
}

QStringList defaultMacroIndicatorsForDimension(const QString& dimension)
{
    if (dimension == QStringLiteral("growth")) {
        return {QStringLiteral("industrial_added_value_yoy"), QStringLiteral("manufacturing_pmi"), QStringLiteral("gdp_yoy")};
    }
    if (dimension == QStringLiteral("inflation")) {
        return {QStringLiteral("cpi_yoy"), QStringLiteral("ppi_yoy")};
    }
    if (dimension == QStringLiteral("credit")) {
        return {QStringLiteral("m2_yoy"), QStringLiteral("social_financing_stock_yoy"), QStringLiteral("m1_m2_spread")};
    }
    if (dimension == QStringLiteral("rates")) {
        return {QStringLiteral("ten_year_bond_yield"), QStringLiteral("shibor_3m")};
    }
    if (dimension == QStringLiteral("policy")) {
        return {QStringLiteral("lpr_1y"), QStringLiteral("reserve_requirement_ratio")};
    }
    if (dimension == QStringLiteral("risk_appetite")) {
        return {QStringLiteral("aa_credit_spread"), QStringLiteral("vix_proxy")};
    }
    return {};
}

QStringList defaultMacroIndicators()
{
    QStringList indicators;
    const QStringList dimensions = defaultMacroDimensions();
    for (const QString& dimension : dimensions) {
        const QStringList dimensionIndicators = defaultMacroIndicatorsForDimension(dimension);
        for (const QString& indicator : dimensionIndicators) {
            if (!indicators.contains(indicator)) {
                indicators.append(indicator);
            }
        }
    }
    return indicators;
}

QString inferValuationMetricFromDescriptor(const QString& descriptor)
{
    return normalizeValuationMetric(descriptor);
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

    if (metric == "revenue_growth") {
        return "revenue_growth";
    }
    if (metric == "net_profit_growth") {
        return "net_profit_growth";
    }
    if (metric == "delta_roe") {
        return "delta_roe";
    }
    if (metric == "sue") {
        return "sue";
    }

    return {};
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

double calculateMean(const std::vector<double>& values)
{
    if (values.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / static_cast<double>(values.size());
}

double calculateStdDev(const std::vector<double>& values)
{
    if (values.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double mean = calculateMean(values);
    if (!std::isfinite(mean)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    double variance = 0.0;
    for (double value : values) {
        const double delta = value - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(values.size());
    return std::sqrt((std::max)(0.0, variance));
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

bool hasMeaningfulVariantValue(const QVariant& value);
QVariantMap canonicalizeParameterAliases(const QVariantMap& rawParameters);
QVariantMap canonicalizeFactorRecordParameters(const QVariantMap& rawFactorRecord);
QVariantMap normalizeCalculationParameters(const QString& majorCategory, const QVariantMap& parameters);

QVariantMap extractParametersFromConfig(const QVariantMap& config)
{
    const QVariantMap directParameters = config.value("parameters").toMap();
    return canonicalizeParameterAliases(directParameters);
}

bool hasMeaningfulVariantValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return false;
    }

    if (value.typeId() == QMetaType::QString) {
        return !value.toString().trimmed().isEmpty();
    }

    if (value.typeId() == QMetaType::QVariantList) {
        return !value.toList().isEmpty();
    }

    if (value.typeId() == QMetaType::QVariantMap) {
        return !value.toMap().isEmpty();
    }

    return true;
}

QVariantMap canonicalizeParameterAliases(const QVariantMap& rawParameters)
{
    static const QHash<QString, QString> aliasToCanonical = {
        {QStringLiteral("commissionRate"), QStringLiteral("transactionCost")},
        {QStringLiteral("commission"), QStringLiteral("transactionCost")},
        {QStringLiteral("transactionCost"), QStringLiteral("transactionCost")},
        {QStringLiteral("slippage"), QStringLiteral("slippageRate")},
        {QStringLiteral("slippageRate"), QStringLiteral("slippageRate")},
        {QStringLiteral("risk_free_rate"), QStringLiteral("riskFreeRate")},
        {QStringLiteral("riskFreeRate"), QStringLiteral("riskFreeRate")},
        {QStringLiteral("benchmark_symbol"), QStringLiteral("benchmarkSymbol")},
        {QStringLiteral("benchmarkSymbol"), QStringLiteral("benchmarkSymbol")},
        {QStringLiteral("period"), QStringLiteral("window")},
        {QStringLiteral("volatilityWindow"), QStringLiteral("window")},
        {QStringLiteral("liquidityWindow"), QStringLiteral("window")},
        {QStringLiteral("sentimentWindow"), QStringLiteral("window")},
        {QStringLiteral("lookback_window"), QStringLiteral("window")},
        {QStringLiteral("lookbackWindow"), QStringLiteral("window")},
        {QStringLiteral("window"), QStringLiteral("window")},
        {QStringLiteral("lookback"), QStringLiteral("lookbackPeriod")},
        {QStringLiteral("lookback_period"), QStringLiteral("lookbackPeriod")},
        {QStringLiteral("lookbackPeriod"), QStringLiteral("lookbackPeriod")},
        {QStringLiteral("valuationMetrics"), QStringLiteral("valuationMetrics")}
    };

    QVariantMap canonicalized;
    for (auto it = rawParameters.begin(); it != rawParameters.end(); ++it) {
        const QString canonicalKey = aliasToCanonical.value(it.key(), it.key());
        const QVariant currentValue = it.value();

        if (!canonicalized.contains(canonicalKey)) {
            canonicalized.insert(canonicalKey, currentValue);
            continue;
        }

        const QVariant existingValue = canonicalized.value(canonicalKey);
        if (!hasMeaningfulVariantValue(existingValue) && hasMeaningfulVariantValue(currentValue)) {
            canonicalized.insert(canonicalKey, currentValue);
        }
    }

    return canonicalized;
}

QVariant coerceNumericLikeVariant(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return value;
    }

    if (value.typeId() == QMetaType::QVariantMap) {
        const QVariantMap input = value.toMap();
        QVariantMap output;
        for (auto it = input.begin(); it != input.end(); ++it) {
            output.insert(it.key(), coerceNumericLikeVariant(it.value()));
        }
        return output;
    }

    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList input = value.toList();
        QVariantList output;
        output.reserve(input.size());
        for (const QVariant& item : input) {
            output.append(coerceNumericLikeVariant(item));
        }
        return output;
    }

    if (value.typeId() != QMetaType::QString) {
        return value;
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return value;
    }

    const QString lowered = text.toLower();
    if (lowered == QStringLiteral("true")) {
        return true;
    }
    if (lowered == QStringLiteral("false")) {
        return false;
    }

    static const QRegularExpression numericPattern(
        QStringLiteral("^[+-]?(?:\\d+(?:\\.\\d+)?|\\.\\d+)(?:[eE][+-]?\\d+)?$"));
    if (!numericPattern.match(text).hasMatch()) {
        return value;
    }

    bool ok = false;
    const double numeric = text.toDouble(&ok);
    if (!ok || !std::isfinite(numeric)) {
        return value;
    }

    return numeric;
}

QVariantMap coerceNumericLikeMap(const QVariantMap& map)
{
    QVariantMap normalized;
    for (auto it = map.begin(); it != map.end(); ++it) {
        normalized.insert(it.key(), coerceNumericLikeVariant(it.value()));
    }
    return normalized;
}

QVariantMap canonicalizeFactorRecordParameters(const QVariantMap& rawFactorRecord)
{
    QVariantMap normalized = rawFactorRecord;
    const QString factorType = rawFactorRecord.value(QStringLiteral("factorType")).toString().trimmed();
    const QString majorCategory = rawFactorRecord.value(QStringLiteral("majorCategory")).toString().trimmed();
    const QString calculationType = !factorType.isEmpty() ? factorType : majorCategory;
    normalized.insert(
        QStringLiteral("parameters"),
        normalizeCalculationParameters(
            calculationType,
            canonicalizeParameterAliases(rawFactorRecord.value(QStringLiteral("parameters")).toMap())
        )
    );
    return normalized;
}

QVariantMap normalizeCalculationParameters(const QString& majorCategory, const QVariantMap& parameters)
{
    QVariantMap normalized = canonicalizeParameterAliases(parameters);
    const QVariantMap originalParameters = parameters;
    const QString factorType = normalizeFactorType(majorCategory);

    if (factorType == "value") {
        const QStringList rawMetrics = variantToStringList(normalized.value("valuationMetrics"));
        QVariantList valuationMetrics;
        valuationMetrics.reserve(rawMetrics.size());
        for (const QString& metric : rawMetrics) {
            const QString normalizedMetric = normalizeValuationMetric(metric);
            if (!normalizedMetric.isEmpty() && !valuationMetrics.contains(normalizedMetric)) {
                valuationMetrics.append(normalizedMetric);
            }
        }
        if (valuationMetrics.isEmpty()) {
            const QString fallbackMetric = normalizeValuationMetric(normalized.value("valuationType").toString());
            if (!fallbackMetric.isEmpty()) {
                valuationMetrics.append(fallbackMetric);
            }
        }
        if (!valuationMetrics.isEmpty()) {
            normalized["valuationMetrics"] = valuationMetrics;
        }

        const auto weightKeyForMetric = [](const QString& metric) -> QString {
            if (metric == QStringLiteral("bp")) {
                return QStringLiteral("bpWeight");
            }
            if (metric == QStringLiteral("ep")) {
                return QStringLiteral("epWeight");
            }
            if (metric == QStringLiteral("dividend_yield")) {
                return QStringLiteral("dividendYieldWeight");
            }
            if (metric == QStringLiteral("cf_p")) {
                return QStringLiteral("cfPWeight");
            }
            return {};
        };

        const QStringList supportedOrder = {QStringLiteral("bp"), QStringLiteral("ep"), QStringLiteral("dividend_yield"), QStringLiteral("cf_p")};
        QSet<QString> selectedMetricSet;
        for (const QVariant& metricValue : valuationMetrics) {
            selectedMetricSet.insert(metricValue.toString());
        }

        QVector<QString> activeWeightKeys;
        QVector<double> sourceWeights;
        double sourceWeightSum = 0.0;
        for (const QString& metric : supportedOrder) {
            const QString weightKey = weightKeyForMetric(metric);
            if (weightKey.isEmpty()) {
                continue;
            }

            if (!selectedMetricSet.contains(metric)) {
                normalized[weightKey] = 0;
                continue;
            }

            double sourceWeight = normalized.value(weightKey).toDouble();
            if (!(sourceWeight > 0.0)) {
                sourceWeight = originalParameters.value(weightKey).toDouble();
            }
            if (!(sourceWeight > 0.0)) {
                sourceWeight = 25.0;
            }

            activeWeightKeys.append(weightKey);
            sourceWeights.append(sourceWeight);
            sourceWeightSum += sourceWeight;
        }

        if (!activeWeightKeys.isEmpty()) {
            if (sourceWeightSum > 0.0) {
                int allocatedWeight = 0;
                for (int index = 0; index < activeWeightKeys.size(); ++index) {
                    int normalizedWeight = 0;
                    if (index == activeWeightKeys.size() - 1) {
                        normalizedWeight = 100 - allocatedWeight;
                    } else {
                        normalizedWeight = qRound(100.0 * sourceWeights[index] / sourceWeightSum);
                        allocatedWeight += normalizedWeight;
                    }
                    normalized[activeWeightKeys[index]] = (std::max)(0, normalizedWeight);
                }
            } else {
                const int equalWeight = activeWeightKeys.isEmpty() ? 0 : 100 / activeWeightKeys.size();
                int allocatedWeight = 0;
                for (int index = 0; index < activeWeightKeys.size(); ++index) {
                    int normalizedWeight = 0;
                    if (index == activeWeightKeys.size() - 1) {
                        normalizedWeight = 100 - allocatedWeight;
                    } else {
                        normalizedWeight = equalWeight;
                        allocatedWeight += normalizedWeight;
                    }
                    normalized[activeWeightKeys[index]] = (std::max)(0, normalizedWeight);
                }
            }
        }

        normalized["use_percentile"] = normalized.value("use_percentile", normalized.value("usePercentile", true));
        normalized["industry_neutral"] = normalized.value("industry_neutral", normalized.value("industryNeutral", false));
        normalized["standardization"] = normalized.value("standardization", QStringLiteral("zscore"));
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
            sizeMetric = "circulating_market_cap";
        }
        normalized["size_metric"] = sizeMetric;
        normalized["log_transform"] = normalized.value("log_transform", normalized.value("logTransform", true));
        normalized["use_percentile"] = normalized.value("use_percentile", normalized.value("usePercentile", true));
        normalized["industry_neutral"] = normalized.value("industry_neutral", normalized.value("industryNeutral", true));
        normalized["standardization"] = normalized.value("standardization", QStringLiteral("zscore"));
    } else if (factorType == "growth") {
        QVariantList growthMetrics;
        const QStringList rawMetrics = variantToStringList(normalized.value("growthMetrics"));
        for (const QString& rawMetric : rawMetrics) {
            const QString normalizedMetric = normalizeGrowthMetric(rawMetric);
            if (!normalizedMetric.isEmpty()) {
                growthMetrics.append(normalizedMetric);
            }
        }

        normalized["growthMetrics"] = growthMetrics;

        QVariantList growthWeights;
        for (const QVariant& metricValue : growthMetrics) {
            const QString selectedMetric = metricValue.toString();
            QString weightKey;
            if (selectedMetric == QStringLiteral("revenue_growth")) {
                weightKey = QStringLiteral("revenueGrowthWeight");
            } else if (selectedMetric == QStringLiteral("net_profit_growth")) {
                weightKey = QStringLiteral("netProfitGrowthWeight");
            } else if (selectedMetric == QStringLiteral("delta_roe")) {
                weightKey = QStringLiteral("deltaRoeWeight");
            } else if (selectedMetric == QStringLiteral("sue")) {
                weightKey = QStringLiteral("sueWeight");
            }

            if (weightKey.isEmpty() || !normalized.contains(weightKey)) {
                growthWeights.clear();
                break;
            }

            bool ok = false;
            const int weight = normalized.value(weightKey).toInt(&ok);
            if (!ok) {
                growthWeights.clear();
                break;
            }
            growthWeights.append(weight);
        }

        if (!growthWeights.isEmpty() && growthWeights.size() == growthMetrics.size()) {
            normalized["growthWeights"] = growthWeights;
        } else {
            normalized.remove("growthWeights");
        }
        normalized["timeframe"] = normalized.value("timeframe", "quarterly");
        normalized["lookback_period"] = normalized.value("lookback_period", normalized.value("lookbackPeriod", 252));
        normalized["standardization"] = normalized.value("standardization", QStringLiteral("zscore"));
        normalized["neutralization_enabled"] = normalized.value("neutralization_enabled", normalized.value("neutralizationEnabled", false));
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
        QVariantList dividendMetrics;
        const QStringList rawDividendMetrics = variantToStringList(normalized.value("dividendMetrics"));
        for (const QString& rawMetric : rawDividendMetrics) {
            const QString metric = normalizeDividendMetric(rawMetric);
            if (!metric.isEmpty() && !dividendMetrics.contains(metric)) {
                dividendMetrics.append(metric);
            }
        }

        if (dividendMetrics.isEmpty()) {
            QString metric = normalizeDividendMetric(normalized.value("metric").toString());
            if (metric.isEmpty()) {
                metric = normalizeDividendMetric(normalized.value("dividendMetric").toString());
            }
            if (metric.isEmpty()) {
                metric = QStringLiteral("dividend_yield");
            }
            dividendMetrics.append(metric);
        }

        normalized["dividendMetrics"] = dividendMetrics;
        normalized["metric"] = dividendMetrics.first().toString();
        normalized["dividendMetric"] = dividendMetrics.first().toString();
        normalized["min_dividend_yield"] = normalized.value("min_dividend_yield", normalized.value("minDividendYield", 0));
        normalized["timeframe"] = normalized.value("timeframe", "annual");
    } else if (factorType == "technical") {
        QVariantList technicalIndicators;
        const QVariant technicalIndicatorsValue = normalized.value("technicalIndicators");
        if (technicalIndicatorsValue.canConvert<QVariantList>()) {
            technicalIndicators = technicalIndicatorsValue.toList();
        }
        if (technicalIndicators.isEmpty()) {
            const QString legacyIndicatorType = normalized.value("indicator_type", normalized.value("indicatorType")).toString().trimmed();
            if (!legacyIndicatorType.isEmpty()) {
                technicalIndicators.append(legacyIndicatorType);
            }
        }
        if (technicalIndicators.isEmpty()) {
            technicalIndicators.append(QStringLiteral("rsi"));
        }

        QStringList normalizedIndicators;
        for (const QVariant& indicatorValue : technicalIndicators) {
            const QString indicatorType = normalizeTechnicalIndicatorTypeId(indicatorValue.toString());
            if (!indicatorType.isEmpty() && !normalizedIndicators.contains(indicatorType)) {
                normalizedIndicators.append(indicatorType);
            }
        }
        if (normalizedIndicators.isEmpty()) {
            normalizedIndicators.append(QStringLiteral("rsi"));
        }

        normalized["technicalIndicators"] = normalizedIndicators;
        normalized["indicator_type"] = normalizedIndicators.first();
        normalized["indicatorType"] = normalizedIndicators.first();
        normalized["indicatorTypes"] = normalizedIndicators;
        normalized["window"] = normalized.value("window", normalized.value("rsiWindow", 14));
        normalized["rsiWindow"] = normalized.value("rsiWindow", normalized.value("window", 14));
        normalized["maWindow"] = normalized.value("maWindow", 20);
        normalized["emaWindow"] = normalized.value("emaWindow", 20);
        normalized["bollWindow"] = normalized.value("bollWindow", 20);
        normalized["bollStdDev"] = normalized.value("bollStdDev", 2.0);
        normalized["kdjWindow"] = normalized.value("kdjWindow", 9);
        normalized["kdjKPeriod"] = normalized.value("kdjKPeriod", 3);
        normalized["kdjDPeriod"] = normalized.value("kdjDPeriod", 3);
        normalized["atrWindow"] = normalized.value("atrWindow", 14);
        normalized["macdFastPeriod"] = normalized.value("macdFastPeriod", 12);
        normalized["macdSlowPeriod"] = normalized.value("macdSlowPeriod", 26);
        normalized["macdSignalPeriod"] = normalized.value("macdSignalPeriod", 9);
        normalized["obvWindow"] = normalized.value("obvWindow", 20);
        normalized["vwapWindow"] = normalized.value("vwapWindow", 20);
        normalized["volumeRatioWindow"] = normalized.value("volumeRatioWindow", 20);
        normalized["turnoverStabilityWindow"] = normalized.value("turnoverStabilityWindow", 60);
        normalized["turnoverStabilityMetric"] = normalized.value("turnoverStabilityMetric", QStringLiteral("turnover_rate"));
        normalized["technicalPriceType"] = normalized.value("technicalPriceType", normalized.value("priceType", QStringLiteral("close")));
        normalized["lookback_period"] = normalized.value("lookback_period", normalized.value("lookbackPeriod", 252));
        normalized["frequency"] = normalized.value("frequency", QString::fromUtf8("日频"));
        normalized["price_type"] = normalized.value("price_type", normalized.value("technicalPriceType", normalized.value("priceType", "close")));
        normalized["use_volume"] = normalizedIndicators.contains(QStringLiteral("obv"));
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
    } else if (factorType == "macro") {
        QStringList macroDimensions = variantToStringList(normalized.value("macroDimensions"));
        QStringList normalizedMacroDimensions;
        for (const QString& rawDimension : macroDimensions) {
            const QString dimension = normalizeMacroDimension(rawDimension);
            if (!dimension.isEmpty() && !normalizedMacroDimensions.contains(dimension)) {
                normalizedMacroDimensions.append(dimension);
            }
        }

        QStringList macroIndicators = variantToStringList(normalized.value("macroIndicators"));
        QStringList normalizedMacroIndicators;
        for (const QString& rawIndicator : macroIndicators) {
            const QString indicator = normalizeMacroIndicator(rawIndicator);
            if (!indicator.isEmpty() && !normalizedMacroIndicators.contains(indicator)) {
                normalizedMacroIndicators.append(indicator);
            }
        }

        const QString legacyMacroMetric = normalizeMacroDimension(normalized.value("macroMetric").toString());
        if (normalizedMacroDimensions.isEmpty() && !legacyMacroMetric.isEmpty()) {
            normalizedMacroDimensions.append(legacyMacroMetric);
        }
        if (normalizedMacroDimensions.isEmpty()) {
            normalizedMacroDimensions = defaultMacroDimensions();
        }

        if (normalizedMacroIndicators.isEmpty()) {
            for (const QString& dimension : normalizedMacroDimensions) {
                const QStringList dimensionIndicators = defaultMacroIndicatorsForDimension(dimension);
                for (const QString& indicator : dimensionIndicators) {
                    if (!normalizedMacroIndicators.contains(indicator)) {
                        normalizedMacroIndicators.append(indicator);
                    }
                }
            }
        }

        if (normalizedMacroIndicators.isEmpty()) {
            normalizedMacroIndicators = defaultMacroIndicators();
        }

        normalized["macroDimensions"] = normalizedMacroDimensions;
        normalized["macroIndicators"] = normalizedMacroIndicators;
        normalized["macroMetric"] = normalizedMacroDimensions.first();
        normalized["macroFrequency"] = normalized.value("macroFrequency", normalized.value("frequency", QStringLiteral("auto")));
        normalized["macroWindow"] = normalized.value("macroWindow", normalized.value("window", normalized.value("lookbackPeriod", 12)));
    } else if (factorType == "industry") {
        normalized["sectorType"] = normalized.value("sectorType", QString::fromUtf8("申万一级"));
        normalized["industryMetric"] = normalized.value("industryMetric", QString::fromUtf8("industry_momentum"));
        normalized["window"] = normalized.value("window", normalized.value("lookbackPeriod", 20));
    } else if (factorType == "sentiment") {
        normalized["sentiment_source"] = normalized.value("sentiment_source", normalized.value("sentimentSource", "market_sentiment"));
        normalized["metric"] = normalized.value("metric", normalized.value("sentimentMetric", "sentiment_score")).toString();
        normalized["window"] = normalized.value("window", normalized.value("sentimentWindow", normalized.value("lookbackDays", 20)));
        normalized["sentiment_weight"] = normalized.value("sentiment_weight", normalized.value("sentimentWeight", 0.3));
    } else if (factorType == "custom") {
        normalized["expression"] = normalized.value("expression", "close / open - 1").toString();
        normalized["variables"] = normalized.value("variables", QVariantList{}).toList();
    } else if (factorType == "low_volatility") {
        normalized["window"] = normalized.value("window", normalized.value("volatilityWindow", 20));
        QVariantList components = normalized.value("components").toList();
        if (components.isEmpty()) {
            components << QStringLiteral("volatility") << QStringLiteral("drawdown") << QStringLiteral("beta");
        }
        normalized["components"] = components;
        normalized["volatilityWeight"] = normalized.value("volatilityWeight", 33.4);
        normalized["drawdownWeight"] = normalized.value("drawdownWeight", 33.3);
        normalized["betaWeight"] = normalized.value("betaWeight", 33.3);
    }

    return normalized;
}

QVariantMap buildDataRequirementsConfig(const QString& majorCategory, const QVariantMap& calculation)
{
    const QString factorType = normalizeFactorType(majorCategory);
    QVariantList required;
    QVariantList optional;
    QString sourceTable;

    if (factorType == "technical") {
        const QStringList indicators = variantToStringList(calculation.value("technicalIndicators"));
        const QStringList resolvedIndicators = indicators.isEmpty()
            ? QStringList{normalizeTechnicalIndicatorTypeId(calculation.value("indicator_type", calculation.value("indicatorType")).toString())}
            : indicators;
        const QString priceField = calculation.value("technicalPriceType", calculation.value("price_type", calculation.value("priceType", "close"))).toString().trimmed().toLower();

        auto appendUnique = [&required](const QString& field) {
            if (!field.isEmpty() && !required.contains(field)) {
                required.append(field);
            }
        };

        for (const QString& indicator : resolvedIndicators) {
            const QString normalizedIndicator = normalizeTechnicalIndicatorTypeId(indicator);
            if (normalizedIndicator == QStringLiteral("rsi")
                    || normalizedIndicator == QStringLiteral("ma")
                    || normalizedIndicator == QStringLiteral("ema")
                    || normalizedIndicator == QStringLiteral("boll")
                    || normalizedIndicator == QStringLiteral("macd")
                    || normalizedIndicator == QStringLiteral("vwap")) {
                appendUnique(priceField.isEmpty() ? QStringLiteral("close") : priceField);
            }
            if (normalizedIndicator == QStringLiteral("macd")
                    || normalizedIndicator == QStringLiteral("kdj")
                    || normalizedIndicator == QStringLiteral("atr")) {
                appendUnique(QStringLiteral("high"));
                appendUnique(QStringLiteral("low"));
                appendUnique(QStringLiteral("close"));
            }
            if (normalizedIndicator == QStringLiteral("obv")
                    || normalizedIndicator == QStringLiteral("vwap")
                    || normalizedIndicator == QStringLiteral("volume_ratio")) {
                appendUnique(QStringLiteral("volume"));
            }
            if (normalizedIndicator == QStringLiteral("turnover_stability")) {
                appendUnique(calculation.value("turnoverStabilityMetric", QStringLiteral("turnover_rate")).toString().trimmed().toLower());
            }
        }

        if (required.isEmpty()) {
            appendUnique(QStringLiteral("close"));
        }
        sourceTable = QStringLiteral("daily_bar");
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
    } else {
        const auto requirementProfile = factor::bridge::resolveFactorRequirementProfile(factorType, calculation);
        required = requirementProfile.requiredFields;
        optional = requirementProfile.optionalFields;
        sourceTable = requirementProfile.sourceTable;
    }

    QVariantMap result;
    result["required"] = required;
    if (!optional.isEmpty()) {
        result["optional"] = optional;
    }
    if (!sourceTable.isEmpty()) {
        result["source_table"] = sourceTable;
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
        const QStringList indicators = variantToStringList(calculation.value("technicalIndicators"));
        const QStringList resolvedIndicators = indicators.isEmpty()
            ? QStringList{normalizeTechnicalIndicatorTypeId(calculation.value("indicator_type", calculation.value("indicatorType")).toString())}
            : indicators;
        int minDataPoints = 1;
        for (const QString& indicator : resolvedIndicators) {
            const QString normalizedIndicator = normalizeTechnicalIndicatorTypeId(indicator);
            if (normalizedIndicator == QStringLiteral("macd")) {
                minDataPoints = (std::max)(minDataPoints, (std::max)(35, calculation.value("macdSlowPeriod", 26).toInt() + calculation.value("macdSignalPeriod", 9).toInt()));
            } else if (normalizedIndicator == QStringLiteral("kdj")) {
                minDataPoints = (std::max)(minDataPoints, (std::max)(1, calculation.value("kdjWindow", 9).toInt() + calculation.value("kdjKPeriod", 3).toInt() + calculation.value("kdjDPeriod", 3).toInt()));
            } else if (normalizedIndicator == QStringLiteral("boll")) {
                minDataPoints = (std::max)(minDataPoints, (std::max)(1, calculation.value("bollWindow", 20).toInt()));
            } else if (normalizedIndicator == QStringLiteral("atr")) {
                minDataPoints = (std::max)(minDataPoints, (std::max)(1, calculation.value("atrWindow", 14).toInt() + 1));
            } else if (normalizedIndicator == QStringLiteral("ma")) {
                minDataPoints = (std::max)(minDataPoints, (std::max)(1, calculation.value("maWindow", 20).toInt()));
            } else if (normalizedIndicator == QStringLiteral("ema")) {
                minDataPoints = (std::max)(minDataPoints, (std::max)(1, calculation.value("emaWindow", 20).toInt()));
            } else if (normalizedIndicator == QStringLiteral("vwap")) {
                minDataPoints = (std::max)(minDataPoints, (std::max)(1, calculation.value("vwapWindow", 20).toInt()));
            } else if (normalizedIndicator == QStringLiteral("volume_ratio")) {
                minDataPoints = (std::max)(minDataPoints, (std::max)(1, calculation.value("volumeRatioWindow", 20).toInt()));
            } else if (normalizedIndicator == QStringLiteral("turnover_stability")) {
                minDataPoints = (std::max)(minDataPoints, (std::max)(1, calculation.value("turnoverStabilityWindow", 60).toInt()));
            } else {
                minDataPoints = (std::max)(minDataPoints, (std::max)(1, calculation.value("rsiWindow", calculation.value("window", 14)).toInt()) + 1);
            }
        }
        rules["min_data_points"] = minDataPoints;
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
        const QStringList dividendMetrics = variantToStringList(calculation.value("dividendMetrics"));
        bool requiresHistory = false;
        for (const QString& rawMetric : dividendMetrics) {
            if (normalizeDividendMetric(rawMetric) == QStringLiteral("dividend_stability")) {
                requiresHistory = true;
                break;
            }
        }
        if (!requiresHistory) {
            const QString metric = normalizeDividendMetric(calculation.value("metric", "dividend_yield").toString());
            requiresHistory = metric == QStringLiteral("dividend_stability");
        }
        rules["min_data_points"] = requiresHistory ? 2 : 1;
        rules["handle_outliers"] = "winsorize_3sigma";
    } else if (factorType == "macro" || factorType == "industry" || factorType == "sentiment") {
        const int window = factorType == "macro"
            ? (std::max)(3, calculation.value("macroWindow", calculation.value("window", calculation.value("lookbackPeriod", 12))).toInt())
            : (std::max)(5, calculation.value("window", 1).toInt());
        rules["min_data_points"] = window + 1;
        rules["handle_outliers"] = "winsorize_3sigma";
    } else if (factorType == "custom") {
        rules["min_data_points"] = 1;
        rules["handle_outliers"] = "winsorize_3sigma";
    } else if (factorType == "low_volatility") {
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
    const QVariantMap rawParameters = coerceNumericLikeMap(
        canonicalizeParameterAliases(factorData.value("parameters").toMap()));
    const QVariantMap calculation = coerceNumericLikeMap(
        normalizeCalculationParameters(factorType, rawParameters));
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

    QString factorId = legacyFactorId;
    if (factorId.isEmpty()) {
        const QVariantMap metadata = config.value(QStringLiteral("metadata")).toMap();
        factorId = chooseFirstNonEmpty(metadata, {"factorId", "factor_id"});
    }
    if (factorId.isEmpty()) {
        factorId = chooseFirstNonEmpty(config, {"factorId", "factor_id"});
    }

    QVariantMap factorMap;
    factorMap["factorId"] = factorId;
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
        QElapsedTimer initializeTimer;
        initializeTimer.start();

        if (m_isLoading) {
            return;
        }

        m_isLoading = true;
        emit isLoadingChanged();

        QElapsedTimer repositoryTimer;
        repositoryTimer.start();
        initializeRepository();
        qDebug() << "[FactorTiming] initializeRepository elapsed(ms):" << repositoryTimer.elapsed();

        if (!m_repository) {
            qWarning() << "FactorService::initialize: 仓储初始化失败";
            m_isLoading = false;
            emit isLoadingChanged();
            return;
        }

        QElapsedTimer domainRuntimeTimer;
        domainRuntimeTimer.start();
        if (!initializeFactorDomainRuntime()) {
            qWarning() << "FactorService::initialize: domain/factor 运行时未完全就绪，将保留旧逻辑兜底";
        }
        qDebug() << "[FactorTiming] initializeFactorDomainRuntime elapsed(ms):" << domainRuntimeTimer.elapsed();

        QElapsedTimer loadTimer;
        loadTimer.start();
        loadFactorsFromDatabase();
        qDebug() << "[FactorTiming] loadFactorsFromDatabase elapsed(ms):" << loadTimer.elapsed();

        m_initialized = true;
        m_isLoading = false;

        emit initializedChanged();
        emit isLoadingChanged();
        emit cacheLoadedChanged();
        qDebug() << "[FactorTiming] FactorService::initialize total elapsed(ms):" << initializeTimer.elapsed();

    } catch (const std::exception& e) {
        m_isLoading = false;
        emit isLoadingChanged();
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
    dataToSave["parameters"] = canonicalizeParameterAliases(dataToSave.value("parameters").toMap());
    QString requestedFactorId = dataToSave.value("factorId").toString().trimmed();
    const QString factorName = dataToSave.value("factorName").toString();

    if (!requestedFactorId.isEmpty()) {
        const QString errorMsg = QStringLiteral("新增因子不应携带 factorId，请走编辑更新流程");
        qWarning() << "FactorService::addFactor:" << errorMsg << requestedFactorId;
        publishOperationReport("addFactor",
                               requestedFactorId,
                               false,
                               "invalid_create_payload",
                               errorMsg);
        return QString();
    }

    dataToSave["factorId"] = generateFactorId(factorName);

    // addFactor 必须创建新实例，避免前端残留 instanceId 导致更新已有实例。
    dataToSave.remove("instanceId");

    QString factorId = dataToSave["factorId"].toString();

    bool dbSuccess = saveFactorToDatabase(dataToSave);
    if (!dbSuccess) {
        QString errorMsg = QString("因子保存到数据库失败: %1").arg(factorId);
        qWarning() << errorMsg;
        publishOperationReport("addFactor", factorId, false, "save_database_failed", errorMsg);
        return QString();
    }

    QString domainSyncError;
    if (!syncFactorDefinitionToDomain(dataToSave, &domainSyncError)) {
        qWarning() << "FactorService::addFactor: 同步 factor_instance 失败，回滚 factors 表:" << factorId;
        deleteFactorFromDatabase(factorId);
        const QString detailedMessage = domainSyncError.trimmed().isEmpty()
            ? QString("同步 factor_instance 失败，已回滚 factors 表: %1").arg(factorId)
            : QString("同步 factor_instance 失败(%1)，已回滚 factors 表: %2").arg(domainSyncError, factorId);
        publishOperationReport("addFactor",
                               factorId,
                               false,
                               "sync_domain_failed_rolled_back",
                               detailedMessage);
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

    QVariantMap previousFactor;
    if (m_repository) {
        previousFactor = m_repository->findById(targetFactorId);
    }

    QVariantMap dataToUpdate = previousFactor;
    for (auto it = factorData.begin(); it != factorData.end(); ++it) {
        dataToUpdate.insert(it.key(), it.value());
    }
    dataToUpdate["factorId"] = targetFactorId;

    if (factorData.contains("parameters")) {
        dataToUpdate["parameters"] = canonicalizeParameterAliases(factorData.value("parameters").toMap());
    } else {
        dataToUpdate["parameters"] = canonicalizeParameterAliases(dataToUpdate.value("parameters").toMap());
    }

    QString errorMessage;
    if (!validateFactorData(dataToUpdate, errorMessage)) {
        qWarning() << "因子数据验证失败:" << errorMessage;
        publishOperationReport("updateFactor", targetFactorId, false, "validation_failed", errorMessage);
        return false;
    }

    bool dbSuccess = updateFactorInDatabase(targetFactorId, dataToUpdate);
    if (!dbSuccess) {
        QString errorMsg = QString("因子更新到数据库失败: %1").arg(targetFactorId);
        qWarning() << errorMsg;
        publishOperationReport("updateFactor", targetFactorId, false, "update_database_failed", errorMsg);
        return false;
    }

    QString domainSyncError;
    if (!syncFactorDefinitionToDomain(dataToUpdate, &domainSyncError)) {
        qWarning() << "FactorService::updateFactor: 同步 factor_instance 失败，尝试回滚 factors 表:" << targetFactorId;
        if (!previousFactor.isEmpty()) {
            updateFactorInDatabase(targetFactorId, previousFactor);
        }
        const QString detailedMessage = domainSyncError.trimmed().isEmpty()
            ? QString("同步 factor_instance 失败，已回滚 factors 表: %1").arg(targetFactorId)
            : QString("同步 factor_instance 失败(%1)，已回滚 factors 表: %2").arg(domainSyncError, targetFactorId);
        publishOperationReport("updateFactor",
                               targetFactorId,
                               false,
                               "sync_domain_failed_rolled_back",
                               detailedMessage);
        return false;
    }

    removeFactorFromCache(targetFactorId);
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

    QVariantMap previousFactor;
    if (m_repository) {
        previousFactor = m_repository->findById(targetFactorId);
    }

    const bool repositoryExists = m_repository && m_repository->exists(targetFactorId);
    if (repositoryExists) {
        bool dbSuccess = deleteFactorFromDatabase(targetFactorId);
        if (!dbSuccess) {
            QString errorMsg = QString("因子从数据库删除失败: %1").arg(targetFactorId);
            qWarning() << errorMsg;
            publishOperationReport("deleteFactor", targetFactorId, false, "delete_database_failed", errorMsg);
            return false;
        }
    } else {
        qWarning() << "FactorService::deleteFactor: repository record missing, continue domain cleanup:" << targetFactorId;
    }

    const bool domainDeleted = removeFactorDefinitionFromDomain(targetFactorId);
    if (!domainDeleted) {
        QString errorMsg = QString("因子从 factor_instance 删除失败: %1").arg(targetFactorId);
        qWarning() << errorMsg;

        if (!previousFactor.isEmpty()) {
            const bool restored = saveFactorToDatabase(previousFactor);
            if (!restored) {
                qWarning() << "FactorService::deleteFactor: 回滚数据库恢复失败:" << targetFactorId;
            }
        }

        publishOperationReport("deleteFactor", targetFactorId, false, "delete_domain_failed", errorMsg);
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

    const QString normalizedFactorId = factorId.trimmed();
    if (!normalizedFactorId.isEmpty()) {
        QReadLocker locker(&m_rwLock);
        const auto cacheIt = m_memoryCache.constFind(normalizedFactorId);
        if (cacheIt != m_memoryCache.constEnd()) {
            qDebug() << "FactorService::getFactorById 命中内存缓存，因子ID:" << normalizedFactorId;
            return cacheIt.value();
        }
    }

    QVariantMap factorMap = getFactorByIdFromRepository(factorId);
    if (factorMap.isEmpty()) {
        qWarning() << "FactorService::getFactorById: repository 未返回结果:" << factorId;
    }
    return factorMap;
}

QVariantMap FactorService::getFactorByIdFromRepository(const QString& factorId)
{
    qDebug() << "FactorService::getFactorByIdFromRepository 开始，因子ID:" << factorId;

    if (!m_repository) {
        qWarning() << "FactorService::getFactorByIdFromRepository: Repository not initialized";
        return QVariantMap();
    }

    const QString effectiveFactorId = resolveRepositoryFactorId(factorId);
    const QString targetFactorId = effectiveFactorId.isEmpty() ? factorId : effectiveFactorId;

    // 优先使用 factor_instance(full_config) 作为读源，避免被 factors/factor_params 的旧快照覆盖。
    const QVariantMap domainFactor = getFactorDefinitionFromDomain(targetFactorId);
    if (!domainFactor.isEmpty()) {
        const QVariantMap normalizedDomainFactor = canonicalizeFactorRecordParameters(domainFactor);
        const QString canonicalFactorId = normalizedDomainFactor.value("factorId").toString().trimmed();
        const QString cacheFactorId = canonicalFactorId.isEmpty() ? targetFactorId : canonicalFactorId;
        saveFactorToCache(cacheFactorId, normalizedDomainFactor);

        if (!factorId.isEmpty() && factorId != cacheFactorId) {
            saveFactorToCache(factorId, normalizedDomainFactor);
        }

        qDebug() << "FactorService::getFactorByIdFromRepository 成功，来源: domain，因子ID:" << cacheFactorId;
        return normalizedDomainFactor;
    }

    try {
        QVariantMap factorMap = canonicalizeFactorRecordParameters(m_repository->findById(targetFactorId));
        if (factorMap.isEmpty()) {
            qWarning() << "FactorService::getFactorByIdFromRepository: 未找到因子" << targetFactorId;
            return QVariantMap();
        }

        const QString canonicalFactorId = factorMap.value("factorId").toString().trimmed();
        const QString cacheFactorId = canonicalFactorId.isEmpty() ? targetFactorId : canonicalFactorId;
        saveFactorToCache(cacheFactorId, factorMap);

        if (!factorId.isEmpty() && factorId != cacheFactorId) {
            saveFactorToCache(factorId, factorMap);
        }

        qDebug() << "FactorService::getFactorByIdFromRepository 成功，来源: repository，因子ID:" << cacheFactorId;
        return factorMap;
    } catch (const std::exception& e) {
        qWarning() << "FactorService::getFactorByIdFromRepository: Error:" << e.what();
        return QVariantMap();
    }
}

QVariantList FactorService::getAllFactors()
{
    QElapsedTimer getAllFactorsTimer;
    getAllFactorsTimer.start();
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
            qDebug() << "[FactorTiming] getAllFactors cache-hit elapsed(ms):" << getAllFactorsTimer.elapsed();
            return factors;
        }
    }
    
    // 缓存未加载或为空，从数据库加载
    QVariantList factors = loadFactorsFromDatabase();
    
    // 更新缓存加载标志
    if (!factors.isEmpty()) {
        m_cacheLoaded = true;
    }
    
    qDebug() << "FactorService::getAllFactors 结束，获取因子数量:" << factors.size();
    qDebug() << "[FactorTiming] getAllFactors repository total elapsed(ms):" << getAllFactorsTimer.elapsed();
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

// 风控过滤辅助
QSet<QString> getRiskBlackList(const QVariantMap& riskConfig) {
    QSet<QString> result;
    if (riskConfig.contains("blacklist")) {
        const QVariantList list = riskConfig.value("blacklist").toList();
        for (const QVariant& v : list) {
            result.insert(v.toString().trimmed());
        }
    }
    return result;
}

bool isST(const QVariantMap& stockInfo) {
    return stockInfo.value("is_st").toBool();
}

bool isSuspended(const QVariantMap& stockInfo) {
    return stockInfo.value("suspended").toBool();
}

bool isLimitUp(const QVariantMap& stockInfo) {
    return stockInfo.value("limit_up").toBool();
}

bool isLimitDown(const QVariantMap& stockInfo) {
    return stockInfo.value("limit_down").toBool();
}

// 信号生成前风控过滤
QList<QVariantMap> filterByPreSignalRisk(const QList<QVariantMap>& stocks, const QVariantMap& riskConfig) {
    QSet<QString> blacklist = getRiskBlackList(riskConfig);
    QList<QVariantMap> filtered;
    for (const QVariantMap& stock : stocks) {
        const QString symbol = stock.value("symbol").toString();
        if (blacklist.contains(symbol)) continue;
        if (isST(stock)) continue;
        if (isSuspended(stock)) continue;
        if (isLimitUp(stock)) continue;
        if (isLimitDown(stock)) continue;
        filtered.append(stock);
    }
    return filtered;
}

// 持仓调整时风控过滤（示例：最大持仓数/单股权重/流动性）
QList<QVariantMap> filterByPositionRisk(const QList<QVariantMap>& candidates, const QVariantMap& riskConfig, int maxPosition=20, double maxWeight=0.1, double minVolume=1e6) {
    QList<QVariantMap> filtered;
    int count = 0;
    for (const QVariantMap& stock : candidates) {
        if (count >= maxPosition) break;
        double weight = stock.value("weight").toDouble();
        double volume = stock.value("volume").toDouble();
        if (weight > maxWeight) continue;
        if (volume < minVolume) continue;
        filtered.append(stock);
        ++count;
    }
    return filtered;
}

// 回测主流程集成风控过滤（伪代码/示例，需根据实际回测主循环插入）
void FactorService::runBacktestWithRiskControl(QList<QVariantMap>& stockPool) {
    // 1. 读取风控配置
    QVariantMap riskConfig;
    if (auto* riskService = RiskConfigService::instance()) {
        riskConfig = riskService->loadAppliedConfiguration();
    }

    // 2. 信号生成前风控过滤
    stockPool = filterByPreSignalRisk(stockPool, riskConfig);

    // 3. ...原始因子信号生成...

    // 4. 持仓调整时风控过滤（如最大持仓数、权重、流动性等）
    stockPool = filterByPositionRisk(stockPool, riskConfig);

    // 5. ...后续回测流程...
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
        // parameters 仅以 factor_instance.full_config 为真源，仓储层不再持久化 parameters 快照。
        QVariantMap repositoryPayload = factorData;
        repositoryPayload.remove(QStringLiteral("parameters"));

        bool success = m_repository->save(repositoryPayload);
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
        // parameters 仅以 factor_instance.full_config 为真源，仓储层不再持久化 parameters 快照。
        QVariantMap repositoryPayload = factorData;
        repositoryPayload.remove(QStringLiteral("parameters"));

        bool success = m_repository->update(factorId, repositoryPayload);
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
        QElapsedTimer loadTimer;
        loadTimer.start();

        // 优先从 factor_instance(full_config) 读取，保证参数与编辑保存路径一致。
        QElapsedTimer domainTimer;
        domainTimer.start();
        const QVariantList domainFactors = getAllFactorDefinitionsFromDomain();
        qDebug() << "[FactorTiming] loadFactorsFromDatabase domain fetch elapsed(ms):" << domainTimer.elapsed();

        if (!domainFactors.isEmpty()) {
            QVariantList factors;
            {
                QWriteLocker locker(&m_rwLock);
                m_memoryCache.clear();

                for (const QVariant& entry : domainFactors) {
                    const QVariantMap factorMap = canonicalizeFactorRecordParameters(entry.toMap());
                    const QString factorId = factorMap.value("factorId").toString();
                    if (factorId.isEmpty()) {
                        qWarning() << "loadFactorsFromDatabase: skip domain factor without factorId";
                        continue;
                    }
                    m_memoryCache[factorId] = factorMap;
                    factors.append(factorMap);
                }

                if (!factors.isEmpty()) {
                    m_cacheLoaded = true;
                }
            }

            if (!factors.isEmpty()) {
                emit factorsLoaded(factors);
                qDebug() << "[FactorTiming] loadFactorsFromDatabase domain path total elapsed(ms):" << loadTimer.elapsed();
                return factors;
            }

            qWarning() << "loadFactorsFromDatabase: domain records contained no valid factorId, falling back to repository";
        }

        qDebug() << "FactorService::loadFactorsFromDatabase: 调用 m_repository->findAll()...";
        // 从数据库加载所有因子
        QElapsedTimer repositoryTimer;
        repositoryTimer.start();
        auto factorMaps = m_repository->findAll();
        qDebug() << "[FactorTiming] loadFactorsFromDatabase repository findAll elapsed(ms):" << repositoryTimer.elapsed();
        //qDebug() << "FactorService::loadFactorsFromDatabase: 数据库查询返回" << factorMaps.size() << "个因子";
        
        // 转换为QVariantList
        QVariantList factors;
        
        // 保存到内存缓存
        {
            QWriteLocker locker(&m_rwLock);
            m_memoryCache.clear(); // 清空现有缓存
            
            for (const auto& rawFactorMap : factorMaps) {
                const QVariantMap factorMap = canonicalizeFactorRecordParameters(rawFactorMap);
                QString factorId = factorMap["factorId"].toString();
                if (factorId.isEmpty()) {
                    qWarning() << "loadFactorsFromDatabase: skip repository factor without factorId";
                    continue;
                }
                m_memoryCache[factorId] = factorMap;
                factors.append(factorMap);
            }
            
            // 设置缓存已加载标志
            m_cacheLoaded = true;
        }
        
        // 直接更新视图模型

        // 发出加载完成信号
        emit factorsLoaded(factors);
        qDebug() << "[FactorTiming] loadFactorsFromDatabase repository path total elapsed(ms):" << loadTimer.elapsed();
        
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
        const QVariantMap normalizedFactorData = canonicalizeFactorRecordParameters(factorData);
        // 保存到内存缓存 - 使用写锁保护整个操作
        QWriteLocker locker(&m_rwLock);
        m_memoryCache[factorId] = normalizedFactorData;
        
        // 保存到全局缓存
        QString cacheKey = QString("factor_%1").arg(factorId);
        QVariantList factorList;
        factorList.append(normalizedFactorData);
        
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
            QVariantMap factorData = canonicalizeFactorRecordParameters(cachedData[0].toMap());
            
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
    
    const QString majorCategory = factorData.value("majorCategory").toString().trimmed();
    const QString factorType = factorData.value("factorType").toString().trimmed();
    if (majorCategory.isEmpty() && factorType.isEmpty()) {
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
    if (factorData.contains("validityDays")) {
        int validityDays = factorData["validityDays"].toInt();
        if (validityDays < 1 || validityDays > 365) {
            errorMessage = "有效天数必须在1到365之间";
            return false;
        }
    }
    
    if (factorData.contains("turnoverRate")) {
        double turnoverRate = factorData["turnoverRate"].toDouble();
        if (turnoverRate < 0.0) {
            errorMessage = "换手率不能为负数";
            return false;
        }
    }

    const QVariantMap normalizedParameters = canonicalizeParameterAliases(factorData.value("parameters").toMap());
    const QString majorCategoryName = factorData.value("majorCategory").toString().trimmed();
    const QString factorTypeName = factorData.value("factorType").toString().trimmed().toLower();
    const QString legacyFactorTypeName = factorData.value("factor_type").toString().trimmed().toLower();
    if (factorTypeName == QStringLiteral("growth")
        || legacyFactorTypeName == QStringLiteral("growth")
        || majorCategoryName == QString::fromUtf8("成长因子")) {
        const QVariantMap normalizedGrowthParameters = normalizeCalculationParameters(
            majorCategoryName.isEmpty() ? QStringLiteral("growth") : majorCategoryName,
            normalizedParameters);
        const QVariantList growthMetrics = normalizedGrowthParameters.value(QStringLiteral("growthMetrics")).toList();
        const QVariantList growthWeights = normalizedGrowthParameters.value(QStringLiteral("growthWeights")).toList();
        if (growthMetrics.isEmpty() || growthWeights.isEmpty() || growthMetrics.size() != growthWeights.size()) {
            errorMessage = "成长因子必须显式提供等长的 growthMetrics 和 growthWeights";
            return false;
        }

        QSet<QString> seenMetrics;
        for (int index = 0; index < growthMetrics.size(); ++index) {
            const QString metric = normalizeGrowthMetric(growthMetrics.at(index).toString());
            if (metric.isEmpty() || seenMetrics.contains(metric)) {
                errorMessage = QStringLiteral("成长因子包含非法或重复的成长指标: %1").arg(growthMetrics.at(index).toString());
                return false;
            }
            seenMetrics.insert(metric);

            bool ok = false;
            const int weight = growthWeights.at(index).toInt(&ok);
            if (!ok) {
                errorMessage = QStringLiteral("成长因子权重必须为整数: %1").arg(metric);
                return false;
            }
            if (weight < 0) {
                errorMessage = QStringLiteral("成长因子权重不能为负数: %1").arg(metric);
                return false;
            }
        }
    }

    for (auto it = normalizedParameters.begin(); it != normalizedParameters.end(); ++it) {
        const QString key = it.key().trimmed();
        const QVariant value = it.value();

        if (!value.isValid() || value.isNull()) {
            continue;
        }

        if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
            continue;
        }

        if (isBooleanParameterKey(key)) {
            if (!isBooleanLikeValue(value)) {
                errorMessage = QString("参数 %1 需要布尔值，当前值: %2")
                    .arg(key, value.toString());
                return false;
            }
            continue;
        }

        if (!isNumericParameterKey(key)) {
            continue;
        }

        if (!isNumericLikeValue(value)) {
            errorMessage = QString("参数 %1 需要数值，当前值: %2")
                .arg(key, value.toString());
            return false;
        }
    }
    
    return true;
}

QString FactorService::generateFactorId(const QString& factorName)
{
    // 生成唯一的因子ID：因子名称_时间戳_UUID片段
    const QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8).toLower();
    QString sanitizedName = factorName.toLower().replace(QRegularExpression("[^a-z0-9_]"), "_");
    if (sanitizedName.isEmpty()) {
        sanitizedName = QStringLiteral("factor");
    }
    return QString("%1_%2_%3").arg(sanitizedName, timestamp, suffix);
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
        const QString lookupId = factorId.trimmed();
        const auto result = m_database->executeQuery(
            "SELECT fi.instance_id, fi.factor_id, fi.instance_name, fi.description AS instance_description, "
            "CAST(fi.full_config AS CHAR) AS full_config, fi.status AS instance_status, "
            "f.factor_name, f.display_name, f.major_category, f.sub_category, "
            "f.description AS factor_description, f.ic_value, f.ir_value, f.validity_days, f.turnover_rate, "
            "f.is_recommended, f.is_favorite, f.status AS factor_status, "
            "f.creator, f.create_date "
            "FROM factor_instance fi "
            "LEFT JOIN factors f ON fi.factor_id = f.factor_id "
            "WHERE fi.instance_id = :id OR fi.factor_id = :id "
            "ORDER BY CASE WHEN fi.instance_id = :id THEN 0 ELSE 1 END, fi.updated_at DESC, fi.created_at DESC "
            "LIMIT 1",
            {{":id", factorId.trimmed()}}
        );

        if (!result.isEmpty()) {
            const QVariantMap factorMap = buildDomainFactorMap(result.getRow(0));
            const QString resolvedFactorId = factorMap.value("factorId").toString().trimmed();
            if (resolvedFactorId.isEmpty()) {
                qWarning() << "FactorService::getFactorDefinitionFromDomain: domain record missing factorId, using lookup id";
                QVariantMap recoveredFactor = factorMap;
                recoveredFactor["factorId"] = lookupId;
                return recoveredFactor;
            }
            return factorMap;
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

bool FactorService::syncFactorDefinitionToDomain(const QVariantMap& factorData, QString* errorMessage)
{
    if (m_syncFactorDefinitionOverrideForTests) {
        return m_syncFactorDefinitionOverrideForTests(factorData);
    }

    if (!initializeFactorDomainRuntime()) {
        qWarning() << "FactorService::syncFactorDefinitionToDomain: domain runtime 未初始化";
        if (errorMessage) {
            *errorMessage = QStringLiteral("domain runtime 未初始化");
        }
        return false;
    }

    const QString factorId = factorData.value("factorId").toString().trimmed();
    if (factorId.isEmpty()) {
        qWarning() << "FactorService::syncFactorDefinitionToDomain: factorId 为空";
        if (errorMessage) {
            *errorMessage = QStringLiteral("factorId 为空");
        }
        return false;
    }

    QString instanceId = determineDomainInstanceId(factorData);
    if (instanceId.isEmpty()) {
        qWarning() << "FactorService::syncFactorDefinitionToDomain: instanceId 为空";
        if (errorMessage) {
            *errorMessage = QStringLiteral("instanceId 为空");
        }
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
        QString lastSyncError;
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
                const int cleanedRows = m_database->executeUpdate(
                    "DELETE FROM factor_instance WHERE factor_id = :factorId AND instance_id <> :instanceId",
                    {
                        {":factorId", factorId},
                        {":instanceId", actualInstanceId}
                    }
                );
                if (cleanedRows <= 0) {
                    qWarning() << "FactorService::syncFactorDefinitionToDomain: duplicate cleanup failed for factorId" << factorId;
                    return false;
                }
            }

            if (persistedInstanceId) {
                *persistedInstanceId = actualInstanceId;
            }
            return true;
        };

        const bool syncOk = factor::bridge::executeDomainSyncWithRetry(
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
            [&lastSyncError](const QString&, const QString& errorMessage) {
                lastSyncError = errorMessage;
                qWarning() << "FactorService::syncFactorDefinitionToDomain: 首次实例验证失败，尝试重建记录:" << errorMessage;
            },
            [&lastSyncError](const QString&, const QString& errorMessage) {
                lastSyncError = errorMessage;
                qWarning() << "FactorService::syncFactorDefinitionToDomain: 重建后实例验证仍失败:" << errorMessage;
            }
        );

        if (!syncOk && errorMessage && errorMessage->trimmed().isEmpty()) {
            *errorMessage = lastSyncError.isEmpty() ? QStringLiteral("同步 factor_instance 失败") : lastSyncError;
        }

        return syncOk;
    } catch (const std::exception& e) {
        qWarning() << "FactorService::syncFactorDefinitionToDomain failed:" << e.what();
        if (errorMessage) {
            *errorMessage = QString::fromUtf8(e.what());
        }
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
    context.parameters = foundation::json::JsonFacade::createObject();
    context.parameters.set("benchmarkSymbol", foundation::json::JsonFacade::createString("000300.SH"));

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
        context.parameters = foundation::json::JsonFacade::createObject();
        context.parameters.set("benchmarkSymbol", foundation::json::JsonFacade::createString("000300.SH"));
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

        QString cacheKey = QString("factor_values_%1_%2_%3").arg(factorId, resolvedInstanceId, date);
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
    QVariantMap factorInfo = getFactorById(factorId);
    QString resolvedInstanceId = factorInfo.value("instanceId").toString().trimmed();
    if (resolvedInstanceId.isEmpty()) {
        resolvedInstanceId = resolveDomainInstanceId(factorId);
    }
    if (resolvedInstanceId.isEmpty()) {
        resolvedInstanceId = factorInfo.value("factorId").toString().trimmed();
    }
    if (resolvedInstanceId.isEmpty()) {
        resolvedInstanceId = factorId.trimmed();
    }

    const QString cacheKey = QString("factor_values_%1_%2_%3").arg(factorId, resolvedInstanceId, date);
    QVariantList cachedData = DataServiceCache::getInstance().getData(cacheKey);
    if (!cachedData.isEmpty() && cachedData[0].canConvert<QVariantMap>()) {
        const QVariantMap cachedResult = cachedData[0].toMap();
        const QString cachedStatus = cachedResult.value("status").toString();
        const int cachedCount = cachedResult.value("count").toInt();
        const int cachedStockValueCount = cachedResult.value("stockValues").toMap().size();
        if (cachedStatus == QStringLiteral("success") && (cachedCount > 0 || cachedStockValueCount > 0)) {
            qDebug() << "FactorService::getFactorValues: 从缓存获取数据，因子ID:" << factorId << "日期:" << date;
            return cachedResult;
        }
    }

    QVariantMap result;
    if (!initializeFactorDomainRuntime()) {
        result["factorId"] = factorId;
        result["instanceId"] = resolvedInstanceId;
        result["date"] = date;
        result["stockValues"] = QVariantMap();
        result["count"] = 0;
        result["status"] = "error";
        result["error"] = QString::fromUtf8("domain/factor 运行时未初始化");
        return result;
    }

    result = getFactorValuesFromDomain(factorId, resolvedInstanceId, date);
    if (result.value("status").toString() == QStringLiteral("success")
        && (result.value("count").toInt() > 0 || !result.value("stockValues").toMap().isEmpty())) {
        QVariantList cacheData;
        cacheData.append(result);
        DataServiceCache::getInstance().storeData(cacheKey, cacheData);
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

QVariantMap FactorService::getUnifiedParameterSchema() const
{
    QVariantMap factorCommon;
    factorCommon["lookbackPeriod"] = QVariantMap{{"type", "int"}, {"default", 252}, {"min", 1}, {"scope", "factor"}};
    factorCommon["skipRecent"] = QVariantMap{{"type", "int"}, {"default", 0}, {"min", 0}, {"scope", "factor"}};
    factorCommon["laggedEnabled"] = QVariantMap{{"type", "bool"}, {"default", false}, {"scope", "factor"}};
    factorCommon["standardization"] = QVariantMap{{"type", "enum"}, {"default", "none"}, {"options", QStringList{"none", "zscore", "rank", "percentile"}}, {"scope", "factor"}};
    factorCommon["neutralizationEnabled"] = QVariantMap{{"type", "bool"}, {"default", false}, {"scope", "factor"}};

    QVariantMap backtestRuntime;
    backtestRuntime["forwardDays"] = QVariantMap{{"type", "int"}, {"default", 1}, {"min", 1}, {"scope", "backtest"}};
    backtestRuntime["rebalanceDays"] = QVariantMap{{"type", "int"}, {"default", 1}, {"min", 1}, {"scope", "backtest"}};
    backtestRuntime["transactionCost"] = QVariantMap{{"type", "percent"}, {"default", 0.001}, {"min", 0.0}, {"scope", "backtest"}};
    backtestRuntime["slippageRate"] = QVariantMap{{"type", "percent"}, {"default", 0.0}, {"min", 0.0}, {"scope", "backtest"}};
    backtestRuntime["riskFreeRate"] = QVariantMap{{"type", "percent"}, {"default", 0.0}, {"min", 0.0}, {"scope", "backtest"}};
    backtestRuntime["benchmarkSymbol"] = QVariantMap{{"type", "string"}, {"default", "000300.SH"}, {"scope", "backtest"}};
    backtestRuntime["numGroups"] = QVariantMap{{"type", "int"}, {"default", 10}, {"min", 2}, {"scope", "backtest"}};

    QVariantMap aliases;
    aliases["transactionCost"] = QStringList{"commissionRate", "commission", "transactionCost"};
    aliases["slippageRate"] = QStringList{"slippageRate", "slippage"};
    aliases["riskFreeRate"] = QStringList{"riskFreeRate", "risk_free_rate"};
    aliases["benchmarkSymbol"] = QStringList{"benchmarkSymbol", "benchmark_symbol"};
    aliases["lookbackPeriod"] = QStringList{"lookbackPeriod", "lookback", "lookback_period"};
    aliases["skipRecent"] = QStringList{"skipRecent", "skip_recent"};

    QVariantMap schema;
    schema["factorCommon"] = factorCommon;
    schema["backtestRuntime"] = backtestRuntime;
    schema["aliases"] = aliases;
    schema["version"] = 1;
    return schema;
}

// 新增方法实现：批量获取因子值（简化版：先保证回测流程跑通）
QVariantMap FactorService::getFactorValuesBatch(const QString& factorId, const QStringList& dates)
{
    qDebug() << "FactorService::getFactorValuesBatch 开始，因子ID:" << factorId << "日期数量:" << dates.size();

    QVariantMap factorInfo = getFactorById(factorId);
    QString resolvedInstanceId = factorInfo.value("instanceId").toString().trimmed();
    if (resolvedInstanceId.isEmpty()) {
        resolvedInstanceId = resolveDomainInstanceId(factorId);
    }
    if (resolvedInstanceId.isEmpty()) {
        resolvedInstanceId = factorInfo.value("factorId").toString().trimmed();
    }
    if (resolvedInstanceId.isEmpty()) {
        resolvedInstanceId = factorId.trimmed();
    }

    QVariantMap result;
    if (!initializeFactorDomainRuntime()) {
        result["factorId"] = factorId;
        result["dates"] = dates;
        result["batchResults"] = QVariantMap();
        result["totalCount"] = 0;
        result["status"] = "error";
        result["error"] = QString::fromUtf8("domain/factor 运行时未初始化");
        return result;
    }

    result = getFactorValuesBatchFromDomain(factorId, resolvedInstanceId, dates);
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
                key.contains("日期", Qt::CaseSensitive) ||
                key.contains("天", Qt::CaseSensitive)) {
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
