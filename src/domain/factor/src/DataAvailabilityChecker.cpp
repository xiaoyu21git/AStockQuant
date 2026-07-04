#include "DataAvailabilityChecker.h"
#include "FactorConfigAccess.h"
#include "infrastructure/include/database/ISqlDatabase.h"

#include <algorithm>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace factor {

namespace {

namespace checker_contract {

constexpr const char* kRequiredKey = "required";
constexpr const char* kSourceTableKey = "sourceTable";
constexpr const char* kDailyBarTable = "mkt.daily_bar";
constexpr const char* kCleanedDailyBarTable = "cleaned_daily_bar";
constexpr const char* kFinancialIndicatorDailyTable = "fund.financial_indicator_daily";
constexpr const char* kSymbolInfoTable = "ref.symbol_info";
constexpr const char* kNewsSentimentTable = "news_sentiment";
constexpr const char* kPolicyDataTable = "policy_data";
constexpr const char* kAlternativeDataTable = "alternative_data";
constexpr const char* kDerivativesDataTable = "derivatives_data";
constexpr const char* kTradeDateColumn = "trade_date";
constexpr const char* kPublishTimeColumn = "publish_time";
constexpr const char* kSymbolColumn = "symbol";
constexpr const char* kSymbolIdColumn = "symbol_id";

} // namespace checker_contract

struct FieldAvailabilitySnapshot {
    bool columnExists{false};
    int validCount{0};
    int nonNullCount{0};
};

std::mutex& columnCacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, bool>& columnCache()
{
    static std::unordered_map<std::string, bool> cache;
    return cache;
}

std::mutex& tableColumnsCacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, std::unordered_set<std::string>>& tableColumnsCache()
{
    static std::unordered_map<std::string, std::unordered_set<std::string>> cache;
    return cache;
}

std::mutex& fieldCacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, bool>& fieldCache()
{
    static std::unordered_map<std::string, bool> cache;
    return cache;
}

std::string buildFieldCacheKey(const std::string& table,
                               const std::string& field,
                               const std::string& date,
                               const std::string& condition)
{
    return table + "|" + field + "|" + date + "|" + condition;
}

std::string normalizeFieldName(const std::string& rawField)
{
    std::string result = rawField;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    // trim whitespace
    auto start = result.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return {};
    auto end = result.find_last_not_of(" \t\n\r");
    return result.substr(start, end - start + 1);
}

bool fieldRequiresPositiveValues(const std::string& rawField)
{
    static const std::unordered_set<std::string> positiveFields = {
        "open", "high", "low", "close", "pre_close",
        "volume", "turnover", "pe_ratio", "pb_ratio",
        "market_cap", "circulating_market_cap",
        "pre_adj_factor", "post_adj_factor",
        "bps", "roe", "roa", "total_assets", "total_liabilities",
        "equity", "net_profit", "total_revenue", "eps",
        "debt_to_equity", "current_ratio", "quick_ratio",
        "dividend_yield", "policy_strength", "policy_count",
        "popularity_score", "comment_count",
        "futures_close", "futures_volume", "open_interest",
        "industry_code", "profit_margin", "gross_margin",
        "operating_margin", "operating_cash_flow"
    };
    return positiveFields.find(normalizeFieldName(rawField)) != positiveFields.end();
}

std::string resolveDateColumn(const std::string& table)
{
    if (table == checker_contract::kSymbolInfoTable) return {};
    if (table == checker_contract::kPolicyDataTable) return checker_contract::kPublishTimeColumn;
    if (table == checker_contract::kDailyBarTable
        || table == checker_contract::kCleanedDailyBarTable
        || table == checker_contract::kNewsSentimentTable
        || table == checker_contract::kAlternativeDataTable
        || table == checker_contract::kDerivativesDataTable)
        return checker_contract::kTradeDateColumn;
    if (table == checker_contract::kFinancialIndicatorDailyTable)
        return checker_contract::kTradeDateColumn;
    return {};
}

std::string buildDatePredicate(const std::string& table, const std::string& column)
{
    if (column.empty()) return {};
    if (table == checker_contract::kFinancialIndicatorDailyTable
        || table == checker_contract::kFinancialIndicatorDailyTable)
        return column + " <= ?";
    if (table == checker_contract::kPolicyDataTable)
        return std::string("DATE(") + column + ") <= ?";
    if (table == checker_contract::kNewsSentimentTable)
        return std::string("DATE(") + column + ") <= ?";
    if (table == checker_contract::kDailyBarTable
        || table == checker_contract::kCleanedDailyBarTable
        || table == checker_contract::kAlternativeDataTable
        || table == checker_contract::kDerivativesDataTable)
        return column + " = ?";
    return {};
}

std::string symbolColumnForTable(const std::string& table)
{
    return (table == checker_contract::kFinancialIndicatorDailyTable)
        ? checker_contract::kSymbolIdColumn
        : checker_contract::kSymbolColumn;
}

bool isDailyBarField(const std::string& normalizedField)
{
    static const std::unordered_set<std::string> fields = {
        "trade_date", "open", "high", "low", "close", "pre_close",
        "volume", "turnover", "amount", "pe_ratio", "pb_ratio",
        "market_cap", "circulating_market_cap", "pre_adj_factor",
        "post_adj_factor", "industry_code", "turnover_amount",
        "adj_factor"
    };
    return fields.find(normalizedField) != fields.end();
}

bool isFinancialField(const std::string& normalizedField)
{
    static const std::unordered_set<std::string> fields = {
        "bps", "roe", "roa", "total_assets", "total_liabilities",
        "equity", "net_profit", "total_revenue", "eps",
        "debt_to_equity", "current_ratio", "quick_ratio",
        "dividend_yield", "operating_cash_flow", "profit_margin",
        "gross_margin", "operating_margin", "report_type"
    };
    return fields.find(normalizedField) != fields.end();
}

bool isSymbolInfoField(const std::string& normalizedField)
{
    static const std::unordered_set<std::string> fields = {
        "symbol", "name", "industry", "market"
    };
    return fields.find(normalizedField) != fields.end();
}

bool isNewsField(const std::string& normalizedField)
{
    return normalizedField.find("news") != std::string::npos
        || normalizedField.find("sentiment") != std::string::npos;
}

bool isPolicyField(const std::string& normalizedField)
{
    return normalizedField.find("policy") != std::string::npos;
}

bool isAlternativeField(const std::string& normalizedField)
{
    return normalizedField.find("popularity") != std::string::npos
        || normalizedField.find("comment") != std::string::npos;
}

bool isDerivativesField(const std::string& normalizedField)
{
    return normalizedField.find("futures") != std::string::npos
        || normalizedField.find("open_interest") != std::string::npos;
}

std::vector<std::string> normalizeUniqueFields(const std::vector<std::string>& fields)
{
    std::vector<std::string> normalized;
    normalized.reserve(fields.size());
    std::unordered_set<std::string> seen;
    for (const auto& field : fields) {
        std::string normalizedField = normalizeFieldName(field);
        if (normalizedField.empty() || !seen.insert(normalizedField).second)
            continue;
        normalized.push_back(normalizedField);
    }
    return normalized;
}

std::string buildFieldNonNullCondition(const std::string& normalizedField)
{
    return normalizedField + " IS NOT NULL";
}

std::string buildFieldValidCondition(const std::string& normalizedField)
{
    if (fieldRequiresPositiveValues(normalizedField))
        return normalizedField + " IS NOT NULL AND " + normalizedField + " > 0";
    return normalizedField + " IS NOT NULL";
}

std::string sqlQuoteIdentifier(const std::string& identifier)
{
    // PG 使用双引号引用标识符，转义内部双引号为两个双引号
    std::string quoted = identifier;
    for (std::size_t pos = quoted.find('"'); pos != std::string::npos; pos = quoted.find('"', pos + 2))
        quoted.replace(pos, 1, "\"\"");
    return "\"" + quoted + "\"";
}

std::unordered_set<std::string> loadTableColumns(
    const std::shared_ptr<astock::database::ISqlDatabase>& db,
    const std::string& tableName)
{
    std::unordered_set<std::string> columns;
    if (!db || tableName.empty()) return columns;

    const std::string cacheKey = tableName;
    {
        std::lock_guard<std::mutex> guard(tableColumnsCacheMutex());
        auto it = tableColumnsCache().find(cacheKey);
        if (it != tableColumnsCache().end())
            return it->second;
    }

    // PG: 用 search_path 中的全部 schema 代替 MySQL 的 DATABASE()
    auto result = db->executeQuery(
        "SELECT COLUMN_NAME AS column_name FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA = ANY(regexp_split_to_array("
        "replace(current_setting('search_path'), ' ', ''), ',')) "
        "AND TABLE_NAME = ?",
        { astock::database::SqlParam{std::string(tableName)} }
    );

    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        std::string col = result.getRow(i).getString("column_name");
        std::transform(col.begin(), col.end(), col.begin(), ::tolower);
        columns.insert(normalizeFieldName(col));
    }

    {
        std::lock_guard<std::mutex> guard(tableColumnsCacheMutex());
        tableColumnsCache()[cacheKey] = columns;
    }
    return columns;
}

bool columnsExistForField(const std::string& normalizedField,
                          const std::unordered_set<std::string>& tableColumns)
{
    return tableColumns.find(normalizedField) != tableColumns.end();
}

std::unordered_map<std::string, FieldAvailabilitySnapshot> fetchFieldAvailabilitySnapshot(
    const std::shared_ptr<astock::database::ISqlDatabase>& db,
    const std::string& table,
    const std::vector<std::string>& fields,
    const std::string& date)
{
    std::unordered_map<std::string, FieldAvailabilitySnapshot> snapshot;
    if (!db || table.empty() || fields.empty()) return snapshot;

    auto tableColumns = loadTableColumns(db, table);
    std::vector<std::string> selectParts;

    for (const auto& field : fields) {
        std::string normalized = normalizeFieldName(field);
        FieldAvailabilitySnapshot fieldSnapshot;
        fieldSnapshot.columnExists = columnsExistForField(normalized, tableColumns);
        snapshot.emplace(field, fieldSnapshot);

        if (!fieldSnapshot.columnExists) continue;

        std::string validCondition = buildFieldValidCondition(normalized);
        std::string validAlias = normalized + "__valid";
        if (fieldRequiresPositiveValues(normalized)) {
            std::string nonNullCondition = buildFieldNonNullCondition(normalized);
            std::string nonNullAlias = normalized + "__nonnull";
            selectParts.push_back("COALESCE(SUM(CASE WHEN " + nonNullCondition + " THEN 1 ELSE 0 END), 0) AS " + sqlQuoteIdentifier(nonNullAlias));
            selectParts.push_back("COALESCE(SUM(CASE WHEN " + validCondition + " THEN 1 ELSE 0 END), 0) AS " + sqlQuoteIdentifier(validAlias));
        } else {
            selectParts.push_back("COALESCE(SUM(CASE WHEN " + validCondition + " THEN 1 ELSE 0 END), 0) AS " + sqlQuoteIdentifier(validAlias));
        }
    }

    if (selectParts.empty()) return snapshot;

    std::string dateCol = resolveDateColumn(table);
    std::string datePred = buildDatePredicate(table, dateCol);

    std::string selectStr;
    for (std::size_t i = 0; i < selectParts.size(); ++i) {
        if (i > 0) selectStr += ", ";
        selectStr += selectParts[i];
    }

    std::string sql = "SELECT " + selectStr + " FROM " + table;
    if (!datePred.empty())
        sql += " WHERE " + datePred;

    auto result = db->executeQuery(sql, date.empty()
        ? std::vector<astock::database::SqlParam>{}
        : std::vector<astock::database::SqlParam>{ astock::database::SqlParam{std::string(date)} });

    if (result.isEmpty()) return snapshot;

    const auto& row = result.getRow(0);
    for (const auto& field : fields) {
        auto it = snapshot.find(field);
        if (it == snapshot.end() || !it->second.columnExists) continue;

        std::string normalized = normalizeFieldName(field);
        std::string validAlias = normalized + "__valid";
        it->second.validCount = row.getInt(validAlias);
        if (fieldRequiresPositiveValues(normalized)) {
            std::string nonNullAlias = normalized + "__nonnull";
            it->second.nonNullCount = row.getInt(nonNullAlias);
        } else {
            it->second.nonNullCount = it->second.validCount;
        }
    }
    return snapshot;
}

DataAvailabilityChecker::CoverageStats fetchCoverageStatsSnapshot(
    const std::shared_ptr<astock::database::ISqlDatabase>& db,
    const std::vector<std::string>& fields,
    const std::string& date,
    const std::string& table)
{
    DataAvailabilityChecker::CoverageStats stats;
    if (!db || table.empty()) return stats;

    std::vector<std::string> normalizedFields = normalizeUniqueFields(fields);
    if (normalizedFields.empty()) {
        stats.totalStocks = 0;
        stats.validStocks = 0;
        stats.coverageRate = 1.0;
        return stats;
    }

    std::string dateCol = resolveDateColumn(table);
    std::string datePred = buildDatePredicate(table, dateCol);
    std::string symCol = symbolColumnForTable(table);

    std::vector<std::string> selectParts;
    selectParts.push_back("COUNT(DISTINCT " + symCol + ") AS total_stocks");
    for (std::size_t i = 0; i < normalizedFields.size(); ++i) {
        const std::string& nf = normalizedFields[i];
        std::string validAlias = nf + "__valid";
        if (fieldRequiresPositiveValues(nf)) {
            selectParts.push_back("COUNT(DISTINCT CASE WHEN " + nf + " IS NOT NULL AND " + nf + " > 0 THEN " + symCol + " END) AS " + validAlias);
        } else {
            selectParts.push_back("COUNT(DISTINCT CASE WHEN " + nf + " IS NOT NULL THEN " + symCol + " END) AS " + validAlias);
        }
    }

    std::string selectStr;
    for (std::size_t i = 0; i < selectParts.size(); ++i) {
        if (i > 0) selectStr += ", ";
        selectStr += selectParts[i];
    }

    std::string sql = "SELECT " + selectStr + " FROM " + table;
    if (!datePred.empty())
        sql += " WHERE " + datePred;

    auto result = db->executeQuery(sql, date.empty()
        ? std::vector<astock::database::SqlParam>{}
        : std::vector<astock::database::SqlParam>{ astock::database::SqlParam{std::string(date)} });

    if (result.isEmpty()) return stats;

    const auto& row = result.getRow(0);
    stats.totalStocks = row.getInt("total_stocks");
    int validStocks = 0;
    for (std::size_t i = 0; i < normalizedFields.size(); ++i) {
        std::string alias = normalizedFields[i] + "__valid";
        int validCount = row.getInt(alias);
        stats.fieldStats[normalizedFields[i]] = validCount;
        validStocks = validStocks == 0 ? validCount : std::min(validStocks, validCount);
    }
    stats.validStocks = validStocks;
    if (stats.totalStocks > 0)
        stats.coverageRate = static_cast<double>(stats.validStocks) / stats.totalStocks;
    return stats;
}

std::string sourceTableDatabaseName(SourceTable sourceTable)
{
    switch (sourceTable) {
    case SourceTable::DAILY_BAR: return checker_contract::kDailyBarTable;
    case SourceTable::FINANCIAL_INDICATOR: return checker_contract::kFinancialIndicatorDailyTable;
    case SourceTable::SYMBOL_INFO: return checker_contract::kSymbolInfoTable;
    case SourceTable::NEWS_SENTIMENT: return checker_contract::kNewsSentimentTable;
    case SourceTable::POLICY_DATA: return checker_contract::kPolicyDataTable;
    case SourceTable::ALTERNATIVE_DATA: return checker_contract::kAlternativeDataTable;
    case SourceTable::DERIVATIVES_DATA: return checker_contract::kDerivativesDataTable;
    case SourceTable::UNKNOWN:
    default: return {};
    }
}

/// @brief 按字段类型分组到对应数据表（支持跨表）
std::map<std::string, std::vector<std::string>> detailGroupFieldsByTable(const std::vector<std::string>& fields)
{
    std::map<std::string, std::vector<std::string>> groups;
    for (const auto& field : fields) {
        std::string nf = normalizeFieldName(field);
        if (nf.empty()) continue;
        std::string table;
        if (isDailyBarField(nf)) table = checker_contract::kDailyBarTable;
        else if (isFinancialField(nf)) table = checker_contract::kFinancialIndicatorDailyTable;
        else if (isSymbolInfoField(nf)) table = checker_contract::kSymbolInfoTable;
        else if (isNewsField(nf)) table = checker_contract::kNewsSentimentTable;
        else if (isPolicyField(nf)) table = checker_contract::kPolicyDataTable;
        else if (isAlternativeField(nf)) table = checker_contract::kAlternativeDataTable;
        else if (isDerivativesField(nf)) table = checker_contract::kDerivativesDataTable;
        else continue;
        groups[table].push_back(nf);
    }
    return groups;
}

std::string inferTableForFields(const std::vector<std::string>& fields)
{
    auto groups = detailGroupFieldsByTable(fields);
    if (groups.empty()) return {};
    if (groups.size() == 1U) return groups.begin()->first;
    // 跨表时返回空（兼容旧行为），新调用方应使用 groupFieldsByTable
    return {};
}

} // anonymous namespace

DataAvailabilityChecker::DataAvailabilityChecker(std::shared_ptr<astock::database::ISqlDatabase> db)
    : db_(db)
{
}

DataStatus DataAvailabilityChecker::checkFactorData(const std::string& instanceId,
                                                    const std::string& startDate,
                                                    const std::string& endDate)
{
    DataStatus result;
    try {
        if (!db_) return createErrorStatus("database connection not initialized");

        auto queryResult = db_->executeQuery(
            "SELECT full_config::text AS full_config FROM factor_instance WHERE instance_id = ?",
            { astock::database::SqlParam{std::string(instanceId)} }
        );

        if (queryResult.isEmpty())
            return createErrorStatus("factor instance not found: " + instanceId);

        const auto& configRow = queryResult.getRow(0);
        auto configJson = foundation::json::JsonFacade::parse(configRow.getString("full_config"));
        result = checkFactorData(configJson, instanceId, startDate, endDate);
    } catch (const std::exception& e) {
        result.availability = DataAvailability::UNAVAILABLE;
        result.message = "检查数据时出错: " + std::string(e.what());
    }
    return result;
}

DataStatus DataAvailabilityChecker::checkFactorData(const foundation::json::JsonFacade& config,
                                                    const std::string&,
                                                    const std::string&,
                                                    const std::string& endDate)
{
    DataStatus result;
    try {
        if (!db_) return createErrorStatus("database connection not initialized");

        if (!config::hasDataRequirementsConfig(config))
            return createErrorStatus("invalid factor config: missing dataRequirements");

        auto dataReq = config::dataRequirementsConfig(config);
        if (!dataReq.isObject())
            return createErrorStatus("invalid factor config: missing dataRequirements");

        auto requiredFields = dataReq.get(checker_contract::kRequiredKey);
        if (!requiredFields.isArray())
            return createErrorStatus("invalid factor config: required is not an array");

        std::vector<std::string> fields;
        for (size_t i = 0; i < requiredFields.size(); ++i)
            fields.push_back(requiredFields.at(i).asString());

        fields = normalizeUniqueFields(fields);
        if (fields.empty()) {
            result.availability = DataAvailability::AVAILABLE;
            result.coverage = 1.0;
            result.message = "无必需字段";
            return result;
        }

        // 检查是否有显式 sourceTable 配置 —— 如果有则按单表检查
        SourceTable sourceTable = SourceTable::UNKNOWN;
        if (dataReq.has(checker_contract::kSourceTableKey)) {
            auto sourceTableValue = dataReq.get(checker_contract::kSourceTableKey);
            int index = sourceTableValue.asInt();
            if (index < static_cast<int>(SourceTable::DAILY_BAR) || index > static_cast<int>(SourceTable::UNKNOWN))
                throw std::runtime_error("dataRequirements.sourceTable 不是有效的枚举值");
            sourceTable = static_cast<SourceTable>(index);
        }

        if (sourceTable != SourceTable::UNKNOWN) {
            // 指定了单表：使用旧路径
            std::string table = sourceTableDatabaseName(sourceTable);
            if (table.empty())
                return createErrorStatus("当前因子配置的 sourceTable 无效");
            result = checkFields(fields, endDate, table);
        } else {
            // 未指定 sourceTable：自动分表，使用跨表检查
            auto groups = groupFieldsByTable(fields);
            if (groups.empty())
                return createErrorStatus("当前因子依赖的数据表尚未接入");

            if (groups.size() == 1U) {
                // 所有字段在同一张表，走旧路径
                result = checkFields(fields, endDate, groups.begin()->first);
            } else {
                // 字段跨多张表，使用跨表检查
                result = checkFieldsCrossTable(fields, endDate);
            }
        }
    } catch (const std::exception& e) {
        result.availability = DataAvailability::UNAVAILABLE;
        result.message = "检查数据时出错: " + std::string(e.what());
    }
    return result;
}

DataStatus DataAvailabilityChecker::checkDataType(DataType type, const std::string& date)
{
    return checkFields(getFieldsForType(type), date);
}

DataStatus DataAvailabilityChecker::checkValuationData(const std::string& date)
{
    std::vector<std::string> fields = {"pe_ratio", "pb_ratio", "market_cap", "dividend_yield", "operating_cash_flow"};
    return checkFields(fields, date, "");
}

DataStatus DataAvailabilityChecker::checkPriceData(const std::string& date)
{
    return checkFields({"close"}, date, checker_contract::kCleanedDailyBarTable);
}

DataAvailabilityChecker::CoverageStats DataAvailabilityChecker::getCoverageStats(
    DataType type, const std::string& date)
{
    CoverageStats stats;
    auto fields = getFieldsForType(type);
    std::string table = resolveTableForFields(fields);
    if (!db_ || table.empty()) return stats;

    try {
        std::string dateCol = resolveDateColumn(table);
        std::string datePred = buildDatePredicate(table, dateCol);
        std::string symCol = symbolColumnForTable(table);

        std::string totalSql = "SELECT COUNT(DISTINCT " + symCol + ") as total FROM " + table;
        if (!datePred.empty())
            totalSql += " WHERE " + datePred;

        auto totalResult = db_->executeQuery(totalSql, date.empty()
            ? std::vector<astock::database::SqlParam>{}
            : std::vector<astock::database::SqlParam>{ astock::database::SqlParam{std::string(date)} });

        if (!totalResult.isEmpty())
            stats.totalStocks = totalResult.getRow(0).getInt("total");

        for (const auto& field : fields) {
            std::string nf = normalizeFieldName(field);
            std::string validCondition = buildFieldValidCondition(nf);
            if (validCondition.empty()) continue;

            std::string validSql = "SELECT COUNT(DISTINCT " + symCol + ") as valid FROM " + table + " WHERE ";
            if (!datePred.empty())
                validSql += datePred + " AND ";
            validSql += validCondition;

            auto validResult = db_->executeQuery(validSql, date.empty()
                ? std::vector<astock::database::SqlParam>{}
                : std::vector<astock::database::SqlParam>{ astock::database::SqlParam{std::string(date)} });

            if (!validResult.isEmpty()) {
                int validCount = validResult.getRow(0).getInt("valid");
                stats.fieldStats[field] = validCount;
                stats.validStocks = stats.validStocks == 0 ? validCount : std::min(stats.validStocks, validCount);
            }
        }

        if (stats.totalStocks > 0)
            stats.coverageRate = static_cast<double>(stats.validStocks) / stats.totalStocks;
    } catch (const std::exception&) {
    }
    return stats;
}

std::map<std::string, DataStatus> DataAvailabilityChecker::checkDateRange(
    const std::string& startDate, const std::string& endDate, DataType type)
{
    std::map<std::string, DataStatus> results;
    try {
        std::vector<std::string> fields = getFieldsForType(type);
        std::string table = resolveTableForFields(fields);
        if (table.empty()) return results;

        std::string dateCol = resolveDateColumn(table);
        std::string sql = "SELECT DISTINCT " + dateCol + " AS data_date FROM " + table + " WHERE " + dateCol + " BETWEEN ? AND ? ORDER BY " + dateCol;

        auto datesResult = db_->executeQuery(sql,
            { astock::database::SqlParam{std::string(startDate)}, astock::database::SqlParam{std::string(endDate)} });

        for (std::size_t i = 0; i < datesResult.rowCount(); i++) {
            std::string date = datesResult.getRow(i).getString("data_date");
            results[date] = checkFields(fields, date, table);
        }
    } catch (const std::exception&) {
    }
    return results;
}

bool DataAvailabilityChecker::isFieldValid(const std::string& table,
                                           const std::string& field,
                                           const std::string& date,
                                           const std::string& condition)
{
    try {
        std::string cacheKey = buildFieldCacheKey(table, field, date, condition);
        {
            std::lock_guard<std::mutex> guard(fieldCacheMutex());
            auto it = fieldCache().find(cacheKey);
            if (it != fieldCache().end()) return it->second;
        }

        std::string nf = normalizeFieldName(field);
        auto tableColumns = loadTableColumns(db_, table);
        if (!columnsExistForField(nf, tableColumns)) return false;

        std::string validCondition = buildFieldValidCondition(nf);
        if (validCondition.empty()) return false;

        std::string datePred = buildDatePredicate(table, resolveDateColumn(table));
        std::string sql = "SELECT COUNT(*) as count FROM " + table + " WHERE ";
        if (!datePred.empty())
            sql += datePred + " AND ";

        if (condition == "IS NOT NULL")
            sql += buildFieldNonNullCondition(nf);
        else
            sql += validCondition;

        auto result = db_->executeQuery(sql, date.empty()
            ? std::vector<astock::database::SqlParam>{}
            : std::vector<astock::database::SqlParam>{ astock::database::SqlParam{std::string(date)} });

        bool valid = !result.isEmpty() && result.getRow(0).getInt("count") > 0;
        {
            std::lock_guard<std::mutex> guard(fieldCacheMutex());
            fieldCache()[cacheKey] = valid;
        }
        return valid;
    } catch (const std::exception&) {
        return false;
    }
}

DataStatus DataAvailabilityChecker::checkFields(const std::vector<std::string>& fields,
                                                const std::string& date,
                                                const std::string& table)
{
    DataStatus status;
    if (fields.empty()) {
        status.availability = DataAvailability::AVAILABLE;
        status.message = "no fields to check";
        return status;
    }

    std::vector<std::string> normalizedFields = normalizeUniqueFields(fields);
    std::vector<std::string> missingFields;
    std::vector<std::string> invalidFields;
    int validFields = 0;

    std::string effectiveTable = table;
    if (effectiveTable.empty())
        effectiveTable = resolveTableForFields(normalizedFields);
    if (effectiveTable.empty()) {
        status.availability = DataAvailability::UNAVAILABLE;
        status.coverage = 0.0;
        status.message = "unable to resolve source table";
        status.missingFields = normalizedFields;
        return status;
    }

    auto fieldSnapshots = fetchFieldAvailabilitySnapshot(db_, effectiveTable, normalizedFields, date);
    for (const auto& field : normalizedFields) {
        auto it = fieldSnapshots.find(field);
        if (it == fieldSnapshots.end() || !it->second.columnExists) {
            missingFields.push_back(field);
            continue;
        }

        if (it->second.validCount > 0) {
            validFields++;
        } else if (fieldRequiresPositiveValues(field) && it->second.nonNullCount > 0) {
            invalidFields.push_back(field);
        } else {
            missingFields.push_back(field);
        }
    }

    if (validFields == static_cast<int>(normalizedFields.size())) {
        status.availability = DataAvailability::AVAILABLE;
        status.coverage = 1.0;
        status.message = "all fields available";
    } else if (validFields > 0) {
        status.availability = DataAvailability::PARTIAL;
        status.coverage = static_cast<double>(validFields) / normalizedFields.size();
        status.message = "部分字段数据可用";
    } else {
        status.availability = DataAvailability::UNAVAILABLE;
        status.coverage = 0.0;
        status.message = "no data available";
    }

    status.missingFields = missingFields;
    status.invalidFields = invalidFields;
    return status;
}

DataStatus DataAvailabilityChecker::checkFieldsCrossTable(
    const std::vector<std::string>& fields, const std::string& date)
{
    DataStatus status;
    if (fields.empty()) {
        status.availability = DataAvailability::AVAILABLE;
        status.message = "no fields to check";
        return status;
    }

    std::vector<std::string> normalizedFields = normalizeUniqueFields(fields);
    if (normalizedFields.empty()) {
        status.availability = DataAvailability::AVAILABLE;
        status.coverage = 1.0;
        status.message = "无必需字段";
        return status;
    }

    // 按表分组
    auto groups = groupFieldsByTable(normalizedFields);
    if (groups.empty()) {
        status.availability = DataAvailability::UNAVAILABLE;
        status.coverage = 0.0;
        status.message = "无法解析字段对应的数据表";
        status.missingFields = normalizedFields;
        return status;
    }

    std::vector<std::string> allMissingFields;
    std::vector<std::string> allInvalidFields;
    int totalValidFields = 0;
    int totalFields = static_cast<int>(normalizedFields.size());
    int fieldsChecked = 0;

    // 对每张表分别执行检查
    for (const auto& [table, tableFields] : groups) {
        if (tableFields.empty()) continue;
        fieldsChecked += static_cast<int>(tableFields.size());

        auto fieldSnapshots = fetchFieldAvailabilitySnapshot(db_, table, tableFields, date);
        for (const auto& field : tableFields) {
            auto it = fieldSnapshots.find(field);
            if (it == fieldSnapshots.end() || !it->second.columnExists) {
                allMissingFields.push_back(field);
                continue;
            }

            if (it->second.validCount > 0) {
                totalValidFields++;
            } else if (fieldRequiresPositiveValues(field) && it->second.nonNullCount > 0) {
                allInvalidFields.push_back(field);
            } else {
                allMissingFields.push_back(field);
            }
        }
    }

    // 如果某些字段未被任何表覆盖，标记为缺失
    std::unordered_set<std::string> covered;
    for (const auto& [table, tableFields] : groups) {
        for (const auto& f : tableFields) covered.insert(f);
    }
    for (const auto& f : normalizedFields) {
        if (covered.find(f) == covered.end()) {
            allMissingFields.push_back(f);
        }
    }

    if (totalValidFields == totalFields) {
        status.availability = DataAvailability::AVAILABLE;
        status.coverage = 1.0;
        status.message = "all fields available (cross-table)";
    } else if (totalValidFields > 0) {
        status.availability = DataAvailability::PARTIAL;
        status.coverage = static_cast<double>(totalValidFields) / totalFields;
        status.message = "部分字段数据可用（跨表）";
    } else {
        status.availability = DataAvailability::UNAVAILABLE;
        status.coverage = 0.0;
        status.message = "no data available";
    }

    status.missingFields = allMissingFields;
    status.invalidFields = allInvalidFields;
    return status;
}

std::vector<std::string> DataAvailabilityChecker::getFieldsForType(DataType type)
{
    switch (type) {
    case DataType::PRICE: return {"close"};
    case DataType::VALUATION: return {"pe_ratio", "pb_ratio", "market_cap", "dividend_yield", "operating_cash_flow"};
    case DataType::VOLUME: return {"volume"};
    case DataType::FINANCIAL: return {"roe", "roa", "profit_margin", "gross_margin", "operating_margin", "net_profit", "eps", "total_revenue", "operating_cash_flow"};
    case DataType::INDUSTRY: return {"industry_code"};
    default: return {};
    }
}

std::string DataAvailabilityChecker::resolveTableForFields(const std::vector<std::string>& fields) const
{
    return inferTableForFields(fields);
}

std::map<std::string, std::vector<std::string>> DataAvailabilityChecker::groupFieldsByTable(
    const std::vector<std::string>& fields)
{
    // 委托给匿名命名空间的独立函数（避免与成员函数同名导致无限递归）
    return detailGroupFieldsByTable(fields);
}

DataStatus DataAvailabilityChecker::createErrorStatus(const std::string& message,
                                                      const std::vector<std::string>& missing,
                                                      const std::vector<std::string>& invalid) const
{
    DataStatus status;
    status.availability = DataAvailability::UNAVAILABLE;
    status.message = message;
    status.missingFields = missing;
    status.invalidFields = invalid;
    return status;
}

} // namespace factor