#include "domain/factor/include/QualityFactor.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"

#include <QDate>
#include <QString>

#include <unordered_set>

namespace factor {

namespace {

QString normalizedMetric(const std::string& metric)
{
    const QString normalized = QString::fromStdString(metric).trimmed().toLower();
    if (normalized == QString::fromUtf8("净资产收益率") || normalized == QStringLiteral("roe")) {
        return QStringLiteral("roe");
    }
    if (normalized == QString::fromUtf8("总资产收益率") || normalized == QStringLiteral("roa")) {
        return QStringLiteral("roa");
    }
    if (normalized == QString::fromUtf8("营业利润率") || normalized == QStringLiteral("operating_margin")) {
        return QStringLiteral("operating_margin");
    }
    if (normalized == QString::fromUtf8("毛利率") || normalized == QStringLiteral("gross_margin") || normalized == QStringLiteral("profit_margin")) {
        return QStringLiteral("gross_margin");
    }
    if (normalized == QStringLiteral("earnings_quality") || normalized == QStringLiteral("net_profit_to_equity") || normalized == QString::fromUtf8("收益质量")) {
        return QStringLiteral("earnings_quality");
    }
    return normalized;
}

QString resolveMetricColumn(const QString& metric)
{
    if (metric == "roe") {
        return "roe";
    }
    if (metric == "roa") {
        return "roa";
    }
    if (metric == "gross_margin" || metric == "operating_margin") {
        return "profit_margin";
    }
    return QString();
}

QString buildReportTypeClause(const std::string& timeframe, const QString& alias)
{
    const QString normalizedTimeframe = QString::fromStdString(timeframe).trimmed().toLower();
    if (normalizedTimeframe == "annual") {
        return QString(" AND %1.report_type = 'FY'").arg(alias);
    }
    if (normalizedTimeframe == "quarterly" || normalizedTimeframe == "ttm") {
        return QString(" AND %1.report_type IN ('Q1', 'Q2', 'Q3', 'Q4')").arg(alias);
    }
    return QString();
}

double normalizeThreshold(double threshold)
{
    return threshold > 1.0 ? threshold / 100.0 : threshold;
}

double computeQualityValue(const astock::database::QueryResultRow& row, const QString& metric)
{
    const QString directColumn = resolveMetricColumn(metric);
    if (!directColumn.isEmpty()) {
        return row.getDouble(directColumn);
    }

    const double netProfit = row.getDouble("net_profit");
    const double equity = row.getDouble("equity");
    if (netProfit <= 0.0 || equity <= 0.0) {
        return 0.0;
    }
    return netProfit / equity;
}

}

QualityFactor::QualityFactor() {
    factorType_ = "质量因子";
}

void QualityFactor::initializeFromDatabase(const std::string& instanceId) {
    BaseFactor::initializeFromDatabase(instanceId);
}

CalculationResult QualityFactor::calculate(const CalculationContext& context) {
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus = checkDataAvailability(context.date);

    if (!result.dataStatus.isValid()) {
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    }

    if (!db_) {
        result.dataStatus = CalculationResult::createError("数据库连接未初始化").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("数据库连接未初始化"));
        return result;
    }

    const QString metric = normalizedMetric(params_.metric);
    const double qualityThreshold = normalizeThreshold(params_.qualityThreshold);
    const QString innerReportTypeClause = buildReportTypeClause(params_.timeframe, "base");
    const QString outerReportTypeClause = buildReportTypeClause(params_.timeframe, "fi");
    const std::unordered_set<std::string> requestedSymbols(context.symbols.begin(), context.symbols.end());

    const QString sql = QString(
        "SELECT si.symbol, fi.trade_date, fi.report_date, fi.report_type, fi.roe, fi.roa, fi.profit_margin, fi.net_profit, fi.equity "
        "FROM financial_indicator_daily fi "
        "JOIN symbol_info si ON si.symbol_id = fi.symbol_id "
        "JOIN ("
        "    SELECT base.symbol_id, MAX(base.trade_date) AS latest_trade_date "
        "    FROM financial_indicator_daily base "
        "    WHERE base.trade_date <= :date%1 "
        "    GROUP BY base.symbol_id"
        ") latest ON latest.symbol_id = fi.symbol_id AND latest.latest_trade_date = fi.trade_date "
        "WHERE 1=1%2 "
        "ORDER BY si.symbol")
        .arg(innerReportTypeClause, outerReportTypeClause);

    auto queryResult = db_->executeQuery(sql, {{":date", QString::fromStdString(context.date)}});

    for (size_t i = 0; i < queryResult.rowCount(); ++i) {
        const auto& row = queryResult.getRow(i);
        const std::string symbol = row.getString("symbol").toStdString();
        if (!requestedSymbols.empty() && requestedSymbols.find(symbol) == requestedSymbols.end()) {
            continue;
        }

        const double factorValue = computeQualityValue(row, metric);
        if (factorValue <= 0.0 || factorValue < qualityThreshold) {
            continue;
        }

        result.values[symbol] = factorValue;
    }

    if (result.values.empty()) {
        result.dataStatus.availability = DataAvailability::UNAVAILABLE;
        result.dataStatus.coverage = 0.0;
        result.dataStatus.message = "未查询到满足条件的质量因子数据";
        result.metadata.set("error", json_helper::toJsonValue(result.dataStatus.message));
        return result;
    }

    result.values = handleOutliers(applyBoundaryRules(result.values, context));
    result.metadata.set("metric", json_helper::toJsonValue(metric.toStdString()));
    result.metadata.set("timeframe", json_helper::toJsonValue(params_.timeframe));
    result.metadata.set("quality_threshold", json_helper::toJsonValue(qualityThreshold));
    result.metadata.set("symbol_count", json_helper::toJsonValue(static_cast<int>(result.values.size())));
    return result;
}

DataRequirements QualityFactor::getDataRequirements() const {
    DataRequirements req;
    const QString metric = normalizedMetric(params_.metric);
    if (metric == "roe") {
        req.requiredFields = {"roe"};
    } else if (metric == "roa") {
        req.requiredFields = {"roa"};
    } else if (metric == "gross_margin" || metric == "operating_margin") {
        req.requiredFields = {"profit_margin"};
    } else {
        req.requiredFields = {"net_profit", "equity"};
    }
    return req;
}

BoundaryRules QualityFactor::getBoundaryRules() const {
    BoundaryRules rules;
    rules.minDataPoints = 1;
    return rules;
}

std::shared_ptr<QualityFactor> QualityFactor::create(
    const std::string& instanceId,
    std::shared_ptr<astock::database::QtMySQLDatabase> db,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {

    auto factor = std::make_shared<QualityFactor>();
    factor->db_ = db;
    factor->dataChecker_ = dataChecker;
    factor->initializeFromDatabase(instanceId);
    return factor;
}

void QualityFactor::loadConfig(const foundation::json::JsonFacade& config) {
    BaseFactor::loadConfig(config);
    if (config.has("calculation")) {
        const auto calculation = config.get("calculation");
        params_.fromJson(calculation);
        if (params_.metric.empty() && calculation.has("qualityMetrics")) {
            const auto metrics = calculation.get("qualityMetrics");
            if (metrics.isArray() && metrics.size() > 0) {
                params_.metric = metrics.at(0).asString();
            }
        }
        params_.metric = normalizedMetric(params_.metric).toStdString();
    }
    dataRequirements_.requiredFields = getDataRequirements().requiredFields;
}

} // namespace factor