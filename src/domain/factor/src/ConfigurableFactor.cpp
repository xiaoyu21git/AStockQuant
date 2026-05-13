#include "domain/factor/include/ConfigurableFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/factor_enums.h"
#include "domain/factor/include/CustomExpressionUtils.h"
#include "domain/factor/include/FactorNeutralizationUtils.h"
#include "domain/factor/include/HistoricalView.h"
#include "domain/factor/include/batch_technical_indicators.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"
#include <QVariant>
#include <QDate>
#include <QDebug>
#include <QElapsedTimer>
#include <QSet>
#include <QStringList>

#include <Eigen/Dense>

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
    std::shared_ptr<HistoricalView> historicalView;
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



FactorType resolveConfiguredFactorType(FactorType fallbackType,
                                       const foundation::json::JsonFacade& config)
{
    if (fallbackType != FactorType::UNKNOWN) {
        return fallbackType;
    }

    if (config.has("factorType")) {
        const FactorType type = factorTypeFromIndex(config.get("factorType").asInt());
        if (type != FactorType::UNKNOWN) {
            return type;
        }
    }

    return FactorType::UNKNOWN;
}

void appendUniqueField(std::vector<std::string>& fields, const std::string& field)
{
    if (field.empty()) {
        return;
    }
    if (std::find(fields.begin(), fields.end(), field) == fields.end()) {
        fields.push_back(field);
    }
}

bool configurableFactorNeedsHistoricalNeutralization(FactorType factorType,
                                                     const ConfigurableFactor::Params& params)
{
    return params.neutralizationEnabled
            && (factorType == FactorType::GROWTH
                || factorType == FactorType::LIQUIDITY
                || factorType == FactorType::DIVIDEND);
}

void applyConfigurableStandardization(const QString& standardization,
                                      std::unordered_map<std::string, double>& values)
{
    if (values.empty()) {
        return;
    }

    if (standardization == QStringLiteral("percentile")) {
        std::vector<std::pair<std::string, double>> ranked(values.begin(), values.end());
        std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
            return left.second < right.second;
        });
        if (ranked.size() == 1) {
            values[ranked.front().first] = 1.0;
            return;
        }
        for (size_t index = 0; index < ranked.size(); ++index) {
            values[ranked[index].first] = static_cast<double>(index) / static_cast<double>(ranked.size() - 1);
        }
        return;
    }

    std::vector<double> finiteValues;
    finiteValues.reserve(values.size());
    for (const auto& [symbol, value] : values) {
        Q_UNUSED(symbol);
        if (std::isfinite(value)) {
            finiteValues.push_back(value);
        }
    }
    if (finiteValues.empty()) {
        return;
    }

    if (standardization == QStringLiteral("zscore")) {
        const double meanValue = std::accumulate(finiteValues.begin(), finiteValues.end(), 0.0)
            / static_cast<double>(finiteValues.size());
        double variance = 0.0;
        for (double value : finiteValues) {
            const double delta = value - meanValue;
            variance += delta * delta;
        }
        const double stdev = std::sqrt(variance / static_cast<double>(finiteValues.size()));
        if (stdev > 1e-12) {
            for (auto& [symbol, value] : values) {
                Q_UNUSED(symbol);
                value = (value - meanValue) / stdev;
            }
        }
        return;
    }

    if (standardization == QStringLiteral("minmax")) {
        const auto [minIt, maxIt] = std::minmax_element(finiteValues.begin(), finiteValues.end());
        const double range = *maxIt - *minIt;
        if (range > 1e-12) {
            for (auto& [symbol, value] : values) {
                Q_UNUSED(symbol);
                value = (value - *minIt) / range;
            }
        }
    }
}

bool applyHistoricalViewIndustrySizeNeutralization(const CalculationContext& context,
                                                   std::unordered_map<std::string, double>& values,
                                                   QString* errorMessage)
{
    return factor::neutralization::applyIndustrySizeNeutralization(context, values, errorMessage);
}

QString normalizeConfiguredTypeText(const QString& rawType)
{
    return rawType.trimmed().toLower();
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

std::vector<TechnicalIndicator> normalizeTechnicalIndicatorList(const QVariant& value)
{
    std::vector<TechnicalIndicator> indicators;
    const auto appendIndicator = [&indicators](TechnicalIndicator indicator) {
        if (indicator != TechnicalIndicator::UNKNOWN
                && std::find(indicators.begin(), indicators.end(), indicator) == indicators.end()) {
            indicators.push_back(indicator);
        }
    };

    if (value.canConvert<QVariantList>()) {
        const QVariantList list = value.toList();
        indicators.reserve(static_cast<size_t>(list.size()));
        for (const QVariant& item : list) {
            appendIndicator(technicalIndicatorFromString(item.toString()));
        }
    } else if (value.isValid()) {
        appendIndicator(technicalIndicatorFromString(value.toString()));
    }

    return indicators;
}

std::vector<TechnicalIndicator> normalizeTechnicalIndicatorList(const foundation::json::JsonFacade& value)
{
    std::vector<TechnicalIndicator> indicators;
    const auto appendIndicator = [&indicators](TechnicalIndicator indicator) {
        if (indicator != TechnicalIndicator::UNKNOWN
                && std::find(indicators.begin(), indicators.end(), indicator) == indicators.end()) {
            indicators.push_back(indicator);
        }
    };

    if (value.isArray()) {
        indicators.reserve(value.size());
        for (size_t index = 0; index < value.size(); ++index) {
            appendIndicator(technicalIndicatorFromString(
                QString::fromStdString(requireStringItem(value, index, "technicalIndicators"))));
        }
    } else {
        if (!value.isString()) {
            throw std::runtime_error("technicalIndicators 不是字符串字段");
        }
        appendIndicator(technicalIndicatorFromString(QString::fromStdString(value.asString())));
    }

    return indicators;
}

TechnicalPriceType technicalPriceTypeFromString(const QString& rawPriceType)
{
    const QString normalized = rawPriceType.trimmed().toLower();
    if (normalized == QString(factor::bridge::MarketBarFieldKeys::CLOSE)) {
        return TechnicalPriceType::CLOSE;
    }
    if (normalized == QString(factor::bridge::MarketBarFieldKeys::OPEN)) {
        return TechnicalPriceType::OPEN;
    }
    if (normalized == QString(factor::bridge::MarketBarFieldKeys::HIGH)) {
        return TechnicalPriceType::HIGH;
    }
    if (normalized == QString(factor::bridge::MarketBarFieldKeys::LOW)) {
        return TechnicalPriceType::LOW;
    }
    return TechnicalPriceType::UNKNOWN;
}

QString priceFieldForType(TechnicalPriceType priceType)
{
    switch (priceType) {
    case TechnicalPriceType::CLOSE:
        return QString(factor::bridge::MarketBarFieldKeys::CLOSE);
    case TechnicalPriceType::OPEN:
        return QString(factor::bridge::MarketBarFieldKeys::OPEN);
    case TechnicalPriceType::HIGH:
        return QString(factor::bridge::MarketBarFieldKeys::HIGH);
    case TechnicalPriceType::LOW:
        return QString(factor::bridge::MarketBarFieldKeys::LOW);
    default:
        return {};
    }
}

SentimentSource sentimentSourceFromString(const QString& rawSource)
{
    const QString normalized = rawSource.trimmed().toLower();
    if (normalized == QStringLiteral("news_sentiment")) {
        return SentimentSource::NEWS;
    }
    if (normalized == QStringLiteral("social_media")) {
        return SentimentSource::SOCIAL_MEDIA;
    }
    if (normalized == QStringLiteral("analyst_rating")) {
        return SentimentSource::ANALYST_RATING;
    }
    if (normalized == QStringLiteral("market_sentiment")) {
        return SentimentSource::MARKET;
    }
    if (normalized == QStringLiteral("policy")) {
        return SentimentSource::POLICY;
    }
    if (normalized == QStringLiteral("alternative") || normalized == QStringLiteral("alternative_data")) {
        return SentimentSource::ALTERNATIVE;
    }
    if (normalized == QStringLiteral("derivatives") || normalized == QStringLiteral("derivatives_data")) {
        return SentimentSource::DERIVATIVES;
    }
    return SentimentSource::UNKNOWN;
}

foundation::json::JsonFacade technicalIndicatorArrayJson(const std::vector<TechnicalIndicator>& indicators)
{
    auto result = foundation::json::JsonFacade::createArray();
    std::unordered_set<int> seen;
    for (const TechnicalIndicator indicator : indicators) {
        if (indicator == TechnicalIndicator::UNKNOWN) {
            continue;
        }
        const int value = static_cast<int>(indicator);
        if (seen.insert(value).second) {
            result.push_back(json_helper::toJsonValue(value));
        }
    }
    return result;
}

foundation::json::JsonFacade macroDimensionArrayJson(const std::vector<MacroDimension>& dimensions)
{
    auto result = foundation::json::JsonFacade::createArray();
    std::unordered_set<int> seen;
    for (const MacroDimension dimension : dimensions) {
        if (dimension == MacroDimension::UNKNOWN) {
            continue;
        }
        const int value = static_cast<int>(dimension);
        if (seen.insert(value).second) {
            result.push_back(json_helper::toJsonValue(value));
        }
    }
    return result;
}

foundation::json::JsonFacade macroIndicatorArrayJson(const std::vector<MacroIndicator>& indicators)
{
    auto result = foundation::json::JsonFacade::createArray();
    std::unordered_set<int> seen;
    for (const MacroIndicator indicator : indicators) {
        if (indicator == MacroIndicator::UNKNOWN) {
            continue;
        }
        const int value = static_cast<int>(indicator);
        if (seen.insert(value).second) {
            result.push_back(json_helper::toJsonValue(value));
        }
    }
    return result;
}

QString macroIndicatorFieldName(MacroIndicator indicator)
{
    switch (indicator) {
    case MacroIndicator::INDUSTRIAL_ADDED_VALUE_YOY:
        return QStringLiteral("industrial_added_value_yoy");
    case MacroIndicator::MANUFACTURING_PMI:
        return QStringLiteral("manufacturing_pmi");
    case MacroIndicator::GDP_YOY:
        return QStringLiteral("gdp_yoy");
    case MacroIndicator::CPI_YOY:
        return QStringLiteral("cpi_yoy");
    case MacroIndicator::PPI_YOY:
        return QStringLiteral("ppi_yoy");
    case MacroIndicator::M2_YOY:
        return QStringLiteral("m2_yoy");
    case MacroIndicator::SOCIAL_FINANCING_STOCK_YOY:
        return QStringLiteral("social_financing_stock_yoy");
    case MacroIndicator::M1_M2_SPREAD:
        return QStringLiteral("m1_m2_spread");
    case MacroIndicator::TEN_YEAR_BOND_YIELD:
        return QStringLiteral("ten_year_bond_yield");
    case MacroIndicator::SHIBOR_3M:
        return QStringLiteral("shibor_3m");
    case MacroIndicator::LPR_1Y:
        return QStringLiteral("lpr_1y");
    case MacroIndicator::RESERVE_REQUIREMENT_RATIO:
        return QStringLiteral("reserve_requirement_ratio");
    case MacroIndicator::AA_CREDIT_SPREAD:
        return QStringLiteral("aa_credit_spread");
    case MacroIndicator::VIX_PROXY:
        return QStringLiteral("vix_proxy");
    default:
        return {};
    }
}

struct MacroIndicatorSpec
{
    MacroDimension dimension;
    QString proxyField;
    double direction = 1.0;
};

QString macroIndicatorProxyField(MacroIndicator indicator)
{
    switch (indicator) {
    case MacroIndicator::INDUSTRIAL_ADDED_VALUE_YOY:
    case MacroIndicator::MANUFACTURING_PMI:
    case MacroIndicator::GDP_YOY:
    case MacroIndicator::CPI_YOY:
    case MacroIndicator::PPI_YOY:
        return QString(factor::bridge::MarketBarFieldKeys::CLOSE);
    case MacroIndicator::M2_YOY:
    case MacroIndicator::SOCIAL_FINANCING_STOCK_YOY:
    case MacroIndicator::M1_M2_SPREAD:
        return QString(factor::bridge::MarketBarFieldKeys::TURNOVER_RATE);
    case MacroIndicator::TEN_YEAR_BOND_YIELD:
    case MacroIndicator::SHIBOR_3M:
        return QString(factor::bridge::MarketBarFieldKeys::PE_RATIO);
    case MacroIndicator::LPR_1Y:
    case MacroIndicator::RESERVE_REQUIREMENT_RATIO:
        return QString(factor::bridge::MarketBarFieldKeys::PB_RATIO);
    case MacroIndicator::AA_CREDIT_SPREAD:
    case MacroIndicator::VIX_PROXY:
        return QString(factor::bridge::MarketBarFieldKeys::VOLUME);
    default:
        return {};
    }
}

double macroIndicatorDirection(MacroIndicator indicator)
{
    switch (indicator) {
    case MacroIndicator::CPI_YOY:
    case MacroIndicator::PPI_YOY:
    case MacroIndicator::TEN_YEAR_BOND_YIELD:
    case MacroIndicator::SHIBOR_3M:
    case MacroIndicator::LPR_1Y:
    case MacroIndicator::RESERVE_REQUIREMENT_RATIO:
    case MacroIndicator::AA_CREDIT_SPREAD:
    case MacroIndicator::VIX_PROXY:
        return -1.0;
    default:
        return 1.0;
    }
}

MacroIndicatorSpec macroIndicatorSpec(MacroIndicator indicator)
{
    return {macroIndicatorDimension(indicator), macroIndicatorProxyField(indicator), macroIndicatorDirection(indicator)};
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

double indicatorWeightForDimension(MacroDimension dimension)
{
    if (dimension == MacroDimension::GROWTH) {
        return 1.0;
    }
    if (dimension == MacroDimension::INFLATION) {
        return 0.9;
    }
    if (dimension == MacroDimension::CREDIT) {
        return 1.1;
    }
    if (dimension == MacroDimension::RATES) {
        return 1.0;
    }
    if (dimension == MacroDimension::POLICY) {
        return 0.9;
    }
    if (dimension == MacroDimension::RISK_APPETITE) {
        return 1.1;
    }
    return 1.0;
}

IndustryMetric industryMetricFromString(const QString& rawMetric)
{
    const QString normalized = rawMetric.trimmed().toLower();
    if (normalized == QStringLiteral("industry_prosperity")) {
        return IndustryMetric::INDUSTRY_PROSPERITY;
    }
    if (normalized == QStringLiteral("industry_momentum")) {
        return IndustryMetric::INDUSTRY_MOMENTUM;
    }
    if (normalized == QStringLiteral("industry_concentration")) {
        return IndustryMetric::INDUSTRY_CONCENTRATION;
    }
    return IndustryMetric::UNKNOWN;
}

QString industryMetricField(IndustryMetric metric)
{
    switch (metric) {
    case IndustryMetric::INDUSTRY_PROSPERITY:
        return QStringLiteral("industry_prosperity");
    case IndustryMetric::INDUSTRY_MOMENTUM:
        return QStringLiteral("industry_momentum");
    case IndustryMetric::INDUSTRY_CONCENTRATION:
        return QStringLiteral("industry_concentration");
    default:
        return {};
    }
}

LiquidityMetric liquidityMetricFromString(const QString& rawMetric)
{
    const QString normalized = rawMetric.trimmed().toLower();
    if (normalized == QStringLiteral("turnover_rate")) {
        return LiquidityMetric::TURNOVER_RATE;
    }
    if (normalized == QStringLiteral("volume")) {
        return LiquidityMetric::VOLUME;
    }
    if (normalized == QStringLiteral("amihud_illiquidity")) {
        return LiquidityMetric::AMIHUD_ILLIQUIDITY;
    }
    if (normalized == QStringLiteral("amplitude")) {
        return LiquidityMetric::AMPLITUDE;
    }
    return LiquidityMetric::UNKNOWN;
}

QString liquidityMetricField(LiquidityMetric metric)
{
    switch (metric) {
    case LiquidityMetric::TURNOVER_RATE:
        return QString(factor::bridge::MarketBarFieldKeys::TURNOVER_RATE);
    case LiquidityMetric::VOLUME:
        return QString(factor::bridge::MarketBarFieldKeys::VOLUME);
    case LiquidityMetric::AMIHUD_ILLIQUIDITY:
        return QStringLiteral("amihud_illiquidity");
    case LiquidityMetric::AMPLITUDE:
        return QString(factor::bridge::MarketBarFieldKeys::AMPLITUDE);
    default:
        return {};
    }
}

DividendMetric dividendMetricFromString(const QString& rawMetric)
{
    const QString normalized = rawMetric.trimmed().toLower();
    if (normalized == QStringLiteral("dividend_yield")) {
        return DividendMetric::DIVIDEND_YIELD;
    }
    if (normalized == QStringLiteral("payout_ratio")) {
        return DividendMetric::PAYOUT_RATIO;
    }
    if (normalized == QStringLiteral("dividend_stability")) {
        return DividendMetric::DIVIDEND_STABILITY;
    }
    return DividendMetric::UNKNOWN;
}

QString dividendMetricField(DividendMetric metric)
{
    switch (metric) {
    case DividendMetric::DIVIDEND_YIELD:
        return QString(factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD);
    case DividendMetric::PAYOUT_RATIO:
        return QString(factor::bridge::FinancialFieldKeys::PAYOUT_RATIO);
    case DividendMetric::DIVIDEND_STABILITY:
        return QString(factor::bridge::FinancialFieldKeys::DIVIDEND_STABILITY);
    default:
        return {};
    }
}

SentimentMetric sentimentMetricFromString(const QString& rawMetric)
{
    const QString normalized = rawMetric.trimmed().toLower();
    if (normalized == QStringLiteral("sentiment_score")) {
        return SentimentMetric::SENTIMENT_SCORE;
    }
    if (normalized == QStringLiteral("social_sentiment")) {
        return SentimentMetric::SOCIAL_SENTIMENT;
    }
    if (normalized == QStringLiteral("investor_sentiment")) {
        return SentimentMetric::INVESTOR_SENTIMENT;
    }
    if (normalized == QStringLiteral("market_sentiment")) {
        return SentimentMetric::MARKET_SENTIMENT;
    }
    return SentimentMetric::UNKNOWN;
}

QString sentimentMetricField(SentimentMetric metric)
{
    switch (metric) {
    case SentimentMetric::SENTIMENT_SCORE:
        return QString(factor::bridge::NewsFieldKeys::SENTIMENT_SCORE);
    case SentimentMetric::SOCIAL_SENTIMENT:
        return QString(factor::bridge::NewsFieldKeys::SOCIAL_SENTIMENT);
    case SentimentMetric::INVESTOR_SENTIMENT:
        return QString(factor::bridge::NewsFieldKeys::INVESTOR_SENTIMENT);
    case SentimentMetric::MARKET_SENTIMENT:
        return QString(factor::bridge::NewsFieldKeys::MARKET_SENTIMENT);
    default:
        return {};
    }
}

QString normalizeSectorType(const QString& rawSectorType)
{
    const QString normalized = rawSectorType.trimmed().toLower();
    if (normalized == QStringLiteral("sw_l1")) {
        return QStringLiteral("sw_l1");
    }
    if (normalized == QStringLiteral("sw_l2")) {
        return QStringLiteral("sw_l2");
    }
    if (normalized == QStringLiteral("citic_l1")) {
        return QStringLiteral("citic_l1");
    }
    if (normalized == QStringLiteral("citic_l2")) {
        return QStringLiteral("citic_l2");
    }
    return normalized;
}

std::vector<TechnicalIndicator> resolvedTechnicalIndicators(const factor::ConfigurableFactor::Params& params)
{
    std::vector<TechnicalIndicator> indicatorTypes;

    for (const TechnicalIndicator indicatorType : params.technicalIndicators) {
        if (indicatorType != TechnicalIndicator::UNKNOWN
                && std::find(indicatorTypes.begin(), indicatorTypes.end(), indicatorType) == indicatorTypes.end()) {
            indicatorTypes.push_back(indicatorType);
        }
    }

    return indicatorTypes;
}

QString resolvedTechnicalTurnoverMetricField(const factor::ConfigurableFactor::Params& params)
{
    const QString metric = QString::fromStdString(params.turnoverStabilityMetric).trimmed().toLower();
    if (metric == QString(factor::bridge::MarketBarFieldKeys::VOLUME)) {
        return QString(factor::bridge::MarketBarFieldKeys::VOLUME);
    }
    return QString(factor::bridge::MarketBarFieldKeys::TURNOVER_RATE);
}

void appendUniqueRequirementField(std::vector<std::string>& fields, const QString& field)
{
    const std::string normalizedField = field.trimmed().toStdString();
    if (normalizedField.empty()) {
        return;
    }
    if (std::find(fields.begin(), fields.end(), normalizedField) == fields.end()) {
        fields.push_back(normalizedField);
    }
}

DataRequirements derivedTechnicalDataRequirements(const factor::ConfigurableFactor::Params& params)
{
    DataRequirements requirements;
    const std::vector<TechnicalIndicator> indicatorTypes = resolvedTechnicalIndicators(params);
    const bool needHighLowSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesHighLow);
    const bool needVolumeSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesVolume);
    const bool needPriceSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesPriceField);
    const bool needTurnoverSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesTurnoverMetric);

    if (needPriceSeries) {
        const QString priceField = priceFieldForType(params.technicalPriceType);
        if (priceField.isEmpty()) {
            return requirements;
        }
        appendUniqueRequirementField(
            requirements.requiredFields,
            priceField);
    }
    if (needHighLowSeries) {
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::HIGH));
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::LOW));
    }
    if (needVolumeSeries) {
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::VOLUME));
    }
    if (needTurnoverSeries) {
        appendUniqueRequirementField(requirements.requiredFields, resolvedTechnicalTurnoverMetricField(params));
    }
    if (params.neutralizationEnabled) {
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::SymbolInfoFieldKeys::INDUSTRY_CODE));
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP));
    }

    return requirements;
}

DataRequirements derivedIndustryDataRequirements(const factor::ConfigurableFactor::Params& params)
{
    DataRequirements requirements;
    const QString metric = industryMetricField(params.industryMetricKind);
    if (!metric.isEmpty()) {
        appendUniqueRequirementField(requirements.requiredFields, metric);
    }
    if (params.neutralizationEnabled) {
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::SymbolInfoFieldKeys::INDUSTRY_CODE));
        appendUniqueRequirementField(requirements.requiredFields, QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP));
    }
    return requirements;
}

BoundaryRules derivedTechnicalBoundaryRules(const factor::ConfigurableFactor::Params& params,
                                           const BoundaryRules& baseRules)
{
    BoundaryRules rules = baseRules;

    const std::vector<TechnicalIndicator> indicatorTypes = resolvedTechnicalIndicators(params);
    const int rsiWindow = (std::max)(2, params.rsiWindow);
    const int maWindow = (std::max)(2, params.maWindow);
    const int emaWindow = (std::max)(2, params.emaWindow);
    const int bollWindow = (std::max)(2, params.bollWindow);
    const int kdjWindow = (std::max)(2, params.kdjWindow);
    const int atrWindow = (std::max)(2, params.atrWindow);
    const int macdFastPeriod = (std::max)(2, params.macdFastPeriod);
    const int macdSlowPeriod = (std::max)(macdFastPeriod + 1, params.macdSlowPeriod);
    const int macdSignalPeriod = (std::max)(2, params.macdSignalPeriod);
    const int obvWindow = (std::max)(2, params.obvWindow);
    const int vwapWindow = (std::max)(2, params.vwapWindow);
    const int volumeRatioWindow = (std::max)(2, params.volumeRatioWindow);
    const int turnoverStabilityWindow = (std::max)(2, params.turnoverStabilityWindow);

    int minDataPoints = 1;
    if (std::any_of(indicatorTypes.begin(), indicatorTypes.end(), [](TechnicalIndicator indicator) { return indicator == TechnicalIndicator::RSI; })) {
        minDataPoints = (std::max)(minDataPoints, rsiWindow + 1);
    }
    if (std::any_of(indicatorTypes.begin(), indicatorTypes.end(), [](TechnicalIndicator indicator) { return indicator == TechnicalIndicator::MACD; })) {
        minDataPoints = (std::max)(minDataPoints, macdSlowPeriod + macdSignalPeriod + 5);
    }
    if (std::any_of(indicatorTypes.begin(), indicatorTypes.end(), [](TechnicalIndicator indicator) { return indicator == TechnicalIndicator::MA; })) {
        minDataPoints = (std::max)(minDataPoints, maWindow);
    }
    if (std::any_of(indicatorTypes.begin(), indicatorTypes.end(), [](TechnicalIndicator indicator) { return indicator == TechnicalIndicator::EMA; })) {
        minDataPoints = (std::max)(minDataPoints, emaWindow);
    }
    if (std::any_of(indicatorTypes.begin(), indicatorTypes.end(), [](TechnicalIndicator indicator) { return indicator == TechnicalIndicator::BOLL; })) {
        minDataPoints = (std::max)(minDataPoints, bollWindow);
    }
    if (std::any_of(indicatorTypes.begin(), indicatorTypes.end(), [](TechnicalIndicator indicator) { return indicator == TechnicalIndicator::KDJ; })) {
        minDataPoints = (std::max)(minDataPoints, kdjWindow + 1);
    }
    if (std::any_of(indicatorTypes.begin(), indicatorTypes.end(), [](TechnicalIndicator indicator) { return indicator == TechnicalIndicator::ATR; })) {
        minDataPoints = (std::max)(minDataPoints, atrWindow + 1);
    }
    if (std::any_of(indicatorTypes.begin(), indicatorTypes.end(), [](TechnicalIndicator indicator) { return indicator == TechnicalIndicator::OBV; })) {
        minDataPoints = (std::max)(minDataPoints, obvWindow + 1);
    }
    if (std::any_of(indicatorTypes.begin(), indicatorTypes.end(), [](TechnicalIndicator indicator) { return indicator == TechnicalIndicator::VWAP; })) {
        minDataPoints = (std::max)(minDataPoints, vwapWindow + 1);
    }
    if (std::any_of(indicatorTypes.begin(), indicatorTypes.end(), [](TechnicalIndicator indicator) { return indicator == TechnicalIndicator::VOLUME_RATIO; })) {
        minDataPoints = (std::max)(minDataPoints, volumeRatioWindow + 1);
    }
    if (std::any_of(indicatorTypes.begin(), indicatorTypes.end(), [](TechnicalIndicator indicator) { return indicator == TechnicalIndicator::TURNOVER_STABILITY; })) {
        minDataPoints = (std::max)(minDataPoints, turnoverStabilityWindow);
    }

    rules.minDataPoints = minDataPoints;
    return rules;
}

QString normalizeConfigurableFrequency(const std::string& frequency)
{
    const QString normalized = QString::fromStdString(frequency).trimmed().toLower();
    if (normalized == QStringLiteral("weekly")) {
        return QStringLiteral("weekly");
    }
    if (normalized == QStringLiteral("monthly")) {
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

double normalizeDividendYieldFloor(double rawValue)
{
    return rawValue > 1.0 ? rawValue / 100.0 : rawValue;
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

double safeFiniteMean(const std::vector<double>& values)
{
    double sum = 0.0;
    int count = 0;
    for (double value : values) {
        if (!std::isfinite(value)) {
            continue;
        }
        sum += value;
        ++count;
    }
    if (count == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return sum / static_cast<double>(count);
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

// Growth 因子单指标映射：把配置指标转换为真正读取的财务字段。
QString growthFieldForMetric(const QString& metric)
{
    if (metric == QStringLiteral("revenue_growth")) {
        return QString(factor::bridge::FinancialFieldKeys::TOTAL_REVENUE);
    }
    if (metric == QStringLiteral("net_profit_growth")) {
        return QString(factor::bridge::FinancialFieldKeys::NET_PROFIT);
    }
    if (metric == QStringLiteral("delta_roe")) {
        return QString(factor::bridge::FinancialFieldKeys::ROE);
    }
    if (metric == QStringLiteral("sue")) {
        return QString(factor::bridge::FinancialFieldKeys::EPS);
    }
    return {};
}

// Growth 因子单指标计算：同比增速，依赖最新值与上一期值。
template <typename LatestFinancialSeriesResolver>
std::unordered_map<std::string, double> computeGrowthYoYScoreMap(
    LatestFinancialSeriesResolver&& latestFinancialSeries,
    const CalculationContext& context,
    const QString& effectiveDate,
    const QString& field)
{
    std::unordered_map<std::string, double> scores;
    const auto seriesMap = latestFinancialSeries(context, field, effectiveDate, 2);
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
}

// Growth 因子单指标计算：ROE 差分，直接使用最新值减上一期值。
template <typename LatestFinancialSeriesResolver>
std::unordered_map<std::string, double> computeGrowthDifferenceScoreMap(
    LatestFinancialSeriesResolver&& latestFinancialSeries,
    const CalculationContext& context,
    const QString& effectiveDate,
    const QString& field)
{
    std::unordered_map<std::string, double> scores;
    const auto seriesMap = latestFinancialSeries(context, field, effectiveDate, 2);
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
}

// Growth 因子单指标计算：SUE 代理，基于 EPS 差分序列的标准化结果。
template <typename LatestFinancialSeriesResolver>
std::unordered_map<std::string, double> computeGrowthSueProxyScoreMap(
    LatestFinancialSeriesResolver&& latestFinancialSeries,
    const CalculationContext& context,
    const QString& effectiveDate)
{
    std::unordered_map<std::string, double> scores;
    const auto seriesMap = latestFinancialSeries(context, QString(factor::bridge::FinancialFieldKeys::EPS), effectiveDate, 5);
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
}

// Growth 因子统一标准化：按配置的 percentile / zscore / minmax 归一化单指标结果。
void normalizeGrowthScoreMap(const QString& standardization, std::unordered_map<std::string, double>& scores)
{
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
}

bool isFinancialMetricField(const QString& rawField)
{
    static const QSet<QString> financialFields = {
        QString(factor::bridge::FinancialFieldKeys::ROE),
        QString(factor::bridge::FinancialFieldKeys::ROA),
        QString(factor::bridge::FinancialFieldKeys::PROFIT_MARGIN),
        QString(factor::bridge::FinancialFieldKeys::GROSS_MARGIN),
        QString(factor::bridge::FinancialFieldKeys::OPERATING_MARGIN),
        QString(factor::bridge::FinancialFieldKeys::NET_PROFIT),
        QString(factor::bridge::FinancialFieldKeys::EQUITY),
        QString(factor::bridge::FinancialFieldKeys::TOTAL_ASSETS),
        QString(factor::bridge::FinancialFieldKeys::EPS),
        QString(factor::bridge::FinancialFieldKeys::TOTAL_REVENUE),
        QString(factor::bridge::FinancialFieldKeys::PAYOUT_RATIO),
        QString(factor::bridge::FinancialFieldKeys::OPERATING_CASH_FLOW)
    };
    return financialFields.contains(rawField.trimmed().toLower());
}

bool isNewsMetricField(const QString& rawField)
{
    static const QSet<QString> newsFields = {
        QString(factor::bridge::NewsFieldKeys::SENTIMENT_SCORE),
        QString(factor::bridge::NewsFieldKeys::MARKET_SENTIMENT),
        QString(factor::bridge::NewsFieldKeys::INVESTOR_SENTIMENT),
        QString(factor::bridge::NewsFieldKeys::SECTOR_SENTIMENT),
        QString(factor::bridge::NewsFieldKeys::THEME_SENTIMENT),
        QString(factor::bridge::NewsFieldKeys::SOCIAL_SENTIMENT),
        QString(factor::bridge::NewsFieldKeys::NEWS_COUNT)
    };
    return newsFields.contains(rawField.trimmed().toLower());
}

bool isPolicyMetricField(const QString& rawField)
{
    static const QSet<QString> policyFields = {
        QString(factor::bridge::PolicyFieldKeys::POLICY_SCORE),
        QString(factor::bridge::PolicyFieldKeys::POLICY_STRENGTH),
        QString(factor::bridge::PolicyFieldKeys::POLICY_COUNT)
    };
    return policyFields.contains(rawField.trimmed().toLower());
}

bool isAlternativeMetricField(const QString& rawField)
{
    static const QSet<QString> alternativeFields = {
        QString(factor::bridge::AlternativeFieldKeys::HOT_RANK),
        QString(factor::bridge::AlternativeFieldKeys::POPULARITY_SCORE),
        QString(factor::bridge::AlternativeFieldKeys::COMMENT_COUNT),
        QString(factor::bridge::AlternativeFieldKeys::COMMENT_SENTIMENT)
    };
    return alternativeFields.contains(rawField.trimmed().toLower());
}

bool isDerivativesMetricField(const QString& rawField)
{
    static const QSet<QString> derivativesFields = {
        QString(factor::bridge::DerivativesFieldKeys::FUTURES_CLOSE),
        QString(factor::bridge::DerivativesFieldKeys::FUTURES_VOLUME),
        QString(factor::bridge::DerivativesFieldKeys::OPEN_INTEREST),
        QString(factor::bridge::DerivativesFieldKeys::BASIS),
        QString(factor::bridge::DerivativesFieldKeys::BASIS_RATE)
    };
    return derivativesFields.contains(rawField.trimmed().toLower());
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
    if (window <= 0 || field.trimmed().isEmpty() || !context.historicalView) {
        return resolvedSeries;
    }

    const std::string fieldName = field.toStdString();
    if (!context.historicalView->hasField(fieldName)) {
        return resolvedSeries;
    }

    const bool useBulkSymbols = !context.symbols.empty();
    const std::string batchKey = buildBatchSeriesKey(context.date, field, window, std::string(), useBulkSymbols);

    if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
        const auto cacheIt = activeBatchComputationCache->seriesByKey.find(batchKey);
        if (cacheIt != activeBatchComputationCache->seriesByKey.end()) {
            return cacheIt->second;
        }
    }

    const std::vector<std::string> batchSymbols = useBulkSymbols
        ? context.symbols
        : context.historicalView->getAvailableSymbols(context.date);
    if (batchSymbols.empty()) {
        return resolvedSeries;
    }

    const auto anchoredBatchValues = context.historicalView->getBatchTimeSeries(
        batchSymbols,
        context.date,
        window,
        {fieldName});

    const auto fieldIt = anchoredBatchValues.find(fieldName);
    if (fieldIt != anchoredBatchValues.end()) {
        resolvedSeries = fieldIt->second;
    }

    if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
        activeBatchComputationCache->seriesByKey[batchKey] = resolvedSeries;
    }

    return resolvedSeries;
}

}

void ConfigurableFactor::Params::fromJson(const foundation::json::JsonFacade& json, FactorType factorType)
{
    configuredType = factorType;
    const bool isGrowth = factorType == FactorType::GROWTH;
    const bool isDividend = factorType == FactorType::DIVIDEND;
    const bool isTechnical = factorType == FactorType::TECHNICAL;
    const bool isLiquidity = factorType == FactorType::LIQUIDITY;
    const bool isMacro = factorType == FactorType::MACRO;
    const bool isIndustry = factorType == FactorType::INDUSTRY;
    const bool isSentiment = factorType == FactorType::SENTIMENT;
    const bool isCustom = factorType == FactorType::CUSTOM;
    const bool hasTechnicalIndicatorConfig = json.has("technicalIndicators");
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

    if (json.has("metric")) {
        metric = requireStringField(json, "metric");
    }

    if (!isGrowth) {
        growthMetrics.clear();
        growthWeights.clear();
    }
    if (!isDividend) {
        dividendMetrics.clear();
    }
    if (!isTechnical) {
        technicalIndicators.clear();
        technicalPriceType = TechnicalPriceType::UNKNOWN;
    }
    if (!isMacro) {
        macroDimensions.clear();
        macroIndicators.clear();
    }
    if (!isIndustry) {
        sectorType.clear();
        industryMetricKind = IndustryMetric::UNKNOWN;
    }
    if (!isSentiment) {
        sentimentSource = SentimentSource::UNKNOWN;
    }
    if (!isCustom) {
        expression.clear();
        variables.clear();
    }

    if (isTechnical && hasTechnicalIndicatorConfig) {
        technicalIndicators.clear();
        technicalPriceType = TechnicalPriceType::UNKNOWN;
    }

    if (isLiquidity && json.has("metric")) {
        liquidityMetric = liquidityMetricFromString(QString::fromStdString(metric));
        if (liquidityMetric == LiquidityMetric::UNKNOWN) {
            throw std::runtime_error("metric 不是有效的流动性枚举");
        }
    }
    if (isDividend && json.has("metric")) {
        dividendMetric = dividendMetricFromString(QString::fromStdString(metric));
        if (dividendMetric == DividendMetric::UNKNOWN) {
            throw std::runtime_error("metric 不是有效的红利枚举");
        }
    }
    if (isSentiment && json.has("metric")) {
        sentimentMetric = sentimentMetricFromString(QString::fromStdString(metric));
        if (sentimentMetric == SentimentMetric::UNKNOWN) {
            throw std::runtime_error("metric 不是有效的情绪枚举");
        }
    }
    if (isIndustry && json.has("industryMetric")) {
        industryMetricKind = industryMetricFromString(QString::fromStdString(requireStringField(json, "industryMetric")));
        if (industryMetricKind == IndustryMetric::UNKNOWN) {
            throw std::runtime_error("industryMetric 不是有效的行业枚举");
        }
    }

    if (isGrowth && hasGrowthIndicatorConfig) {
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

                if (!hasExplicitWeights || !weights.isArray() || weights.size() != metrics.size()) {
                    validGrowthConfig = false;
                }

                for (size_t index = 0; index < metrics.size(); ++index) {
                    const QString growthMetric = normalizeGrowthMetricText(
                        QString::fromStdString(requireStringItem(metrics, index, "growthMetrics")));
                    const double weight = hasExplicitWeights ? weights.at(index).asDouble() : 0.0;
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

    if (isDividend && json.has("dividendMetrics")) {
        dividendMetrics.clear();
        const auto metrics = json.get("dividendMetrics");
        if (metrics.isArray()) {
            for (size_t index = 0; index < metrics.size(); ++index) {
                const DividendMetric dividendMetric = dividendMetricFromString(
                    QString::fromStdString(requireStringItem(metrics, index, "dividendMetrics")));
                if (dividendMetric != DividendMetric::UNKNOWN) {
                    dividendMetrics.push_back(dividendMetricField(dividendMetric).toStdString());
                }
            }
        } else {
            if (!metrics.isString()) {
                throw std::runtime_error("dividendMetrics 不是字符串字段");
            }
            const DividendMetric dividendMetric = dividendMetricFromString(QString::fromStdString(metrics.asString()));
            if (dividendMetric != DividendMetric::UNKNOWN) {
                dividendMetrics.push_back(dividendMetricField(dividendMetric).toStdString());
            }
        }
    }
    if (isDividend && dividendMetrics.empty()) {
        const QString dividendMetricName = dividendMetricField(dividendMetric);
        if (dividendMetricName.isEmpty()) {
            throw std::runtime_error("dividendMetrics 不能为空且 metric 不是有效的红利枚举");
        }
        dividendMetrics.push_back(dividendMetricName.toStdString());
    }
    if (isDividend && metric.empty() && !dividendMetrics.empty()) {
        metric = dividendMetrics.front();
    }
    if (isTechnical && json.has("technicalIndicators")) {
        const auto indicators = normalizeTechnicalIndicatorList(json.get("technicalIndicators"));
        for (const TechnicalIndicator indicator : indicators) {
            if (indicator != TechnicalIndicator::UNKNOWN) {
                technicalIndicators.push_back(indicator);
            }
        }
    }
    if (isTechnical && json.has("technicalCombinationMode")) technicalCombinationMode = json.get("technicalCombinationMode").asString();
    if (isTechnical && technicalCombinationMode.empty() && json.has("combinationMode")) technicalCombinationMode = json.get("combinationMode").asString();
    if (isTechnical && json.has("maWindow")) maWindow = json.get("maWindow").asInt();
    if (isTechnical && json.has("emaWindow")) emaWindow = json.get("emaWindow").asInt();
    if (isTechnical && json.has("bollWindow")) bollWindow = json.get("bollWindow").asInt();
    if (isTechnical && json.has("bollStdDev")) bollStdDev = json.get("bollStdDev").asDouble();
    if (isTechnical && json.has("kdjWindow")) kdjWindow = json.get("kdjWindow").asInt();
    if (isTechnical && json.has("kdjKPeriod")) kdjKPeriod = json.get("kdjKPeriod").asInt();
    if (isTechnical && json.has("kdjDPeriod")) kdjDPeriod = json.get("kdjDPeriod").asInt();
    if (isTechnical && json.has("atrWindow")) atrWindow = json.get("atrWindow").asInt();
    if (isTechnical && json.has("vwapWindow")) vwapWindow = json.get("vwapWindow").asInt();
    if (isTechnical && json.has("volumeRatioWindow")) volumeRatioWindow = json.get("volumeRatioWindow").asInt();
    if (isSentiment && json.has("sentimentSource")) {
        sentimentSource = sentimentSourceFromString(QString::fromStdString(json.get("sentimentSource").asString()));
    }
    if (isCustom && json.has("expression")) expression = json.get("expression").asString();
    if (isIndustry && json.has("sectorType")) sectorType = json.get("sectorType").asString();
    if (isMacro && json.has("macroDimensions")) {
        macroDimensions.clear();
        const auto dimensions = json.get("macroDimensions");
        if (!dimensions.isArray()) {
            throw std::runtime_error("macroDimensions 不是数组字段");
        }
        for (size_t index = 0; index < dimensions.size(); ++index) {
            const MacroDimension dimension = macroDimensionFromString(
                QString::fromStdString(requireStringItem(dimensions, index, "macroDimensions")));
            if (dimension == MacroDimension::UNKNOWN) {
                throw std::runtime_error("macroDimensions 包含不支持的维度");
            }
            if (std::find(macroDimensions.begin(), macroDimensions.end(), dimension) == macroDimensions.end()) {
                macroDimensions.push_back(dimension);
            }
        }
    }
    if (isMacro && json.has("macroIndicators")) {
        macroIndicators.clear();
        const auto indicators = json.get("macroIndicators");
        if (!indicators.isArray()) {
            throw std::runtime_error("macroIndicators 不是数组字段");
        }
        for (size_t index = 0; index < indicators.size(); ++index) {
            const MacroIndicator indicator = macroIndicatorFromString(
                QString::fromStdString(requireStringItem(indicators, index, "macroIndicators")));
            if (indicator == MacroIndicator::UNKNOWN) {
                throw std::runtime_error("macroIndicators 包含不支持的指标");
            }
            if (std::find(macroIndicators.begin(), macroIndicators.end(), indicator) == macroIndicators.end()) {
                macroIndicators.push_back(indicator);
            }
        }
    }
    if (isMacro && json.has("macroFrequency")) macroFrequency = requireStringField(json, "macroFrequency");
    if (isMacro && macroFrequency.empty() && json.has("frequency")) macroFrequency = requireStringField(json, "frequency");
    if (isMacro && json.has("macroWindow")) macroWindow = json.get("macroWindow").asInt();
    if (isMacro && macroWindow <= 0 && json.has("window")) macroWindow = json.get("window").asInt();
    if (isMacro && json.has("benchmarkSymbol")) benchmarkSymbol = requireStringField(json, "benchmarkSymbol");
    if (json.has("priceType")) {
        priceType = technicalPriceTypeFromString(QString::fromStdString(requireStringField(json, "priceType")));
    }
    if (json.has("useVolume")) useVolume = json.get("useVolume").asBool();
    if (json.has("frequency")) frequency = requireStringField(json, "frequency");
    if (json.has("laggedEnabled")) laggedEnabled = json.get("laggedEnabled").asBool();
    if (json.has("standardization")) standardization = requireStringField(json, "standardization");
    if (json.has("neutralizationEnabled")) neutralizationEnabled = json.get("neutralizationEnabled").asBool();
    if (isCustom && json.has("variables")) {
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
                }
                variables.push_back(std::move(binding));
            }
        }
    }
    if (json.has("window")) window = json.get("window").asInt();
    if (isTechnical && json.has("rsiWindow")) rsiWindow = json.get("rsiWindow").asInt();
    if (isTechnical && json.has("macdFastPeriod")) macdFastPeriod = json.get("macdFastPeriod").asInt();
    if (isTechnical && json.has("macdSlowPeriod")) macdSlowPeriod = json.get("macdSlowPeriod").asInt();
    if (isTechnical && json.has("macdSignalPeriod")) macdSignalPeriod = json.get("macdSignalPeriod").asInt();
    if (isTechnical && json.has("obvWindow")) obvWindow = json.get("obvWindow").asInt();
    if (isTechnical && json.has("turnoverStabilityWindow")) turnoverStabilityWindow = json.get("turnoverStabilityWindow").asInt();
    if (isTechnical && json.has("turnoverStabilityMetric")) turnoverStabilityMetric = requireStringField(json, "turnoverStabilityMetric");
    if (isTechnical && json.has("technicalPriceType")) {
        technicalPriceType = technicalPriceTypeFromString(
            QString::fromStdString(requireStringField(json, "technicalPriceType")));
    }
    if (json.has("lookbackPeriod")) lookbackPeriod = json.get("lookbackPeriod").asInt();
    if (isDividend && json.has("minDividendYield")) minDividendYield = json.get("minDividendYield").asDouble();
}

ConfigurableFactor::ConfigurableFactor()
{
    factorType_ = FactorType::UNKNOWN;
}

std::vector<CalculationResult> ConfigurableFactor::calculateBatch(const std::vector<CalculationContext>& contexts)
{
    if (contexts.empty()) {
        return {};
    }

    BatchComputationCache cache;
    cache.historicalView = contexts.front().historicalView;
    BatchComputationCacheScope scope(cache);
    return BaseFactor::calculateBatch(contexts);
}

CalculationResult ConfigurableFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(
            context,
            QStringLiteral("已移除通用因子运行期数据库取数路径，请由引擎提供 HistoricalView").toStdString());
    }

    switch (configuredFactorType()) {
    case FactorType::GROWTH:
        return calculateGrowth(context);
    case FactorType::LIQUIDITY:
        return calculateLiquidity(context);
    case FactorType::TECHNICAL:
        return calculateTechnical(context);
    case FactorType::DIVIDEND:
        return calculateDividend(context);
    case FactorType::MACRO:
        return calculateMacro(context);
    case FactorType::INDUSTRY:
        return calculateIndustry(context);
    case FactorType::SENTIMENT:
        return calculateSentiment(context);
    case FactorType::CUSTOM:
        return calculateCustom(context);
    default:
        return CalculationResult::createError(
            QString::fromUtf8("未识别的通用因子类型枚举值: %1").arg(QString::number(static_cast<int>(configuredFactorType()))).toStdString());
    }
}

DataRequirements ConfigurableFactor::getDataRequirements() const
{
    const FactorType factorType = configuredFactorType();
    if (factorType == FactorType::TECHNICAL) {
        return derivedTechnicalDataRequirements(params_);
    }
    if (factorType == FactorType::INDUSTRY) {
        DataRequirements requirements = dataRequirements_;
        const DataRequirements derivedRequirements = derivedIndustryDataRequirements(params_);
        for (const std::string& field : derivedRequirements.requiredFields) {
            appendUniqueField(requirements.requiredFields, field);
        }
        for (const std::string& field : derivedRequirements.optionalFields) {
            appendUniqueField(requirements.optionalFields, field);
        }
        for (const std::string& field : derivedRequirements.alternativeFields) {
            appendUniqueField(requirements.alternativeFields, field);
        }
        return requirements;
    }

    DataRequirements requirements = dataRequirements_;
    if (configurableFactorNeedsHistoricalNeutralization(factorType, params_)) {
        appendUniqueField(requirements.requiredFields, "industry_code");
        appendUniqueField(requirements.requiredFields, "market_cap");
    }
    return requirements;
}

BoundaryRules ConfigurableFactor::getBoundaryRules() const
{
    if (configuredFactorType() == FactorType::TECHNICAL) {
        return derivedTechnicalBoundaryRules(params_, boundaryRules_);
    }
    return boundaryRules_;
}

std::shared_ptr<ConfigurableFactor> ConfigurableFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<ConfigurableFactor>();
    factor->dataChecker_ = dataChecker;
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config, info.factorType);
    return factor;
}

void ConfigurableFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    loadConfig(config, FactorType::UNKNOWN);
}

void ConfigurableFactor::loadConfig(const foundation::json::JsonFacade& config, FactorType factorType)
{
    BaseFactor::loadConfig(config);
    params_ = Params{};
    params_.configuredType = resolveConfiguredFactorType(factorType, config);
    if (config.has("calculation")) {
        params_.fromJson(config.get("calculation"), params_.configuredType);
    }
    factorType_ = params_.configuredType;
    if (params_.configuredType == FactorType::TECHNICAL) {
        dataRequirements_ = derivedTechnicalDataRequirements(params_);
        boundaryRules_ = derivedTechnicalBoundaryRules(params_, boundaryRules_);
    }
}

FactorType ConfigurableFactor::configuredFactorType() const
{
    return params_.configuredType;
}

std::vector<std::string> ConfigurableFactor::effectiveSymbols(const CalculationContext& context) const
{
    if (!context.symbols.empty()) {
        return context.symbols;
    }
    if (context.historicalView) {
        return context.historicalView->getAvailableSymbols(context.date);
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
    if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
        const std::string batchKey = buildBatchCrossSectionKey(context.date, normalizedField);
        const auto cacheIt = activeBatchComputationCache->crossSectionsByKey.find(batchKey);
        if (cacheIt != activeBatchComputationCache->crossSectionsByKey.end()) {
            return cacheIt->second;
        }
    }
    const std::vector<std::string> symbols = effectiveSymbols(context);
    if (context.historicalView && context.historicalView->hasField(normalizedField.toStdString())) {
        const auto batchValues = context.historicalView->getBatchCrossSections(context.date,
                                                                            symbols,
                                                                            {normalizedField.toStdString()});
        std::unordered_map<std::string, double> resolvedValues;
        const auto fieldIt = batchValues.find(normalizedField.toStdString());
        if (fieldIt != batchValues.end()) {
            resolvedValues = fieldIt->second;
        }
        if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
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
    if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
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
    if (context.historicalView && context.historicalView->hasField(field.toStdString())) {
        const std::vector<std::string> batchSymbols = useBulkSymbols ? context.symbols : std::vector<std::string>{symbol};
        const std::string fieldName = field.toStdString();
        const auto anchoredBatchValues = context.historicalView->getBatchTimeSeries(
            batchSymbols,
            context.date,
            window,
            {fieldName});
        std::unordered_map<std::string, std::vector<double>> resolvedSeries;
        const auto fieldIt = anchoredBatchValues.find(fieldName);
        if (fieldIt != anchoredBatchValues.end()) {
            resolvedSeries = fieldIt->second;
        }
        if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
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
    if (normalizedField.isEmpty() || !context.historicalView || !context.historicalView->hasField(normalizedField.toStdString())) {
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
    if (normalizedField.isEmpty() || !context.historicalView || !context.historicalView->hasField(normalizedField.toStdString())) {
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
    (void)context;
    std::unordered_map<std::string, QString> result;
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
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用缓存数据集";

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
    const QString frequency = normalizeConfigurableFrequency(params_.frequency);
    const QString standardization = normalizeConfigurableStandardization(params_.standardization);
    std::unordered_set<std::string> seenMetrics;

    struct GrowthMetricSelection {
        QString metric;
        double weight{0.0};
        QString field;
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

        const QString field = growthFieldForMetric(metric);
        if (field.isEmpty()) {
            return failGrowth(QStringLiteral("成长因子配置包含不支持的指标"));
        }
        selections.push_back({metric, weight, field});
    }

    auto resolveGrowthEffectiveDate = [&]() {
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

        const int maxOffset = (std::max)(0, params_.lookbackPeriod);
        const int startOffset = params_.laggedEnabled ? 1 : 0;
        const std::vector<std::string> symbols = context.symbols.empty()
            ? context.historicalView->getAvailableSymbols(context.date)
            : context.symbols;
        for (int offset = startOffset; offset <= maxOffset; ++offset) {
            const QString candidate = anchorDate.isValid()
                ? anchorDate.addDays(-offset).toString(Qt::ISODate)
                : QDate::fromString(effectiveDate, Qt::ISODate).addDays(-offset).toString(Qt::ISODate);
            CalculationContext candidateContext = context;
            candidateContext.date = candidate.toStdString();
            candidateContext.symbols = symbols;

            bool hasAllFields = true;
            for (const auto& selection : selections) {
                if (currentFieldCrossSection(candidateContext, selection.field).empty()) {
                    hasAllFields = false;
                    break;
                }
            }

            if (hasAllFields) {
                return candidate;
            }
        }

        return effectiveDate;
    };

    const QString effectiveDate = resolveGrowthEffectiveDate();
    CalculationContext effectiveContext = context;
    effectiveContext.date = effectiveDate.toStdString();
    auto latestFinancialSeriesResolver = [this](const CalculationContext& queryContext,
                                                const QString& field,
                                                const QString& date,
                                                int limit) {
        return latestFinancialSeries(queryContext, field, date, limit);
    };

    std::unordered_map<std::string, double> combinedScores;
    std::unordered_map<std::string, double> activeWeightSums;

    // 单项成长指标先独立计算，再按统一标准化规则合成。
    for (const auto& selection : selections) {
        if (selection.weight == 0.0) {
            continue;
        }

        std::unordered_map<std::string, double> metricScores;
        if (selection.metric == "revenue_growth") {
            metricScores = computeGrowthYoYScoreMap(latestFinancialSeriesResolver, effectiveContext, effectiveDate, selection.field);
        } else if (selection.metric == "net_profit_growth") {
            metricScores = computeGrowthYoYScoreMap(latestFinancialSeriesResolver, effectiveContext, effectiveDate, selection.field);
        } else if (selection.metric == "delta_roe") {
            metricScores = computeGrowthDifferenceScoreMap(latestFinancialSeriesResolver, effectiveContext, effectiveDate, selection.field);
        } else if (selection.metric == "sue") {
            metricScores = computeGrowthSueProxyScoreMap(latestFinancialSeriesResolver, effectiveContext, effectiveDate);
        } else {
            return failGrowth(QStringLiteral("成长因子配置包含不支持的指标"));
        }

        normalizeGrowthScoreMap(standardization, metricScores);

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
        ? QStringLiteral("requested")
        : QStringLiteral("disabled");
    result.metadata.set("dataMode", json_helper::toJsonValue(growthDataMode.toStdString()));
    if (combinedScores.empty()) {
        result.metadata.set("emptyReason", json_helper::toJsonValue(QStringLiteral("成长因子没有可用财务数据").toStdString()));
        return result;
    }

    for (const auto& [symbol, weightedScore] : combinedScores) {
        const double weightSum = activeWeightSums[symbol];
        if (weightSum > 1e-12) {
            result.values[symbol] = weightedScore / weightSum;
        }
    }

    if (params_.neutralizationEnabled && !result.values.empty()) {
        QString errorMessage;
        if (!applyHistoricalViewIndustrySizeNeutralization(effectiveContext, result.values, &errorMessage)) {
            result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
            growthNeutralizationMode = QStringLiteral("historical_view_neutralization_failed");
            result.values.clear();
        } else {
            growthNeutralizationMode = QStringLiteral("historical_view_cross_section_industry_size");
        }
    }

    if (!result.values.empty()) {
        applyConfigurableStandardization(standardization, result.values);
    }

    result.metadata.set("metric", json_helper::toJsonValue(selectedMetrics.empty() ? std::string() : selectedMetrics.front()));
    result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
    result.metadata.set("frequency", json_helper::toJsonValue(frequency.toStdString()));
    result.metadata.set("lookbackPeriod", json_helper::toJsonValue(params_.lookbackPeriod));
    result.metadata.set("laggedEnabled", json_helper::toJsonValue(params_.laggedEnabled));
    result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
    result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(params_.neutralizationEnabled));
    result.metadata.set("neutralizationMode", json_helper::toJsonValue(growthNeutralizationMode.toStdString()));
    result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    return result;
}

CalculationResult ConfigurableFactor::calculateLiquidity(const CalculationContext& context) const
{
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();

    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用缓存数据集";

    const LiquidityMetric metricKind = params_.liquidityMetric;
    const QString metric = liquidityMetricField(metricKind);
    if (metric.isEmpty()) {
        result.dataStatus = CalculationResult::createError("流动性因子缺少有效 metric 枚举").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("流动性因子缺少有效 metric 枚举"));
        return result;
    }
    const QString frequency = normalizeConfigurableFrequency(params_.frequency);
    const QString standardization = normalizeConfigurableStandardization(params_.standardization);
    const int window = (std::max)(1, params_.window);
    bool laggedDateResolvedByProvider = false;
    QString liquidityNeutralizationMode = params_.neutralizationEnabled
        ? QStringLiteral("requested")
        : QStringLiteral("disabled");
    auto resolvePreviousAvailableDate = [&](const QString& anchorDate, const QString& requiredField) {
        if (anchorDate.isEmpty()) {
            return QString::fromStdString(context.date);
        }

        const int maxOffset = (std::max)(45, params_.lookbackPeriod);
        const std::vector<std::string> symbols = context.symbols.empty()
            ? context.historicalView->getAvailableSymbols(context.date)
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
        QString requiredField = QString(factor::bridge::MarketBarFieldKeys::TURNOVER_RATE);
        switch (metricKind) {
        case LiquidityMetric::VOLUME:
            requiredField = QString(factor::bridge::MarketBarFieldKeys::VOLUME);
            break;
        case LiquidityMetric::AMPLITUDE:
            requiredField = QString(factor::bridge::MarketBarFieldKeys::AMPLITUDE);
            break;
        case LiquidityMetric::AMIHUD_ILLIQUIDITY:
            requiredField = QString(factor::bridge::MarketBarFieldKeys::CLOSE);
            break;
        case LiquidityMetric::TURNOVER_RATE:
        case LiquidityMetric::UNKNOWN:
            break;
        }
        effectiveDate = resolvePreviousAvailableDate(effectiveDate, requiredField);
    }

    CalculationContext effectiveContext = context;
    effectiveContext.date = effectiveDate.toStdString();

    const auto symbols = effectiveSymbols(effectiveContext);
    effectiveContext.symbols = symbols;
    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);

    auto calculateLiquidityBody = [&]() -> CalculationResult {
        size_t populatedSymbolCount = 0;
        const auto closesBySymbol = fetchBatchSeriesMap(effectiveContext, QString(factor::bridge::MarketBarFieldKeys::CLOSE), window + 1);
        const auto volumesBySymbol = fetchBatchSeriesMap(effectiveContext, QString(factor::bridge::MarketBarFieldKeys::VOLUME), window + 1);
        const QString metricField = metric;
        const auto metricBySymbol = fetchBatchSeriesMap(effectiveContext, metricField, window);

        const std::vector<std::string> activeSymbols = [&]() {
            std::vector<std::string> validSymbols;
            validSymbols.reserve(symbols.size());
            for (const auto& symbol : symbols) {
                if (metricKind == LiquidityMetric::AMIHUD_ILLIQUIDITY) {
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
            result.metadata.set("emptyReason", json_helper::toJsonValue("流动性因子没有可用价格或成交量数据"));
            result.metadata.set("laggedDateMode", json_helper::toJsonValue(params_.laggedEnabled
                ? (laggedDateResolvedByProvider ? "provider_scan" : "anchor_date")
                : "disabled"));
            result.metadata.set("neutralizationMode", json_helper::toJsonValue(liquidityNeutralizationMode.toStdString()));
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
        if (metricKind == LiquidityMetric::AMIHUD_ILLIQUIDITY) {
            const size_t closeLength = findCommonLength(closesBySymbol);
            const size_t volumeLength = findCommonLength(volumesBySymbol);
            commonLength = (std::min)(closeLength, volumeLength);
        } else {
            commonLength = findCommonLength(metricBySymbol);
        }

        if (commonLength == 0 || (metric == QStringLiteral("amihud_illiquidity") && commonLength < 2)) {
            result.dataStatus = CalculationResult::createError("流动性因子没有可用价格或成交量数据").dataStatus;
            result.metadata.set("emptyReason", json_helper::toJsonValue("流动性因子没有可用价格或成交量数据"));
            result.metadata.set("laggedDateMode", json_helper::toJsonValue(params_.laggedEnabled
                ? (laggedDateResolvedByProvider ? "provider_scan" : "anchor_date")
                : "disabled"));
            result.metadata.set("neutralizationMode", json_helper::toJsonValue(liquidityNeutralizationMode.toStdString()));
            return result;
        }

        Eigen::VectorXd rawScores(static_cast<int>(activeSymbols.size()));
        rawScores.setConstant(std::numeric_limits<double>::quiet_NaN());

        // 指标级原始值计算：volume 取窗口均值，amplitude 取负窗口均值，
        // amihud_illiquidity 取负的日度价格冲击/成交量均值。
        if (metricKind == LiquidityMetric::VOLUME) {
            const Eigen::MatrixXd metricMatrix = collectMatrix(metricBySymbol, commonLength);
            rawScores = metricMatrix.rowwise().mean();
        } else if (metricKind == LiquidityMetric::AMPLITUDE) {
            const Eigen::MatrixXd metricMatrix = collectMatrix(metricBySymbol, commonLength);
            rawScores = -metricMatrix.rowwise().mean();
        } else if (metricKind == LiquidityMetric::AMIHUD_ILLIQUIDITY) {
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

        std::unordered_map<std::string, double> rawValueMap;
        rawValueMap.reserve(activeSymbols.size());
        for (int row = 0; row < rawScores.size(); ++row) {
            const double value = rawScores(row);
            if (!std::isfinite(value)) {
                continue;
            }
            rawValueMap[activeSymbols[static_cast<size_t>(row)]] = value;
        }

        if (rawValueMap.empty()) {
            result.dataStatus = CalculationResult::createError("流动性因子没有可用价格或成交量数据").dataStatus;
            result.metadata.set("emptyReason", json_helper::toJsonValue("流动性因子没有可用价格或成交量数据"));
            result.metadata.set("laggedDateMode", json_helper::toJsonValue(params_.laggedEnabled
                ? (laggedDateResolvedByProvider ? "provider_scan" : "anchor_date")
                : "disabled"));
            result.metadata.set("neutralizationMode", json_helper::toJsonValue(liquidityNeutralizationMode.toStdString()));
            return result;
        }

        if (params_.neutralizationEnabled) {
            QString errorMessage;
            if (!applyHistoricalViewIndustrySizeNeutralization(effectiveContext, rawValueMap, &errorMessage)) {
                result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                liquidityNeutralizationMode = QStringLiteral("historical_view_neutralization_failed");
                result.metadata.set("laggedDateMode", json_helper::toJsonValue(params_.laggedEnabled
                    ? (laggedDateResolvedByProvider ? "provider_scan" : "anchor_date")
                    : "disabled"));
                result.metadata.set("neutralizationMode", json_helper::toJsonValue(liquidityNeutralizationMode.toStdString()));
                return result;
            }
            liquidityNeutralizationMode = QStringLiteral("historical_view_cross_section_industry_size");
        }

        applyConfigurableStandardization(standardization, rawValueMap);

        for (const auto& [symbol, score] : rawValueMap) {
            if (std::isfinite(score) && score != 0.0) {
                result.values[symbol] = score;
                ++populatedSymbolCount;
            }
        }

    result.metadata.set("metric", json_helper::toJsonValue(metric.toStdString()));
    result.metadata.set("window", json_helper::toJsonValue(window));
    result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
    result.metadata.set("frequency", json_helper::toJsonValue(frequency.toStdString()));
    result.metadata.set("laggedEnabled", json_helper::toJsonValue(params_.laggedEnabled));
    result.metadata.set("laggedDateMode", json_helper::toJsonValue(params_.laggedEnabled
        ? (laggedDateResolvedByProvider ? "provider_scan" : "anchor_date")
        : "disabled"));
    result.metadata.set("lookbackPeriod", json_helper::toJsonValue(params_.lookbackPeriod));
    result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
    result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(params_.neutralizationEnabled));
    result.metadata.set("neutralizationMode", json_helper::toJsonValue(liquidityNeutralizationMode.toStdString()));
    result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));

    const qint64 elapsedMs = elapsedTimer.elapsed();
    if (elapsedMs >= 300) {
        qDebug() << "ConfigurableFactor(liquidity): 计算耗时较长"
                 << "date=" << QString::fromStdString(context.date)
                 << "metric=" << metric
                 << "window=" << window
                 << "symbolCount=" << static_cast<int>(symbols.size())
                 << "resultCount=" << static_cast<int>(populatedSymbolCount)
                 << "usingHistoricalView=" << static_cast<bool>(context.historicalView)
                 << "elapsedMs=" << elapsedMs;
    }
    return result;
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.historicalView = context.historicalView;
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
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用缓存数据集";
    const QString frequency = normalizeConfigurableFrequency(params_.frequency);
    const QString standardization = normalizeConfigurableStandardization(params_.standardization);

    std::vector<TechnicalIndicator> indicatorTypes;
    bool technicalUsedDefaultFallback = false;
    const QString technicalConfigMode = !params_.technicalIndicators.empty()
        ? QStringLiteral("technical_indicators")
        : QStringLiteral("missing_technical_indicators");

    for (const TechnicalIndicator indicatorType : params_.technicalIndicators) {
        if (indicatorType != TechnicalIndicator::UNKNOWN
                && std::find(indicatorTypes.begin(), indicatorTypes.end(), indicatorType) == indicatorTypes.end()) {
            indicatorTypes.push_back(indicatorType);
        }
    }
    if (indicatorTypes.empty()) {
        result.dataStatus = CalculationResult::createError("技术因子缺少有效技术指标配置").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("技术因子缺少有效技术指标配置"));
        result.metadata.set("technicalConfigMode", json_helper::toJsonValue(technicalConfigMode.toStdString()));
        return result;
    }

    const QString technicalResolvedConfigMode = technicalConfigMode;

    const QString combinationMode = QString::fromStdString(params_.technicalCombinationMode).trimmed().toLower();
    const QString priceField = priceFieldForType(params_.technicalPriceType);
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
    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);
    const bool needHighLowSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesHighLow);
    const bool needVolumeSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesVolume);
    const bool needPriceSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesPriceField);
    if (needPriceSeries && priceField.isEmpty()) {
        result.dataStatus = CalculationResult::createError("技术因子缺少合法价格字段配置").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("技术因子缺少合法价格字段配置"));
        result.metadata.set("technicalConfigMode", json_helper::toJsonValue(technicalResolvedConfigMode.toStdString()));
        result.metadata.set("indicatorTypes", technicalIndicatorArrayJson(indicatorTypes));
        return result;
    }
    const QString turnoverMetricField = [&]() {
        const QString metric = QString::fromStdString(params_.turnoverStabilityMetric).trimmed().toLower();
        if (metric == QStringLiteral("turnover") || metric == QString(factor::bridge::MarketBarFieldKeys::TURNOVER_RATE) || metric.isEmpty()) {
            return QString(factor::bridge::MarketBarFieldKeys::TURNOVER_RATE);
        }
        if (metric == QString(factor::bridge::MarketBarFieldKeys::VOLUME)) {
            return QString(factor::bridge::MarketBarFieldKeys::VOLUME);
        }
        return QString(factor::bridge::MarketBarFieldKeys::TURNOVER_RATE);
    }();
    const bool needTurnoverSeries = std::any_of(indicatorTypes.begin(), indicatorTypes.end(), technicalIndicatorUsesTurnoverMetric);

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

    std::vector<std::string> runtimeSymbols = symbols;
    technicalContext.symbols = runtimeSymbols;

    if (runtimeSymbols.empty()) {
        result.dataStatus = CalculationResult::createError("技术因子缺少可用标的").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("技术因子缺少可用标的"));
        result.metadata.set("technicalConfigMode", json_helper::toJsonValue(technicalResolvedConfigMode.toStdString()));
        result.metadata.set("indicatorTypes", technicalIndicatorArrayJson(indicatorTypes));
        return result;
    }

    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用缓存数据集";

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
    if (needPriceSeries) {
        appendField(priceFieldName);
    }
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

    auto resolveTechnicalEffectiveDate = [&]() {
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

        const int maxOffset = (std::max)(0, params_.lookbackPeriod);
        const int startOffset = params_.laggedEnabled ? 1 : 0;
        for (int offset = startOffset; offset <= maxOffset; ++offset) {
            const QString candidate = anchorDate.isValid()
                ? anchorDate.addDays(-offset).toString(Qt::ISODate)
                : effectiveDate;
            CalculationContext candidateContext = context;
            candidateContext.date = candidate.toStdString();
            candidateContext.symbols = runtimeSymbols;

            bool hasAnyField = false;
            for (const std::string& fieldName : requestedFields) {
                if (!currentFieldCrossSection(candidateContext, QString::fromStdString(fieldName)).empty()) {
                    hasAnyField = true;
                    break;
                }
            }
            if (hasAnyField) {
                return candidate;
            }
        }

        return effectiveDate;
    };

    QString technicalNeutralizationMode = params_.neutralizationEnabled
        ? QStringLiteral("requested")
        : QStringLiteral("disabled");
    const QString effectiveDate = resolveTechnicalEffectiveDate();
    technicalContext.date = effectiveDate.toStdString();

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchData;
    batchData = technicalContext.historicalView->getBatchTimeSeries(
        runtimeSymbols,
        technicalContext.date,
        static_cast<int>(technicalLookbackWindow),
        requestedFields);

    const auto findSeriesMap = [&](const std::string& fieldName) -> const std::unordered_map<std::string, std::vector<double>>* {
        const auto fieldIt = batchData.find(fieldName);
        if (fieldIt == batchData.end() || fieldIt->second.empty()) {
            return nullptr;
        }
        return &fieldIt->second;
    };

    const auto* closesBySymbol = needPriceSeries ? findSeriesMap(priceFieldName) : nullptr;
    QString actualPriceField = needPriceSeries ? priceField : QStringLiteral("not_used");
    bool priceFieldDerived = false;
    if (needPriceSeries && !closesBySymbol) {
        result.dataStatus = CalculationResult::createError("技术因子没有可用价格数据").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("技术因子没有可用价格数据"));
        result.metadata.set("technicalConfigMode", json_helper::toJsonValue(technicalResolvedConfigMode.toStdString()));
        result.metadata.set("indicatorTypes", technicalIndicatorArrayJson(indicatorTypes));
        return result;
    }

    const auto* highsBySymbol = needHighLowSeries ? findSeriesMap("high") : nullptr;
    const auto* lowsBySymbol = needHighLowSeries ? findSeriesMap("low") : nullptr;
    const auto* volumesBySymbol = needVolumeSeries ? findSeriesMap("volume") : nullptr;
    const auto* turnoverSeriesBySymbol = needTurnoverSeries ? findSeriesMap(turnoverFieldName) : nullptr;

    std::unordered_map<std::string, std::vector<double>> scoresBySymbol;
    scoresBySymbol.reserve(symbols.size());

    for (const TechnicalIndicator indicatorType : indicatorTypes) {
        std::unordered_map<std::string, double> indicatorScores;
        switch (indicatorType) {
        case TechnicalIndicator::RSI:
            indicatorScores = batchCalculateRsi(*closesBySymbol, rsiWindow);
            break;
        case TechnicalIndicator::MACD:
            indicatorScores = batchCalculateMacd(*closesBySymbol, macdFastPeriod, macdSlowPeriod, macdSignalPeriod);
            break;
        case TechnicalIndicator::MA:
            indicatorScores = batchCalculateMa(*closesBySymbol, maWindow);
            break;
        case TechnicalIndicator::EMA:
            indicatorScores = batchCalculateEma(*closesBySymbol, emaWindow);
            break;
        case TechnicalIndicator::BOLL:
            indicatorScores = batchCalculateBoll(*closesBySymbol, bollWindow, bollStdDev);
            break;
        case TechnicalIndicator::KDJ:
            if (highsBySymbol && lowsBySymbol) {
                indicatorScores = batchCalculateKdj(*highsBySymbol, *lowsBySymbol, *closesBySymbol, kdjWindow, kdjKPeriod, kdjDPeriod);
            }
            break;
        case TechnicalIndicator::ATR:
            if (highsBySymbol && lowsBySymbol) {
                indicatorScores = batchCalculateAtr(*highsBySymbol, *lowsBySymbol, *closesBySymbol, atrWindow);
            }
            break;
        case TechnicalIndicator::OBV:
            if (volumesBySymbol) {
                indicatorScores = batchCalculateObv(*closesBySymbol, *volumesBySymbol, obvWindow);
            }
            break;
        case TechnicalIndicator::VWAP:
            if (volumesBySymbol) {
                indicatorScores = batchCalculateVwap(*closesBySymbol, *volumesBySymbol);
            }
            break;
        case TechnicalIndicator::VOLUME_RATIO:
            if (volumesBySymbol) {
                indicatorScores = batchCalculateVolumeRatio(*volumesBySymbol, volumeRatioWindow);
            }
            break;
        case TechnicalIndicator::TURNOVER_STABILITY:
            if (turnoverSeriesBySymbol) {
                indicatorScores = batchCalculateTurnoverStability(*turnoverSeriesBySymbol, turnoverStabilityWindow);
            }
            break;
        default:
            break;
        }
        for (const auto& [symbol, score] : indicatorScores) {
            if (std::isfinite(score)) {
                scoresBySymbol[symbol].push_back(score);
            }
        }
    }

    std::unordered_map<std::string, double> rawValueMap;
    rawValueMap.reserve(runtimeSymbols.size());
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
            rawValueMap[symbol] = combinedScore;
        }
    }

    if (rawValueMap.empty()) {
        result.dataStatus = CalculationResult::createError("技术因子没有可用价格数据").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("技术因子没有可用价格数据"));
        result.metadata.set("technicalConfigMode", json_helper::toJsonValue(technicalResolvedConfigMode.toStdString()));
        result.metadata.set("indicatorTypes", technicalIndicatorArrayJson(indicatorTypes));
        return result;
    }

    if (params_.neutralizationEnabled) {
        QString errorMessage;
        if (!applyHistoricalViewIndustrySizeNeutralization(technicalContext, rawValueMap, &errorMessage)) {
            result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
            technicalNeutralizationMode = QStringLiteral("historical_view_neutralization_failed");
            result.metadata.set("technicalConfigMode", json_helper::toJsonValue(technicalResolvedConfigMode.toStdString()));
            result.metadata.set("indicatorTypes", technicalIndicatorArrayJson(indicatorTypes));
            result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
            result.metadata.set("frequency", json_helper::toJsonValue(frequency.toStdString()));
            result.metadata.set("lookbackPeriod", json_helper::toJsonValue(params_.lookbackPeriod));
            result.metadata.set("laggedEnabled", json_helper::toJsonValue(params_.laggedEnabled));
            result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
            result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(params_.neutralizationEnabled));
            result.metadata.set("neutralizationMode", json_helper::toJsonValue(technicalNeutralizationMode.toStdString()));
            result.metadata.set("symbolCount", json_helper::toJsonValue(0));
            return result;
        }
        technicalNeutralizationMode = QStringLiteral("historical_view_cross_section_industry_size");
    }

    applyConfigurableStandardization(standardization, rawValueMap);

    for (const auto& [symbol, value] : rawValueMap) {
        if (std::isfinite(value)) {
            result.values[symbol] = value;
        }
    }

    result.metadata.set("indicatorType", json_helper::toJsonValue(static_cast<int>(indicatorTypes.front())));
    result.metadata.set("indicatorTypes", technicalIndicatorArrayJson(indicatorTypes));
    result.metadata.set("technicalConfigMode", json_helper::toJsonValue(technicalResolvedConfigMode.toStdString()));
    result.metadata.set("priceType", json_helper::toJsonValue(priceField.toStdString()));
    result.metadata.set("actualPriceField", json_helper::toJsonValue(actualPriceField.toStdString()));
    result.metadata.set("priceFieldDerived", json_helper::toJsonValue(priceFieldDerived));
    result.metadata.set("useVolume", json_helper::toJsonValue(params_.useVolume));
    result.metadata.set("window", json_helper::toJsonValue(rsiWindow));
    result.metadata.set("technicalCombinationMode", json_helper::toJsonValue(combinationMode.toStdString()));
    result.metadata.set("maWindow", json_helper::toJsonValue(maWindow));
    result.metadata.set("emaWindow", json_helper::toJsonValue(emaWindow));
    result.metadata.set("bollWindow", json_helper::toJsonValue(bollWindow));
    result.metadata.set("bollStdDev", json_helper::toJsonValue(bollStdDev));
    result.metadata.set("kdjWindow", json_helper::toJsonValue(kdjWindow));
    result.metadata.set("kdjKPeriod", json_helper::toJsonValue(kdjKPeriod));
    result.metadata.set("kdjDPeriod", json_helper::toJsonValue(kdjDPeriod));
    result.metadata.set("atrWindow", json_helper::toJsonValue(atrWindow));
    result.metadata.set("macdFastPeriod", json_helper::toJsonValue(macdFastPeriod));
    result.metadata.set("macdSlowPeriod", json_helper::toJsonValue(macdSlowPeriod));
    result.metadata.set("macdSignalPeriod", json_helper::toJsonValue(macdSignalPeriod));
    result.metadata.set("obvWindow", json_helper::toJsonValue(obvWindow));
    result.metadata.set("vwapWindow", json_helper::toJsonValue(vwapWindow));
    result.metadata.set("volumeRatioWindow", json_helper::toJsonValue(volumeRatioWindow));
    result.metadata.set("turnoverStabilityWindow", json_helper::toJsonValue(turnoverStabilityWindow));
    result.metadata.set("turnoverStabilityMetric", json_helper::toJsonValue(QString::fromStdString(params_.turnoverStabilityMetric).toStdString()));
    result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
    result.metadata.set("frequency", json_helper::toJsonValue(frequency.toStdString()));
    result.metadata.set("lookbackPeriod", json_helper::toJsonValue(params_.lookbackPeriod));
    result.metadata.set("laggedEnabled", json_helper::toJsonValue(params_.laggedEnabled));
    result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
    result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(params_.neutralizationEnabled));
    result.metadata.set("neutralizationMode", json_helper::toJsonValue(technicalNeutralizationMode.toStdString()));
    result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    return result;
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.historicalView = context.historicalView;
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
    const QString frequency = normalizeConfigurableFrequency(params_.frequency);
    const QString standardization = normalizeConfigurableStandardization(params_.standardization);
    const QString dividendConfigMode = !params_.dividendMetrics.empty()
        ? QStringLiteral("dividend_metrics")
        : QStringLiteral("dividend_metric");

    auto calculateDividendBody = [&]() -> CalculationResult {

    QStringList dividendMetrics;
    for (const std::string& rawMetric : params_.dividendMetrics) {
        const DividendMetric dividendMetric = dividendMetricFromString(QString::fromStdString(rawMetric));
        const QString metric = dividendMetricField(dividendMetric);
        if (!metric.isEmpty() && !dividendMetrics.contains(metric)) {
            dividendMetrics.append(metric);
        }
    }
    if (dividendMetrics.isEmpty()) {
        const QString metric = dividendMetricField(params_.dividendMetric);
        if (metric.isEmpty()) {
            result.dataStatus = CalculationResult::createError("红利因子缺少有效 metric 枚举").dataStatus;
            result.metadata.set("emptyReason", json_helper::toJsonValue("红利因子缺少有效 metric 枚举"));
            return result;
        }
        dividendMetrics.append(metric);
    }

    auto appendCommonMetadata = [&](const QString& effectiveDate, const QString& neutralizationMode) {
        result.metadata.set("metric", json_helper::toJsonValue(dividendMetrics.front().toStdString()));
        result.metadata.set("dividendConfigMode", json_helper::toJsonValue(dividendConfigMode.toStdString()));
        result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
        result.metadata.set("frequency", json_helper::toJsonValue(frequency.toStdString()));
        result.metadata.set("lookbackPeriod", json_helper::toJsonValue(params_.lookbackPeriod));
        result.metadata.set("laggedEnabled", json_helper::toJsonValue(params_.laggedEnabled));
        result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
        result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(params_.neutralizationEnabled));
        result.metadata.set("neutralizationMode", json_helper::toJsonValue(neutralizationMode.toStdString()));
        result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    };

    auto resolveDividendEffectiveDate = [&]() {
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

        const int maxOffset = (std::max)(0, params_.lookbackPeriod);
        const int startOffset = params_.laggedEnabled ? 1 : 0;
        const std::vector<std::string> symbols = context.symbols.empty()
            ? context.historicalView->getAvailableSymbols(context.date)
            : context.symbols;
        for (int offset = startOffset; offset <= maxOffset; ++offset) {
            const QString candidate = anchorDate.isValid()
                ? anchorDate.addDays(-offset).toString(Qt::ISODate)
                : effectiveDate;
            CalculationContext candidateContext = context;
            candidateContext.date = candidate.toStdString();
            candidateContext.symbols = symbols;

            bool hasAnyField = false;
            for (const QString& metric : dividendMetrics) {
                if (!currentFieldCrossSection(candidateContext, metric).empty()) {
                    hasAnyField = true;
                    break;
                }
            }
            if (hasAnyField) {
                return candidate;
            }
        }

        return effectiveDate;
    };

    QString dividendNeutralizationMode = params_.neutralizationEnabled
        ? QStringLiteral("requested")
        : QStringLiteral("disabled");
    const QString effectiveDate = resolveDividendEffectiveDate();
    CalculationContext effectiveContext = context;
    effectiveContext.date = effectiveDate.toStdString();

    const std::vector<std::string> symbols = effectiveSymbols(effectiveContext);
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
    if (context.historicalView && !batchFields.empty()) {
        batchCrossSections = context.historicalView->getBatchCrossSections(effectiveDate.toStdString(), symbols, batchFields);
        if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
            for (const auto& [fieldName, symbolValues] : batchCrossSections) {
                activeBatchComputationCache->crossSectionsByKey[buildBatchCrossSectionKey(effectiveDate.toStdString(), QString::fromStdString(fieldName))] = symbolValues;
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
        appendCommonMetadata(effectiveDate, dividendNeutralizationMode);
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

                if (metric == dividendMetricField(DividendMetric::DIVIDEND_YIELD) && normalizeDividendYieldFloor(params_.minDividendYield) > 0.0
                    && directIt->second < normalizeDividendYieldFloor(params_.minDividendYield)) {
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
        result.metadata.set("emptyReason", json_helper::toJsonValue("红利因子没有可用分红数据"));
        appendCommonMetadata(effectiveDate, dividendNeutralizationMode);
        return result;
    }

    if (params_.neutralizationEnabled) {
        QString errorMessage;
        if (!applyHistoricalViewIndustrySizeNeutralization(effectiveContext, result.values, &errorMessage)) {
            result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
            result.values.clear();
            dividendNeutralizationMode = QStringLiteral("historical_view_neutralization_failed");
            appendCommonMetadata(effectiveDate, dividendNeutralizationMode);
            return result;
        }
        dividendNeutralizationMode = QStringLiteral("historical_view_cross_section_industry_size");
    }

    applyConfigurableStandardization(standardization, result.values);

    appendCommonMetadata(effectiveDate, dividendNeutralizationMode);
    result.metadata.set("dataMode", json_helper::toJsonValue("batch_cross_section"));
    return result;
    };

    return calculateDividendBody();
}

CalculationResult ConfigurableFactor::calculateMacro(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    const QString benchmarkSymbol = QString::fromStdString(params_.benchmarkSymbol).trimmed().isEmpty()
        ? QStringLiteral("000300.SH")
        : QString::fromStdString(params_.benchmarkSymbol).trimmed();
    const QString priceField = priceFieldForType(params_.priceType);
    const QString frequency = QString::fromStdString(params_.macroFrequency).trimmed().toLower();
    const QString standardization = normalizeConfigurableStandardization(params_.standardization);
    const int baseWindow = params_.macroWindow > 0 ? params_.macroWindow : (params_.lookbackPeriod > 0 ? params_.lookbackPeriod : params_.window);
    const int resolvedWindow = (std::max)(3, baseWindow) * macroWindowScale(frequency);

    std::vector<MacroDimension> selectedDimensions;
    const QString macroResolvedConfigMode = QStringLiteral("configured");
    for (MacroDimension dimension : params_.macroDimensions) {
        if (dimension != MacroDimension::UNKNOWN
                && std::find(selectedDimensions.begin(), selectedDimensions.end(), dimension) == selectedDimensions.end()) {
            selectedDimensions.push_back(dimension);
        }
    }
    std::vector<MacroIndicator> selectedIndicators;
    for (MacroIndicator indicator : params_.macroIndicators) {
        if (indicator != MacroIndicator::UNKNOWN
                && std::find(selectedIndicators.begin(), selectedIndicators.end(), indicator) == selectedIndicators.end()) {
            selectedIndicators.push_back(indicator);
        }
    }
    std::vector<MacroIndicator> dimensionScopedIndicators;
    for (MacroIndicator indicator : selectedIndicators) {
        const MacroIndicatorSpec spec = macroIndicatorSpec(indicator);
        if (std::find(selectedDimensions.begin(), selectedDimensions.end(), spec.dimension) != selectedDimensions.end()
                && std::find(dimensionScopedIndicators.begin(), dimensionScopedIndicators.end(), indicator) == dimensionScopedIndicators.end()) {
            dimensionScopedIndicators.push_back(indicator);
        }
    }
    selectedIndicators = dimensionScopedIndicators;
    if (selectedDimensions.empty() || selectedIndicators.empty()) {
        result.dataStatus = CalculationResult::createError("宏观因子必须显式提供 macroDimensions 和 macroIndicators").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("宏观因子必须显式提供 macroDimensions 和 macroIndicators"));
        return result;
    }

    std::vector<std::string> benchmarkFields;
    std::unordered_set<std::string> seenBenchmarkFields;
    benchmarkFields.reserve(static_cast<size_t>(selectedIndicators.size()) * 2);
    for (MacroIndicator indicator : selectedIndicators) {
        const MacroIndicatorSpec spec = macroIndicatorSpec(indicator);
        const QStringList candidateFields = {macroIndicatorFieldName(indicator), spec.proxyField};
        for (const QString& candidateField : candidateFields) {
            const std::string fieldName = candidateField.trimmed().toStdString();
            if (!fieldName.empty() && seenBenchmarkFields.insert(fieldName).second) {
                benchmarkFields.push_back(fieldName);
            }
        }
    }

    const auto symbols = effectiveSymbols(context);
    if (symbols.empty() || selectedIndicators.empty()) {
        result.dataStatus = CalculationResult::createError("宏观因子缺少可用标的或驱动指标").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("宏观因子缺少可用标的或驱动指标"));
        return result;
    }

    CalculationContext effectiveContext = context;
    effectiveContext.symbols = symbols;

    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);

    auto calculateMacroBody = [&]() -> CalculationResult {
    auto appendCommonMetadata = [&](const QString& effectiveDate, const QString& neutralizationMode) {
        result.metadata.set("macroConfigMode", json_helper::toJsonValue(macroResolvedConfigMode.toStdString()));
        result.metadata.set("macroDimensions", macroDimensionArrayJson(selectedDimensions));
        result.metadata.set("macroIndicators", macroIndicatorArrayJson(selectedIndicators));
        result.metadata.set("macroFrequency", json_helper::toJsonValue(frequency.toStdString()));
        result.metadata.set("macroWindow", json_helper::toJsonValue(baseWindow));
        result.metadata.set("macroMode", json_helper::toJsonValue("proxy_sensitivity"));
        result.metadata.set("benchmarkSymbol", json_helper::toJsonValue(benchmarkSymbol.toStdString()));
        result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
        result.metadata.set("frequency", json_helper::toJsonValue(frequency.toStdString()));
        result.metadata.set("lookbackPeriod", json_helper::toJsonValue(params_.lookbackPeriod));
        result.metadata.set("laggedEnabled", json_helper::toJsonValue(params_.laggedEnabled));
        result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
        result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(params_.neutralizationEnabled));
        result.metadata.set("neutralizationMode", json_helper::toJsonValue(neutralizationMode.toStdString()));
        result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    };

    auto resolveMacroEffectiveDate = [&]() {
        QString effectiveDate = QString::fromStdString(context.date);
        QDate anchorDate = QDate::fromString(effectiveDate, Qt::ISODate);
        if (anchorDate.isValid()) {
            if (frequency == QStringLiteral("weekly")) {
                const int shiftToPreviousFriday = anchorDate.dayOfWeek() >= 5 ? anchorDate.dayOfWeek() - 5 : anchorDate.dayOfWeek() + 2;
                anchorDate = anchorDate.addDays(-shiftToPreviousFriday);
            } else if (frequency == QStringLiteral("monthly")) {
                anchorDate = QDate(anchorDate.year(), anchorDate.month(), 1).addDays(-1);
            } else if (frequency == QStringLiteral("quarterly")) {
                const int quarterStartMonth = ((anchorDate.month() - 1) / 3) * 3 + 1;
                anchorDate = QDate(anchorDate.year(), quarterStartMonth, 1).addDays(-1);
            }
            effectiveDate = anchorDate.toString(Qt::ISODate);
        }

        const int maxOffset = (std::max)(0, params_.lookbackPeriod);
        const int startOffset = params_.laggedEnabled ? 1 : 0;
        const std::vector<std::string> benchmarkSymbols{benchmarkSymbol.toStdString()};
        for (int offset = startOffset; offset <= maxOffset; ++offset) {
            const QString candidate = anchorDate.isValid()
                ? anchorDate.addDays(-offset).toString(Qt::ISODate)
                : effectiveDate;
            if (context.historicalView->getCrossSection(candidate.toStdString(), priceField.toStdString(), symbols).empty()) {
                continue;
            }

            bool hasBenchmarkSeries = false;
            for (const auto& fieldName : benchmarkFields) {
                if (!context.historicalView->getCrossSection(candidate.toStdString(), fieldName, benchmarkSymbols).empty()) {
                    hasBenchmarkSeries = true;
                    break;
                }
            }
            if (hasBenchmarkSeries) {
                return candidate;
            }
        }

        return effectiveDate;
    };

    const QString effectiveDate = resolveMacroEffectiveDate();
    effectiveContext.date = effectiveDate.toStdString();
    QString macroNeutralizationMode = params_.neutralizationEnabled
        ? QStringLiteral("requested")
        : QStringLiteral("disabled");

    std::unordered_map<std::string, double> weightedScores;
    std::unordered_map<std::string, int> scoreCounts;
    const auto priceSeriesBySymbol = fetchBatchSeriesMap(effectiveContext, priceField, resolvedWindow + 1);
    const SeriesMatrixBatch priceSeriesBatch = collectSeriesMatrix(priceSeriesBySymbol, 2);
    const Eigen::MatrixXd allSymbolReturns = buildReturnMatrix(priceSeriesBatch.values);
    std::unordered_map<std::string, std::vector<double>> benchmarkSeriesByField;
    if (context.historicalView && !benchmarkFields.empty()) {
        const auto benchmarkBatchValues = context.historicalView->getBatchTimeSeries(
            {benchmarkSymbol.toStdString()},
            effectiveDate.toStdString(),
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

    for (MacroIndicator indicator : selectedIndicators) {
        const MacroIndicatorSpec spec = macroIndicatorSpec(indicator);
        const double dimensionWeight = indicatorWeightForDimension(spec.dimension);

        std::vector<double> benchmarkSeries;
        const QStringList candidateFields = {macroIndicatorFieldName(indicator), spec.proxyField};
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
        appendCommonMetadata(effectiveDate, macroNeutralizationMode);
        return result;
    }

    for (const auto& [symbol, weightedScore] : weightedScores) {
        const int count = scoreCounts[symbol];
        if (count <= 0) {
            continue;
        }
        result.values[symbol] = std::clamp(std::tanh(weightedScore / static_cast<double>(count)), -1.0, 1.0);
    }

    if (params_.neutralizationEnabled) {
        QString errorMessage;
        if (!applyHistoricalViewIndustrySizeNeutralization(effectiveContext, result.values, &errorMessage)) {
            result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
            result.values.clear();
            macroNeutralizationMode = QStringLiteral("historical_view_neutralization_failed");
            appendCommonMetadata(effectiveDate, macroNeutralizationMode);
            return result;
        }
        macroNeutralizationMode = QStringLiteral("historical_view_cross_section_industry_size");
    }

    applyConfigurableStandardization(standardization, result.values);

    const double coverage = static_cast<double>(result.values.size()) / static_cast<double>((std::max)(size_t(1), symbols.size()));
    result.dataStatus.availability = result.values.size() == symbols.size() ? DataAvailability::AVAILABLE : DataAvailability::PARTIAL;
    result.dataStatus.coverage = coverage;
    result.dataStatus.message = "使用宏观代理敏感度运行时";
    appendCommonMetadata(effectiveDate, macroNeutralizationMode);
    return result;
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.historicalView = context.historicalView;
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
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用行业字段";
    const QString industryMetric = industryMetricField(params_.industryMetricKind);
    if (industryMetric.isEmpty()) {
        result.dataStatus = CalculationResult::createError("行业因子缺少有效 metric 枚举").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("行业因子缺少有效 metric 枚举"));
        return result;
    }
    const QString sectorType = normalizeSectorType(QString::fromStdString(params_.sectorType));
    const QString frequency = normalizeConfigurableFrequency(params_.frequency);
    const QString standardization = normalizeConfigurableStandardization(params_.standardization);
    const int window = (std::max)(1, params_.window);
    const double sectorWeight = sectorIndustryWeight(sectorType);

    auto appendCommonMetadata = [&](const QString& effectiveDate, const QString& neutralizationMode) {
        result.metadata.set("industryMetric", json_helper::toJsonValue(industryMetric.toStdString()));
        result.metadata.set("sectorType", json_helper::toJsonValue(sectorType.toStdString()));
        result.metadata.set("window", json_helper::toJsonValue(window));
        result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
        result.metadata.set("frequency", json_helper::toJsonValue(frequency.toStdString()));
        result.metadata.set("lookbackPeriod", json_helper::toJsonValue(params_.lookbackPeriod));
        result.metadata.set("laggedEnabled", json_helper::toJsonValue(params_.laggedEnabled));
        result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
        result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(params_.neutralizationEnabled));
        result.metadata.set("neutralizationMode", json_helper::toJsonValue(neutralizationMode.toStdString()));
        result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    };

    if (industryMetric.isEmpty()) {
        result.dataStatus = CalculationResult::createError("行业因子缺少有效行业指标配置").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("行业因子缺少有效行业指标配置"));
        appendCommonMetadata(QString::fromStdString(context.date), QStringLiteral("disabled"));
        return result;
    }

    auto resolveIndustryEffectiveDate = [&]() {
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

        const int maxOffset = (std::max)(0, params_.lookbackPeriod);
        const int startOffset = params_.laggedEnabled ? 1 : 0;
        const std::vector<std::string> symbols = context.symbols.empty()
            ? context.historicalView->getAvailableSymbols(context.date)
            : context.symbols;
        for (int offset = startOffset; offset <= maxOffset; ++offset) {
            const QString candidate = anchorDate.isValid()
                ? anchorDate.addDays(-offset).toString(Qt::ISODate)
                : effectiveDate;
            CalculationContext candidateContext = context;
            candidateContext.date = candidate.toStdString();
            candidateContext.symbols = symbols;
            if (!currentFieldCrossSection(candidateContext, industryMetric).empty()) {
                return candidate;
            }
        }

        return effectiveDate;
    };

    const QString effectiveDate = resolveIndustryEffectiveDate();
    CalculationContext effectiveContext = context;
    effectiveContext.date = effectiveDate.toStdString();
    effectiveContext.symbols = effectiveSymbols(effectiveContext);
    QString industryNeutralizationMode = params_.neutralizationEnabled
        ? QStringLiteral("requested")
        : QStringLiteral("disabled");

    const auto metricValues = currentFieldCrossSection(effectiveContext, industryMetric);
    if (metricValues.empty()) {
        result.dataStatus = CalculationResult::createError("行业因子缺少真实行业字段，已禁止使用代理模型回测").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("行业因子缺少真实行业字段，已禁止使用代理模型回测"));
        appendCommonMetadata(effectiveDate, industryNeutralizationMode);
        return result;
    }

    const auto metricSeriesBySymbol = fetchBatchSeriesMap(effectiveContext, industryMetric, window);

    for (const auto& [symbol, value] : metricValues) {
        double resolvedValue = value;
        const auto seriesIt = metricSeriesBySymbol.find(symbol);
        if (seriesIt != metricSeriesBySymbol.end()) {
            const double aggregatedValue = safeFiniteMean(seriesIt->second);
            if (std::isfinite(aggregatedValue)) {
                resolvedValue = aggregatedValue;
            }
        }
        resolvedValue *= sectorWeight;
        if (std::isfinite(resolvedValue)) {
            result.values[symbol] = resolvedValue;
        }
    }

    if (result.values.empty()) {
        result.dataStatus = CalculationResult::createError("行业因子字段存在但没有可用数值").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("行业因子字段存在但没有可用数值"));
        appendCommonMetadata(effectiveDate, industryNeutralizationMode);
        return result;
    }

    if (params_.neutralizationEnabled) {
        QString errorMessage;
        if (!applyHistoricalViewIndustrySizeNeutralization(effectiveContext, result.values, &errorMessage)) {
            result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
            result.values.clear();
            industryNeutralizationMode = QStringLiteral("historical_view_neutralization_failed");
            appendCommonMetadata(effectiveDate, industryNeutralizationMode);
            return result;
        }
        industryNeutralizationMode = QStringLiteral("historical_view_cross_section_industry_size");
    }

    applyConfigurableStandardization(standardization, result.values);

    appendCommonMetadata(effectiveDate, industryNeutralizationMode);
    result.metadata.set("dataMode", json_helper::toJsonValue("direct_cross_section"));
    return result;
}

CalculationResult ConfigurableFactor::calculateSentiment(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用情绪字段/代理模型";

    const int window = (std::max)(5, params_.window);
    const auto symbols = effectiveSymbols(context);
    const QString metric = sentimentMetricField(params_.sentimentMetric);
    if (metric.isEmpty()) {
        result.dataStatus = CalculationResult::createError("情绪因子缺少有效 metric 枚举").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("情绪因子缺少有效 metric 枚举"));
        return result;
    }

    const QString frequency = normalizeConfigurableFrequency(params_.frequency);
    const QString standardization = normalizeConfigurableStandardization(params_.standardization);

    CalculationContext effectiveContext = context;
    effectiveContext.symbols = symbols;

    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);

    auto calculateSentimentBody = [&]() -> CalculationResult {
        auto resolveSentimentEffectiveDate = [&]() {
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

            const int maxOffset = (std::max)(0, params_.lookbackPeriod);
            const int startOffset = params_.laggedEnabled ? 1 : 0;
            for (int offset = startOffset; offset <= maxOffset; ++offset) {
                const QString candidate = anchorDate.isValid()
                    ? anchorDate.addDays(-offset).toString(Qt::ISODate)
                    : effectiveDate;
                CalculationContext candidateContext = effectiveContext;
                candidateContext.date = candidate.toStdString();
                if (!currentFieldCrossSection(candidateContext, metric).empty()) {
                    return candidate;
                }
            }

            return effectiveDate;
        };

        const QString effectiveDate = resolveSentimentEffectiveDate();
        effectiveContext.date = effectiveDate.toStdString();
        QString neutralizationMode = params_.neutralizationEnabled
            ? QStringLiteral("requested")
            : QStringLiteral("disabled");

        const auto appendCommonMetadata = [&](const QString& resolvedNeutralizationMode) {
            result.metadata.set("metric", json_helper::toJsonValue(metric.toStdString()));
            result.metadata.set("sentimentSource", json_helper::toJsonValue(static_cast<int>(params_.sentimentSource)));
            result.metadata.set("dataMode", json_helper::toJsonValue("direct"));
            result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
            result.metadata.set("frequency", json_helper::toJsonValue(frequency.toStdString()));
            result.metadata.set("lookbackPeriod", json_helper::toJsonValue(params_.lookbackPeriod));
            result.metadata.set("laggedEnabled", json_helper::toJsonValue(params_.laggedEnabled));
            result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
            result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(params_.neutralizationEnabled));
            result.metadata.set("neutralizationMode", json_helper::toJsonValue(resolvedNeutralizationMode.toStdString()));
            result.metadata.set("window", json_helper::toJsonValue(window));
        };

        const auto directMetricMap = currentFieldCrossSection(effectiveContext, metric);
        const auto metricSeriesBySymbol = fetchBatchSeriesMap(effectiveContext, metric, window);
        if (!directMetricMap.empty() || !metricSeriesBySymbol.empty()) {
            for (const auto& symbol : symbols) {
                double resolvedValue = std::numeric_limits<double>::quiet_NaN();
                const auto seriesIt = metricSeriesBySymbol.find(symbol);
                if (seriesIt != metricSeriesBySymbol.end()) {
                    resolvedValue = safeFiniteMean(seriesIt->second);
                }
                if (!std::isfinite(resolvedValue)) {
                    const auto directIt = directMetricMap.find(symbol);
                    if (directIt != directMetricMap.end()) {
                        resolvedValue = directIt->second;
                    }
                }
                if (std::isfinite(resolvedValue)) {
                    result.values[symbol] = resolvedValue;
                }
            }
            if (result.values.empty()) {
                result.dataStatus = CalculationResult::createError("情绪因子字段存在但没有可用数值").dataStatus;
                result.metadata.set("emptyReason", json_helper::toJsonValue("情绪因子字段存在但没有可用数值"));
                appendCommonMetadata(neutralizationMode);
                return result;
            }

            if (params_.neutralizationEnabled) {
                QString errorMessage;
                if (!applyHistoricalViewIndustrySizeNeutralization(effectiveContext, result.values, &errorMessage)) {
                    result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                    result.values.clear();
                    neutralizationMode = QStringLiteral("historical_view_neutralization_failed");
                    appendCommonMetadata(neutralizationMode);
                    return result;
                }
                neutralizationMode = QStringLiteral("historical_view_cross_section_industry_size");
            }

            applyConfigurableStandardization(standardization, result.values);
            const double coverage = static_cast<double>(result.values.size()) / static_cast<double>((std::max)(size_t(1), symbols.size()));
            result.dataStatus.availability = result.values.size() == symbols.size() ? DataAvailability::AVAILABLE : DataAvailability::PARTIAL;
            result.dataStatus.coverage = coverage;
            appendCommonMetadata(neutralizationMode);
            return result;
        }

        result.dataStatus = CalculationResult::createError("情绪因子缺少真实情绪字段，已禁止使用市场宽度代理回测").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("情绪因子缺少真实情绪字段，已禁止使用市场宽度代理回测"));
        appendCommonMetadata(neutralizationMode);
        return result;
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.historicalView = context.historicalView;
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
    const QString resolvedExpression = expression.trimmed();
    if (resolvedExpression.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("自定义因子必须显式提供 expression");
        }
        return results;
    }
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
    if (context.historicalView && !batchFields.empty()) {
        batchCrossSections = context.historicalView->getBatchCrossSections(context.date, symbols, batchFields);
        if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
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
    const QString frequency = normalizeConfigurableFrequency(params_.frequency);
    const QString standardization = normalizeConfigurableStandardization(params_.standardization);
    const auto symbols = effectiveSymbols(context);
    const QString customResolvedExpressionMode = QStringLiteral("configured");
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用自定义表达式";

    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);

    auto calculateCustomBody = [&]() -> CalculationResult {
        auto resolveCustomEffectiveDate = [&]() {
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

            const int maxOffset = (std::max)(0, params_.lookbackPeriod);
            const int startOffset = params_.laggedEnabled ? 1 : 0;
            for (int offset = startOffset; offset <= maxOffset; ++offset) {
                const QString candidate = anchorDate.isValid()
                    ? anchorDate.addDays(-offset).toString(Qt::ISODate)
                    : effectiveDate;
                CalculationContext candidateContext = context;
                candidateContext.date = candidate.toStdString();
                candidateContext.symbols = symbols;
                QString candidateError;
                const auto candidateValues = evaluateCustomExpression(candidateContext,
                                                                     QString::fromStdString(params_.expression),
                                                                     symbols,
                                                                     &candidateError);
                if (!candidateValues.empty()) {
                    return candidate;
                }
            }

            return effectiveDate;
        };

        const QString effectiveDate = resolveCustomEffectiveDate();
        CalculationContext effectiveContext = context;
        effectiveContext.date = effectiveDate.toStdString();
        effectiveContext.symbols = symbols;
        QString neutralizationMode = params_.neutralizationEnabled
            ? QStringLiteral("requested")
            : QStringLiteral("disabled");

        const auto appendCommonMetadata = [&](const QString& resolvedNeutralizationMode) {
            result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
            result.metadata.set("frequency", json_helper::toJsonValue(frequency.toStdString()));
            result.metadata.set("lookbackPeriod", json_helper::toJsonValue(params_.lookbackPeriod));
            result.metadata.set("laggedEnabled", json_helper::toJsonValue(params_.laggedEnabled));
            result.metadata.set("standardization", json_helper::toJsonValue(standardization.toStdString()));
            result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(params_.neutralizationEnabled));
            result.metadata.set("neutralizationMode", json_helper::toJsonValue(resolvedNeutralizationMode.toStdString()));
        };

        QString errorMessage;
        result.values = evaluateCustomExpression(effectiveContext,
                                                 QString::fromStdString(params_.expression),
                                                 symbols,
                                                 &errorMessage);

        if (result.values.empty()) {
            const QString fallbackError = errorMessage.isEmpty()
                ? QStringLiteral("自定义表达式没有可用字段数据")
                : errorMessage;
            result.dataStatus = CalculationResult::createError(fallbackError.toStdString()).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(fallbackError.toStdString()));
            appendCommonMetadata(neutralizationMode);
        } else if (!errorMessage.isEmpty()) {
            result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
            appendCommonMetadata(neutralizationMode);
        } else {
            if (params_.neutralizationEnabled) {
                QString neutralizationError;
                if (!applyHistoricalViewIndustrySizeNeutralization(effectiveContext, result.values, &neutralizationError)) {
                    result.dataStatus = CalculationResult::createError(neutralizationError.toStdString()).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(neutralizationError.toStdString()));
                    result.values.clear();
                    neutralizationMode = QStringLiteral("historical_view_neutralization_failed");
                    appendCommonMetadata(neutralizationMode);
                    result.metadata.set("expression", json_helper::toJsonValue(params_.expression));
                    result.metadata.set("customExpressionMode", json_helper::toJsonValue(customResolvedExpressionMode.toStdString()));
                    result.metadata.set("variableCount", json_helper::toJsonValue(static_cast<int>(params_.variables.size())));
                    result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
                    return result;
                }
                neutralizationMode = QStringLiteral("historical_view_cross_section_industry_size");
            }

            applyConfigurableStandardization(standardization, result.values);
            const double coverage = static_cast<double>(result.values.size()) / static_cast<double>((std::max)(size_t(1), symbols.size()));
            result.dataStatus.availability = result.values.size() == symbols.size() ? DataAvailability::AVAILABLE : DataAvailability::PARTIAL;
            result.dataStatus.coverage = coverage;
            appendCommonMetadata(neutralizationMode);
        }
        result.metadata.set("expression", json_helper::toJsonValue(params_.expression));
        result.metadata.set("customExpressionMode", json_helper::toJsonValue(customResolvedExpressionMode.toStdString()));
        result.metadata.set("variableCount", json_helper::toJsonValue(static_cast<int>(params_.variables.size())));
        result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
        return result;
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.historicalView = context.historicalView;
        BatchComputationCacheScope scope(cache);
        return calculateCustomBody();
    }

    return calculateCustomBody();
}

} // namespace factor