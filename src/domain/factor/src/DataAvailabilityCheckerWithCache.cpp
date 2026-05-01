#include "domain/factor/include/DataAvailabilityCheckerWithCache.h"
#include "infrastructure/include/database/DatabaseConnection.h"
#include <algorithm>
#include <cmath>

namespace factor {

namespace {

std::vector<std::string> normalizeFields(const std::vector<std::string>& fields)
{
    std::vector<std::string> normalized;
    normalized.reserve(fields.size());
    for (const auto& field : fields) {
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

DataAvailabilityCheckerWithCache::DataAvailabilityCheckerWithCache(
    std::shared_ptr<DatabaseConnection> db,
    std::shared_ptr<FactorCacheManager> cacheManager)
    : db_(db), cacheManager_(cacheManager) {
}

void DataAvailabilityCheckerWithCache::setCacheManager(
    std::shared_ptr<FactorCacheManager> cacheManager) {
    cacheManager_ = cacheManager;
}

DataStatus DataAvailabilityCheckerWithCache::checkFactorData(const std::string& instanceId,
                                                    const std::string& startDate,
                                                    const std::string& endDate) {
    DataStatus result;
    
    try {
        // 查询因子配置
        auto queryResult = db_->executeQuery(
            "SELECT full_config FROM factor_instance WHERE instance_id = ?",
            {instanceId}
        );
        
        if (queryResult.empty()) {
            return createErrorStatus("因子实例不存在: " + instanceId);
        }
        
        // 解析配置
        auto configJson = foundation::json::JsonFacade::parse(
            queryResult.getString("full_config")
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
        
        // 检查每个字段
        std::vector<std::string> fields;
        for (size_t i = 0; i < requiredFields.size(); i++) {
            fields.push_back(requiredFields.at(i).asString());
        }

        fields = normalizeFields(fields);
        if (fields.empty()) {
            result.availability = DataAvailability::AVAILABLE;
            result.coverage = 1.0;
            result.message = "无必需字段";
            return result;
        }
        
        // 简化：只检查结束日期的数据
        result = checkFieldsWithCache(fields, endDate);
        
    } catch (const std::exception& e) {
        result.availability = DataAvailability::UNAVAILABLE;
        result.message = "检查数据时出错: " + std::string(e.what());
    }
    
    return result;
}

DataStatus DataAvailabilityCheckerWithCache::checkDataType(DataType type,
                                                  const std::string& date) {
    auto fields = normalizeFields(getFieldsForType(type));
    return checkFieldsWithCache(fields, date);
}

DataStatus DataAvailabilityCheckerWithCache::checkValuationData(const std::string& date) {
    std::vector<std::string> fields = {"pe_ratio", "pb_ratio", "market_cap", "dividend_yield", "operating_cash_flow"};
    return checkFieldsWithCache(fields, date, "");
}

DataStatus DataAvailabilityCheckerWithCache::checkPriceData(const std::string& date) {
    std::vector<std::string> fields = {"close"};
    return checkFieldsWithCache(fields, date);
}

DataAvailabilityCheckerWithCache::CoverageStats DataAvailabilityCheckerWithCache::getCoverageStats(
    DataType type, const std::string& date) {
    
    CoverageStats stats;
    auto fields = normalizeFields(getFieldsForType(type));
    
    try {
        QStringList selectParts;
        selectParts.append(QStringLiteral("COUNT(DISTINCT symbol) AS total_stocks"));
        for (const auto& field : fields) {
            const QString fieldName = QString::fromStdString(field);
            selectParts.append(QStringLiteral("COUNT(DISTINCT CASE WHEN %1 IS NOT NULL AND %1 > 0 THEN symbol END) AS %2")
                .arg(fieldName, fieldName));
        }

        const QString sql = QStringLiteral("SELECT %1 FROM daily_bar WHERE trade_date = ?").arg(selectParts.join(QStringLiteral(", ")));
        auto result = db_->executeQuery(sql.toStdString(), {date});
        if (!result.empty()) {
            stats.totalStocks = result.getInt("total_stocks");
            for (const auto& field : fields) {
                const int validCount = result.getInt(QString::fromStdString(field));
                stats.fieldStats[field] = validCount;
                stats.validStocks = std::max(stats.validStocks, validCount);
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

std::map<std::string, DataStatus> DataAvailabilityCheckerWithCache::checkDateRange(
    const std::string& startDate,
    const std::string& endDate,
    DataType type) {
    
    std::map<std::string, DataStatus> results;
    
    try {
        const auto fields = normalizeFields(getFieldsForType(type));
        // 查询日期范围内的所有交易日
        auto datesResult = db_->executeQuery(
            "SELECT DISTINCT trade_date FROM daily_bar "
            "WHERE trade_date BETWEEN ? AND ? "
            "ORDER BY trade_date",
            {startDate, endDate}
        );
        
        for (size_t i = 0; i < datesResult.rowCount(); i++) {
            std::string date = datesResult.getRow(i).getString("trade_date");
            results[date] = checkFieldsWithCache(fields, date, "daily_bar");
        }
        
    } catch (const std::exception& e) {
        // 出错时返回空结果
    }
    
    return results;
}

void DataAvailabilityCheckerWithCache::clearCache() {
    if (cacheManager_) {
        cacheManager_->clearAll();
    }
}

// ============ 私有方法实现 ============

bool DataAvailabilityCheckerWithCache::isFieldValid(const std::string& table,
                                           const std::string& field,
                                           const std::string& date,
                                           const std::string& condition) {
    try {
        auto result = db_->executeQuery(
            "SELECT COUNT(*) as count FROM " + table + 
            " WHERE trade_date = ? AND " + field + " IS NOT NULL AND " + 
            field + " " + condition,
            {date}
        );
        
        return !result.empty() && result.getInt("count") > 0;
        
    } catch (const std::exception&) {
        return false;
    }
}

DataStatus DataAvailabilityCheckerWithCache::checkFieldsWithCache(
    const std::vector<std::string>& fields,
    const std::string& date,
    const std::string& table) {
    const auto normalizedFields = normalizeFields(fields);
    
    // 如果没有缓存管理器，直接检查
    if (!cacheManager_ || !cacheManager_->isCacheAvailable()) {
        return checkFieldsWithoutCache(normalizedFields, date, table);
    }
    
    // 生成缓存键
    std::string dataType = "custom";
    if (normalizedFields == normalizeFields(getFieldsForType(DataType::PRICE))) {
        dataType = "price";
    } else if (normalizedFields == normalizeFields(getFieldsForType(DataType::VALUATION))) {
        dataType = "valuation";
    } else if (normalizedFields == normalizeFields(getFieldsForType(DataType::VOLUME))) {
        dataType = "volume";
    }
    
    // 尝试从缓存获取
    foundation::json::JsonFacade cachedStatus;
    if (cacheManager_->getDataAvailability(date, dataType, cachedStatus)) {
        return DataStatus::fromJson(cachedStatus);
    }
    
    // 缓存未命中，执行实际检查
    DataStatus status = checkFieldsWithoutCache(normalizedFields, date, table);
    
    // 缓存结果
    cacheManager_->setDataAvailability(date, dataType, status.toJson());
    
    return status;
}

DataStatus DataAvailabilityCheckerWithCache::checkFieldsWithoutCache(
    const std::vector<std::string>& fields,
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
        std::string effectiveTable = table;
        if (effectiveTable.empty()) {
            effectiveTable = field == "operating_cash_flow" || field == "roe" || field == "roa" || field == "profit_margin"
                || field == "net_profit" || field == "equity" || field == "eps" || field == "total_revenue"
                ? "financial_indicator_daily"
                : "daily_bar";
        }

        if (isFieldValid(effectiveTable, field, date, "> 0")) {
            validFields++;
        } else if (isFieldValid(effectiveTable, field, date, "IS NOT NULL")) {
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

std::vector<std::string> DataAvailabilityCheckerWithCache::getFieldsForType(DataType type) {
    switch (type) {
        case DataType::PRICE:
            return {"close"};
        case DataType::VALUATION:
            return {"pe_ratio", "pb_ratio", "market_cap", "dividend_yield", "operating_cash_flow"};
        case DataType::VOLUME:
            return {"volume"};
        case DataType::FINANCIAL:
            return {"roe", "net_profit", "equity", "eps", "total_revenue", "operating_cash_flow"};
        case DataType::INDUSTRY:
            return {"industry_code"};
        default:
            return {};
    }
}

std::string DataAvailabilityCheckerWithCache::getDataTypeString(DataType type) {
    switch (type) {
        case DataType::PRICE: return "price";
        case DataType::VALUATION: return "valuation";
        case DataType::VOLUME: return "volume";
        case DataType::FINANCIAL: return "financial";
        case DataType::INDUSTRY: return "industry";
        default: return "unknown";
    }
}

DataStatus DataAvailabilityCheckerWithCache::createErrorStatus(const std::string& message,
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