#include "domain/factor/include/DataAvailabilityChecker.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"
#include <algorithm>
#include <QSet>
#include <QStringList>

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

std::string resolveDateColumn(const std::string& table)
{
    if (table == "symbol_info") {
        return {};
    }
    if (table == "policy_data") {
        return "publish_time";
    }
    if (table == "news_sentiment") {
        return "trade_date";
    }
    if (table == "stock_news" || table == "news_data" || table == "news") {
        return "publish_time";
    }
    return table == "financial_indicator" ? "report_date" : "trade_date";
}

QString buildDatePredicate(const std::string& table, const QString& column)
{
    if (column.trimmed().isEmpty()) {
        return QString();
    }
    if (table == "financial_indicator") {
        return QString("%1 <= ?").arg(column);
    }
    if (table == "policy_data") {
        return QString("DATE(%1) <= ?").arg(column);
    }
    if (table == "news_sentiment" || table == "stock_news" || table == "news_data" || table == "news") {
        return QString("DATE(%1) <= ?").arg(column);
    }
    return QString("%1 = ?").arg(column);
}

QString normalizeFieldName(const QString& rawField)
{
    const QString field = rawField.trimmed().toLower();
    if (field == "adj_factor") {
        return {};
    }
    if (field == "revenue_growth") {
        return "total_revenue";
    }
    return field;
}

bool isDailyBarField(const QString& rawField)
{
    static const QSet<QString> dailyBarFields = {
        "open", "high", "low", "close", "pre_close", "volume", "turnover",
        "change_pct", "change_amt", "amplitude", "turnover_rate",
        "pe_ratio", "pb_ratio", "market_cap", "circulating_market_cap", "dividend_yield"
    };
    return dailyBarFields.contains(normalizeFieldName(rawField));
}

bool isFinancialField(const QString& rawField)
{
    static const QSet<QString> financialFields = {
        "roe", "roa", "profit_margin", "gross_margin", "operating_margin",
        "net_profit", "equity", "total_assets", "eps", "total_revenue", "payout_ratio",
        "operating_cash_flow"
    };
    return financialFields.contains(normalizeFieldName(rawField));
}

bool isSymbolInfoField(const QString& rawField)
{
    static const QSet<QString> symbolInfoFields = {
        "industry", "industry_code", "exchange", "asset_class", "status", "list_date", "name"
    };
    return symbolInfoFields.contains(normalizeFieldName(rawField));
}

bool isNewsField(const QString& rawField)
{
    static const QSet<QString> newsFields = {
        "sentiment_score", "market_sentiment", "investor_sentiment",
        "sector_sentiment", "theme_sentiment", "social_sentiment", "news_count"
    };
    return newsFields.contains(normalizeFieldName(rawField));
}

bool isPolicyField(const QString& rawField)
{
    static const QSet<QString> policyFields = {
        "policy_score", "policy_strength", "policy_count"
    };
    return policyFields.contains(normalizeFieldName(rawField));
}

bool isAlternativeField(const QString& rawField)
{
    static const QSet<QString> alternativeFields = {
        "hot_rank", "popularity_score", "comment_count", "comment_sentiment"
    };
    return alternativeFields.contains(normalizeFieldName(rawField));
}

bool isDerivativesField(const QString& rawField)
{
    static const QSet<QString> derivativesFields = {
        "futures_close", "futures_volume", "open_interest", "basis", "basis_rate"
    };
    return derivativesFields.contains(normalizeFieldName(rawField));
}

bool fieldRequiresPositiveValues(const QString& rawField)
{
    static const QSet<QString> positiveFields = {
        "open", "high", "low", "close", "pre_close", "volume", "turnover",
        "pe_ratio", "pb_ratio", "market_cap", "circulating_market_cap",
        "roe", "roa", "total_assets", "equity", "net_profit", "total_revenue", "eps", "dividend_yield",
        "policy_strength", "policy_count", "popularity_score", "comment_count",
        "futures_close", "futures_volume", "open_interest"
    };
    return positiveFields.contains(normalizeFieldName(rawField));
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
        {{":table_name", tableName}, {":column_name", columnName}});
    return !result.isEmpty() && result.getRow(0).getInt("count") > 0;
}

std::string normalizeSourceTableName(const std::string& rawSourceTable)
{
    const QString sourceTable = QString::fromStdString(rawSourceTable).trimmed().toLower();
    if (sourceTable.isEmpty()) {
        return {};
    }
    if (sourceTable == "market_sentiment") {
        return "news_sentiment";
    }
    if (sourceTable == "social_media" || sourceTable == "investor_sentiment") {
        return "news_sentiment";
    }
    if (sourceTable == "policy") {
        return "policy_data";
    }
    if (sourceTable == "alternative") {
        return "alternative_data";
    }
    if (sourceTable == "derivatives") {
        return "derivatives_data";
    }
    return sourceTable.toStdString();
}

std::vector<std::string> normalizeFields(const std::vector<std::string>& fields)
{
    std::vector<std::string> normalized;
    normalized.reserve(fields.size());
    for (const auto& field : fields) {
        // 旧配置里常把 adj_factor 作为必需字段，但当前回测链路实际使用的是未复权 close。
        if (field == "adj_factor") {
            continue;
        }
        if (field == "revenue_growth") {
            normalized.push_back("total_revenue");
            continue;
        }
        normalized.push_back(field);
    }
    return normalized;
}

std::string inferTableForFields(const std::vector<std::string>& fields)
{
    if (fields.empty()) {
        return "daily_bar";
    }

    bool hasDailyBar = false;
    bool hasFinancial = false;
    bool hasSymbolInfo = false;
    bool hasNews = false;
    bool hasPolicy = false;
    bool hasAlternative = false;
    bool hasDerivatives = false;
    for (const auto& field : fields) {
        const QString normalizedField = normalizeFieldName(QString::fromStdString(field));
        if (normalizedField.isEmpty()) {
            continue;
        }
        if (isDailyBarField(normalizedField)) {
            hasDailyBar = true;
        } else if (isFinancialField(normalizedField)) {
            hasFinancial = true;
        } else if (isSymbolInfoField(normalizedField)) {
            hasSymbolInfo = true;
        } else if (isNewsField(normalizedField)) {
            hasNews = true;
        } else if (isPolicyField(normalizedField)) {
            hasPolicy = true;
        } else if (isAlternativeField(normalizedField)) {
            hasAlternative = true;
        } else if (isDerivativesField(normalizedField)) {
            hasDerivatives = true;
        } else {
            hasDailyBar = true;
        }
    }

    const int tableKinds = static_cast<int>(hasDailyBar)
        + static_cast<int>(hasFinancial)
        + static_cast<int>(hasSymbolInfo)
        + static_cast<int>(hasNews)
        + static_cast<int>(hasPolicy)
        + static_cast<int>(hasAlternative)
        + static_cast<int>(hasDerivatives);
    if (tableKinds > 1) {
        return {};
    }

    if (hasFinancial && !hasDailyBar && !hasNews && !hasSymbolInfo && !hasPolicy && !hasAlternative && !hasDerivatives) {
        return "financial_indicator";
    }
    if (hasSymbolInfo && !hasDailyBar && !hasFinancial && !hasNews && !hasPolicy && !hasAlternative && !hasDerivatives) {
        return "symbol_info";
    }
    if (hasNews && !hasDailyBar && !hasFinancial && !hasSymbolInfo && !hasPolicy && !hasAlternative && !hasDerivatives) {
        return "news_sentiment";
    }
    if (hasPolicy && !hasDailyBar && !hasFinancial && !hasSymbolInfo && !hasNews && !hasAlternative && !hasDerivatives) {
        return "policy_data";
    }
    if (hasAlternative && !hasDailyBar && !hasFinancial && !hasSymbolInfo && !hasNews && !hasPolicy && !hasDerivatives) {
        return "alternative_data";
    }
    if (hasDerivatives && !hasDailyBar && !hasFinancial && !hasSymbolInfo && !hasNews && !hasPolicy && !hasAlternative) {
        return "derivatives_data";
    }

    return "daily_bar";
}

}

DataAvailabilityChecker::DataAvailabilityChecker(std::shared_ptr<astock::database::QtMySQLDatabase> db)
    : db_(db) {
}

DataStatus DataAvailabilityChecker::checkFactorData(const std::string& instanceId,
                                                    const std::string& startDate,
                                                    const std::string& endDate) {
    DataStatus result;
    
    try {
        if (!db_) {
            return createErrorStatus("数据库连接未初始化");
        }

        // 查询因子配置
        auto queryResult = db_->executeQuery(
            "SELECT CAST(full_config AS CHAR) AS full_config FROM factor_instance WHERE instance_id = ?",
            makePositionalParams({QString::fromStdString(instanceId)})
        );
        
        if (queryResult.isEmpty()) {
            return createErrorStatus("因子实例不存在: " + instanceId);
        }

        const auto& configRow = queryResult.getRow(0);
        
        // 解析配置
        auto configJson = foundation::json::JsonFacade::parse(
            configRow.getString("full_config").toStdString()
        );
        
        // 获取数据需求
        auto dataReq = configJson.get("data_requirements");
        if (!dataReq.isObject()) {
            return createErrorStatus("无效的因子配置: 缺少data_requirements");
        }
        
        auto requiredFields = dataReq.get("required");
        if (!requiredFields.isArray()) {
            return createErrorStatus("无效的因子配置: required字段不是数组");
        }

        std::vector<std::string> fields;
        for (size_t i = 0; i < requiredFields.size(); ++i) {
            fields.push_back(requiredFields.at(i).asString());
        }

        fields = normalizeFields(fields);

        if (fields.empty()) {
            result.availability = DataAvailability::AVAILABLE;
            result.coverage = 1.0;
            result.message = "无必需字段";
            return result;
        }

        std::string sourceTable;
        if (dataReq.has("source_table")) {
            sourceTable = normalizeSourceTableName(dataReq.get("source_table").asString());
        } else if (dataReq.has("sourceTable")) {
            sourceTable = normalizeSourceTableName(dataReq.get("sourceTable").asString());
        }

        const std::string table = !sourceTable.empty() ? sourceTable : resolveTableForFields(fields);
        if (table.empty()) {
            return createErrorStatus("当前因子依赖的数据表尚未接入");
        }

        result = checkFields(fields, endDate, table);
        
    } catch (const std::exception& e) {
        result.availability = DataAvailability::UNAVAILABLE;
        result.message = "检查数据时出错: " + std::string(e.what());
    }
    
    return result;
}

DataStatus DataAvailabilityChecker::checkDataType(DataType type,
                                                  const std::string& date) {
    auto fields = getFieldsForType(type);
    return checkFields(fields, date);
}

DataStatus DataAvailabilityChecker::checkValuationData(const std::string& date) {
    std::vector<std::string> fields = {"pe_ratio", "pb_ratio", "market_cap", "dividend_yield", "operating_cash_flow"};
    return checkFields(fields, date, "");
}

DataStatus DataAvailabilityChecker::checkPriceData(const std::string& date) {
    std::vector<std::string> fields = {"close"};
    return checkFields(fields, date, "cleaned_daily_bar");
}

DataAvailabilityChecker::CoverageStats DataAvailabilityChecker::getCoverageStats(
    DataType type, const std::string& date) {
    
    CoverageStats stats;
    auto fields = getFieldsForType(type);
    const std::string table = resolveTableForFields(fields);
    if (!db_ || table.empty()) {
        return stats;
    }
    
    try {
        const QString dateColumn = QString::fromStdString(resolveDateColumn(table));
        const QString datePredicate = buildDatePredicate(table, dateColumn);

        // 查询总股票数
        const QString symbolColumn = table == "financial_indicator" ? QStringLiteral("symbol_id") : QStringLiteral("symbol");
        QString totalSql = QString("SELECT COUNT(DISTINCT %1) as total FROM %2")
            .arg(symbolColumn, QString::fromStdString(table));
        if (!datePredicate.isEmpty()) {
            totalSql += QStringLiteral(" WHERE ") + datePredicate;
        }
        auto totalResult = datePredicate.isEmpty()
            ? db_->executeQuery(totalSql, {})
            : db_->executeQuery(totalSql, makePositionalParams({QString::fromStdString(date)}));
        
        if (!totalResult.isEmpty()) {
            stats.totalStocks = totalResult.getRow(0).getInt("total");
        }
        
        // 查询每个字段的有效数量
        for (const auto& field : fields) {
            QString validSql;
            const QString normalizedField = normalizeFieldName(QString::fromStdString(field));
            const QString symbolColumn = table == "financial_indicator" ? QStringLiteral("symbol_id") : QStringLiteral("symbol");
            const QString fieldCondition = fieldRequiresPositiveValues(normalizedField)
                ? QStringLiteral("%1 IS NOT NULL AND %1 > 0")
                : QStringLiteral("%1 IS NOT NULL");

            if (datePredicate.isEmpty()) {
                validSql = QString("SELECT COUNT(DISTINCT %1) as valid FROM %2 WHERE ")
                    .arg(symbolColumn, QString::fromStdString(table));
            } else {
                validSql = QString("SELECT COUNT(DISTINCT %1) as valid FROM %2 WHERE %3 AND ")
                    .arg(symbolColumn, QString::fromStdString(table), datePredicate);
            }
            validSql += fieldCondition.arg(normalizedField);

            auto validResult = datePredicate.isEmpty()
                ? db_->executeQuery(validSql, {})
                : db_->executeQuery(validSql, makePositionalParams({QString::fromStdString(date)}));
            
            if (!validResult.isEmpty()) {
                int validCount = validResult.getRow(0).getInt("valid");
                stats.fieldStats[field] = validCount;
                if (stats.validStocks == 0) {
                    stats.validStocks = validCount;
                } else {
                    stats.validStocks = std::min(stats.validStocks, validCount);
                }
            }
        }
        
        // 计算覆盖率
        if (stats.totalStocks > 0) {
            stats.coverageRate = static_cast<double>(stats.validStocks) / stats.totalStocks;
        }
        
    } catch (const std::exception& e) {
        // 出错时返回空统计
    }
    
    return stats;
}

std::map<std::string, DataStatus> DataAvailabilityChecker::checkDateRange(
    const std::string& startDate,
    const std::string& endDate,
    DataType type) {
    
    std::map<std::string, DataStatus> results;
    
    try {
        const std::string table = resolveTableForFields(getFieldsForType(type));
        if (table.empty()) {
            return results;
        }

        const QString dateColumn = QString::fromStdString(resolveDateColumn(table));

        // 查询日期范围内的所有交易日
        auto datesResult = db_->executeQuery(
            QString("SELECT DISTINCT %1 AS data_date FROM %2 WHERE %1 BETWEEN ? AND ? ORDER BY %1")
                .arg(dateColumn, QString::fromStdString(table)),
            makePositionalParams({QString::fromStdString(startDate), QString::fromStdString(endDate)})
        );
        
        for (size_t i = 0; i < datesResult.rowCount(); i++) {
            std::string date = datesResult.getRow(i).getString("data_date").toStdString();
            results[date] = checkDataType(type, date);
        }
        
    } catch (const std::exception& e) {
        // 出错时返回空结果
    }
    
    return results;
}

// ============ 私有方法实现 ============

bool DataAvailabilityChecker::isFieldValid(const std::string& table,
                                           const std::string& field,
                                           const std::string& date,
                                           const std::string& condition) {
    try {
        const QString dateColumn = QString::fromStdString(resolveDateColumn(table));
        const QString datePredicate = buildDatePredicate(table, dateColumn);
        const QString normalizedField = normalizeFieldName(QString::fromStdString(field));
        if (normalizedField.isEmpty()
                || !tableHasColumn(db_, QString::fromStdString(table), normalizedField)) {
            return false;
        }

        QString sql = QString("SELECT COUNT(*) as count FROM %1 WHERE ")
            .arg(QString::fromStdString(table));
        if (!datePredicate.isEmpty()) {
            sql += datePredicate + QStringLiteral(" AND ");
        }
        if (condition == "IS NOT NULL") {
            sql += QString("%1 IS NOT NULL").arg(normalizedField);
        } else {
            sql += QString("%1 IS NOT NULL AND %1 %2").arg(normalizedField, QString::fromStdString(condition));
        }

        auto result = datePredicate.isEmpty()
            ? db_->executeQuery(sql, {})
            : db_->executeQuery(sql, makePositionalParams({QString::fromStdString(date)}));
        
        return !result.isEmpty() && result.getRow(0).getInt("count") > 0;
        
    } catch (const std::exception&) {
        return false;
    }
}

DataStatus DataAvailabilityChecker::checkFields(const std::vector<std::string>& fields,
                                                const std::string& date,
                                                const std::string& table) {
    DataStatus status;
    
    if (fields.empty()) {
        status.availability = DataAvailability::AVAILABLE;
        status.message = "无数据需求";
        return status;
    }
    
    std::vector<std::string> missingFields;
    std::vector<std::string> invalidFields;
    int validFields = 0;
    
    for (const auto& field : fields) {
        const QString normalizedField = normalizeFieldName(QString::fromStdString(field));
        if (normalizedField.isEmpty()) {
            continue;
        }

        std::string effectiveTable = table;
        if (effectiveTable.empty()) {
            effectiveTable = resolveTableForFields(std::vector<std::string>{normalizedField.toStdString()});
        }
        if (effectiveTable.empty()) {
            effectiveTable = "daily_bar";
        }

        if (fieldRequiresPositiveValues(normalizedField)
                ? isFieldValid(effectiveTable, normalizedField.toStdString(), date, "> 0")
                : isFieldValid(effectiveTable, normalizedField.toStdString(), date, "IS NOT NULL")) {
            validFields++;
        } else if (isFieldValid(effectiveTable, normalizedField.toStdString(), date, "IS NOT NULL")) {
            // 字段存在但值为0或负数
            invalidFields.push_back(normalizedField.toStdString());
        } else {
            // 字段不存在
            missingFields.push_back(normalizedField.toStdString());
        }
    }
    
    // 判断可用性
    if (validFields == fields.size()) {
        status.availability = DataAvailability::AVAILABLE;
        status.coverage = 1.0;
        status.message = "所有字段数据可用";
    } else if (validFields > 0) {
        status.availability = DataAvailability::PARTIAL;
        status.coverage = static_cast<double>(validFields) / fields.size();
        status.message = "部分字段数据可用";
    } else {
        status.availability = DataAvailability::UNAVAILABLE;
        status.coverage = 0.0;
        status.message = "无可用数据";
    }
    
    status.missingFields = missingFields;
    status.invalidFields = invalidFields;
    
    return status;
}

std::vector<std::string> DataAvailabilityChecker::getFieldsForType(DataType type) {
    switch (type) {
        case DataType::PRICE:
            return {"close"};
        case DataType::VALUATION:
            return {"pe_ratio", "pb_ratio", "market_cap", "dividend_yield", "operating_cash_flow"};
        case DataType::VOLUME:
            return {"volume"};
        case DataType::FINANCIAL:
            return {"roe", "roa", "profit_margin", "net_profit", "equity", "eps", "total_revenue", "operating_cash_flow"};
        case DataType::INDUSTRY:
            return {"industry_code"};
        default:
            return {};
    }
}

std::string DataAvailabilityChecker::resolveTableForFields(const std::vector<std::string>& fields) const {
    return inferTableForFields(fields);
}

DataStatus DataAvailabilityChecker::createErrorStatus(const std::string& message,
                                                      const std::vector<std::string>& missing,
                                                      const std::vector<std::string>& invalid) const {
    DataStatus status;
    status.availability = DataAvailability::UNAVAILABLE;
    status.message = message;
    status.missingFields = missing;
    status.invalidFields = invalid;
    return status;
}

} // namespace factor