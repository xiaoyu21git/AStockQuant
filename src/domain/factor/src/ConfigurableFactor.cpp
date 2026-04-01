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

}

void ConfigurableFactor::Params::fromJson(const foundation::json::JsonFacade& json)
{
    variables.clear();
    if (json.has("factor_type")) configuredType = json.get("factor_type").asString();
    if (configuredType.empty() && json.has("factorType")) configuredType = json.get("factorType").asString();
    if (json.has("metric")) metric = json.get("metric").asString();
    if (metric.empty() && json.has("liquidity_metric")) metric = json.get("liquidity_metric").asString();
    if (metric.empty() && json.has("valuation_type")) metric = json.get("valuation_type").asString();
    if (json.has("timeframe")) timeframe = json.get("timeframe").asString();
    if (json.has("indicator_type")) indicatorType = json.get("indicator_type").asString();
    if (json.has("sentiment_source")) sentimentSource = json.get("sentiment_source").asString();
    if (json.has("expression")) expression = json.get("expression").asString();
    if (json.has("sector_type")) sectorType = json.get("sector_type").asString();
    if (json.has("macro_factor")) macroFactor = json.get("macro_factor").asString();
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
    if (json.has("lookback_period")) lookbackPeriod = json.get("lookback_period").asInt();
    if (json.has("min_dividend_yield")) minDividendYield = json.get("min_dividend_yield").asDouble();
    if (json.has("sentiment_weight")) sentimentWeight = json.get("sentiment_weight").asDouble();
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
    return QString::fromStdString(params_.metric).trimmed().toLower();
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
    const std::vector<std::string> symbols = effectiveSymbols(context);
    if (context.dataProvider && context.dataProvider->hasField(field.toStdString())) {
        return context.dataProvider->getCrossSection(context.date, field.toStdString(), symbols);
    }
    std::unordered_map<std::string, double> result;
    if (!db_) {
        return result;
    }

    const QString sql = QString("SELECT symbol, %1 AS field_value FROM daily_bar WHERE trade_date = :date AND %1 IS NOT NULL")
        .arg(field);
    auto queryResult = db_->executeQuery(sql, makeNamedParams({{":date", QString::fromStdString(context.date)}}));
    const std::unordered_set<std::string> requested(symbols.begin(), symbols.end());
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
    if (!db_) {
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
    if (!db_ || limit <= 0) {
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
    const int window = (std::max)(1, params_.window);
    const auto symbols = effectiveSymbols(context);
    size_t populatedSymbolCount = 0;
    for (const auto& symbol : symbols) {
        double score = std::numeric_limits<double>::quiet_NaN();
        if (metric == "volume") {
            const auto values = seriesForField(context, symbol, "volume", window);
            score = safeMean(values);
        } else if (metric == "amplitude") {
            const auto values = seriesForField(context, symbol, "amplitude", window);
            score = -safeMean(values);
        } else if (metric == "amihud_illiquidity") {
            const auto closes = seriesForField(context, symbol, "close", window + 1);
            const auto volumes = seriesForField(context, symbol, "volume", window + 1);
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
            const auto values = seriesForField(context, symbol, "turnover_rate", window);
            score = safeMean(values);
        }

        if (std::isfinite(score) && score != 0.0) {
            result.values[symbol] = score;
            ++populatedSymbolCount;
        }
    }

    result.metadata.set("metric", json_helper::toJsonValue(metric.toStdString()));
    result.metadata.set("window", json_helper::toJsonValue(window));

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

    const QString indicatorType = QString::fromStdString(params_.indicatorType).trimmed();
    const int window = (std::max)(2, params_.window);
    const auto symbols = effectiveSymbols(context);
    for (const auto& symbol : symbols) {
        double score = std::numeric_limits<double>::quiet_NaN();
        if (indicatorType == QString::fromUtf8("成交量指标")) {
            auto volumeSeries = seriesForField(context, symbol, "volume", window);
            if (volumeSeries.size() >= 2) {
                const double latest = volumeSeries.back();
                volumeSeries.pop_back();
                score = safeRatio(latest - safeMean(volumeSeries), safeMean(volumeSeries));
            }
        } else if (indicatorType == QString::fromUtf8("波动率指标")) {
            const auto closes = seriesForField(context, symbol, "close", window + 1);
            std::vector<double> returns;
            for (size_t i = 1; i < closes.size(); ++i) {
                if (closes[i - 1] <= 0.0) {
                    continue;
                }
                returns.push_back((closes[i] - closes[i - 1]) / closes[i - 1]);
            }
            score = -safeStdDev(returns);
        } else if (indicatorType == QString::fromUtf8("动量指标")) {
            const auto closes = seriesForField(context, symbol, "close", window + 1);
            if (closes.size() >= 2 && closes.front() > 0.0) {
                score = (closes.back() - closes.front()) / closes.front();
            }
        } else {
            const auto closes = seriesForField(context, symbol, "close", window);
            if (closes.size() >= 2) {
                const double latest = closes.back();
                score = safeRatio(latest - safeMean(closes), safeMean(closes));
            }
        }

        if (std::isfinite(score) && score != 0.0) {
            result.values[symbol] = score;
        }
    }

    result.metadata.set("indicator_type", json_helper::toJsonValue(indicatorType.toStdString()));
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
    result.dataStatus.message = "使用红利代理模型";

    const QString metric = normalizedMetric();
    const auto peMap = currentFieldCrossSection(context, "pe_ratio");
    const auto pbMap = currentFieldCrossSection(context, "pb_ratio");
    const auto roeMap = latestFinancialMetric(context, "roe", QString::fromStdString(context.date));
    const auto marginMap = latestFinancialMetric(context, "profit_margin", QString::fromStdString(context.date));
    const auto stabilityMap = latestFinancialSeries(context, "net_profit", QString::fromStdString(context.date), 4);
    const auto symbols = effectiveSymbols(context);

    for (const auto& symbol : symbols) {
        const double pe = peMap.count(symbol) ? peMap.at(symbol) : 0.0;
        const double pb = pbMap.count(symbol) ? pbMap.at(symbol) : 0.0;
        const double roe = roeMap.count(symbol) ? roeMap.at(symbol) : 0.0;
        const double margin = marginMap.count(symbol) ? marginMap.at(symbol) : 0.0;
        double score = 0.0;

        if (metric == "payout_ratio") {
            score = std::max(0.0, 0.6 * margin + 0.4 * roe);
        } else if (metric == "dividend_stability") {
            const auto seriesIt = stabilityMap.find(symbol);
            if (seriesIt == stabilityMap.end() || seriesIt->second.size() < 2) {
                continue;
            }
            const double meanProfit = std::abs(safeMean(seriesIt->second));
            const double stdevProfit = safeStdDev(seriesIt->second);
            score = meanProfit <= 1e-12 ? 0.0 : 1.0 / (1.0 + safeRatio(stdevProfit, meanProfit));
        } else {
            const double valuationProxy = pe > 0.0 ? (1.0 / pe) : (pb > 0.0 ? 1.0 / pb : 0.0);
            score = std::max(0.0, 0.7 * valuationProxy + 0.3 * std::max(0.0, roe));
            if (params_.minDividendYield > 0.0 && score < params_.minDividendYield / 100.0) {
                continue;
            }
        }

        if (std::isfinite(score) && score > 0.0) {
            result.values[symbol] = score;
        }
    }

    result.metadata.set("metric", json_helper::toJsonValue(metric.toStdString()));
    result.metadata.set("proxy", json_helper::toJsonValue("valuation_profitability_proxy"));
    return result;
}

CalculationResult ConfigurableFactor::calculateMacroSector(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    if (context.dataProvider) {
        result.dataStatus.availability = DataAvailability::AVAILABLE;
        result.dataStatus.coverage = 1.0;
        result.dataStatus.message = "使用行业轮动代理模型";
    } else {
        result.dataStatus = checkDataAvailability(context.date);
    }
    if (!result.dataStatus.isValid()) {
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    }

    const int window = (std::max)(5, params_.window);
    const auto symbols = effectiveSymbols(context);
    const auto industryMap = industryBySymbol(context);
    std::unordered_map<std::string, double> stockReturn;
    std::unordered_map<QString, std::vector<double>> industryReturns;

    for (const auto& symbol : symbols) {
        const auto closes = seriesForField(context, symbol, "close", window + 1);
        if (closes.size() < 2 || closes.front() <= 0.0) {
            continue;
        }
        const double value = (closes.back() - closes.front()) / closes.front();
        stockReturn[symbol] = value;
        const auto industryIt = industryMap.find(symbol);
        if (industryIt != industryMap.end() && !industryIt->second.isEmpty()) {
            industryReturns[industryIt->second].push_back(value);
        }
    }

    std::unordered_map<QString, double> industryScore;
    for (const auto& [industry, values] : industryReturns) {
        industryScore[industry] = safeMean(values);
    }

    for (const auto& symbol : symbols) {
        const auto industryIt = industryMap.find(symbol);
        if (industryIt != industryMap.end() && industryScore.find(industryIt->second) != industryScore.end()) {
            result.values[symbol] = industryScore[industryIt->second];
        } else if (stockReturn.find(symbol) != stockReturn.end()) {
            result.values[symbol] = stockReturn[symbol];
        }
    }

    result.metadata.set("proxy", json_helper::toJsonValue("industry_rotation_proxy"));
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
        result.dataStatus.message = "使用情绪代理模型";
    } else {
        result.dataStatus = checkDataAvailability(context.date);
    }
    if (!result.dataStatus.isValid()) {
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    }

    const int window = (std::max)(5, params_.window);
    const auto symbols = effectiveSymbols(context);
    const auto changeMap = currentFieldCrossSection(context, "change_pct");
    const auto turnoverMap = currentFieldCrossSection(context, "turnover_rate");

    int upCount = 0;
    int downCount = 0;
    for (const auto& [symbol, change] : changeMap) {
        if (change > 0.0) ++upCount;
        else if (change < 0.0) ++downCount;
    }
    const double breadth = (upCount + downCount) == 0 ? 0.0 : static_cast<double>(upCount - downCount) / (upCount + downCount);

    for (const auto& symbol : symbols) {
        const auto closes = seriesForField(context, symbol, "close", window + 1);
        if (closes.size() < 2 || closes.front() <= 0.0) {
            continue;
        }
        const double stockMomentum = (closes.back() - closes.front()) / closes.front();
        const double turnover = turnoverMap.count(symbol) ? turnoverMap.at(symbol) : 0.0;
        const double score = (1.0 - params_.sentimentWeight) * stockMomentum + params_.sentimentWeight * breadth + 0.1 * turnover;
        if (std::isfinite(score)) {
            result.values[symbol] = score;
        }
    }

    result.metadata.set("proxy", json_helper::toJsonValue("market_breadth_proxy"));
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
    return result;
}

} // namespace factor