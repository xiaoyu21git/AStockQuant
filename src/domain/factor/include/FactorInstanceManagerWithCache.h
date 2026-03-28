#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "foundation/json/json_facade.h"
#include "foundation/Utils/Uuid.h"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "BaseFactorWithCache.h"
#include "DataAvailabilityCheckerWithCache.h"
#include "FactorCacheManager.h"

// 前向声明
class DatabaseConnection;

namespace factor {

// 因子实例信息
struct FactorInstanceInfo {
    foundation::utils::Uuid instanceId;
    std::string instanceName;
    std::string description;
    std::string factorType;  // 动量、价值、质量等
    DataStatus dataStatus;   // 当前数据状态
    bool isAvailable;        // 是否可用（基于数据状态）
    foundation::json::JsonFacade config;  // 完整配置
    
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();
        json.set("instance_id", instanceId.to_string());
        json.set("instance_name", instanceName);
        json.set("description", description);
        json.set("factor_type", factorType);
        json.set("data_status", dataStatus.toJson());
        json.set("is_available", isAvailable);
        json.set("config", config);
        return json;
    }
    
    static FactorInstanceInfo fromJson(const foundation::json::JsonFacade& json) {
        FactorInstanceInfo info;
        
        if (json.has("instance_id")) {
            info.instanceId = foundation::utils::Uuid::from_string(
                json.get("instance_id").asString()
            );
        }
        
        if (json.has("instance_name")) {
            info.instanceName = json.get("instance_name").asString();
        }
        
        if (json.has("description")) {
            info.description = json.get("description").asString();
        }
        
        if (json.has("factor_type")) {
            info.factorType = json.get("factor_type").asString();
        }
        
        if (json.has("data_status")) {
            info.dataStatus = DataStatus::fromJson(json.get("data_status"));
        }
        
        if (json.has("is_available")) {
            info.isAvailable = json.get("is_available").asBool();
        }
        
        if (json.has("config")) {
            info.config = json.get("config");
        }
        
        return info;
    }
};

// 因子实例管理器（带缓存支持）
class FactorInstanceManagerWithCache {
public:
    FactorInstanceManagerWithCache(
        std::shared_ptr<DatabaseConnection> db,
        std::shared_ptr<DataAvailabilityCheckerWithCache> dataChecker,
        std::shared_ptr<FactorCacheManager> cacheManager = nullptr);
    
    ~FactorInstanceManagerWithCache() = default;
    
    // 禁止拷贝
    FactorInstanceManagerWithCache(const FactorInstanceManagerWithCache&) = delete;
    FactorInstanceManagerWithCache& operator=(const FactorInstanceManagerWithCache&) = delete;
    
    // 允许移动
    FactorInstanceManagerWithCache(FactorInstanceManagerWithCache&&) = default;
    FactorInstanceManagerWithCache& operator=(FactorInstanceManagerWithCache&&) = default;
    
    // 设置缓存管理器
    void setCacheManager(std::shared_ptr<FactorCacheManager> cacheManager);
    
    // 创建因子实例（带缓存）
    std::shared_ptr<BaseFactorWithCache> createInstance(const std::string& instanceId);
    
    // 获取实例信息（带缓存）
    FactorInstanceInfo getInstanceInfo(const std::string& instanceId);
    
    // 列出所有可用实例（根据数据可用性过滤）
    std::vector<FactorInstanceInfo> listAvailableInstances();
    
    // 列出所有实例（不过滤）
    std::vector<FactorInstanceInfo> listAllInstances();
    
    // 批量检查实例可用性
    std::map<std::string, DataStatus> batchCheckAvailability(
        const std::vector<std::string>& instanceIds,
        const std::string& date);
    
    // 更新实例配置
    bool updateInstanceConfig(const std::string& instanceId,
                             const foundation::json::JsonFacade& newConfig);
    
    // 刷新缓存
    void refreshCache();
    
    // 清除因子缓存
    void invalidateFactorCache(const std::string& instanceId);
    
    // 获取统计信息
    struct Statistics {
        int totalInstances = 0;
        int availableInstances = 0;
        int unavailableInstances = 0;
        std::map<std::string, int> instancesByType;
        
        foundation::json::JsonFacade toJson() const {
            auto json = foundation::json::JsonFacade::createObject();
            json.set("total_instances", totalInstances);
            json.set("available_instances", availableInstances);
            json.set("unavailable_instances", unavailableInstances);
            
            auto typeJson = foundation::json::JsonFacade::createObject();
            for (const auto& [type, count] : instancesByType) {
                typeJson.set(type, count);
            }
            json.set("instances_by_type", typeJson);
            
            return json;
        }
    };
    
    Statistics getStatistics() const;
    
private:
    std::shared_ptr<DatabaseConnection> db_;
    std::shared_ptr<DataAvailabilityCheckerWithCache> dataChecker_;
    std::shared_ptr<FactorCacheManager> cacheManager_;
    std::shared_ptr<foundation::thread::ThreadPoolExecutor> threadPool_;
    
    // 实例缓存（线程安全）
    mutable std::mutex cacheMutex_;
    std::unordered_map<std::string, std::shared_ptr<BaseFactorWithCache>> instanceCache_;
    std::unordered_map<std::string, FactorInstanceInfo> infoCache_;
    
    // 从数据库加载实例配置
    FactorInstanceInfo loadInstanceFromDB(const std::string& instanceId);
    
    // 从缓存加载实例信息
    bool loadInstanceFromCache(const std::string& instanceId,
                              FactorInstanceInfo& info);
    
    // 保存实例信息到缓存
    void saveInstanceToCache(const FactorInstanceInfo& info);
    
    // 创建具体因子类型
    std::shared_ptr<BaseFactorWithCache> createMomentumFactor(
        const FactorInstanceInfo& info);
    std::shared_ptr<BaseFactorWithCache> createValueFactor(
        const FactorInstanceInfo& info);
    std::shared_ptr<BaseFactorWithCache> createQualityFactor(
        const FactorInstanceInfo& info);
    std::shared_ptr<BaseFactorWithCache> createSizeFactor(
        const FactorInstanceInfo& info);
    std::shared_ptr<BaseFactorWithCache> createLowVolFactor(
        const FactorInstanceInfo& info);
    
    // 解析配置
    struct ParsedConfig {
        struct DataRequirements {
            std::vector<std::string> required;
            std::vector<std::string> optional;
        } dataRequirements;
        
        struct CalculationParams {
            std::unordered_map<std::string, foundation::json::JsonFacade> params;
        } calculationParams;
        
        struct BoundaryRules {
            int minDataPoints;
            std::string handleNewStock;
            std::string handleSuspended;
            std::string handleDelisted;
        } boundaryRules;
    };
    
    ParsedConfig parseConfig(const foundation::json::JsonFacade& config);
    
    // 检查数据可用性并更新状态
    void updateInstanceAvailability(FactorInstanceInfo& info,
                                   const std::string& date = "");
    
    // 批量加载实例信息
    std::vector<FactorInstanceInfo> loadAllInstancesFromDB();
    
    // 批量加载实例信息到缓存
    void loadAllInstancesToCache();
};

} // namespace factor