#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "foundation/json/json_facade.h"
#include "foundation/Utils/Uuid.h"
#include "DataAvailabilityCheckerWithCache.h"
#include "HistoricalView.h"
#include "FactorCacheManager.h"

// 前向声明
class DatabaseConnection;
namespace factor {

// 计算上下文
struct CalculationContext {
    std::string date;                          // 计算日期
    std::vector<std::string> symbols;          // 股票代码列表
    std::shared_ptr<HistoricalView> historicalView;  // 引擎裁剪后的历史视图
    
    CalculationContext() = default;
    
    CalculationContext(const std::string& d,
                      const std::vector<std::string>& s,
                      std::shared_ptr<HistoricalView> view)
        : date(d), symbols(s), historicalView(std::move(view)) {}
};

// 数据需求
struct DataRequirements {
    std::vector<std::string> requiredFields;
    std::vector<std::string> optionalFields;
    std::vector<std::string> alternativeFields;  // 替代字段（当主字段不可用时）
    
    bool hasAlternative(const std::string& field) const {
        return std::find(alternativeFields.begin(), 
                        alternativeFields.end(), field) != alternativeFields.end();
    }
    
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();
        
        auto requiredArray = foundation::json::JsonFacade::createArray();
        for (const auto& field : requiredFields) {
            requiredArray.push_back(foundation::json::JsonFacade::createString(field));
        }
        json.set("required", requiredArray);
        
        auto optionalArray = foundation::json::JsonFacade::createArray();
        for (const auto& field : optionalFields) {
            optionalArray.push_back(foundation::json::JsonFacade::createString(field));
        }
        json.set("optional", optionalArray);
        
        auto alternativeArray = foundation::json::JsonFacade::createArray();
        for (const auto& field : alternativeFields) {
            alternativeArray.push_back(foundation::json::JsonFacade::createString(field));
        }
        json.set("alternative", alternativeArray);
        
        return json;
    }
};

// 边界规则
struct BoundaryRules {
    int minDataPoints = 21;
    std::string handleNewStock = "exclude_if_lt_60d";  // exclude_if_lt_60d, include
    std::string handleSuspended = "forward_fill";      // forward_fill, exclude, set_null
    std::string handleDelisted = "keep_until_delist";  // keep_until_delist, exclude
    std::string handleOutliers = "winsorize_3sigma";   // winsorize_3sigma, exclude, keep
    
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();
        json.set("minDataPoints", minDataPoints);
        json.set("handleNewStock", handleNewStock);
        json.set("handleSuspended", handleSuspended);
        json.set("handleDelisted", handleDelisted);
        json.set("handleOutliers", handleOutliers);
        return json;
    }
};

// 计算结果
struct CalculationResult {
    foundation::utils::Uuid calculationId;
    std::string date;
    std::unordered_map<std::string, double> values;  // symbol -> factor value
    DataStatus dataStatus;
    foundation::json::JsonFacade metadata;
    
    bool isEmpty() const { return values.empty(); }
    
    static CalculationResult createError(const std::string& errorMsg) {
        CalculationResult result;
        result.dataStatus.availability = DataAvailability::UNAVAILABLE;
        result.dataStatus.message = errorMsg;
        return result;
    }
    
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();
        json.set("calculation_id", calculationId.to_string());
        json.set("trade_date", date);
        json.set("data_status", dataStatus.toJson());
        json.set("metadata", metadata);
        
        auto valuesJson = foundation::json::JsonFacade::createObject();
        for (const auto& [symbol, value] : values) {
            valuesJson.set(symbol, value);
        }
        json.set("values", valuesJson);
        
        return json;
    }
    
    static CalculationResult fromJson(const foundation::json::JsonFacade& json) {
        CalculationResult result;
        
        if (json.has("calculation_id")) {
            result.calculationId = foundation::utils::Uuid::from_string(
                json.get("calculation_id").asString()
            );
        }
        
        if (json.has("trade_date")) {
            result.date = json.get("trade_date").asString();
        }
        
        if (json.has("data_status")) {
            result.dataStatus = DataStatus::fromJson(json.get("data_status"));
        }
        
        if (json.has("metadata")) {
            result.metadata = json.get("metadata");
        }
        
        if (json.has("values")) {
            auto valuesJson = json.get("values");
            if (valuesJson.isObject()) {
                auto keys = valuesJson.getKeys();
                for (const auto& key : keys) {
                    result.values[key] = valuesJson.get(key).asDouble();
                }
            }
        }
        
        return result;
    }
};

// 因子基类（带缓存支持）
class BaseFactorWithCache {
public:
    BaseFactorWithCache();
    virtual ~BaseFactorWithCache() = default;
    
    // 禁止拷贝
    BaseFactorWithCache(const BaseFactorWithCache&) = delete;
    BaseFactorWithCache& operator=(const BaseFactorWithCache&) = delete;
    
    // 允许移动
    BaseFactorWithCache(BaseFactorWithCache&&) = default;
    BaseFactorWithCache& operator=(BaseFactorWithCache&&) = default;
    
    // 设置缓存管理器
    void setCacheManager(std::shared_ptr<FactorCacheManager> cacheManager);
    
    // 计算接口（带缓存支持）
    virtual CalculationResult calculate(const CalculationContext& context);
    
    // 计算接口（无缓存）
    virtual CalculationResult calculateWithoutCache(const CalculationContext& context) = 0;
    
    // 批量计算（带缓存支持）
    virtual std::vector<CalculationResult> calculateBatch(
        const std::vector<CalculationContext>& contexts);
    
    // 数据需求
    virtual DataRequirements getDataRequirements() const = 0;
    
    // 边界规则
    virtual BoundaryRules getBoundaryRules() const = 0;
    
    // 检查数据可用性
    virtual DataStatus checkDataAvailability(const std::string& date) const;
    
    // 获取实例信息
    foundation::utils::Uuid getInstanceId() const { return instanceId_; }
    std::string getName() const { return name_; }
    std::string getDescription() const { return description_; }
    std::string getFactorType() const { return factorType_; }
    
    // 序列化/反序列化
    foundation::json::JsonFacade toJson() const;
    void fromJson(const foundation::json::JsonFacade& json);
    
protected:
    foundation::utils::Uuid instanceId_;
    std::string name_;
    std::string description_;
    std::string factorType_;
    
    DataRequirements dataRequirements_;
    BoundaryRules boundaryRules_;
    
    std::shared_ptr<DataAvailabilityCheckerWithCache> dataChecker_;
    std::shared_ptr<DatabaseConnection> db_;
    std::shared_ptr<FactorCacheManager> cacheManager_;
    
    // 边界规则处理
    virtual std::unordered_map<std::string, double> applyBoundaryRules(
        const std::unordered_map<std::string, double>& rawValues,
        const CalculationContext& context);
    
    // 异常值处理
    virtual std::unordered_map<std::string, double> handleOutliers(
        const std::unordered_map<std::string, double>& values);
    
    // 加载配置
    virtual void loadConfig(const foundation::json::JsonFacade& config);
};

} // namespace factor