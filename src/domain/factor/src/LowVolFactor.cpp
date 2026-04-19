#include "domain/factor/include/LowVolFactor.h"
#include "domain/factor/include/FactorDataProvider.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"

#include <cmath>
#include <numeric>

namespace factor {

namespace {

QString earliestLowVolSeriesDate(const QDate& anchorDate, int window)
{
    const int lookbackDays = std::max(45, (window + 10) * 2);
    return anchorDate.addDays(-lookbackDays).toString("yyyy-MM-dd");
}

QString normalizeVolatilityType(const std::string& rawVolatilityType)
{
    const QString volatilityType = QString::fromStdString(rawVolatilityType).trimmed().toLower();
    if (volatilityType == QStringLiteral("historical") || volatilityType == QStringLiteral("standard") || volatilityType == QString::fromUtf8("历史波动率")) {
        return QStringLiteral("standard");
    }
    if (volatilityType == QStringLiteral("downside") || volatilityType == QString::fromUtf8("下行波动率")) {
        return QStringLiteral("downside");
    }
    if (volatilityType == QStringLiteral("realized") || volatilityType == QString::fromUtf8("已实现波动率")) {
        return QStringLiteral("realized");
    }
    return QStringLiteral("standard");
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

    std::map<std::string, std::vector<double>> closesBySymbol;
    if (context.dataProvider && context.dataProvider->hasField("close")) {
        std::vector<std::string> symbols = context.symbols;
        if (symbols.empty()) {
            symbols = context.dataProvider->getAvailableSymbols(context.date);
        }
        for (const auto& symbol : symbols) {
            const auto series = context.dataProvider->getSeries(
                symbol,
                startDate.toStdString(),
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
                {":start_date", startDate},
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

        const auto trailingBegin = closes.end() - params_.window;
        std::vector<double> trailingCloses(trailingBegin, closes.end());
        result.values[symbol] = -computeVolatility(trailingCloses);
    }

    result.metadata.set("window", json_helper::toJsonValue(params_.window));
    result.metadata.set("volatility_type", json_helper::toJsonValue(normalizeVolatilityType(params_.volatilityType).toStdString()));
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

    const QString volatilityType = normalizeVolatilityType(params_.volatilityType);
    if (volatilityType == QStringLiteral("downside")) {
        std::vector<double> downsideReturns;
        downsideReturns.reserve(returns.size());
        for (double value : returns) {
            if (value < 0.0) {
                downsideReturns.push_back(value);
            }
        }
        if (downsideReturns.empty()) {
            return 0.0;
        }
        returns = std::move(downsideReturns);
    }

    if (volatilityType == QStringLiteral("realized")) {
        double squaredReturnSum = 0.0;
        for (double value : returns) {
            squaredReturnSum += value * value;
        }
        return std::sqrt(squaredReturnSum / static_cast<double>(returns.size()));
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
        const auto calculation = config.get("calculation");
        params_.fromJson(calculation);
        if (calculation.has("volatilityWindow")) {
            params_.window = calculation.get("volatilityWindow").asInt();
        }
        if (calculation.has("volatilityType")) {
            params_.volatilityType = calculation.get("volatilityType").asString();
        }
    }
    dataRequirements_.requiredFields = {"close"};
}

} // namespace factor