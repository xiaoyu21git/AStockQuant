#include "domain/factor/include/ConfigurableFactor.h"
#include "domain/factor/include/CustomExpressionUtils.h"
#include "domain/factor/include/FactorDataProvider.h"
#include "domain/factor/include/batch_technical_indicators.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"

#include <QDate>
#include <QDebug>
#include <QElapsedTimer>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cctype>

#include <map>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <stack>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace factor {

namespace {
struct BatchComputationCache
{
    std::shared_ptr<FactorDataProvider> dataProvider;
    std::unordered_map<std::string, std::unordered_map<std::string, double>> crossSectionsByKey;
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> seriesByKey;
};

thread_local BatchComputationCache* activeBatchComputationCache = nullptr;

class BatchComputationCacheScope
{
public:
    explicit BatchComputationCacheScope(BatchComputationCache& cache)
        : previous_(activeBatchComputationCache)
    {
        activeBatchComputationCache = &cache;
    }

    ~BatchComputationCacheScope()
    {
        activeBatchComputationCache = previous_;
    }

private:
    BatchComputationCache* previous_ = nullptr;
};

std::mutex& tableExistsCacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, bool>& tableExistsCache()
{
    static std::unordered_map<std::string, bool> cache;
    return cache;
}

std::mutex& tableColumnsCacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, QSet<QString>>& tableColumnsCache()
{
    static std::unordered_map<std::string, QSet<QString>> cache;
    return cache;
}

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
    if (normalized == "macro" || normalized == QString::fromUtf8("宏观因子")) {
        return "macro";
    }
    if (normalized == "industry" || normalized == QString::fromUtf8("行业因子")) {
        return "industry";
    }
    if (normalized == "sentiment" || normalized == QString::fromUtf8("情绪因子")) {
        return "sentiment";
    }
    if (normalized == "custom" || normalized == QString::fromUtf8("自定义因子") || normalized == QString::fromUtf8("自定义")) {
        return "custom";
    }
    return normalized;
}

std::string requireStringField(const foundation::json::JsonFacade& json, const char* fieldName)
{
    const auto value = json.get(fieldName);
    if (!value.isString()) {
        throw std::runtime_error(QStringLiteral("%1 不是字符串字段").arg(QString::fromUtf8(fieldName)).toStdString());
    }
    return value.asString();
}

std::string requireStringItem(const foundation::json::JsonFacade& arrayValue,
                              size_t index,
                              const char* fieldName)
{
    if (!arrayValue.isArray() || index >= arrayValue.size()) {
        throw std::runtime_error(QStringLiteral("%1 数组项越界").arg(QString::fromUtf8(fieldName)).toStdString());
    }

    const auto item = arrayValue.at(index);
    if (!item.isString()) {
        throw std::runtime_error(
            QStringLiteral("%1[%2] 不是字符串字段").arg(QString::fromUtf8(fieldName)).arg(QString::number(index)).toStdString());
    }
    return item.asString();
}

QString normalizeTechnicalIndicatorType(const QString& rawType)
{
    const QString normalized = rawType.trimmed().toLower();
    if (normalized == QStringLiteral("rsi") || normalized == QStringLiteral("relative_strength_index")
            || normalized == QStringLiteral("趋势指标") || normalized == QStringLiteral("trend")
            || normalized == QStringLiteral("trend_indicator")) {
        return QStringLiteral("rsi");
    }
    if (normalized == QStringLiteral("macd") || normalized == QStringLiteral("macd_indicator")
            || normalized == QStringLiteral("动量指标") || normalized == QStringLiteral("momentum")
            || normalized == QStringLiteral("momentum_indicator")) {
        return QStringLiteral("macd");
    }
    if (normalized == QStringLiteral("ma") || normalized == QStringLiteral("moving_average")
            || normalized == QStringLiteral("sma") || normalized == QStringLiteral("移动平均")
            || normalized == QStringLiteral("均线")) {
        return QStringLiteral("ma");
    }
    if (normalized == QStringLiteral("ema") || normalized == QStringLiteral("exponential_moving_average")
            || normalized == QStringLiteral("指数移动平均")) {
        return QStringLiteral("ema");
    }
    if (normalized == QStringLiteral("boll") || normalized == QStringLiteral("bollinger")
            || normalized == QStringLiteral("bollinger_bands") || normalized == QStringLiteral("布林带")) {
        return QStringLiteral("boll");
    }
    if (normalized == QStringLiteral("kdj") || normalized == QStringLiteral("stochastic")
            || normalized == QStringLiteral("随机指标")) {
        return QStringLiteral("kdj");
    }
    if (normalized == QStringLiteral("atr") || normalized == QStringLiteral("average_true_range")
            || normalized == QStringLiteral("真实波幅")) {
        return QStringLiteral("atr");
    }
    if (normalized == QStringLiteral("obv") || normalized == QStringLiteral("obv_indicator")
            || normalized == QStringLiteral("成交量指标") || normalized == QStringLiteral("volume")
            || normalized == QStringLiteral("volume_indicator")) {
        return QStringLiteral("obv");
    }
    if (normalized == QStringLiteral("vwap") || normalized == QStringLiteral("volume_weighted_average_price")
            || normalized == QStringLiteral("成交量加权平均价")) {
        return QStringLiteral("vwap");
    }
    if (normalized == QStringLiteral("volume_ratio") || normalized == QStringLiteral("量比")) {
        return QStringLiteral("volume_ratio");
    }
    if (normalized == QStringLiteral("turnover_stability") || normalized == QStringLiteral("turnover_stability_indicator")
            || normalized == QStringLiteral("波动率指标") || normalized == QStringLiteral("volatility")
            || normalized == QStringLiteral("volatility_indicator")) {
        return QStringLiteral("turnover_stability");
    }
    return normalized;
}

std::vector<QString> normalizeTechnicalIndicatorList(const QVariant& value)
{
    std::vector<QString> indicators;
    if (value.canConvert<QVariantList>()) {
        const QVariantList list = value.toList();
        indicators.reserve(static_cast<size_t>(list.size()));
        for (const QVariant& item : list) {
            const QString indicator = normalizeTechnicalIndicatorType(item.toString());
            if (!indicator.isEmpty() && std::find(indicators.begin(), indicators.end(), indicator) == indicators.end()) {
                indicators.push_back(indicator);
            }
        }
    } else if (value.isValid()) {
        const QString indicator = normalizeTechnicalIndicatorType(value.toString());
        if (!indicator.isEmpty()) {
            indicators.push_back(indicator);
        }
    }

    return indicators;
}

std::vector<QString> normalizeTechnicalIndicatorList(const foundation::json::JsonFacade& value)
{
    std::vector<QString> indicators;
    if (value.isArray()) {
        indicators.reserve(value.size());
        for (size_t index = 0; index < value.size(); ++index) {
            const QString indicator = normalizeTechnicalIndicatorType(
                QString::fromStdString(requireStringItem(value, index, "technicalIndicators")));
            if (!indicator.isEmpty() && std::find(indicators.begin(), indicators.end(), indicator) == indicators.end()) {
                indicators.push_back(indicator);
            }
        }
    } else {
        if (!value.isString()) {
            throw std::runtime_error("technicalIndicators 不是字符串字段");
        }
        const QString indicator = normalizeTechnicalIndicatorType(QString::fromStdString(value.asString()));
        if (!indicator.isEmpty()) {
            indicators.push_back(indicator);
        }
    }

    return indicators;
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

QString normalizeMacroMetric(const QString& rawMetric)
{
    const QString normalized = rawMetric.trimmed().toLower();
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
            || rawDimension == QString::fromUtf8("货币信用")) {
        return QStringLiteral("credit");
    }
    if (dimension == QStringLiteral("rates")
            || rawDimension == QString::fromUtf8("利率水平")
            || dimension == QStringLiteral("interest_rate_sensitivity")) {
        return QStringLiteral("rates");
    }
    if (dimension == QStringLiteral("policy")
            || rawDimension == QString::fromUtf8("政策环境")) {
        return QStringLiteral("policy");
    }
    if (dimension == QStringLiteral("risk_appetite")
            || rawDimension == QString::fromUtf8("风险偏好")) {
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
    return {
        QStringLiteral("growth"),
        QStringLiteral("inflation"),
        QStringLiteral("credit"),
        QStringLiteral("rates"),
        QStringLiteral("policy"),
        QStringLiteral("risk_appetite")
    };
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

struct MacroIndicatorSpec
{
    QString dimension;
    QString proxyField;
    double direction = 1.0;
};

MacroIndicatorSpec macroIndicatorSpec(const QString& rawIndicator)
{
    const QString indicator = normalizeMacroIndicator(rawIndicator);
    if (indicator == QStringLiteral("industrial_added_value_yoy")
            || indicator == QStringLiteral("manufacturing_pmi")
            || indicator == QStringLiteral("gdp_yoy")) {
        return {QStringLiteral("growth"), QStringLiteral("close"), 1.0};
    }
    if (indicator == QStringLiteral("cpi_yoy") || indicator == QStringLiteral("ppi_yoy")) {
        return {QStringLiteral("inflation"), QStringLiteral("close"), -1.0};
    }
    if (indicator == QStringLiteral("m2_yoy")
            || indicator == QStringLiteral("social_financing_stock_yoy")
            || indicator == QStringLiteral("m1_m2_spread")) {
        return {QStringLiteral("credit"), QStringLiteral("turnover_rate"), 1.0};
    }
    if (indicator == QStringLiteral("ten_year_bond_yield") || indicator == QStringLiteral("shibor_3m")) {
        return {QStringLiteral("rates"), QStringLiteral("pe_ratio"), -1.0};
    }
    if (indicator == QStringLiteral("lpr_1y") || indicator == QStringLiteral("reserve_requirement_ratio")) {
        return {QStringLiteral("policy"), QStringLiteral("pb_ratio"), -1.0};
    }
    if (indicator == QStringLiteral("aa_credit_spread") || indicator == QStringLiteral("vix_proxy")) {
        return {QStringLiteral("risk_appetite"), QStringLiteral("volume"), -1.0};
    }
    return {QStringLiteral("growth"), QStringLiteral("close"), 1.0};
}

int macroWindowScale(const QString& frequency)
{
    const QString normalized = frequency.trimmed().toLower();
    if (normalized == QStringLiteral("weekly")) {
        return 5;
    }
    if (normalized == QStringLiteral("monthly")) {
        return 21;
    }
    if (normalized == QStringLiteral("quarterly")) {
        return 63;
    }
    if (normalized == QStringLiteral("event")) {
        return 5;
    }
    return 1;
}

std::vector<double> percentChangeSeries(const std::vector<double>& values)
{
    std::vector<double> changes;
    if (values.size() < 2) {
        return changes;
    }

    changes.reserve(values.size() - 1);
    for (size_t index = 1; index < values.size(); ++index) {
        const double previous = values[index - 1];
        const double current = values[index];
        if (!std::isfinite(previous) || !std::isfinite(current)) {
            continue;
        }

        if (std::abs(previous) <= 1e-12) {
            changes.push_back(current - previous);
        } else {
            changes.push_back((current - previous) / std::abs(previous));
        }
    }
    return changes;
}

std::vector<double> takeTail(const std::vector<double>& values, size_t count)
{
    if (count == 0 || values.empty()) {
        return {};
    }
    if (values.size() <= count) {
        return values;
    }
    return std::vector<double>(values.end() - static_cast<std::ptrdiff_t>(count), values.end());
}

struct SeriesMatrixBatch
{
    std::vector<std::string> symbols;
    Eigen::MatrixXd values;
};

SeriesMatrixBatch collectSeriesMatrix(const std::unordered_map<std::string, std::vector<double>>& seriesBySymbol,
                                     size_t minimumLength)
{
    SeriesMatrixBatch batch;
    size_t commonLength = std::numeric_limits<size_t>::max();

    for (const auto& [symbol, values] : seriesBySymbol) {
        if (values.size() < minimumLength) {
            continue;
        }
        batch.symbols.push_back(symbol);
        commonLength = (std::min)(commonLength, values.size());
    }

    if (batch.symbols.empty() || commonLength == std::numeric_limits<size_t>::max()) {
        return batch;
    }

    batch.values.resize(static_cast<int>(batch.symbols.size()), static_cast<int>(commonLength));
    for (int row = 0; row < batch.values.rows(); ++row) {
        const auto& values = seriesBySymbol.at(batch.symbols[static_cast<size_t>(row)]);
        const size_t offset = values.size() - commonLength;
        for (int column = 0; column < batch.values.cols(); ++column) {
            batch.values(row, column) = values[offset + static_cast<size_t>(column)];
        }
    }

    return batch;
}

Eigen::VectorXd buildReturnVector(const std::vector<double>& values)
{
    if (values.size() < 2) {
        return {};
    }

    Eigen::VectorXd returns(static_cast<int>(values.size() - 1));
    for (int index = 1; index < static_cast<int>(values.size()); ++index) {
        const double previous = values[static_cast<size_t>(index - 1)];
        const double current = values[static_cast<size_t>(index)];
        if (!std::isfinite(previous) || !std::isfinite(current)) {
            returns(index - 1) = std::numeric_limits<double>::quiet_NaN();
            continue;
        }

        if (std::abs(previous) <= 1e-12) {
            returns(index - 1) = current - previous;
        } else {
            returns(index - 1) = (current - previous) / std::abs(previous);
        }
    }

    return returns;
}

Eigen::MatrixXd buildReturnMatrix(const Eigen::MatrixXd& values)
{
    if (values.cols() < 2) {
        return {};
    }

    Eigen::MatrixXd returns(values.rows(), values.cols() - 1);
    for (int column = 1; column < values.cols(); ++column) {
        const Eigen::ArrayXd previous = values.col(column - 1).array();
        const Eigen::ArrayXd current = values.col(column).array();
        const Eigen::ArrayXd delta = current - previous;
        const Eigen::ArrayXd ratio = delta / previous.abs().max(1e-12);
        returns.col(column - 1) = (previous.abs() <= 1e-12).select(delta, ratio).matrix();
    }

    return returns;
}

Eigen::VectorXd batchCorrelate(const Eigen::MatrixXd& symbolReturns, const Eigen::VectorXd& benchmarkReturns)
{
    if (symbolReturns.cols() < 2 || benchmarkReturns.size() < 2) {
        return {};
    }

    const int commonLength = (std::min)(static_cast<int>(symbolReturns.cols()), static_cast<int>(benchmarkReturns.size()));
    if (commonLength < 2) {
        return {};
    }

    const Eigen::MatrixXd alignedSymbolReturns = symbolReturns.rightCols(commonLength);
    const Eigen::VectorXd alignedBenchmarkReturns = benchmarkReturns.tail(commonLength);
    const double benchmarkMean = alignedBenchmarkReturns.mean();
    const Eigen::VectorXd benchmarkCentered = alignedBenchmarkReturns.array() - benchmarkMean;
    const double benchmarkVariance = benchmarkCentered.squaredNorm();
    if (!std::isfinite(benchmarkVariance) || benchmarkVariance <= 1e-12) {
        return Eigen::VectorXd::Constant(alignedSymbolReturns.rows(), std::numeric_limits<double>::quiet_NaN());
    }

    const Eigen::VectorXd symbolMeans = alignedSymbolReturns.rowwise().mean();
    const Eigen::MatrixXd symbolCentered = alignedSymbolReturns.colwise() - symbolMeans;
    const Eigen::VectorXd covariances = symbolCentered * benchmarkCentered;
    const Eigen::VectorXd rowVariances = symbolCentered.array().square().rowwise().sum();
    const Eigen::VectorXd denominators = (rowVariances.array() * benchmarkVariance).sqrt();

    Eigen::VectorXd correlations = covariances.array() / denominators.array();
    for (int row = 0; row < correlations.size(); ++row) {
        if (!std::isfinite(correlations(row)) || denominators(row) <= 1e-12) {
            correlations(row) = std::numeric_limits<double>::quiet_NaN();
        }
    }

    return correlations;
}

double calculateCorrelation(const std::vector<double>& lhs, const std::vector<double>& rhs)
{
    const size_t length = (std::min)(lhs.size(), rhs.size());
    if (length < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    double lhsMean = 0.0;
    double rhsMean = 0.0;
    for (size_t index = 0; index < length; ++index) {
        lhsMean += lhs[index];
        rhsMean += rhs[index];
    }
    lhsMean /= static_cast<double>(length);
    rhsMean /= static_cast<double>(length);

    double covariance = 0.0;
    double lhsVariance = 0.0;
    double rhsVariance = 0.0;
    for (size_t index = 0; index < length; ++index) {
        const double lhsDelta = lhs[index] - lhsMean;
        const double rhsDelta = rhs[index] - rhsMean;
        covariance += lhsDelta * rhsDelta;
        lhsVariance += lhsDelta * lhsDelta;
        rhsVariance += rhsDelta * rhsDelta;
    }

    if (lhsVariance <= 1e-12 || rhsVariance <= 1e-12) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    return covariance / std::sqrt(lhsVariance * rhsVariance);
}

double indicatorWeightForDimension(const QString& dimension)
{
    if (dimension == QStringLiteral("growth")) {
        return 1.0;
    }
    if (dimension == QStringLiteral("inflation")) {
        return 0.9;
    }
    if (dimension == QStringLiteral("credit")) {
        return 1.1;
    }
    if (dimension == QStringLiteral("rates")) {
        return 1.0;
    }
    if (dimension == QStringLiteral("policy")) {
        return 0.9;
    }
    if (dimension == QStringLiteral("risk_appetite")) {
        return 1.1;
    }
    return 1.0;
}

QString normalizeIndustryMetric(const QString& rawMetric)
{
    const QString normalized = rawMetric.trimmed().toLower();
    if (normalized == QStringLiteral("行业景气度") || normalized == QStringLiteral("industry_prosperity")) {
        return QStringLiteral("industry_prosperity");
    }
    if (normalized == QStringLiteral("行业动量") || normalized == QStringLiteral("industry_momentum")) {
        return QStringLiteral("industry_momentum");
    }
    if (normalized == QStringLiteral("行业集中度") || normalized == QStringLiteral("industry_concentration")) {
        return QStringLiteral("industry_concentration");
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
        if (normalized == QStringLiteral("revenue_growth")) {
            return QStringLiteral("revenue_growth");
        }
        if (normalized == QStringLiteral("net_profit_growth")) {
            return QStringLiteral("net_profit_growth");
        }
        if (normalized == QStringLiteral("delta_roe")) {
            return QStringLiteral("delta_roe");
        }
        if (normalized == QStringLiteral("sue")) {
            return QStringLiteral("sue");
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

std::vector<double> emaSeries(const std::vector<double>& values, int period)
{
    std::vector<double> result;
    if (values.empty()) {
        return result;
    }

    const int resolvedPeriod = (std::max)(2, period);
    const double alpha = 2.0 / (resolvedPeriod + 1.0);
    result.reserve(values.size());
    double ema = values.front();
    result.push_back(ema);
    for (size_t index = 1; index < values.size(); ++index) {
        ema = alpha * values[index] + (1.0 - alpha) * ema;
        result.push_back(ema);
    }
    return result;
}

double calculateRsiScore(const std::vector<double>& closes, int period)
{
    if (closes.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const int resolvedPeriod = (std::max)(2, period);
    const size_t startIndex = closes.size() > static_cast<size_t>(resolvedPeriod + 1)
        ? closes.size() - static_cast<size_t>(resolvedPeriod + 1)
        : 1;

    double gainSum = 0.0;
    double lossSum = 0.0;
    int sampleCount = 0;
    for (size_t index = startIndex; index < closes.size(); ++index) {
        const double delta = closes[index] - closes[index - 1];
        if (delta > 0.0) {
            gainSum += delta;
        } else {
            lossSum += -delta;
        }
        ++sampleCount;
    }

    if (sampleCount <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double avgGain = gainSum / static_cast<double>(sampleCount);
    const double avgLoss = lossSum / static_cast<double>(sampleCount);
    const double rsi = avgLoss <= 1e-12
        ? 100.0
        : 100.0 - (100.0 / (1.0 + avgGain / avgLoss));
    return std::clamp((rsi - 50.0) / 50.0, -1.0, 1.0);
}

double calculateMacdScore(const std::vector<double>& closes, int fastPeriod, int slowPeriod, int signalPeriod)
{
    if (closes.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const int resolvedFast = (std::max)(2, fastPeriod);
    const int resolvedSlow = (std::max)(resolvedFast + 1, slowPeriod);
    const int resolvedSignal = (std::max)(2, signalPeriod);

    const std::vector<double> fastEma = emaSeries(closes, resolvedFast);
    const std::vector<double> slowEma = emaSeries(closes, resolvedSlow);
    if (fastEma.size() != closes.size() || slowEma.size() != closes.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    std::vector<double> macdLine;
    macdLine.reserve(closes.size());
    for (size_t index = 0; index < closes.size(); ++index) {
        macdLine.push_back(fastEma[index] - slowEma[index]);
    }

    const std::vector<double> signalLine = emaSeries(macdLine, resolvedSignal);
    if (signalLine.size() != macdLine.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double histogram = macdLine.back() - signalLine.back();
    const double scale = (std::max)(1e-6, std::abs(closes.back()));
    return std::clamp(std::tanh(histogram / scale), -1.0, 1.0);
}

double calculateMaScore(const std::vector<double>& closes, int period)
{
    if (closes.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const int resolvedPeriod = (std::max)(2, period);
    const size_t count = (std::min)(closes.size(), static_cast<size_t>(resolvedPeriod));
    std::vector<double> window(closes.end() - static_cast<std::ptrdiff_t>(count), closes.end());
    const double mean = safeMean(window);
    return std::clamp(std::tanh(safeRatio(closes.back() - mean, (std::max)(1e-6, std::abs(mean)))), -1.0, 1.0);
}

double calculateEmaScore(const std::vector<double>& closes, int period)
{
    const std::vector<double> emaValues = emaSeries(closes, period);
    if (emaValues.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double lastEma = emaValues.back();
    return std::clamp(std::tanh(safeRatio(closes.back() - lastEma, (std::max)(1e-6, std::abs(lastEma)))), -1.0, 1.0);
}

double calculateBollScore(const std::vector<double>& closes, int period, double stdMultiplier)
{
    if (closes.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const int resolvedPeriod = (std::max)(2, period);
    const size_t count = (std::min)(closes.size(), static_cast<size_t>(resolvedPeriod));
    std::vector<double> window(closes.end() - static_cast<std::ptrdiff_t>(count), closes.end());
    const double mean = safeMean(window);
    const double deviation = safeStdDev(window);
    const double scale = (std::max)(1e-6, deviation * (std::max)(1.0, stdMultiplier));
    return std::clamp(std::tanh((closes.back() - mean) / scale), -1.0, 1.0);
}

double calculateKdjScore(const std::vector<double>& highs,
                         const std::vector<double>& lows,
                         const std::vector<double>& closes,
                         int window,
                         int kPeriod,
                         int dPeriod)
{
    const size_t usable = (std::min)({highs.size(), lows.size(), closes.size()});
    if (usable < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const int resolvedWindow = (std::max)(2, window);
    const size_t startIndex = usable > static_cast<size_t>(resolvedWindow) ? usable - static_cast<size_t>(resolvedWindow) : 0;
    double highestHigh = -std::numeric_limits<double>::infinity();
    double lowestLow = std::numeric_limits<double>::infinity();
    for (size_t index = startIndex; index < usable; ++index) {
        highestHigh = (std::max)(highestHigh, highs[index]);
        lowestLow = (std::min)(lowestLow, lows[index]);
    }

    if (!std::isfinite(highestHigh) || !std::isfinite(lowestLow) || highestHigh <= lowestLow) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double rsv = 100.0 * (closes[usable - 1] - lowestLow) / (highestHigh - lowestLow);
    const double kAlpha = 1.0 / (std::max)(2, kPeriod);
    const double dAlpha = 1.0 / (std::max)(2, dPeriod);
    double kValue = 50.0 + (rsv - 50.0) * kAlpha;
    double dValue = 50.0 + (kValue - 50.0) * dAlpha;
    const double jValue = 3.0 * kValue - 2.0 * dValue;
    return std::clamp((jValue - 50.0) / 50.0, -1.0, 1.0);
}

double calculateAtrScore(const std::vector<double>& highs,
                         const std::vector<double>& lows,
                         const std::vector<double>& closes,
                         int window)
{
    const size_t usable = (std::min)({highs.size(), lows.size(), closes.size()});
    if (usable < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const int resolvedWindow = (std::max)(2, window);
    const size_t startIndex = usable > static_cast<size_t>(resolvedWindow + 1) ? usable - static_cast<size_t>(resolvedWindow + 1) : 1;
    std::vector<double> trueRanges;
    for (size_t index = startIndex; index < usable; ++index) {
        const double currentHigh = highs[index];
        const double currentLow = lows[index];
        const double previousClose = closes[index - 1];
        const double range1 = currentHigh - currentLow;
        const double range2 = std::abs(currentHigh - previousClose);
        const double range3 = std::abs(currentLow - previousClose);
        trueRanges.push_back((std::max)({range1, range2, range3}));
    }

    const double atr = safeMean(trueRanges);
    return std::clamp(-safeRatio(atr, (std::max)(1e-6, std::abs(closes[usable - 1]))), -1.0, 1.0);
}

double calculateVwapScore(const std::vector<double>& closes, const std::vector<double>& volumes)
{
    const size_t usable = (std::min)(closes.size(), volumes.size());
    if (usable < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    double priceVolumeSum = 0.0;
    double volumeSum = 0.0;
    for (size_t index = 0; index < usable; ++index) {
        if (volumes[index] <= 0.0) {
            continue;
        }
        priceVolumeSum += closes[index] * volumes[index];
        volumeSum += volumes[index];
    }
    if (volumeSum <= 1e-12) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double vwap = priceVolumeSum / volumeSum;
    return std::clamp(std::tanh(safeRatio(closes[usable - 1] - vwap, (std::max)(1e-6, std::abs(vwap)))), -1.0, 1.0);
}

double calculateVolumeRatioScore(const std::vector<double>& volumes, int period)
{
    if (volumes.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const int resolvedPeriod = (std::max)(2, period);
    const size_t count = (std::min)(volumes.size(), static_cast<size_t>(resolvedPeriod));
    std::vector<double> window(volumes.end() - static_cast<std::ptrdiff_t>(count), volumes.end());
    const double mean = safeMean(window);
    return std::clamp(std::tanh(safeRatio(volumes.back() - mean, (std::max)(1e-6, std::abs(mean)))), -1.0, 1.0);
}

double calculateObvScore(const std::vector<double>& closes, const std::vector<double>& volumes)
{
    const size_t length = (std::min)(closes.size(), volumes.size());
    if (length < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    double obv = 0.0;
    for (size_t index = 1; index < length; ++index) {
        if (closes[index] > closes[index - 1]) {
            obv += volumes[index];
        } else if (closes[index] < closes[index - 1]) {
            obv -= volumes[index];
        }
    }

    std::vector<double> volumeHistory(volumes.begin(), volumes.begin() + static_cast<std::ptrdiff_t>(length));
    const double averageVolume = safeMean(volumeHistory);
    const double normalized = safeRatio(obv, (std::max)(1e-6, averageVolume * static_cast<double>(length)));
    return std::clamp(std::tanh(normalized), -1.0, 1.0);
}

double calculateTurnoverStabilityScore(const std::vector<double>& values)
{
    if (values.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double mean = safeMean(values);
    const double stdDev = safeStdDev(values);
    const double coefficientOfVariation = safeRatio(stdDev, (std::max)(1e-6, std::abs(mean)));
    const double normalized = 1.0 - std::clamp(coefficientOfVariation, 0.0, 2.0) / 2.0;
    return std::clamp(normalized * 2.0 - 1.0, -1.0, 1.0);
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
        QStringLiteral("payout_ratio"),
        QStringLiteral("operating_cash_flow")
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

    const std::string cacheKey = tableName.trimmed().toStdString();
    {
        std::lock_guard<std::mutex> guard(tableExistsCacheMutex());
        const auto cacheIt = tableExistsCache().find(cacheKey);
        if (cacheIt != tableExistsCache().end()) {
            return cacheIt->second;
        }
    }

    const auto result = db->executeQuery(
        "SELECT COUNT(*) AS count FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table_name",
        makeNamedParams({{":table_name", tableName}}));
    const bool exists = !result.isEmpty() && result.getRow(0).getInt("count") > 0;

    {
        std::lock_guard<std::mutex> guard(tableExistsCacheMutex());
        tableExistsCache()[cacheKey] = exists;
    }

    return exists;
}

QSet<QString> loadTableColumns(const std::shared_ptr<astock::database::QtMySQLDatabase>& db,
                               const QString& tableName)
{
    QSet<QString> columns;
    if (!db || tableName.trimmed().isEmpty()) {
        return columns;
    }

    const std::string cacheKey = tableName.trimmed().toStdString();
    {
        std::lock_guard<std::mutex> guard(tableColumnsCacheMutex());
        const auto cacheIt = tableColumnsCache().find(cacheKey);
        if (cacheIt != tableColumnsCache().end()) {
            return cacheIt->second;
        }
    }

    const auto result = db->executeQuery(
        "SELECT COLUMN_NAME AS column_name FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table_name",
        makeNamedParams({{":table_name", tableName}}));

    for (size_t index = 0; index < result.rowCount(); ++index) {
        const QString columnName = result.getRow(index).getString("column_name");
        columns.insert(columnName.trimmed().toLower());
    }

    {
        std::lock_guard<std::mutex> guard(tableColumnsCacheMutex());
        tableColumnsCache()[cacheKey] = columns;
    }

    return columns;
}

bool tableHasColumn(const std::shared_ptr<astock::database::QtMySQLDatabase>& db,
                    const QString& tableName,
                    const QString& columnName)
{
    if (!db || tableName.trimmed().isEmpty() || columnName.trimmed().isEmpty()) {
        return false;
    }

    const QSet<QString> columns = loadTableColumns(db, tableName);
    return columns.contains(columnName.trimmed().toLower());
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

std::string buildBatchCrossSectionKey(const std::string& date, const QString& field)
{
    return date + "|cross|" + field.trimmed().toStdString();
}

std::string buildBatchSeriesKey(const std::string& date,
                                const QString& field,
                                int window,
                                const std::string& symbol,
                                bool useBulkSymbols)
{
    std::ostringstream stream;
    stream << date << "|series|" << field.trimmed().toStdString() << "|w" << window;
    if (useBulkSymbols) {
        stream << "|bulk";
    } else {
        stream << "|single|" << symbol;
    }
    return stream.str();
}

std::unordered_map<std::string, std::vector<double>> fetchBatchSeriesMap(
    const CalculationContext& context,
    const QString& field,
    int window)
{
    std::unordered_map<std::string, std::vector<double>> resolvedSeries;
    if (window <= 0 || field.trimmed().isEmpty() || !context.dataProvider) {
        return resolvedSeries;
    }

    const std::string fieldName = field.toStdString();
    if (!context.dataProvider->hasField(fieldName)) {
        return resolvedSeries;
    }

    const bool useBulkSymbols = !context.symbols.empty();
    const std::string batchKey = buildBatchSeriesKey(context.date, field, window, std::string(), useBulkSymbols);

    if (activeBatchComputationCache && activeBatchComputationCache->dataProvider == context.dataProvider) {
        const auto cacheIt = activeBatchComputationCache->seriesByKey.find(batchKey);
        if (cacheIt != activeBatchComputationCache->seriesByKey.end()) {
            return cacheIt->second;
        }
    }

    const std::vector<std::string> batchSymbols = useBulkSymbols
        ? context.symbols
        : context.dataProvider->getAvailableSymbols(context.date);
    if (batchSymbols.empty()) {
        return resolvedSeries;
    }

    const auto anchoredBatchValues = context.dataProvider->getBatchTimeSeries(
        batchSymbols,
        context.date,
        window,
        {fieldName});

    const auto fieldIt = anchoredBatchValues.find(fieldName);
    if (fieldIt != anchoredBatchValues.end()) {
        resolvedSeries = fieldIt->second;
    }

    if (activeBatchComputationCache && activeBatchComputationCache->dataProvider == context.dataProvider) {
        activeBatchComputationCache->seriesByKey[batchKey] = resolvedSeries;
    }

    return resolvedSeries;
}

}

QString normalizeDividendMetric(const QString& rawMetric);
QString normalizeDividendMetric(const std::string& rawMetric);

void ConfigurableFactor::Params::fromJson(const foundation::json::JsonFacade& json)
{
    const bool hasTechnicalIndicatorConfig = json.has("technicalIndicators")
        || json.has("indicator_type")
        || json.has("indicatorType");
    const bool hasGrowthIndicatorConfig = json.has("growthMetrics")
        || json.has("growthWeights");

    const auto normalizeGrowthMetricText = [](const QString& rawMetric) {
        const QString metric = rawMetric.trimmed().toLower();
        if (metric.isEmpty()) {
            return QString();
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
        return QString();
    };

    if (hasTechnicalIndicatorConfig) {
        indicatorTypes.clear();
        technicalIndicators.clear();
    }

    if (hasGrowthIndicatorConfig) {
        growthMetrics.clear();
        growthWeights.clear();
        metric.clear();

        if (json.has("growthMetrics")) {
            const auto metrics = json.get("growthMetrics");
            if (metrics.isArray() && metrics.size() > 0) {
                std::vector<std::string> parsedGrowthMetrics;
                std::vector<double> parsedGrowthWeights;
                bool validGrowthConfig = true;
                const bool hasExplicitWeights = json.has("growthWeights");
                const auto weights = hasExplicitWeights ? json.get("growthWeights") : foundation::json::JsonFacade();

                if (hasExplicitWeights && (!weights.isArray() || weights.size() != metrics.size())) {
                    validGrowthConfig = false;
                }

                for (size_t index = 0; index < metrics.size(); ++index) {
                    const QString growthMetric = normalizeGrowthMetricText(
                        QString::fromStdString(requireStringItem(metrics, index, "growthMetrics")));
                    const double weight = hasExplicitWeights ? weights.at(index).asDouble() : 1.0;
                    if (growthMetric.isEmpty() || !std::isfinite(weight)
                        || std::find(parsedGrowthMetrics.begin(), parsedGrowthMetrics.end(), growthMetric.toStdString()) != parsedGrowthMetrics.end()) {
                        validGrowthConfig = false;
                        break;
                    }
                    parsedGrowthMetrics.push_back(growthMetric.toStdString());
                    parsedGrowthWeights.push_back(weight);
                }

                if (validGrowthConfig) {
                    growthMetrics = parsedGrowthMetrics;
                    growthWeights = parsedGrowthWeights;
                    metric = parsedGrowthMetrics.front();
                }
            }
        }
    }

    if (json.has("dividendMetrics")) {
        const auto metrics = json.get("dividendMetrics");
        if (metrics.isArray()) {
            for (size_t index = 0; index < metrics.size(); ++index) {
                const QString dividendMetric = normalizeDividendMetric(
                    QString::fromStdString(requireStringItem(metrics, index, "dividendMetrics")));
                if (!dividendMetric.isEmpty()) {
                    dividendMetrics.push_back(dividendMetric.toStdString());
                }
            }
        } else {
            if (!metrics.isString()) {
                throw std::runtime_error("dividendMetrics 不是字符串字段");
            }
            const QString dividendMetric = normalizeDividendMetric(QString::fromStdString(metrics.asString()));
            if (!dividendMetric.isEmpty()) {
                dividendMetrics.push_back(dividendMetric.toStdString());
            }
        }
    }
    if (dividendMetrics.empty() && !hasGrowthIndicatorConfig) {
        QString dividendMetric = normalizeDividendMetric(QString::fromStdString(metric));
        if (dividendMetric.isEmpty() && json.has("dividendMetric")) {
            const QString rawDividendMetric = QString::fromStdString(requireStringField(json, "dividendMetric"));
            dividendMetric = normalizeDividendMetric(rawDividendMetric);
        }
        if (dividendMetric.isEmpty()) {
            dividendMetric = QStringLiteral("dividend_yield");
        }
        dividendMetrics.push_back(dividendMetric.toStdString());
    }
    if (metric.empty() && !dividendMetrics.empty() && !hasGrowthIndicatorConfig) {
        metric = dividendMetrics.front();
    }
    if (json.has("timeframe")) timeframe = requireStringField(json, "timeframe");
    if (json.has("technicalIndicators")) {
        const auto indicators = normalizeTechnicalIndicatorList(json.get("technicalIndicators"));
        for (const QString& indicator : indicators) {
            const std::string normalizedIndicator = indicator.toStdString();
            indicatorTypes.push_back(normalizedIndicator);
            technicalIndicators.push_back(normalizedIndicator);
        }
    }
    if (json.has("indicator_type")) indicatorType = json.get("indicator_type").asString();
    if (indicatorType.empty() && json.has("indicatorType")) indicatorType = json.get("indicatorType").asString();
    if (indicatorTypes.empty() && !indicatorType.empty()) {
        const std::string normalizedIndicator = normalizeTechnicalIndicatorType(QString::fromStdString(indicatorType)).toStdString();
        if (!normalizedIndicator.empty()) {
            indicatorTypes.push_back(normalizedIndicator);
            technicalIndicators.push_back(normalizedIndicator);
        }
    }
    if (json.has("technicalCombinationMode")) technicalCombinationMode = json.get("technicalCombinationMode").asString();
    if (technicalCombinationMode.empty() && json.has("combinationMode")) technicalCombinationMode = json.get("combinationMode").asString();
    if (json.has("maWindow")) maWindow = json.get("maWindow").asInt();
    if (json.has("emaWindow")) emaWindow = json.get("emaWindow").asInt();
    if (json.has("bollWindow")) bollWindow = json.get("bollWindow").asInt();
    if (json.has("bollStdDev")) bollStdDev = json.get("bollStdDev").asDouble();
    if (json.has("kdjWindow")) kdjWindow = json.get("kdjWindow").asInt();
    if (json.has("kdjKPeriod")) kdjKPeriod = json.get("kdjKPeriod").asInt();
    if (json.has("kdjDPeriod")) kdjDPeriod = json.get("kdjDPeriod").asInt();
    if (json.has("atrWindow")) atrWindow = json.get("atrWindow").asInt();
    if (json.has("vwapWindow")) vwapWindow = json.get("vwapWindow").asInt();
    if (json.has("volumeRatioWindow")) volumeRatioWindow = json.get("volumeRatioWindow").asInt();
    if (json.has("sentiment_source")) sentimentSource = json.get("sentiment_source").asString();
    if (sentimentSource.empty() && json.has("sentimentSource")) sentimentSource = json.get("sentimentSource").asString();
    if (json.has("expression")) expression = json.get("expression").asString();
    if (json.has("sector_type")) sectorType = json.get("sector_type").asString();
    if (sectorType.empty() && json.has("sectorType")) sectorType = json.get("sectorType").asString();
    if (json.has("macroMetric")) macroMetric = json.get("macroMetric").asString();
    if (macroMetric.empty() && json.has("macro_metric")) macroMetric = json.get("macro_metric").asString();
    if (json.has("macroDimensions")) {
        macroDimensions.clear();
        const auto dimensions = json.get("macroDimensions");
        if (dimensions.isArray()) {
            for (size_t index = 0; index < dimensions.size(); ++index) {
                const QString dimension = normalizeMacroDimension(
                    QString::fromStdString(requireStringItem(dimensions, index, "macroDimensions")));
                if (!dimension.isEmpty() && std::find(macroDimensions.begin(), macroDimensions.end(), dimension.toStdString()) == macroDimensions.end()) {
                    macroDimensions.push_back(dimension.toStdString());
                }
            }
        }
    }
    if (macroDimensions.empty()) {
        const QString legacyDimension = normalizeMacroDimension(QString::fromStdString(macroMetric));
        if (!legacyDimension.isEmpty()) {
            macroDimensions.push_back(legacyDimension.toStdString());
        }
    }
    if (macroDimensions.empty()) {
        const QStringList defaults = defaultMacroDimensions();
        for (const QString& dimension : defaults) {
            macroDimensions.push_back(dimension.toStdString());
        }
    }

    if (json.has("macroIndicators")) {
        macroIndicators.clear();
        const auto indicators = json.get("macroIndicators");
        if (indicators.isArray()) {
            for (size_t index = 0; index < indicators.size(); ++index) {
                const QString indicator = normalizeMacroIndicator(
                    QString::fromStdString(requireStringItem(indicators, index, "macroIndicators")));
                if (!indicator.isEmpty() && std::find(macroIndicators.begin(), macroIndicators.end(), indicator.toStdString()) == macroIndicators.end()) {
                    macroIndicators.push_back(indicator.toStdString());
                }
            }
        }
    }
    if (macroIndicators.empty()) {
        const QString legacyIndicator = normalizeMacroIndicator(QString::fromStdString(macroMetric));
        if (!legacyIndicator.isEmpty()) {
            macroIndicators.push_back(legacyIndicator.toStdString());
        }
    }
    if (macroIndicators.empty()) {
        for (const std::string& rawDimension : macroDimensions) {
            const QStringList defaults = defaultMacroIndicatorsForDimension(QString::fromStdString(rawDimension));
            for (const QString& indicator : defaults) {
                if (std::find(macroIndicators.begin(), macroIndicators.end(), indicator.toStdString()) == macroIndicators.end()) {
                    macroIndicators.push_back(indicator.toStdString());
                }
            }
        }
    }
    if (macroIndicators.empty()) {
        const QStringList defaults = defaultMacroIndicators();
        for (const QString& indicator : defaults) {
            macroIndicators.push_back(indicator.toStdString());
        }
    }
    if (json.has("macroFrequency")) macroFrequency = requireStringField(json, "macroFrequency");
    if (macroFrequency.empty() && json.has("macro_frequency")) macroFrequency = requireStringField(json, "macro_frequency");
    if (macroFrequency.empty() && json.has("frequency")) macroFrequency = requireStringField(json, "frequency");
    if (json.has("macroWindow")) macroWindow = json.get("macroWindow").asInt();
    if (macroWindow <= 0 && json.has("macro_window")) macroWindow = json.get("macro_window").asInt();
    if (macroWindow <= 0 && json.has("window")) macroWindow = json.get("window").asInt();
    if (json.has("industryMetric")) industryMetric = requireStringField(json, "industryMetric");
    if (industryMetric.empty() && json.has("industry_metric")) industryMetric = requireStringField(json, "industry_metric");
    if (json.has("price_type")) priceType = requireStringField(json, "price_type");
    if (priceType == "close" && json.has("priceType")) priceType = requireStringField(json, "priceType");
    if (json.has("use_volume")) useVolume = json.get("use_volume").asBool();
    if (!useVolume && json.has("useVolume")) useVolume = json.get("useVolume").asBool();
    if (json.has("frequency")) frequency = requireStringField(json, "frequency");
    if (json.has("laggedEnabled")) laggedEnabled = json.get("laggedEnabled").asBool();
    if (json.has("lagged_enabled")) laggedEnabled = json.get("lagged_enabled").asBool();
    if (json.has("standardization")) standardization = requireStringField(json, "standardization");
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
                binding.name = requireStringField(variable, "name");
                if (binding.name.empty()) {
                    continue;
                }
                if (variable.has("field")) {
                    binding.field = requireStringField(variable, "field");
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
    if (json.has("rsiWindow")) rsiWindow = json.get("rsiWindow").asInt();
    if (json.has("macdFastPeriod")) macdFastPeriod = json.get("macdFastPeriod").asInt();
    if (json.has("macdSlowPeriod")) macdSlowPeriod = json.get("macdSlowPeriod").asInt();
    if (json.has("macdSignalPeriod")) macdSignalPeriod = json.get("macdSignalPeriod").asInt();
    if (json.has("obvWindow")) obvWindow = json.get("obvWindow").asInt();
    if (json.has("turnoverStabilityWindow")) turnoverStabilityWindow = json.get("turnoverStabilityWindow").asInt();
    if (json.has("turnoverStabilityMetric")) turnoverStabilityMetric = requireStringField(json, "turnoverStabilityMetric");
    if (json.has("technicalPriceType")) technicalPriceType = requireStringField(json, "technicalPriceType");
    if (technicalPriceType.empty() && json.has("priceType")) technicalPriceType = requireStringField(json, "priceType");
    if (technicalPriceType.empty() && json.has("price_type")) technicalPriceType = requireStringField(json, "price_type");
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

std::vector<CalculationResult> ConfigurableFactor::calculateBatch(const std::vector<CalculationContext>& contexts)
{
    if (contexts.empty()) {
        return {};
    }

    BatchComputationCache cache;
    cache.dataProvider = contexts.front().dataProvider;
    BatchComputationCacheScope scope(cache);
    return BaseFactor::calculateBatch(contexts);
}

CalculationResult ConfigurableFactor::calculate(const CalculationContext& context)
{
    const QString type = normalizedType();
    if (type == "growth") return calculateGrowth(context);
    if (type == "liquidity") return calculateLiquidity(context);
    if (type == "technical") return calculateTechnical(context);
    if (type == "dividend") return calculateDividend(context);
    if (type == "macro") return calculateMacro(context);
    if (type == "industry") return calculateIndustry(context);
    if (type == "sentiment") return calculateSentiment(context);
    if (type == "custom") return calculateCustom(context);
    return CalculationResult::createError(QString::fromUtf8("未识别的通用因子类型: %1").arg(type).toStdString());
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

QString normalizeDividendMetric(const std::string& rawMetric)
{
    return normalizeDividendMetric(QString::fromStdString(rawMetric));
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
    return {};
}

std::unordered_map<std::string, double> ConfigurableFactor::currentFieldCrossSection(
    const CalculationContext& context,
    const QString& field) const
{
    const QString normalizedField = field.trimmed().toLower();
    if (normalizedField.isEmpty()) {
        return {};
    }
    if (activeBatchComputationCache && activeBatchComputationCache->dataProvider == context.dataProvider) {
        const std::string batchKey = buildBatchCrossSectionKey(context.date, normalizedField);
        const auto cacheIt = activeBatchComputationCache->crossSectionsByKey.find(batchKey);
        if (cacheIt != activeBatchComputationCache->crossSectionsByKey.end()) {
            return cacheIt->second;
        }
    }
    const std::vector<std::string> symbols = effectiveSymbols(context);
    if (context.dataProvider && context.dataProvider->hasField(normalizedField.toStdString())) {
        const auto batchValues = context.dataProvider->getBatchCrossSections(context.date,
                                                                            symbols,
                                                                            {normalizedField.toStdString()});
        std::unordered_map<std::string, double> resolvedValues;
        const auto fieldIt = batchValues.find(normalizedField.toStdString());
        if (fieldIt != batchValues.end()) {
            resolvedValues = fieldIt->second;
        }
        if (activeBatchComputationCache && activeBatchComputationCache->dataProvider == context.dataProvider) {
            const std::string batchKey = buildBatchCrossSectionKey(context.date, normalizedField);
            activeBatchComputationCache->crossSectionsByKey[batchKey] = resolvedValues;
        }
        return resolvedValues;
    }

    return {};
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

    const bool useBulkSymbols = !context.symbols.empty()
        && std::find(context.symbols.begin(), context.symbols.end(), symbol) != context.symbols.end();
    if (activeBatchComputationCache && activeBatchComputationCache->dataProvider == context.dataProvider) {
        const std::string batchKey = buildBatchSeriesKey(context.date, field, window, symbol, useBulkSymbols);
        const auto cacheIt = activeBatchComputationCache->seriesByKey.find(batchKey);
        if (cacheIt != activeBatchComputationCache->seriesByKey.end()) {
            const auto symbolIt = cacheIt->second.find(symbol);
            if (symbolIt != cacheIt->second.end()) {
                return symbolIt->second;
            }
            return values;
        }
    }
    if (context.dataProvider && context.dataProvider->hasField(field.toStdString())) {
        const std::vector<std::string> batchSymbols = useBulkSymbols ? context.symbols : std::vector<std::string>{symbol};
        const std::string fieldName = field.toStdString();
        const auto anchoredBatchValues = context.dataProvider->getBatchTimeSeries(
            batchSymbols,
            context.date,
            window,
            {fieldName});
        std::unordered_map<std::string, std::vector<double>> resolvedSeries;
        const auto fieldIt = anchoredBatchValues.find(fieldName);
        if (fieldIt != anchoredBatchValues.end()) {
            resolvedSeries = fieldIt->second;
        }
        if (activeBatchComputationCache && activeBatchComputationCache->dataProvider == context.dataProvider) {
            const std::string batchKey = buildBatchSeriesKey(context.date, field, window, symbol, useBulkSymbols);
            activeBatchComputationCache->seriesByKey[batchKey] = resolvedSeries;
        }
        const auto symbolIt = resolvedSeries.find(symbol);
        if (symbolIt != resolvedSeries.end()) {
            return symbolIt->second;
        }
        return values;
    }

    if (isFinancialMetricField(field.trimmed().toLower())) {
        const auto seriesMap = latestFinancialSeries(context, field, QString::fromStdString(context.date), window);
        const auto seriesIt = seriesMap.find(symbol);
        if (seriesIt != seriesMap.end()) {
            for (double value : seriesIt->second) {
                if (std::isfinite(value)) {
                    values.push_back(value);
                }
            }
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
    Q_UNUSED(date);
    const QString normalizedField = field.trimmed().toLower();
    if (normalizedField.isEmpty() || !context.dataProvider || !context.dataProvider->hasField(normalizedField.toStdString())) {
        return {};
    }

    return currentFieldCrossSection(context, normalizedField);
}

std::unordered_map<std::string, std::vector<double>> ConfigurableFactor::latestFinancialSeries(
    const CalculationContext& context,
    const QString& field,
    const QString& date,
    int limit) const
{
    Q_UNUSED(date);
    std::unordered_map<std::string, std::vector<double>> result;
    if (limit <= 0) {
        return result;
    }

    const QString normalizedField = field.trimmed().toLower();
    if (normalizedField.isEmpty() || !context.dataProvider || !context.dataProvider->hasField(normalizedField.toStdString())) {
        return result;
    }

    const std::vector<std::string> symbols = effectiveSymbols(context);
    for (const auto& symbol : symbols) {
        const auto series = seriesForField(context, symbol, normalizedField, limit);
        if (!series.empty()) {
            result[symbol] = series;
        }
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
    if (context.dataProvider) {
        result.dataStatus.availability = DataAvailability::AVAILABLE;
        result.dataStatus.coverage = 1.0;
        result.dataStatus.message = "使用缓存数据集";
    } else if (db_) {
        result.dataStatus.availability = DataAvailability::AVAILABLE;
        result.dataStatus.coverage = 1.0;
        result.dataStatus.message = "使用数据库回放数据";
    } else {
        result.dataStatus = checkDataAvailability(context.date);
    }
    if (!result.dataStatus.isValid()) {
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    }

    auto failGrowth = [&](const QString& message) {
        result.dataStatus = CalculationResult::createError(message.toStdString()).dataStatus;
        result.metadata.set("error", json_helper::toJsonValue(message.toStdString()));
        return result;
    };

    const std::vector<std::string>& selectedMetrics = params_.growthMetrics;
    const std::vector<double>& selectedWeights = params_.growthWeights;
    if (selectedMetrics.empty() || selectedWeights.empty() || selectedMetrics.size() != selectedWeights.size()) {
        return failGrowth(QStringLiteral("成长因子配置必须显式提供等长的 growthMetrics 和 growthWeights"));
    }

    const size_t pairCount = selectedMetrics.size();
    const QString standardization = normalizeConfigurableStandardization(params_.standardization);
    std::unordered_set<std::string> seenMetrics;

    struct GrowthMetricSelection {
        QString metric;
        double weight{0.0};
        QString field;
    };

    auto fieldForMetric = [](const QString& metric) -> QString {
        if (metric == QStringLiteral("revenue_growth")) {
            return QStringLiteral("total_revenue");
        }
        if (metric == QStringLiteral("net_profit_growth")) {
            return QStringLiteral("net_profit");
        }
        if (metric == QStringLiteral("delta_roe")) {
            return QStringLiteral("roe");
        }
        if (metric == QStringLiteral("sue")) {
            return QStringLiteral("eps");
        }
        return {};
    };

    std::vector<GrowthMetricSelection> selections;
    selections.reserve(pairCount);

    for (size_t index = 0; index < pairCount; ++index) {
        const QString metric = QString::fromStdString(selectedMetrics[index]).trimmed().toLower();
        const double weight = selectedWeights[index];
        if (metric.isEmpty() || !std::isfinite(weight) || weight < 0.0) {
            return failGrowth(QStringLiteral("成长因子配置包含非法指标或权重"));
        }
        if (seenMetrics.find(metric.toStdString()) != seenMetrics.end()) {
            return failGrowth(QStringLiteral("成长因子配置不允许重复指标"));
        }
        seenMetrics.insert(metric.toStdString());

        const QString field = fieldForMetric(metric);
        if (field.isEmpty()) {
            return failGrowth(QStringLiteral("成长因子配置包含不支持的指标"));
        }
        selections.push_back({metric, weight, field});
    }

    auto computeYoYScoreMap = [&](const QString& field) {
        std::unordered_map<std::string, double> scores;
        const auto seriesMap = latestFinancialSeries(context, field, QString::fromStdString(context.date), 2);
        for (const auto& [symbol, values] : seriesMap) {
            if (values.size() < 2) {
                continue;
            }

            const double previousValue = values[1];
            if (std::abs(previousValue) < 1e-12) {
                continue;
            }

            const double growth = safeRatio(values[0] - values[1], std::abs(previousValue));
            if (std::isfinite(growth)) {
                scores[symbol] = growth;
            }
        }
        return scores;
    };

    auto computeDifferenceScoreMap = [&](const QString& field) {
        std::unordered_map<std::string, double> scores;
        const auto seriesMap = latestFinancialSeries(context, field, QString::fromStdString(context.date), 2);
        for (const auto& [symbol, values] : seriesMap) {
            if (values.size() < 2) {
                continue;
            }

            const double delta = values[0] - values[1];
            if (std::isfinite(delta)) {
                scores[symbol] = delta;
            }
        }
        return scores;
    };

    auto computeSueProxyScoreMap = [&]() {
        std::unordered_map<std::string, double> scores;
        const auto seriesMap = latestFinancialSeries(context, QStringLiteral("eps"), QString::fromStdString(context.date), 5);
        for (const auto& [symbol, values] : seriesMap) {
            if (values.size() < 2) {
                continue;
            }

            std::vector<double> changes;
            changes.reserve(values.size() - 1);
            for (size_t index = 0; index + 1 < values.size(); ++index) {
                changes.push_back(values[index] - values[index + 1]);
            }

            if (changes.empty()) {
                continue;
            }

            const double currentChange = changes.front();
            if (changes.size() < 2) {
                if (std::isfinite(currentChange)) {
                    scores[symbol] = currentChange;
                }
                continue;
            }

            double historyMean = 0.0;
            for (size_t index = 1; index < changes.size(); ++index) {
                historyMean += changes[index];
            }
            historyMean /= static_cast<double>(changes.size() - 1);

            double variance = 0.0;
            for (size_t index = 1; index < changes.size(); ++index) {
                const double diff = changes[index] - historyMean;
                variance += diff * diff;
            }
            variance /= static_cast<double>(changes.size() - 1);

            const double stdDev = std::sqrt(std::max(variance, 0.0));
            const double sueScore = stdDev > 1e-12 ? (currentChange - historyMean) / stdDev : currentChange;
            if (std::isfinite(sueScore)) {
                scores[symbol] = sueScore;
            }
        }
        return scores;
    };

    auto normalizeScoreMap = [&](std::unordered_map<std::string, double>& scores) {
        if (scores.empty()) {
            return;
        }

        if (standardization == QStringLiteral("percentile")) {
            std::vector<std::pair<std::string, double>> ranked(scores.begin(), scores.end());
            std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
                return left.second < right.second;
            });
            if (ranked.size() == 1) {
                scores[ranked.front().first] = 1.0;
                return;
            }
            for (size_t index = 0; index < ranked.size(); ++index) {
                scores[ranked[index].first] = static_cast<double>(index) / static_cast<double>(ranked.size() - 1);
            }
            return;
        }

        std::vector<double> values;
        values.reserve(scores.size());
        for (const auto& [symbol, value] : scores) {
            Q_UNUSED(symbol);
            if (std::isfinite(value)) {
                values.push_back(value);
            }
        }

        if (values.empty()) {
            return;
        }

        if (standardization == QStringLiteral("zscore")) {
            const double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
            double variance = 0.0;
            for (double value : values) {
                const double delta = value - mean;
                variance += delta * delta;
            }
            const double stdev = std::sqrt(variance / static_cast<double>(values.size()));
            if (stdev > 1e-12) {
                for (auto& [symbol, value] : scores) {
                    Q_UNUSED(symbol);
                    value = (value - mean) / stdev;
                }
            }
        } else if (standardization == QStringLiteral("minmax")) {
            const auto [minIt, maxIt] = std::minmax_element(values.begin(), values.end());
            const double range = *maxIt - *minIt;
            if (range > 1e-12) {
                for (auto& [symbol, value] : scores) {
                    Q_UNUSED(symbol);
                    value = (value - *minIt) / range;
                }
            }
        }
    };

    std::unordered_map<std::string, double> combinedScores;
    std::unordered_map<std::string, double> activeWeightSums;

    for (const auto& selection : selections) {
        if (selection.weight == 0.0) {
            continue;
        }

        std::unordered_map<std::string, double> metricScores;
        if (selection.metric == "revenue_growth") {
            metricScores = computeYoYScoreMap(selection.field);
        } else if (selection.metric == "net_profit_growth") {
            metricScores = computeYoYScoreMap(selection.field);
        } else if (selection.metric == "delta_roe") {
            metricScores = computeDifferenceScoreMap(selection.field);
        } else if (selection.metric == "sue") {
            metricScores = computeSueProxyScoreMap();
        } else {
            return failGrowth(QStringLiteral("成长因子配置包含不支持的指标"));
        }

        normalizeScoreMap(metricScores);

        for (const auto& [symbol, score] : metricScores) {
            if (!std::isfinite(score)) {
                continue;
            }
            combinedScores[symbol] += score * selection.weight;
            activeWeightSums[symbol] += selection.weight;
        }
    }

    QString growthDataMode = QStringLiteral("financial_series_direct");
    QString growthNeutralizationMode = params_.neutralizationEnabled
        ? (db_ ? QStringLiteral("requested") : QStringLiteral("requested_no_db"))
        : QStringLiteral("disabled");
    result.metadata.set("data_mode", json_helper::toJsonValue(growthDataMode.toStdString()));
    if (combinedScores.empty()) {
        result.metadata.set("empty_reason", json_helper::toJsonValue(QStringLiteral("成长因子没有可用财务数据").toStdString()));
        return result;
    }

    for (const auto& [symbol, weightedScore] : combinedScores) {
        const double weightSum = activeWeightSums[symbol];
        if (weightSum > 1e-12) {
            result.values[symbol] = weightedScore / weightSum;
        }
    }

    if (params_.neutralizationEnabled && db_ && !result.values.empty()) {
        const auto industryMap = industryBySymbol(context);
        std::unordered_map<QString, std::vector<double>> groupedValues;
        for (const auto& [symbol, value] : result.values) {
            const auto industryIt = industryMap.find(symbol);
            if (industryIt != industryMap.end() && !industryIt->second.isEmpty() && std::isfinite(value)) {
                groupedValues[industryIt->second].push_back(value);
            }
        }

        if (groupedValues.empty()) {
            growthNeutralizationMode = QStringLiteral("requested_no_industry");
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

        if (!groupedValues.empty()) {
            growthNeutralizationMode = QStringLiteral("applied");
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

    result.metadata.set("metric", json_helper::toJsonValue(selectedMetrics.empty() ? std::string() : selectedMetrics.front()));
    result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
    result.metadata.set("neutralization_enabled", json_helper::toJsonValue(params_.neutralizationEnabled));
    result.metadata.set("neutralization_mode", json_helper::toJsonValue(growthNeutralizationMode.toStdString()));
    result.metadata.set("symbol_count", json_helper::toJsonValue(static_cast<int>(result.values.size())));
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
    bool laggedDateResolvedByProvider = false;
    QString liquidityNeutralizationMode = params_.neutralizationEnabled
        ? (db_ ? QStringLiteral("requested") : QStringLiteral("requested_no_db"))
        : QStringLiteral("disabled");
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
                CalculationContext candidateContext = context;
                candidateContext.date = candidate.toStdString();
                candidateContext.symbols = symbols;
                if (currentFieldCrossSection(candidateContext, requiredField).empty()) {
                    continue;
                }
                laggedDateResolvedByProvider = true;
                return candidate;
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
    effectiveContext.symbols = symbols;
    const bool useLocalBatchCache = context.dataProvider
        && (!activeBatchComputationCache || activeBatchComputationCache->dataProvider != context.dataProvider);

    auto calculateLiquidityBody = [&]() -> CalculationResult {
        size_t populatedSymbolCount = 0;
        const auto closesBySymbol = fetchBatchSeriesMap(effectiveContext, QStringLiteral("close"), window + 1);
        const auto volumesBySymbol = fetchBatchSeriesMap(effectiveContext, QStringLiteral("volume"), window + 1);
        const QString metricField = metric == QStringLiteral("volume")
            ? QStringLiteral("volume")
            : (metric == QStringLiteral("amplitude") ? QStringLiteral("amplitude") : QStringLiteral("turnover_rate"));
        const auto metricBySymbol = fetchBatchSeriesMap(effectiveContext, metricField, window);

        const std::vector<std::string> activeSymbols = [&]() {
            std::vector<std::string> validSymbols;
            validSymbols.reserve(symbols.size());
            for (const auto& symbol : symbols) {
                if (metric == QStringLiteral("amihud_illiquidity")) {
                    const auto closeIt = closesBySymbol.find(symbol);
                    const auto volumeIt = volumesBySymbol.find(symbol);
                    if (closeIt == closesBySymbol.end() || volumeIt == volumesBySymbol.end()) {
                        continue;
                    }
                    if (closeIt->second.size() < 2 || volumeIt->second.size() < 2) {
                        continue;
                    }
                } else {
                    const auto metricIt = metricBySymbol.find(symbol);
                    if (metricIt == metricBySymbol.end() || metricIt->second.empty()) {
                        continue;
                    }
                }
                validSymbols.push_back(symbol);
            }
            return validSymbols;
        }();

        if (activeSymbols.empty()) {
            result.dataStatus = CalculationResult::createError("流动性因子没有可用价格或成交量数据").dataStatus;
            result.metadata.set("empty_reason", json_helper::toJsonValue("流动性因子没有可用价格或成交量数据"));
            result.metadata.set("lagged_date_mode", json_helper::toJsonValue(params_.laggedEnabled
                ? (laggedDateResolvedByProvider ? "provider_scan" : "anchor_date")
                : "disabled"));
            result.metadata.set("neutralization_mode", json_helper::toJsonValue(liquidityNeutralizationMode.toStdString()));
            return result;
        }

        const auto findCommonLength = [&](const auto& seriesBySymbol) -> size_t {
            size_t commonLength = std::numeric_limits<size_t>::max();
            for (const auto& symbol : activeSymbols) {
                const auto seriesIt = seriesBySymbol.find(symbol);
                if (seriesIt == seriesBySymbol.end() || seriesIt->second.empty()) {
                    continue;
                }
                commonLength = (std::min)(commonLength, seriesIt->second.size());
            }
            return commonLength == std::numeric_limits<size_t>::max() ? 0 : commonLength;
        };

        const auto collectMatrix = [&](const auto& seriesBySymbol, size_t commonLength) {
            Eigen::MatrixXd matrix(static_cast<int>(activeSymbols.size()), static_cast<int>(commonLength));
            for (int row = 0; row < matrix.rows(); ++row) {
                const auto seriesIt = seriesBySymbol.find(activeSymbols[static_cast<size_t>(row)]);
                const auto& values = seriesIt->second;
                const size_t offset = values.size() - commonLength;
                for (int column = 0; column < matrix.cols(); ++column) {
                    matrix(row, column) = values[offset + static_cast<size_t>(column)];
                }
            }
            return matrix;
        };

        size_t commonLength = 0;
        if (metric == QStringLiteral("amihud_illiquidity")) {
            const size_t closeLength = findCommonLength(closesBySymbol);
            const size_t volumeLength = findCommonLength(volumesBySymbol);
            commonLength = (std::min)(closeLength, volumeLength);
        } else {
            commonLength = findCommonLength(metricBySymbol);
        }

        if (commonLength == 0 || (metric == QStringLiteral("amihud_illiquidity") && commonLength < 2)) {
            result.dataStatus = CalculationResult::createError("流动性因子没有可用价格或成交量数据").dataStatus;
            result.metadata.set("empty_reason", json_helper::toJsonValue("流动性因子没有可用价格或成交量数据"));
            result.metadata.set("lagged_date_mode", json_helper::toJsonValue(params_.laggedEnabled
                ? (laggedDateResolvedByProvider ? "provider_scan" : "anchor_date")
                : "disabled"));
            result.metadata.set("neutralization_mode", json_helper::toJsonValue(liquidityNeutralizationMode.toStdString()));
            return result;
        }

        Eigen::VectorXd rawScores(static_cast<int>(activeSymbols.size()));
        rawScores.setConstant(std::numeric_limits<double>::quiet_NaN());

        if (metric == QStringLiteral("volume")) {
            const Eigen::MatrixXd metricMatrix = collectMatrix(metricBySymbol, commonLength);
            rawScores = metricMatrix.rowwise().mean();
        } else if (metric == QStringLiteral("amplitude")) {
            const Eigen::MatrixXd metricMatrix = collectMatrix(metricBySymbol, commonLength);
            rawScores = -metricMatrix.rowwise().mean();
        } else if (metric == QStringLiteral("amihud_illiquidity")) {
            const Eigen::MatrixXd closeMatrix = collectMatrix(closesBySymbol, commonLength);
            const Eigen::MatrixXd volumeMatrix = collectMatrix(volumesBySymbol, commonLength);
            const Eigen::MatrixXd previousClose = closeMatrix.leftCols(static_cast<int>(commonLength) - 1);
            const Eigen::MatrixXd currentClose = closeMatrix.rightCols(static_cast<int>(commonLength) - 1);
            const Eigen::MatrixXd currentVolume = volumeMatrix.rightCols(static_cast<int>(commonLength) - 1);
            const Eigen::MatrixXd previousCloseAbs = previousClose.array().abs().matrix().unaryExpr([](double value) {
                return (std::max)(1e-12, value);
            });
            const Eigen::MatrixXd volumeSafe = currentVolume.array().abs().matrix().unaryExpr([](double value) {
                return (std::max)(1e-12, value);
            });
            const Eigen::MatrixXd ratioMatrix = (currentClose - previousClose).array().abs().matrix()
                .cwiseQuotient(previousCloseAbs)
                .cwiseQuotient(volumeSafe);

            for (int row = 0; row < ratioMatrix.rows(); ++row) {
                double sum = 0.0;
                int count = 0;
                for (int column = 0; column < ratioMatrix.cols(); ++column) {
                    const double ratio = ratioMatrix(row, column);
                    if (!std::isfinite(ratio)) {
                        continue;
                    }
                    sum += ratio;
                    ++count;
                }
                if (count > 0) {
                    rawScores(row) = -sum / static_cast<double>(count);
                }
            }
        } else {
            const Eigen::MatrixXd metricMatrix = collectMatrix(metricBySymbol, commonLength);
            rawScores = metricMatrix.rowwise().mean();
        }

        std::vector<int> validIndices;
        std::vector<double> validValues;
        validIndices.reserve(static_cast<size_t>(rawScores.size()));
        validValues.reserve(static_cast<size_t>(rawScores.size()));
        for (int row = 0; row < rawScores.size(); ++row) {
            const double value = rawScores(row);
            if (!std::isfinite(value)) {
                continue;
            }
            validIndices.push_back(row);
            validValues.push_back(value);
        }

        if (validValues.empty()) {
            result.dataStatus = CalculationResult::createError("流动性因子没有可用价格或成交量数据").dataStatus;
            result.metadata.set("empty_reason", json_helper::toJsonValue("流动性因子没有可用价格或成交量数据"));
            result.metadata.set("lagged_date_mode", json_helper::toJsonValue(params_.laggedEnabled
                ? (laggedDateResolvedByProvider ? "provider_scan" : "anchor_date")
                : "disabled"));
            result.metadata.set("neutralization_mode", json_helper::toJsonValue(liquidityNeutralizationMode.toStdString()));
            return result;
        }

        if (standardization == QStringLiteral("percentile")) {
            std::vector<std::pair<double, int>> ranked;
            ranked.reserve(validValues.size());
            for (size_t index = 0; index < validValues.size(); ++index) {
                ranked.emplace_back(validValues[index], validIndices[index]);
            }
            std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
                return left.first < right.first;
            });
            if (ranked.size() == 1) {
                rawScores(ranked.front().second) = 1.0;
            } else {
                for (size_t index = 0; index < ranked.size(); ++index) {
                    rawScores(ranked[index].second) = static_cast<double>(index) / static_cast<double>(ranked.size() - 1);
                }
            }
        } else if (standardization == QStringLiteral("zscore")) {
            const double meanValue = std::accumulate(validValues.begin(), validValues.end(), 0.0) / static_cast<double>(validValues.size());
            double variance = 0.0;
            for (double value : validValues) {
                const double delta = value - meanValue;
                variance += delta * delta;
            }
            const double stdev = std::sqrt(variance / static_cast<double>(validValues.size()));
            if (stdev > 1e-12) {
                for (int index : validIndices) {
                    rawScores(index) = (rawScores(index) - meanValue) / stdev;
                }
            }
        } else if (standardization == QStringLiteral("minmax")) {
            const auto [minIt, maxIt] = std::minmax_element(validValues.begin(), validValues.end());
            const double range = *maxIt - *minIt;
            if (range > 1e-12) {
                for (int index : validIndices) {
                    rawScores(index) = (rawScores(index) - *minIt) / range;
                }
            }
        }

        for (size_t index = 0; index < activeSymbols.size(); ++index) {
            const double score = rawScores(static_cast<int>(index));
            if (std::isfinite(score) && score != 0.0) {
                result.values[activeSymbols[index]] = score;
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

        if (groupedValues.empty()) {
            liquidityNeutralizationMode = QStringLiteral("requested_no_industry");
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

        if (!groupedValues.empty()) {
            liquidityNeutralizationMode = QStringLiteral("applied");
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
    result.metadata.set("lagged_date_mode", json_helper::toJsonValue(params_.laggedEnabled
        ? (laggedDateResolvedByProvider ? "provider_scan" : "anchor_date")
        : "disabled"));
    result.metadata.set("lookback_period", json_helper::toJsonValue(params_.lookbackPeriod));
    result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
    result.metadata.set("neutralization_enabled", json_helper::toJsonValue(params_.neutralizationEnabled));
    result.metadata.set("neutralization_mode", json_helper::toJsonValue(liquidityNeutralizationMode.toStdString()));
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
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.dataProvider = context.dataProvider;
        BatchComputationCacheScope scope(cache);
        return calculateLiquidityBody();
    }

    return calculateLiquidityBody();
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

    QStringList indicatorTypes;
    bool technicalUsedDefaultFallback = false;
    const std::vector<std::string>& configuredIndicators = params_.technicalIndicators.empty()
        ? params_.indicatorTypes
        : params_.technicalIndicators;
    const QString technicalConfigMode = !params_.technicalIndicators.empty()
        ? QStringLiteral("technical_indicators")
        : (!params_.indicatorTypes.empty()
            ? QStringLiteral("indicator_types")
            : (!params_.indicatorType.empty()
                ? QStringLiteral("indicator_type")
                : QStringLiteral("default_rsi")));

    for (const std::string& rawType : configuredIndicators) {
        const QString indicatorType = normalizeTechnicalIndicatorType(QString::fromStdString(rawType));
        if (!indicatorType.isEmpty() && !indicatorTypes.contains(indicatorType)) {
            indicatorTypes.append(indicatorType);
        }
    }
    if (indicatorTypes.isEmpty()) {
        const QString legacyIndicatorType = normalizeTechnicalIndicatorType(QString::fromStdString(params_.indicatorType));
        if (!legacyIndicatorType.isEmpty()) {
            indicatorTypes.append(legacyIndicatorType);
        }
    }
    if (indicatorTypes.isEmpty()) {
        indicatorTypes.append(QStringLiteral("rsi"));
        technicalUsedDefaultFallback = true;
    }

    const QString technicalResolvedConfigMode = technicalUsedDefaultFallback
        ? QStringLiteral("default_rsi_fallback")
        : technicalConfigMode;

    const QString combinationMode = QString::fromStdString(params_.technicalCombinationMode).trimmed().toLower();
    const QString priceField = normalizePriceField(QString::fromStdString(
        params_.technicalPriceType.empty() ? params_.priceType : params_.technicalPriceType));
    const int rsiWindow = (std::max)(2, params_.rsiWindow);
    const int maWindow = (std::max)(2, params_.maWindow);
    const int emaWindow = (std::max)(2, params_.emaWindow);
    const int bollWindow = (std::max)(2, params_.bollWindow);
    const double bollStdDev = params_.bollStdDev > 0.0 ? params_.bollStdDev : 2.0;
    const int kdjWindow = (std::max)(2, params_.kdjWindow);
    const int kdjKPeriod = (std::max)(2, params_.kdjKPeriod);
    const int kdjDPeriod = (std::max)(2, params_.kdjDPeriod);
    const int atrWindow = (std::max)(2, params_.atrWindow);
    const int macdFastPeriod = (std::max)(2, params_.macdFastPeriod);
    const int macdSlowPeriod = (std::max)(macdFastPeriod + 1, params_.macdSlowPeriod);
    const int macdSignalPeriod = (std::max)(2, params_.macdSignalPeriod);
    const int obvWindow = (std::max)(2, params_.obvWindow);
    const int vwapWindow = (std::max)(2, params_.vwapWindow);
    const int volumeRatioWindow = (std::max)(2, params_.volumeRatioWindow);
    const int turnoverStabilityWindow = (std::max)(2, params_.turnoverStabilityWindow);
    const auto symbols = effectiveSymbols(context);
    CalculationContext technicalContext = context;
    technicalContext.symbols = symbols;
    const bool useLocalBatchCache = context.dataProvider
        && (!activeBatchComputationCache || activeBatchComputationCache->dataProvider != context.dataProvider);
    const bool needHighLowSeries = indicatorTypes.contains(QStringLiteral("kdj")) || indicatorTypes.contains(QStringLiteral("atr"));
    const bool needVolumeSeries = indicatorTypes.contains(QStringLiteral("obv"))
        || indicatorTypes.contains(QStringLiteral("vwap"))
        || indicatorTypes.contains(QStringLiteral("volume_ratio"))
        || indicatorTypes.contains(QStringLiteral("turnover_stability"));
    const QString turnoverMetricField = [&]() {
        const QString metric = QString::fromStdString(params_.turnoverStabilityMetric).trimmed().toLower();
        if (metric == QStringLiteral("turnover") || metric == QStringLiteral("turnover_rate") || metric.isEmpty()) {
            return QStringLiteral("turnover_rate");
        }
        if (metric == QStringLiteral("volume")) {
            return QStringLiteral("volume");
        }
        return QStringLiteral("turnover_rate");
    }();
    const bool needTurnoverSeries = indicatorTypes.contains(QStringLiteral("turnover_stability"));

    auto calculateTechnicalBody = [&]() -> CalculationResult {
    const size_t closeWindow = static_cast<size_t>((std::max)({
        rsiWindow + 1,
        maWindow,
        emaWindow,
        bollWindow,
        kdjWindow + 1,
        atrWindow + 1,
        macdSlowPeriod + macdSignalPeriod + 5,
        obvWindow + 1,
        vwapWindow + 1,
        volumeRatioWindow + 1
    }));
    const size_t highLowHistoryWindow = static_cast<size_t>((std::max)({kdjWindow + 1, atrWindow + 1, static_cast<int>(closeWindow)}));
    const size_t volumeHistoryWindow = static_cast<size_t>((std::max)({obvWindow + 1, vwapWindow + 1, volumeRatioWindow + 1, turnoverStabilityWindow}));
    const size_t maxWindow = (std::max)({closeWindow, highLowHistoryWindow, volumeHistoryWindow, static_cast<size_t>(turnoverStabilityWindow)});
    const size_t technicalLookbackWindow = maxWindow + 5;

    auto loadSymbolsFromDatabase = [&]() {
        std::vector<std::string> dbSymbols;
        if (!db_) {
            return dbSymbols;
        }

        const auto symbolResult = db_->executeQuery(
            "SELECT DISTINCT symbol FROM daily_bar WHERE trade_date = :date ORDER BY symbol",
            makeNamedParams({{":date", QString::fromStdString(technicalContext.date)}}));
        for (size_t index = 0; index < symbolResult.rowCount(); ++index) {
            const QString symbol = symbolResult.getRow(index).getString("symbol").trimmed();
            if (!symbol.isEmpty()) {
                dbSymbols.push_back(symbol.toStdString());
            }
        }

        return dbSymbols;
    };

    auto loadTechnicalBatchFromDatabase = [&](const std::vector<std::string>& batchSymbols) {
        std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchData;
        if (!db_ || batchSymbols.empty()) {
            return batchData;
        }

        const QDate endDate = QDate::fromString(QString::fromStdString(technicalContext.date), Qt::ISODate);
        if (!endDate.isValid()) {
            return batchData;
        }

        const int lookbackDays = (std::max)(180, static_cast<int>(technicalLookbackWindow * 4));
        const QString startDate = endDate.addDays(-lookbackDays).toString(Qt::ISODate);
        const QString resolvedPriceField = priceField == QStringLiteral("adj_close")
            ? QStringLiteral("close")
            : priceField;

        QStringList selectColumns;
        selectColumns << QStringLiteral("symbol") << QStringLiteral("trade_date") << resolvedPriceField;
        if (needHighLowSeries) {
            selectColumns << QStringLiteral("high") << QStringLiteral("low");
        }
        if (needVolumeSeries) {
            selectColumns << QStringLiteral("volume");
        }
        if (needTurnoverSeries) {
            selectColumns << QStringLiteral("turnover_rate");
        }

        QString sql = QStringLiteral("SELECT %1 FROM daily_bar WHERE trade_date BETWEEN :start_date AND :end_date ORDER BY symbol, trade_date ASC")
            .arg(selectColumns.join(", "));
        const auto queryResult = db_->executeQuery(sql, makeNamedParams({
            {QStringLiteral(":start_date"), startDate},
            {QStringLiteral(":end_date"), QString::fromStdString(technicalContext.date)}
        }));

        const auto appendValue = [&](const QString& fieldName,
                                     const std::string& symbol,
                                     const QVariant& value) {
            if (!value.isValid() || value.isNull() || !value.canConvert<double>()) {
                return;
            }
            batchData[fieldName.toStdString()][symbol].push_back(value.toDouble());
        };

        for (size_t index = 0; index < queryResult.rowCount(); ++index) {
            const auto& row = queryResult.getRow(index);
            const std::string symbol = row.getString("symbol").trimmed().toStdString();
            if (symbol.empty()) {
                continue;
            }

            appendValue(resolvedPriceField, symbol, row.getValue(resolvedPriceField));
            if (needHighLowSeries) {
                appendValue(QStringLiteral("high"), symbol, row.getValue(QStringLiteral("high")));
                appendValue(QStringLiteral("low"), symbol, row.getValue(QStringLiteral("low")));
            }
            if (needVolumeSeries) {
                appendValue(QStringLiteral("volume"), symbol, row.getValue(QStringLiteral("volume")));
            }
            if (needTurnoverSeries) {
                appendValue(QStringLiteral("turnover_rate"), symbol, row.getValue(QStringLiteral("turnover_rate")));
            }
        }

        for (auto& [fieldName, fieldSeriesBySymbol] : batchData) {
            for (auto& [symbol, values] : fieldSeriesBySymbol) {
                if (values.size() > technicalLookbackWindow) {
                    values.erase(values.begin(), values.end() - static_cast<std::ptrdiff_t>(technicalLookbackWindow));
                }
            }
        }

        return batchData;
    };

    std::vector<std::string> runtimeSymbols = symbols;
    if (runtimeSymbols.empty() && !technicalContext.dataProvider) {
        runtimeSymbols = loadSymbolsFromDatabase();
    }
    technicalContext.symbols = runtimeSymbols;

    if (runtimeSymbols.empty()) {
        result.dataStatus = CalculationResult::createError("技术因子缺少可用标的").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("技术因子缺少可用标的"));
        result.metadata.set("technical_config_mode", json_helper::toJsonValue(technicalResolvedConfigMode.toStdString()));
        result.metadata.set("indicator_types", json_helper::toJsonValue(indicatorTypes.join(",").toStdString()));
        return result;
    }

    if (technicalContext.dataProvider) {
        result.dataStatus.availability = DataAvailability::AVAILABLE;
        result.dataStatus.coverage = 1.0;
        result.dataStatus.message = "使用缓存数据集";
    } else if (db_) {
        result.dataStatus.availability = DataAvailability::AVAILABLE;
        result.dataStatus.coverage = 1.0;
        result.dataStatus.message = "使用数据库回放数据";
    } else {
        result.dataStatus = CalculationResult::createError("技术因子缺少数据提供器或数据库连接").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("技术因子缺少数据提供器或数据库连接"));
        result.metadata.set("technical_config_mode", json_helper::toJsonValue(technicalResolvedConfigMode.toStdString()));
        result.metadata.set("indicator_types", json_helper::toJsonValue(indicatorTypes.join(",").toStdString()));
        return result;
    }

    const std::string priceFieldName = priceField.toStdString();
    const std::string turnoverFieldName = turnoverMetricField.toStdString();
    std::vector<std::string> requestedFields;
    requestedFields.reserve(5);
    std::unordered_set<std::string> requestedFieldSet;
    const auto appendField = [&](const std::string& fieldName) {
        if (requestedFieldSet.insert(fieldName).second) {
            requestedFields.push_back(fieldName);
        }
    };
    appendField(priceFieldName);
    if (needHighLowSeries) {
        appendField("high");
        appendField("low");
    }
    if (needVolumeSeries) {
        appendField("volume");
    }
    if (needTurnoverSeries) {
        appendField(turnoverFieldName);
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchData;
    if (technicalContext.dataProvider) {
        batchData = technicalContext.dataProvider->getBatchTimeSeries(
            runtimeSymbols,
            technicalContext.date,
            static_cast<int>(technicalLookbackWindow),
            requestedFields);
    } else {
        batchData = loadTechnicalBatchFromDatabase(runtimeSymbols);
    }

    const auto findSeriesMap = [&](const std::string& fieldName) -> const std::unordered_map<std::string, std::vector<double>>* {
        const auto fieldIt = batchData.find(fieldName);
        if (fieldIt == batchData.end()) {
            return nullptr;
        }
        return &fieldIt->second;
    };

    const auto* closesBySymbol = findSeriesMap(priceFieldName);
    if (!closesBySymbol) {
        result.dataStatus = CalculationResult::createError("技术因子没有可用价格数据").dataStatus;
        result.metadata.set("empty_reason", json_helper::toJsonValue("技术因子没有可用价格数据"));
        result.metadata.set("technical_config_mode", json_helper::toJsonValue(technicalResolvedConfigMode.toStdString()));
        result.metadata.set("indicator_types", json_helper::toJsonValue(indicatorTypes.join(",").toStdString()));
        return result;
    }

    const auto* highsBySymbol = needHighLowSeries ? findSeriesMap("high") : nullptr;
    const auto* lowsBySymbol = needHighLowSeries ? findSeriesMap("low") : nullptr;
    const auto* volumesBySymbol = needVolumeSeries ? findSeriesMap("volume") : nullptr;
    const auto* turnoverSeriesBySymbol = needTurnoverSeries ? findSeriesMap(turnoverFieldName) : nullptr;

    std::unordered_map<std::string, std::vector<double>> scoresBySymbol;
    scoresBySymbol.reserve(symbols.size());

    for (const QString& indicatorType : indicatorTypes) {
        std::unordered_map<std::string, double> indicatorScores;
        if (indicatorType == QStringLiteral("rsi")) {
            indicatorScores = batchCalculateRsi(*closesBySymbol, rsiWindow);
        } else if (indicatorType == QStringLiteral("macd")) {
            indicatorScores = batchCalculateMacd(*closesBySymbol, macdFastPeriod, macdSlowPeriod, macdSignalPeriod);
        } else if (indicatorType == QStringLiteral("ma")) {
            indicatorScores = batchCalculateMa(*closesBySymbol, maWindow);
        } else if (indicatorType == QStringLiteral("ema")) {
            indicatorScores = batchCalculateEma(*closesBySymbol, emaWindow);
        } else if (indicatorType == QStringLiteral("boll")) {
            indicatorScores = batchCalculateBoll(*closesBySymbol, bollWindow, bollStdDev);
        } else if (indicatorType == QStringLiteral("kdj")) {
            if (highsBySymbol && lowsBySymbol) {
                indicatorScores = batchCalculateKdj(*highsBySymbol, *lowsBySymbol, *closesBySymbol, kdjWindow, kdjKPeriod, kdjDPeriod);
            }
        } else if (indicatorType == QStringLiteral("atr")) {
            if (highsBySymbol && lowsBySymbol) {
                indicatorScores = batchCalculateAtr(*highsBySymbol, *lowsBySymbol, *closesBySymbol, atrWindow);
            }
        } else if (indicatorType == QStringLiteral("obv")) {
            if (volumesBySymbol) {
                indicatorScores = batchCalculateObv(*closesBySymbol, *volumesBySymbol);
            }
        } else if (indicatorType == QStringLiteral("vwap")) {
            if (volumesBySymbol) {
                indicatorScores = batchCalculateVwap(*closesBySymbol, *volumesBySymbol);
            }
        } else if (indicatorType == QStringLiteral("volume_ratio")) {
            if (volumesBySymbol) {
                indicatorScores = batchCalculateVolumeRatio(*volumesBySymbol, volumeRatioWindow);
            }
        } else if (indicatorType == QStringLiteral("turnover_stability")) {
            if (turnoverSeriesBySymbol) {
                indicatorScores = batchCalculateTurnoverStability(*turnoverSeriesBySymbol);
            }
        }
        for (const auto& [symbol, score] : indicatorScores) {
            if (std::isfinite(score)) {
                scoresBySymbol[symbol].push_back(score);
            }
        }
    }

    for (const auto& symbol : runtimeSymbols) {
        const auto scoreIt = scoresBySymbol.find(symbol);
        if (scoreIt == scoresBySymbol.end() || scoreIt->second.empty()) {
            continue;
        }

        double combinedScore = safeMean(scoreIt->second);
        if (combinationMode == QStringLiteral("normalized_average") && scoreIt->second.size() > 1) {
            double magnitude = 0.0;
            for (double score : scoreIt->second) {
                magnitude += std::abs(score);
            }
            if (magnitude > 1e-12) {
                combinedScore = combinedScore / (magnitude / static_cast<double>(scoreIt->second.size()));
            }
        }
        combinedScore = std::clamp(combinedScore, -1.0, 1.0);
        if (std::isfinite(combinedScore)) {
            result.values[symbol] = combinedScore;
        }
    }

    if (result.values.empty()) {
        result.dataStatus = CalculationResult::createError("技术因子没有可用价格数据").dataStatus;
        result.metadata.set("empty_reason", json_helper::toJsonValue("技术因子没有可用价格数据"));
        result.metadata.set("technical_config_mode", json_helper::toJsonValue(technicalResolvedConfigMode.toStdString()));
        result.metadata.set("indicator_types", json_helper::toJsonValue(indicatorTypes.join(",").toStdString()));
        return result;
    }

    result.metadata.set("indicator_type", json_helper::toJsonValue(indicatorTypes.front().toStdString()));
    result.metadata.set("indicator_types", json_helper::toJsonValue(indicatorTypes.join(",").toStdString()));
    result.metadata.set("technical_config_mode", json_helper::toJsonValue(technicalResolvedConfigMode.toStdString()));
    result.metadata.set("price_type", json_helper::toJsonValue(priceField.toStdString()));
    result.metadata.set("use_volume", json_helper::toJsonValue(params_.useVolume));
    result.metadata.set("window", json_helper::toJsonValue(rsiWindow));
    result.metadata.set("technical_combination_mode", json_helper::toJsonValue(combinationMode.toStdString()));
    result.metadata.set("ma_window", json_helper::toJsonValue(maWindow));
    result.metadata.set("ema_window", json_helper::toJsonValue(emaWindow));
    result.metadata.set("boll_window", json_helper::toJsonValue(bollWindow));
    result.metadata.set("boll_std_dev", json_helper::toJsonValue(bollStdDev));
    result.metadata.set("kdj_window", json_helper::toJsonValue(kdjWindow));
    result.metadata.set("kdj_k_period", json_helper::toJsonValue(kdjKPeriod));
    result.metadata.set("kdj_d_period", json_helper::toJsonValue(kdjDPeriod));
    result.metadata.set("atr_window", json_helper::toJsonValue(atrWindow));
    result.metadata.set("macd_fast_period", json_helper::toJsonValue(macdFastPeriod));
    result.metadata.set("macd_slow_period", json_helper::toJsonValue(macdSlowPeriod));
    result.metadata.set("macd_signal_period", json_helper::toJsonValue(macdSignalPeriod));
    result.metadata.set("obv_window", json_helper::toJsonValue(obvWindow));
    result.metadata.set("vwap_window", json_helper::toJsonValue(vwapWindow));
    result.metadata.set("volume_ratio_window", json_helper::toJsonValue(volumeRatioWindow));
    result.metadata.set("turnover_stability_window", json_helper::toJsonValue(turnoverStabilityWindow));
    result.metadata.set("turnover_stability_metric", json_helper::toJsonValue(QString::fromStdString(params_.turnoverStabilityMetric).toStdString()));
    return result;
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.dataProvider = context.dataProvider;
        BatchComputationCacheScope scope(cache);
        return calculateTechnicalBody();
    }
    return calculateTechnicalBody();
}

CalculationResult ConfigurableFactor::calculateDividend(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用红利字段";
    const QString dividendConfigMode = !params_.dividendMetrics.empty()
        ? QStringLiteral("dividend_metrics")
        : (!params_.metric.empty() ? QStringLiteral("metric") : QStringLiteral("default_dividend_yield"));

    auto calculateDividendBody = [&]() -> CalculationResult {

    QStringList dividendMetrics;
    for (const std::string& rawMetric : params_.dividendMetrics) {
        const QString metric = normalizeDividendMetric(QString::fromStdString(rawMetric));
        if (!metric.isEmpty() && !dividendMetrics.contains(metric)) {
            dividendMetrics.append(metric);
        }
    }
    if (dividendMetrics.isEmpty()) {
        const QString metric = normalizedMetric().isEmpty() ? QStringLiteral("dividend_yield") : normalizedMetric();
        dividendMetrics.append(metric);
    }

    const std::vector<std::string> symbols = effectiveSymbols(context);
    std::vector<std::string> batchFields;
    batchFields.reserve(static_cast<size_t>(dividendMetrics.size()));
    std::unordered_set<std::string> seenBatchFields;
    for (const QString& metric : dividendMetrics) {
        const std::string fieldName = metric.toStdString();
        if (seenBatchFields.insert(fieldName).second) {
            batchFields.push_back(fieldName);
        }
    }

    std::unordered_map<std::string, std::unordered_map<std::string, double>> batchCrossSections;
    if (context.dataProvider && !batchFields.empty()) {
        batchCrossSections = context.dataProvider->getBatchCrossSections(context.date, symbols, batchFields);
        if (activeBatchComputationCache && activeBatchComputationCache->dataProvider == context.dataProvider) {
            for (const auto& [fieldName, symbolValues] : batchCrossSections) {
                activeBatchComputationCache->crossSectionsByKey[buildBatchCrossSectionKey(context.date, QString::fromStdString(fieldName))] = symbolValues;
            }
        }
    }

    bool hasAnyMetricData = false;
    for (const QString& metric : dividendMetrics) {
        const auto fieldIt = batchCrossSections.find(metric.toStdString());
        const std::unordered_map<std::string, double> metricMap = fieldIt != batchCrossSections.end() ? fieldIt->second : std::unordered_map<std::string, double>{};
        if (!metricMap.empty()) {
            hasAnyMetricData = true;
        }
        // Keep the per-metric maps in input order so the downstream weighting logic stays unchanged.
        batchCrossSections[metric.toStdString()] = metricMap;
    }

    if (!hasAnyMetricData) {
        result.dataStatus = CalculationResult::createError("红利因子缺少真实底层字段，已禁止使用代理模型回测").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("红利因子缺少真实底层字段，已禁止使用代理模型回测"));
        result.metadata.set("metric", json_helper::toJsonValue(dividendMetrics.front().toStdString()));
        result.metadata.set("dividend_config_mode", json_helper::toJsonValue(dividendConfigMode.toStdString()));
        return result;
    }

    for (const auto& symbol : symbols) {
        std::vector<double> scores;
        bool rejectedByYieldFloor = false;

        for (const QString& metric : dividendMetrics) {
            const auto fieldIt = batchCrossSections.find(metric.toStdString());
            const auto& directMetricMap = fieldIt != batchCrossSections.end()
                ? fieldIt->second
                : std::unordered_map<std::string, double>{};
            const auto directIt = directMetricMap.find(symbol);
            if (directIt == directMetricMap.end() || !std::isfinite(directIt->second)) {
                continue;
            }

            if (metric == QStringLiteral("dividend_yield") && params_.minDividendYield > 0.0
                    && directIt->second < params_.minDividendYield / 100.0) {
                rejectedByYieldFloor = true;
                break;
            }

            scores.push_back(directIt->second);
        }

        if (rejectedByYieldFloor || scores.empty()) {
            continue;
        }

        result.values[symbol] = safeMean(scores);
    }

    if (result.values.empty()) {
        result.dataStatus = CalculationResult::createError("红利因子没有可用分红数据").dataStatus;
        result.metadata.set("empty_reason", json_helper::toJsonValue("红利因子没有可用分红数据"));
        result.metadata.set("metric", json_helper::toJsonValue(dividendMetrics.front().toStdString()));
        result.metadata.set("dividend_config_mode", json_helper::toJsonValue(dividendConfigMode.toStdString()));
        return result;
    }

    result.metadata.set("metric", json_helper::toJsonValue(dividendMetrics.front().toStdString()));
    result.metadata.set("dividend_config_mode", json_helper::toJsonValue(dividendConfigMode.toStdString()));
    result.metadata.set("data_mode", json_helper::toJsonValue("batch_cross_section"));
    return result;
    };

    return calculateDividendBody();
}

CalculationResult ConfigurableFactor::calculateMacro(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    const QString benchmarkSymbol = context.parameters.has("benchmarkSymbol")
        ? QString::fromStdString(context.parameters.get("benchmarkSymbol").asString()).trimmed()
        : QStringLiteral("000300.SH");
    const QString priceField = normalizePriceField(QString::fromStdString(params_.priceType));
    const QString frequency = QString::fromStdString(params_.macroFrequency).trimmed().toLower();
    const int baseWindow = params_.macroWindow > 0 ? params_.macroWindow : (params_.lookbackPeriod > 0 ? params_.lookbackPeriod : params_.window);
    const int resolvedWindow = (std::max)(3, baseWindow) * macroWindowScale(frequency);

    QStringList selectedDimensions;
    const bool macroUsedDefaultFallback = params_.macroDimensions.empty()
        && params_.macroIndicators.empty()
        && params_.macroMetric.empty();
    const QString macroConfigMode = (!params_.macroDimensions.empty() || !params_.macroIndicators.empty())
        ? QStringLiteral("configured")
        : (!params_.macroMetric.empty() ? QStringLiteral("macro_metric") : QStringLiteral("default_macro_sets"));
    const QString macroResolvedConfigMode = macroUsedDefaultFallback
        ? QStringLiteral("default_macro_sets_fallback")
        : macroConfigMode;
    for (const std::string& rawDimension : params_.macroDimensions) {
        const QString dimension = normalizeMacroDimension(QString::fromStdString(rawDimension));
        if (!dimension.isEmpty() && !selectedDimensions.contains(dimension)) {
            selectedDimensions.append(dimension);
        }
    }
    if (selectedDimensions.isEmpty()) {
        const QString legacyDimension = normalizeMacroDimension(normalizeMacroMetric(QString::fromStdString(params_.macroMetric)));
        if (!legacyDimension.isEmpty()) {
            selectedDimensions.append(legacyDimension);
        }
    }
    if (selectedDimensions.isEmpty()) {
        selectedDimensions = defaultMacroDimensions();
    }

    QStringList selectedIndicators;
    for (const std::string& rawIndicator : params_.macroIndicators) {
        const QString indicator = normalizeMacroIndicator(QString::fromStdString(rawIndicator));
        if (!indicator.isEmpty() && !selectedIndicators.contains(indicator)) {
            selectedIndicators.append(indicator);
        }
    }
    if (selectedIndicators.isEmpty()) {
        for (const QString& dimension : selectedDimensions) {
            const QStringList dimensionIndicators = defaultMacroIndicatorsForDimension(dimension);
            for (const QString& indicator : dimensionIndicators) {
                if (!selectedIndicators.contains(indicator)) {
                    selectedIndicators.append(indicator);
                }
            }
        }
    }
    if (selectedIndicators.isEmpty()) {
        selectedIndicators = defaultMacroIndicators();
    }

    std::vector<std::string> benchmarkFields;
    std::unordered_set<std::string> seenBenchmarkFields;
    benchmarkFields.reserve(static_cast<size_t>(selectedIndicators.size()) * 2);
    for (const QString& indicator : selectedIndicators) {
        const MacroIndicatorSpec spec = macroIndicatorSpec(indicator);
        const QStringList candidateFields = {indicator, spec.proxyField};
        for (const QString& candidateField : candidateFields) {
            const std::string fieldName = candidateField.trimmed().toStdString();
            if (!fieldName.empty() && seenBenchmarkFields.insert(fieldName).second) {
                benchmarkFields.push_back(fieldName);
            }
        }
    }

    const auto symbols = effectiveSymbols(context);
    if (symbols.empty() || selectedIndicators.isEmpty()) {
        result.dataStatus = CalculationResult::createError("宏观因子缺少可用标的或驱动指标").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("宏观因子缺少可用标的或驱动指标"));
        return result;
    }

    CalculationContext effectiveContext = context;
    effectiveContext.symbols = symbols;

    const bool useLocalBatchCache = context.dataProvider
        && (!activeBatchComputationCache || activeBatchComputationCache->dataProvider != context.dataProvider);

    auto calculateMacroBody = [&]() -> CalculationResult {

    std::unordered_map<std::string, double> weightedScores;
    std::unordered_map<std::string, int> scoreCounts;
    QStringList activeIndicators;
    const auto priceSeriesBySymbol = fetchBatchSeriesMap(effectiveContext, priceField, resolvedWindow + 1);
    const SeriesMatrixBatch priceSeriesBatch = collectSeriesMatrix(priceSeriesBySymbol, 2);
    const Eigen::MatrixXd allSymbolReturns = buildReturnMatrix(priceSeriesBatch.values);
    std::unordered_map<std::string, std::vector<double>> benchmarkSeriesByField;
    if (context.dataProvider && !benchmarkFields.empty()) {
        const auto benchmarkBatchValues = context.dataProvider->getBatchTimeSeries(
            {benchmarkSymbol.toStdString()},
            context.date,
            resolvedWindow + 1,
            benchmarkFields);

        for (const std::string& fieldName : benchmarkFields) {
            const auto fieldIt = benchmarkBatchValues.find(fieldName);
            if (fieldIt == benchmarkBatchValues.end()) {
                continue;
            }

            const auto symbolIt = fieldIt->second.find(benchmarkSymbol.toStdString());
            if (symbolIt != fieldIt->second.end()) {
                benchmarkSeriesByField.emplace(fieldName, symbolIt->second);
            }
        }
    }

    for (const QString& indicator : selectedIndicators) {
        const MacroIndicatorSpec spec = macroIndicatorSpec(indicator);
        const double dimensionWeight = indicatorWeightForDimension(spec.dimension);

        std::vector<double> benchmarkSeries;
        const QStringList candidateFields = {indicator, spec.proxyField};
        for (const QString& candidateField : candidateFields) {
            const auto benchmarkIt = benchmarkSeriesByField.find(candidateField.trimmed().toStdString());
            if (benchmarkIt != benchmarkSeriesByField.end() && benchmarkIt->second.size() >= 2) {
                benchmarkSeries = benchmarkIt->second;
                break;
            }
        }

        if (benchmarkSeries.size() < 2) {
            continue;
        }

        const Eigen::VectorXd benchmarkReturns = buildReturnVector(benchmarkSeries);
        if (benchmarkReturns.size() < 2 || allSymbolReturns.cols() < 2 || priceSeriesBatch.symbols.empty()) {
            continue;
        }

        activeIndicators.append(indicator);
        const Eigen::VectorXd correlations = batchCorrelate(allSymbolReturns, benchmarkReturns);
        for (int row = 0; row < correlations.size(); ++row) {
            const double correlation = correlations(row);
            if (!std::isfinite(correlation)) {
                continue;
            }

            const std::string& symbol = priceSeriesBatch.symbols[static_cast<size_t>(row)];
            weightedScores[symbol] += correlation * spec.direction * dimensionWeight;
            scoreCounts[symbol] += 1;
        }
    }

    if (weightedScores.empty()) {
        result.dataStatus = CalculationResult::createError("宏观因子缺少可用代理数据").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("宏观因子缺少可用代理数据"));
        const std::string fallbackMetric = selectedIndicators.isEmpty() ? std::string() : selectedIndicators.front().toStdString();
        result.metadata.set("macro_metric", json_helper::toJsonValue(fallbackMetric));
        return result;
    }

    for (const auto& [symbol, weightedScore] : weightedScores) {
        const int count = scoreCounts[symbol];
        if (count <= 0) {
            continue;
        }
        result.values[symbol] = std::clamp(std::tanh(weightedScore / static_cast<double>(count)), -1.0, 1.0);
    }

    const double coverage = static_cast<double>(result.values.size()) / static_cast<double>((std::max)(size_t(1), symbols.size()));
    result.dataStatus.availability = result.values.size() == symbols.size() ? DataAvailability::AVAILABLE : DataAvailability::PARTIAL;
    result.dataStatus.coverage = coverage;
    result.dataStatus.message = "使用宏观代理敏感度运行时";
    const std::string macroMetricValue = activeIndicators.isEmpty() ? std::string() : activeIndicators.front().toStdString();
    result.metadata.set("macro_metric", json_helper::toJsonValue(macroMetricValue));
    result.metadata.set("macro_config_mode", json_helper::toJsonValue(macroResolvedConfigMode.toStdString()));
    result.metadata.set("macro_dimensions", json_helper::toJsonValue(selectedDimensions.join(",").toStdString()));
    result.metadata.set("macro_indicators", json_helper::toJsonValue(selectedIndicators.join(",").toStdString()));
    result.metadata.set("macro_frequency", json_helper::toJsonValue(frequency.toStdString()));
    result.metadata.set("macro_window", json_helper::toJsonValue(baseWindow));
    result.metadata.set("macro_mode", json_helper::toJsonValue("proxy_sensitivity"));
    result.metadata.set("benchmark_symbol", json_helper::toJsonValue(benchmarkSymbol.toStdString()));
    result.metadata.set("symbol_count", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    return result;
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.dataProvider = context.dataProvider;
        BatchComputationCacheScope scope(cache);
        return calculateMacroBody();
    }

    return calculateMacroBody();
}

CalculationResult ConfigurableFactor::calculateIndustry(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    const QString industryMetric = normalizeIndustryMetric(QString::fromStdString(params_.industryMetric));
    const QString sectorType = normalizeSectorType(QString::fromStdString(params_.sectorType));
    result.dataStatus = CalculationResult::createError("行业因子当前尚未实现，已禁止进入回测").dataStatus;
    result.metadata.set("error", json_helper::toJsonValue("行业因子当前尚未实现，已禁止进入回测"));
    result.metadata.set("industry_metric", json_helper::toJsonValue(industryMetric.toStdString()));
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

    const bool useLocalBatchCache = context.dataProvider
        && (!activeBatchComputationCache || activeBatchComputationCache->dataProvider != context.dataProvider);

    auto calculateSentimentBody = [&]() -> CalculationResult {
        const auto directMetricMap = currentFieldCrossSection(context, metric);
        if (!directMetricMap.empty()) {
            for (const auto& [symbol, value] : directMetricMap) {
                if (std::isfinite(value)) {
                    result.values[symbol] = value;
                }
            }
            if (result.values.empty()) {
                result.dataStatus = CalculationResult::createError("情绪因子字段存在但没有可用数值").dataStatus;
                result.metadata.set("empty_reason", json_helper::toJsonValue("情绪因子字段存在但没有可用数值"));
                result.metadata.set("metric", json_helper::toJsonValue(metric.toStdString()));
                result.metadata.set("sentiment_source", json_helper::toJsonValue(source.toStdString()));
                return result;
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
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.dataProvider = context.dataProvider;
        BatchComputationCacheScope scope(cache);
        return calculateSentimentBody();
    }

    return calculateSentimentBody();

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
    std::unordered_map<std::string, QString> sourceFieldByVariable;
    std::vector<std::string> batchFields;
    std::unordered_set<std::string> seenBatchFields;
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
        const std::string variableKey = variable.toStdString();
        sourceFieldByVariable[variableKey] = sourceField;
        const std::string fieldKey = sourceField.toStdString();
        if (!fieldKey.empty() && seenBatchFields.insert(fieldKey).second) {
            batchFields.push_back(fieldKey);
        }
    }

    std::unordered_map<std::string, std::unordered_map<std::string, double>> batchCrossSections;
    if (context.dataProvider && !batchFields.empty()) {
        batchCrossSections = context.dataProvider->getBatchCrossSections(context.date, symbols, batchFields);
        if (activeBatchComputationCache && activeBatchComputationCache->dataProvider == context.dataProvider) {
            for (const auto& [fieldName, symbolValues] : batchCrossSections) {
                activeBatchComputationCache->crossSectionsByKey[buildBatchCrossSectionKey(context.date, QString::fromStdString(fieldName))] = symbolValues;
            }
        }
    }

    for (const auto& symbol : symbols) {
        std::unordered_map<std::string, double> variableMap;
        bool missingVariable = false;
        for (const QString& variable : variables) {
            const auto* binding = findCustomVariableBinding(variable);
            const std::string variableKey = variable.toStdString();
            const auto sourceFieldIt = sourceFieldByVariable.find(variableKey);
            if (sourceFieldIt == sourceFieldByVariable.end()) {
                if (binding && binding->hasDefaultValue) {
                    variableMap[variableKey] = binding->defaultValue;
                    continue;
                }
                missingVariable = true;
                break;
            }

            const auto fieldIt = batchCrossSections.find(sourceFieldIt->second.toStdString());
            if (fieldIt == batchCrossSections.end()) {
                if (binding && binding->hasDefaultValue) {
                    variableMap[variableKey] = binding->defaultValue;
                    continue;
                }
                missingVariable = true;
                break;
            }
            const auto valueIt = fieldIt->second.find(symbol);
            if (valueIt == fieldIt->second.end()) {
                if (binding && binding->hasDefaultValue) {
                    variableMap[variableKey] = binding->defaultValue;
                    continue;
                }
                missingVariable = true;
                break;
            }
            variableMap[variableKey] = valueIt->second;
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
    const bool customUsedDefaultFallback = QString::fromStdString(params_.expression).trimmed().isEmpty();
    const QString customExpressionMode = customUsedDefaultFallback
        ? QStringLiteral("default_expression")
        : QStringLiteral("configured");
    const QString customResolvedExpressionMode = customUsedDefaultFallback
        ? QStringLiteral("default_expression_fallback")
        : customExpressionMode;
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

    const bool useLocalBatchCache = context.dataProvider
        && (!activeBatchComputationCache || activeBatchComputationCache->dataProvider != context.dataProvider);

    auto calculateCustomBody = [&]() -> CalculationResult {
        QString errorMessage;
        result.values = evaluateCustomExpression(context,
                                                 QString::fromStdString(params_.expression),
                                                 effectiveSymbols(context),
                                                 &errorMessage);
        if (result.values.empty()) {
            const QString fallbackError = errorMessage.isEmpty()
                ? QStringLiteral("自定义表达式没有可用字段数据")
                : errorMessage;
            result.dataStatus = CalculationResult::createError(fallbackError.toStdString()).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(fallbackError.toStdString()));
        } else if (!errorMessage.isEmpty()) {
            result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
        }
        result.metadata.set("expression", json_helper::toJsonValue(params_.expression));
        result.metadata.set("custom_expression_mode", json_helper::toJsonValue(customResolvedExpressionMode.toStdString()));
        result.metadata.set("variable_count", json_helper::toJsonValue(static_cast<int>(params_.variables.size())));
        result.metadata.set("symbol_count", json_helper::toJsonValue(static_cast<int>(result.values.size())));
        return result;
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.dataProvider = context.dataProvider;
        BatchComputationCacheScope scope(cache);
        return calculateCustomBody();
    }

    return calculateCustomBody();
}

} // namespace factor