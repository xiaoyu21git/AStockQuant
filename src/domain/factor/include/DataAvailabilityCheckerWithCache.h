#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include "foundation/json/json_facade.h"
#include "foundation/Utils/Uuid.h"
#include "FactorCacheManager.h"

// 前向声明
class DatabaseConnection;

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
        json.set("availability", static_cast<int>(availability));
        json.set("coverage", coverage);
        json.set("message", message);
        
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
    
    // 从JSON解析
    static DataStatus fromJson(const foundation::json::JsonFacade& json) {
        DataStatus status;
        
        if (json.has("availability")) {
            status.availability = static_cast<DataAvailability>(
                json.get("availability").asInt()
            );
        }
        
        if (json.has("coverage")) {
            status.coverage = json.get("coverage").asDouble();
        }
        
        if (json.has("message")) {
            status.message = json.get("message").asString();
        }
        
        if (json.has("missing_fields")) {
            auto missing = json.get("missing_fields");
            if (missing.isArray()) {
                for (size_t i = 0; i < missing.size(); i++) {
                    status.missingFields.push_back(missing.at(i).asString());
                }
            }
        }
        
        if (json.has("invalid_fields")) {
            auto invalid = json.get("invalid_fields");
            if (invalid.isArray()) {
                for (size_t i = 0; i < invalid.size(); i++) {
                    status.invalidFields.push_back(invalid.at(i).asString());
                }
            }
        }
        
        return status;
    }
};

// 数据可用性检查器（带缓存支持）
class DataAvailabilityCheckerWithCache {
public:
    DataAvailabilityCheckerWithCache(
        std::shared_ptr<DatabaseConnection> db,
        std::shared_ptr<FactorCacheManager> cacheManager = nullptr);
    
    ~DataAvailabilityCheckerWithCache() = default;
    
    // 设置缓存管理器
    void setCacheManager(std::shared_ptr<FactorCacheManager> cacheManager);
    
    // 检查因子数据可用性（带缓存）
    DataStatus checkFactorData(const std::string& instanceId,
                               const std::string& startDate,
                               const std::string& endDate);
    
    // 检查特定数据类型（带缓存）
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
            json.set("total_stocks", totalStocks);
            json.set("valid_stocks", validStocks);
            json.set("coverage_rate", coverageRate);
            
            auto fieldsJson = foundation::json::JsonFacade::createObject();
            for (const auto& [field, count] : fieldStats) {
                fieldsJson.set(field, count);
            }
            json.set("field_stats", fieldsJson);
            
            return json;
        }
        
        static CoverageStats fromJson(const foundation::json::JsonFacade& json) {
            CoverageStats stats;
            
            if (json.has("total_stocks")) {
                stats.totalStocks = json.get("total_stocks").asInt();
            }
            
            if (json.has("valid_stocks")) {
                stats.validStocks = json.get("valid_stocks").asInt();
            }
            
            if (json.has("coverage_rate")) {
                stats.coverageRate = json.get("coverage_rate").asDouble();
            }
            
            if (json.has("field_stats")) {
                auto fields = json.get("field_stats");
                if (fields.isObject()) {
                    auto keys = fields.getKeys();
                    for (const auto& key : keys) {
                        stats.fieldStats[key] = fields.get(key).asInt();
                    }
                }
            }
            
            return stats;
        }
    };
    
    CoverageStats getCoverageStats(DataType type,
                                   const std::string& date);
    
    // 批量检查日期范围
    std::map<std::string, DataStatus> checkDateRange(const std::string& startDate,
                                                     const std::string& endDate,
                                                     DataType type);
    
    // 清除缓存
    void clearCache();
    
private:
    std::shared_ptr<DatabaseConnection> db_;
    std::shared_ptr<FactorCacheManager> cacheManager_;
    
    // 内部辅助方法
    bool isFieldValid(const std::string& table,
                      const std::string& field,
                      const std::string& date,
                      const std::string& condition = "> 0");
    
    // 检查字段列表（带缓存）
    DataStatus checkFieldsWithCache(const std::vector<std::string>& fields,
                                   const std::string& date,
                                   const std::string& table = "daily_bar");
    
    // 检查字段列表（无缓存）
    DataStatus checkFieldsWithoutCache(const std::vector<std::string>& fields,
                                      const std::string& date,
                                      const std::string& table = "daily_bar");
    
    // 数据类型到字段映射
    std::vector<std::string> getFieldsForType(DataType type);
    
    // 数据类型到字符串映射
    std::string getDataTypeString(DataType type);
    
    // 创建错误状态
    DataStatus createErrorStatus(const std::string& message,
                                 const std::vector<std::string>& missing = {},
                                 const std::vector<std::string>& invalid = {}) const;
};

} // namespace factor