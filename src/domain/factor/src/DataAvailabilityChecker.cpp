#include "domain/factor/include/DataAvailabilityChecker.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"
#include <algorithm>

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
    return table == "financial_indicator" ? "report_date" : "trade_date";
}

QString buildDatePredicate(const std::string& table, const QString& column)
{
    if (table == "financial_indicator") {
        return QString("%1 <= ?").arg(column);
    }
    return QString("%1 = ?").arg(column);
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

        const std::string table = resolveTableForFields(fields);
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
    std::vector<std::string> fields = {"pe_ratio", "pb_ratio", "market_cap"};
    return checkFields(fields, date);
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
        auto totalResult = db_->executeQuery(
            QString("SELECT COUNT(DISTINCT symbol) as total FROM %1 WHERE %2")
                .arg(QString::fromStdString(table), datePredicate),
            makePositionalParams({QString::fromStdString(date)})
        );

        if (table == "financial_indicator") {
            totalResult = db_->executeQuery(
                QString("SELECT COUNT(DISTINCT symbol_id) as total FROM %1 WHERE %2")
                    .arg(QString::fromStdString(table), datePredicate),
                makePositionalParams({QString::fromStdString(date)})
            );
        }
        
        if (!totalResult.isEmpty()) {
            stats.totalStocks = totalResult.getRow(0).getInt("total");
        }
        
        // 查询每个字段的有效数量
        for (const auto& field : fields) {
            QString validSql;
            if (table == "financial_indicator") {
                validSql = QString("SELECT COUNT(DISTINCT symbol_id) as valid FROM %1 WHERE %2 AND %3 IS NOT NULL AND %3 > 0")
                    .arg(QString::fromStdString(table), datePredicate, QString::fromStdString(field));
            } else {
                validSql = QString("SELECT COUNT(DISTINCT symbol) as valid FROM %1 WHERE %2 AND %3 IS NOT NULL AND %3 > 0")
                    .arg(QString::fromStdString(table), datePredicate, QString::fromStdString(field));
            }

            auto validResult = db_->executeQuery(
                validSql,
                makePositionalParams({QString::fromStdString(date)})
            );
            
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

        auto result = db_->executeQuery(
            QString("SELECT COUNT(*) as count FROM %1 WHERE %2 AND %3 IS NOT NULL AND %3 %4")
                .arg(QString::fromStdString(table),
                     datePredicate,
                     QString::fromStdString(field),
                     QString::fromStdString(condition)),
            makePositionalParams({QString::fromStdString(date)})
        );
        
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
        if (isFieldValid(table, field, date, "> 0")) {
            validFields++;
        } else if (isFieldValid(table, field, date, "IS NOT NULL")) {
            // 字段存在但值为0或负数
            invalidFields.push_back(field);
        } else {
            // 字段不存在
            missingFields.push_back(field);
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
            return {"pe_ratio", "pb_ratio", "market_cap"};
        case DataType::VOLUME:
            return {"volume"};
        case DataType::FINANCIAL:
            return {"roe", "roa", "profit_margin", "net_profit", "equity", "eps", "total_revenue"};
        case DataType::INDUSTRY:
            return {"industry_code"};
        default:
            return {};
    }
}

std::string DataAvailabilityChecker::resolveTableForFields(const std::vector<std::string>& fields) const {
    if (fields.empty()) {
        return "daily_bar";
    }

    const bool hasPriceLike = std::any_of(fields.begin(), fields.end(), [](const std::string& field) {
        return field == "close" || field == "open" || field == "high" || field == "low"
            || field == "pre_close" || field == "volume" || field == "turnover"
            || field == "change_pct" || field == "change_amt" || field == "amplitude"
            || field == "turnover_rate" || field == "adj_factor";
    });
    if (hasPriceLike) {
        return "daily_bar";
    }

    const bool hasValuation = std::any_of(fields.begin(), fields.end(), [](const std::string& field) {
        return field == "pe_ratio" || field == "pb_ratio" || field == "market_cap" || field == "circulating_market_cap";
    });
    if (hasValuation) {
        return "daily_bar";
    }

    const bool hasFinancial = std::any_of(fields.begin(), fields.end(), [](const std::string& field) {
        return field == "roe" || field == "roa" || field == "profit_margin"
            || field == "gross_margin" || field == "operating_margin"
            || field == "net_profit" || field == "equity" || field == "total_assets"
            || field == "eps" || field == "total_revenue";
    });
    if (hasFinancial) {
        return "financial_indicator";
    }

    const bool hasDividendLike = std::any_of(fields.begin(), fields.end(), [](const std::string& field) {
        return field == "dividend_yield" || field == "payout_ratio" || field == "dividend_stability";
    });
    if (hasDividendLike) {
        return {};
    }

    return "daily_bar";
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