#include "domain/factor/include/LowVolFactor.h"
#include "domain/factor/include/FactorDataProvider.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace factor {

namespace {

QString earliestLowVolSeriesDate(const QDate& anchorDate, int window)
{
    const int lookbackDays = std::max(45, (window + 10) * 2);
    return anchorDate.addDays(-lookbackDays).toString("yyyy-MM-dd");
}

QString benchmarkSymbolFromParameters(const foundation::json::JsonFacade& parameters)
{
    if (parameters.has("benchmarkSymbol")) {
        return QString::fromStdString(parameters.get("benchmarkSymbol").asString()).trimmed();
    }
    return QStringLiteral("000300.SH");
}

std::vector<std::string> selectedComponentsOrDefault(const std::vector<std::string>& components)
{
    if (!components.empty()) {
        return components;
    }
    return {"volatility", "drawdown", "beta"};
}

double normalizedComponentWeight(double weight)
{
    return weight > 0.0 ? weight : 0.0;
}

std::vector<double> extractCloses(const std::vector<FactorDataPoint>& series)
{
    std::vector<double> closes;
    closes.reserve(series.size());
    for (const auto& point : series) {
        closes.push_back(point.value);
    }
    return closes;
}

std::unordered_map<std::string, double> buildCloseLookup(const std::vector<FactorDataPoint>& series)
{
    std::unordered_map<std::string, double> lookup;
    lookup.reserve(series.size());
    for (const auto& point : series) {
        if (std::isfinite(point.value) && point.value > 0.0) {
            lookup.emplace(point.date, point.value);
        }
    }
    return lookup;
}

double normalizedInverseScore(double rawValue, double minValue, double maxValue)
{
    if (!std::isfinite(rawValue)) {
        return 0.0;
    }
    if (!std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) {
        return 1.0;
    }
    return (maxValue - rawValue) / (maxValue - minValue);
}

}

LowVolFactor::LowVolFactor() {
    factorType_ = "低波因子";
}

void LowVolFactor::initializeFromDatabase(const std::string& instanceId) {
    BaseFactor::initializeFromDatabase(instanceId);
}

CalculationResult LowVolFactor::calculate(const CalculationContext& context) {
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;

    auto failWithMessage = [&](const QString& message) {
        result.dataStatus.availability = DataAvailability::UNAVAILABLE;
        result.dataStatus.coverage = 0.0;
        result.dataStatus.message = message.toStdString();
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    };

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

    const QDate endDate = QDate::fromString(QString::fromStdString(context.date), "yyyy-MM-dd");
    const QString startDate = earliestLowVolSeriesDate(endDate, params_.window);
    const std::vector<std::string> components = selectedComponentsOrDefault(params_.components);
    const bool needsBeta = std::find(components.begin(), components.end(), "beta") != components.end();
    const QString benchmarkSymbol = benchmarkSymbolFromParameters(context.parameters);
    const std::map<std::string, double> componentWeights = {
        {"volatility", normalizedComponentWeight(params_.volatilityWeight)},
        {"drawdown", normalizedComponentWeight(params_.drawdownWeight)},
        {"beta", normalizedComponentWeight(params_.betaWeight)}
    };

    double activeWeightSum = 0.0;
    for (const auto& component : components) {
        activeWeightSum += componentWeights.at(component);
    }
    if (activeWeightSum <= 0.0) {
        return failWithMessage(QStringLiteral("低波因子权重总和不能为 0"));
    }

    std::vector<FactorDataPoint> benchmarkSeries;
    if (needsBeta) {
        auto loadBenchmarkFromDatabase = [&]() {
            auto benchmarkQuery = db_->executeQuery(
                "SELECT trade_date, close FROM daily_bar "
                "WHERE symbol = :symbol AND trade_date BETWEEN :start_date AND :end_date "
                "ORDER BY trade_date",
                {
                    {":symbol", benchmarkSymbol},
                    {":start_date", startDate},
                    {":end_date", endDate.toString("yyyy-MM-dd")}
                }
            );

            std::vector<FactorDataPoint> series;
            series.reserve(benchmarkQuery.rowCount());
            for (size_t i = 0; i < benchmarkQuery.rowCount(); ++i) {
                const auto& row = benchmarkQuery.getRow(i);
                series.push_back(FactorDataPoint{row.getString("trade_date").toStdString(), row.getDouble("close")});
            }
            return series;
        };

        benchmarkSeries = loadBenchmarkFromDatabase();

        if (benchmarkSeries.size() < 2) {
            return failWithMessage(QStringLiteral("低波因子需要基准指数 %1 的收盘价序列，但当前数据不可用").arg(benchmarkSymbol));
        }
    }

    std::map<std::string, std::vector<FactorDataPoint>> seriesBySymbol;
    if (context.dataProvider && context.dataProvider->hasField("close")) {
        std::vector<std::string> symbols = context.symbols;
        if (symbols.empty()) {
            symbols = context.dataProvider->getAvailableSymbols(context.date);
        }
        for (const auto& symbol : symbols) {
            seriesBySymbol[symbol] = context.dataProvider->getSeries(
                symbol,
                startDate.toStdString(),
                endDate.toString("yyyy-MM-dd").toStdString(),
                "close"
            );
        }
    } else {
        auto queryResult = db_->executeQuery(
            "SELECT symbol, trade_date, close FROM daily_bar "
            "WHERE trade_date BETWEEN :start_date AND :end_date "
            "ORDER BY symbol, trade_date",
            {
                {":start_date", startDate},
                {":end_date", endDate.toString("yyyy-MM-dd")}
            }
        );

        for (size_t i = 0; i < queryResult.rowCount(); ++i) {
            const auto& row = queryResult.getRow(i);
            seriesBySymbol[row.getString("symbol").toStdString()].push_back(
                FactorDataPoint{row.getString("trade_date").toStdString(), row.getDouble("close")}
            );
        }
    }

    std::map<std::string, SymbolMetrics> metricsBySymbol;
    for (const auto& [symbol, series] : seriesBySymbol) {
        if (static_cast<int>(series.size()) < params_.window) {
            continue;
        }

        const auto trailingBegin = series.end() - params_.window;
        std::vector<FactorDataPoint> trailingSeries(trailingBegin, series.end());

        SymbolMetrics metrics;
        if (std::find(components.begin(), components.end(), "volatility") != components.end()) {
            metrics.volatility = computeVolatility(extractCloses(trailingSeries));
        }
        if (std::find(components.begin(), components.end(), "drawdown") != components.end()) {
            metrics.maxDrawdown = computeMaxDrawdown(extractCloses(trailingSeries));
        }
        if (needsBeta) {
            metrics.beta = computeBeta(trailingSeries, benchmarkSeries);
        }

        if ((std::find(components.begin(), components.end(), "volatility") != components.end() && !metrics.volatility.has_value())
            || (std::find(components.begin(), components.end(), "drawdown") != components.end() && !metrics.maxDrawdown.has_value())
            || (needsBeta && !metrics.beta.has_value())) {
            continue;
        }

        metricsBySymbol[symbol] = std::move(metrics);
    }

    if (metricsBySymbol.empty()) {
        return failWithMessage(QStringLiteral("低波因子没有可用样本"));
    }

    std::map<std::string, double> weightedScores;
    std::map<std::string, double> usedWeights;
    auto accumulateScores = [&](const char* componentName, std::optional<double> SymbolMetrics::*member) {
        const std::string componentKey = componentName == std::string("volatility_count") ? "volatility"
            : componentName == std::string("drawdown_count") ? "drawdown"
            : "beta";
        const double componentWeight = componentWeights.at(componentKey);
        if (componentWeight <= 0.0) {
            return;
        }

        std::vector<std::pair<std::string, double>> rawValues;
        rawValues.reserve(metricsBySymbol.size());
        for (const auto& [symbol, metrics] : metricsBySymbol) {
            const auto& metricValue = metrics.*member;
            if (metricValue.has_value()) {
                rawValues.emplace_back(symbol, *metricValue);
            }
        }

        if (rawValues.empty()) {
            return;
        }

        double minValue = rawValues.front().second;
        double maxValue = rawValues.front().second;
        for (const auto& [symbol, value] : rawValues) {
            (void)symbol;
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }

        for (const auto& [symbol, value] : rawValues) {
            weightedScores[symbol] += normalizedInverseScore(value, minValue, maxValue) * componentWeight;
            usedWeights[symbol] += componentWeight;
        }

        result.metadata.set(componentName, json_helper::toJsonValue(static_cast<int>(rawValues.size())));
    };

    if (std::find(components.begin(), components.end(), "volatility") != components.end()) {
        accumulateScores("volatility_count", &SymbolMetrics::volatility);
    }
    if (std::find(components.begin(), components.end(), "drawdown") != components.end()) {
        accumulateScores("drawdown_count", &SymbolMetrics::maxDrawdown);
    }
    if (needsBeta) {
        accumulateScores("beta_count", &SymbolMetrics::beta);
    }

    for (const auto& [symbol, weightedScore] : weightedScores) {
        const double denominator = usedWeights[symbol];
        if (denominator <= 0.0) {
            continue;
        }
        result.values[symbol] = weightedScore / denominator;
    }

    result.metadata.set("window", json_helper::toJsonValue(params_.window));
    result.metadata.set("volatility_type", json_helper::toJsonValue(params_.volatilityType));
    auto componentsJson = foundation::json::JsonFacade::createArray();
    for (const auto& component : components) {
        componentsJson.push_back(json_helper::toJsonValue(component));
    }
    result.metadata.set("components", componentsJson);
    if (needsBeta) {
        result.metadata.set("benchmark_symbol", json_helper::toJsonValue(benchmarkSymbol.toStdString()));
    }
    result.metadata.set("volatility_weight", json_helper::toJsonValue(params_.volatilityWeight));
    result.metadata.set("drawdown_weight", json_helper::toJsonValue(params_.drawdownWeight));
    result.metadata.set("beta_weight", json_helper::toJsonValue(params_.betaWeight));
    result.metadata.set("symbol_count", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    return result;
}

DataRequirements LowVolFactor::getDataRequirements() const {
    DataRequirements req;
    req.requiredFields = {"close"};
    return req;
}

BoundaryRules LowVolFactor::getBoundaryRules() const {
    BoundaryRules rules;
    rules.minDataPoints = params_.window;
    rules.handleOutliers = "winsorize_3sigma";
    return rules;
}

std::shared_ptr<LowVolFactor> LowVolFactor::create(
    const std::string& instanceId,
    std::shared_ptr<astock::database::QtMySQLDatabase> db,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {

    auto factor = std::make_shared<LowVolFactor>();
    factor->db_ = db;
    factor->dataChecker_ = dataChecker;
    factor->initializeFromDatabase(instanceId);
    return factor;
}

std::optional<double> LowVolFactor::computeVolatility(const std::vector<double>& closes) const {
    if (closes.size() < 2) {
        return std::nullopt;
    }

    std::vector<double> returns;
    returns.reserve(closes.size() - 1);
    for (size_t i = 1; i < closes.size(); ++i) {
        if (closes[i - 1] <= 0.0 || closes[i] <= 0.0) {
            continue;
        }
        returns.push_back((closes[i] - closes[i - 1]) / closes[i - 1]);
    }

    if (returns.empty()) {
        return std::nullopt;
    }

    const double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double variance = 0.0;
    for (double value : returns) {
        const double delta = value - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(returns.size());
    return std::sqrt(variance);
}

std::optional<double> LowVolFactor::computeMaxDrawdown(const std::vector<double>& closes) const {
    if (closes.size() < 2) {
        return std::nullopt;
    }

    double peak = 0.0;
    double maxDrawdown = 0.0;
    bool hasValidClose = false;
    for (double close : closes) {
        if (!std::isfinite(close) || close <= 0.0) {
            continue;
        }
        hasValidClose = true;
        peak = peak <= 0.0 ? close : std::max(peak, close);
        if (peak > 0.0) {
            maxDrawdown = std::max(maxDrawdown, (peak - close) / peak);
        }
    }

    if (!hasValidClose) {
        return std::nullopt;
    }

    return maxDrawdown;
}

std::optional<double> LowVolFactor::computeBeta(
    const std::vector<FactorDataPoint>& symbolSeries,
    const std::vector<FactorDataPoint>& benchmarkSeries) const {

    if (symbolSeries.size() < 2 || benchmarkSeries.size() < 2) {
        return std::nullopt;
    }

    const auto benchmarkLookup = buildCloseLookup(benchmarkSeries);

    std::vector<double> symbolReturns;
    std::vector<double> benchmarkReturns;
    symbolReturns.reserve(symbolSeries.size() - 1);
    benchmarkReturns.reserve(symbolSeries.size() - 1);

    for (size_t i = 1; i < symbolSeries.size(); ++i) {
        const auto& previousSymbol = symbolSeries[i - 1];
        const auto& currentSymbol = symbolSeries[i];
        if (!std::isfinite(previousSymbol.value) || !std::isfinite(currentSymbol.value)
            || previousSymbol.value <= 0.0 || currentSymbol.value <= 0.0) {
            continue;
        }

        const auto previousBenchmark = benchmarkLookup.find(previousSymbol.date);
        const auto currentBenchmark = benchmarkLookup.find(currentSymbol.date);
        if (previousBenchmark == benchmarkLookup.end() || currentBenchmark == benchmarkLookup.end()) {
            continue;
        }

        if (previousBenchmark->second <= 0.0 || currentBenchmark->second <= 0.0) {
            continue;
        }

        symbolReturns.push_back((currentSymbol.value - previousSymbol.value) / previousSymbol.value);
        benchmarkReturns.push_back((currentBenchmark->second - previousBenchmark->second) / previousBenchmark->second);
    }

    if (symbolReturns.size() < 2 || benchmarkReturns.size() < 2) {
        return std::nullopt;
    }

    const double symbolMean = std::accumulate(symbolReturns.begin(), symbolReturns.end(), 0.0) / symbolReturns.size();
    const double benchmarkMean = std::accumulate(benchmarkReturns.begin(), benchmarkReturns.end(), 0.0) / benchmarkReturns.size();

    double covariance = 0.0;
    double benchmarkVariance = 0.0;
    for (size_t i = 0; i < symbolReturns.size() && i < benchmarkReturns.size(); ++i) {
        const double symbolDelta = symbolReturns[i] - symbolMean;
        const double benchmarkDelta = benchmarkReturns[i] - benchmarkMean;
        covariance += symbolDelta * benchmarkDelta;
        benchmarkVariance += benchmarkDelta * benchmarkDelta;
    }

    if (benchmarkVariance <= 0.0) {
        return std::nullopt;
    }

    covariance /= static_cast<double>(symbolReturns.size());
    benchmarkVariance /= static_cast<double>(benchmarkReturns.size());
    if (benchmarkVariance <= 0.0) {
        return std::nullopt;
    }

    return covariance / benchmarkVariance;
}

void LowVolFactor::loadConfig(const foundation::json::JsonFacade& config) {
    BaseFactor::loadConfig(config);
    if (config.has("calculation")) {
        const auto calculation = config.get("calculation");
        params_.fromJson(calculation);
        if (calculation.has("volatilityWindow")) {
            params_.window = calculation.get("volatilityWindow").asInt();
        }
    }
    dataRequirements_.requiredFields = {"close"};
}

} // namespace factor