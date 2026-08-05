#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include "factor_enums.h"
#include "foundation/json/json_facade.h"
#include "JsonFacadeHelpers.h"

// 前向声明
namespace astock {
namespace database {
class ISqlDatabase;
}
}

namespace factor {

// 数据类型枚举
enum class DataType {
    PRICE,      // 价格数据
    VALUATION,  // 估值数据（PE/PB/市值）
    VOLUME,     // 成交量
    FINANCIAL,  // 财务数据
    INDUSTRY    // 行业数据
};

// 数据可用性状态
enum class DataAvailability {
    AVAILABLE,      // 数据完全可用
    PARTIAL,        // 数据部分可用
    UNAVAILABLE,    // 数据不可用
    INVALID         // 数据无效（如PE=0）
};

// 数据状态结构
struct DataStatus {
    DataAvailability availability = DataAvailability::UNAVAILABLE;
    double coverage = 0.0;           // 数据覆盖率 0-1
    std::string message;             // 状态描述
    std::vector<std::string> missingFields;  // 缺失字段
    std::vector<std::string> invalidFields;  // 无效字段（如PE=0）
    
    bool isValid() const { 
        return availability == DataAvailability::AVAILABLE || 
               availability == DataAvailability::PARTIAL;
    }
    
    // 转换为JSON
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();
        json.set("availability", json_helper::toJsonValue(static_cast<int>(availability)));
        json.set("coverage", json_helper::toJsonValue(coverage));
        json.set("message", json_helper::toJsonValue(message));
        
        auto missingArray = foundation::json::JsonFacade::createArray();
        for (const auto& field : missingFields) {
            missingArray.push_back(foundation::json::JsonFacade::createString(field));
        }
        json.set("missing_fields", missingArray);
        
        auto invalidArray = foundation::json::JsonFacade::createArray();
        for (const auto& field : invalidFields) {
            invalidArray.push_back(foundation::json::JsonFacade::createString(field));
        }
        json.set("invalid_fields", invalidArray);
        
        return json;
    }

    static DataStatus fromJson(const foundation::json::JsonFacade& json) {
        DataStatus status;
        if (json.has("availability")) {
            status.availability = static_cast<DataAvailability>(json.get("availability").asInt());
        }
        if (json.has("coverage")) {
            status.coverage = json.get("coverage").asDouble();
        }
        if (json.has("message")) {
            status.message = json.get("message").asString();
        }
        if (json.has("missing_fields")) {
            auto missingArray = json.get("missing_fields");
            for (size_t i = 0; i < missingArray.size(); ++i) {
                status.missingFields.push_back(missingArray.at(i).asString());
            }
        }
        if (json.has("invalid_fields")) {
            auto invalidArray = json.get("invalid_fields");
            for (size_t i = 0; i < invalidArray.size(); ++i) {
                status.invalidFields.push_back(invalidArray.at(i).asString());
            }
        }
        return status;
    }
};

// 数据可用性检查器
class DataAvailabilityChecker {
public:
    explicit DataAvailabilityChecker(std::shared_ptr<astock::database::ISqlDatabase> db);
    ~DataAvailabilityChecker() = default;
    
    // 检查因子数据可用性
    DataStatus checkFactorData(const std::string& instanceId,
                               const std::string& startDate,
                               const std::string& endDate);

    // 使用已解析的因子配置检查数据可用性，避免再次回查 factor_instance
    DataStatus checkFactorData(const foundation::json::JsonFacade& config,
                               const std::string& instanceId,
                               const std::string& startDate,
                               const std::string& endDate);
    
    /// @brief 跨表检查字段数据可用性（支持字段分布在多个表中）
    DataStatus checkFieldsCrossTable(const std::vector<std::string>& fields,
                                     const std::string& date = "");

    /// @brief 按字段类型将字段分组到对应的数据表名
    /// @return map<table_name → [normalized_fields]>
    static std::map<std::string, std::vector<std::string>> groupFieldsByTable(
        const std::vector<std::string>& fields);

    // 检查特定数据类型
    DataStatus checkDataType(DataType type,
                             const std::string& date);
    
    // 检查估值数据有效性（PE/PB是否>0）
    DataStatus checkValuationData(const std::string& date);
    
    // 检查价格数据可用性
    DataStatus checkPriceData(const std::string& date);
    
    // 获取数据覆盖率统计
    struct CoverageStats {
        int totalStocks = 0;
        int validStocks = 0;
        double coverageRate = 0.0;
        std::map<std::string, int> fieldStats;  // 各字段有效数量
        
        foundation::json::JsonFacade toJson() const {
            auto json = foundation::json::JsonFacade::createObject();
            json.set("total_stocks", json_helper::toJsonValue(totalStocks));
            json.set("valid_stocks", json_helper::toJsonValue(validStocks));
            json.set("coverage_rate", json_helper::toJsonValue(coverageRate));
            
            auto fieldsJson = foundation::json::JsonFacade::createObject();
            for (const auto& [field, count] : fieldStats) {
                fieldsJson.set(field, json_helper::toJsonValue(count));
            }
            json.set("field_stats", fieldsJson);
            
            return json;
        }
    };
    
    CoverageStats getCoverageStats(DataType type,
                                   const std::string& date);
    
    // 批量检查日期范围
    std::map<std::string, DataStatus> checkDateRange(const std::string& startDate,
                                                     const std::string& endDate,
                                                     DataType type);
    
private:
    std::shared_ptr<astock::database::ISqlDatabase> db_;
    mutable std::mutex cacheMutex_;
    mutable std::unordered_map<std::string, bool> columnExistenceCache_;
    mutable std::unordered_map<std::string, bool> fieldValidityCache_;
    
    // 内部辅助方法
    bool isFieldValid(const std::string& table,
                      const std::string& field,
                      const std::string& date,
                      const std::string& condition = "> 0");
    
    // 检查字段列表
    DataStatus checkFields(const std::vector<std::string>& fields,
                           const std::string& date,
                           const std::string& table = "mkt.daily_bar");
    
    // 数据类型到字段映射
    std::vector<std::string> getFieldsForType(DataType type);
    std::string resolveTableForFields(const std::vector<std::string>& fields) const;
    
    // 创建错误状态
    DataStatus createErrorStatus(const std::string& message,
                                 const std::vector<std::string>& missing = {},
                                 const std::vector<std::string>& invalid = {}) const;
};

} // namespace factor