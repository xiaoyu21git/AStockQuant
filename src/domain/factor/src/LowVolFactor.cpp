#include "domain/factor/include/LowVolFactor.h"
#include "domain/factor/include/FactorDataProvider.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"

#include <cmath>
#include <numeric>

namespace factor {

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
    const QDate startDate = endDate.addDays(-(params_.window - 1));

    std::map<std::string, std::vector<double>> closesBySymbol;
    if (context.dataProvider && context.dataProvider->hasField("close")) {
        std::vector<std::string> symbols = context.symbols;
        if (symbols.empty()) {
            symbols = context.dataProvider->getAvailableSymbols(context.date);
        }
        for (const auto& symbol : symbols) {
            const auto series = context.dataProvider->getSeries(
                symbol,
                startDate.toString("yyyy-MM-dd").toStdString(),
                endDate.toString("yyyy-MM-dd").toStdString(),
                "close"
            );
            auto& closes = closesBySymbol[symbol];
            closes.reserve(series.size());
            for (const auto& point : series) {
                closes.push_back(point.value);
            }
        }
    } else {
        auto queryResult = db_->executeQuery(
            "SELECT symbol, trade_date, close FROM daily_bar "
            "WHERE trade_date BETWEEN :start_date AND :end_date "
            "ORDER BY symbol, trade_date",
            {
                {":start_date", startDate.toString("yyyy-MM-dd")},
                {":end_date", endDate.toString("yyyy-MM-dd")}
            }
        );

        for (size_t i = 0; i < queryResult.rowCount(); ++i) {
            const auto& row = queryResult.getRow(i);
            closesBySymbol[row.getString("symbol").toStdString()].push_back(row.getDouble("close"));
        }
    }

    for (const auto& [symbol, closes] : closesBySymbol) {
        if (static_cast<int>(closes.size()) < params_.window) {
            continue;
        }
        result.values[symbol] = -computeVolatility(closes);
    }

    result.metadata.set("window", json_helper::toJsonValue(params_.window));
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

double LowVolFactor::computeVolatility(const std::vector<double>& closes) const {
    if (closes.size() < 2) {
        return 0.0;
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
        return 0.0;
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

void LowVolFactor::loadConfig(const foundation::json::JsonFacade& config) {
    BaseFactor::loadConfig(config);
    if (config.has("calculation")) {
        params_.fromJson(config.get("calculation"));
    }
    dataRequirements_.requiredFields = {"close"};
}

} // namespace factor