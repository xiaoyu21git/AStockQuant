#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "foundation/json/json_facade.h"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "BaseFactor.h"
#include "DataAvailabilityChecker.h"
#include "JsonFacadeHelpers.h"

// 前向声明
namespace astock {
namespace database {
class QtMySQLDatabase;
}
}

namespace factor {

// 因子实例信息
struct FactorInstanceInfo {
    std::string instanceId;
    std::string instanceName;
    std::string description;
    std::string factorType;  // 动量、价值、质量等
    DataStatus dataStatus;   // 当前数据状态
    bool isAvailable;        // 是否可用（基于数据状态）
    foundation::json::JsonFacade config;  // 完整配置
    
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();
        json.set("instance_id", json_helper::toJsonValue(instanceId));
        json.set("instance_name", json_helper::toJsonValue(instanceName));
        json.set("description", json_helper::toJsonValue(description));
        json.set("factor_type", json_helper::toJsonValue(factorType));
        json.set("data_status", dataStatus.toJson());
        json.set("is_available", json_helper::toJsonValue(isAvailable));
        json.set("config", config);
        return json;
    }
};

// 因子实例管理器
class FactorInstanceManager {
public:
    FactorInstanceManager(std::shared_ptr<astock::database::QtMySQLDatabase> db,
                         std::shared_ptr<DataAvailabilityChecker> dataChecker);
    ~FactorInstanceManager() = default;
    
    // 禁止拷贝
    FactorInstanceManager(const FactorInstanceManager&) = delete;
    FactorInstanceManager& operator=(const FactorInstanceManager&) = delete;
    
    // 允许移动
    FactorInstanceManager(FactorInstanceManager&&) = default;
    FactorInstanceManager& operator=(FactorInstanceManager&&) = default;
    
    // 创建因子实例
    std::shared_ptr<BaseFactor> createInstance(const std::string& instanceId);
    
    // 获取实例信息（包含数据状态）
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
    
    // 获取统计信息
    struct Statistics {
        int totalInstances = 0;
        int availableInstances = 0;
        int unavailableInstances = 0;
        std::map<std::string, int> instancesByType;
        
        foundation::json::JsonFacade toJson() const {
            auto json = foundation::json::JsonFacade::createObject();
            json.set("total_instances", json_helper::toJsonValue(totalInstances));
            json.set("available_instances", json_helper::toJsonValue(availableInstances));
            json.set("unavailable_instances", json_helper::toJsonValue(unavailableInstances));
            
            auto typeJson = foundation::json::JsonFacade::createObject();
            for (const auto& [type, count] : instancesByType) {
                typeJson.set(type, json_helper::toJsonValue(count));
            }
            json.set("instances_by_type", typeJson);
            
            return json;
        }
    };
    
    Statistics getStatistics() const;

    std::shared_ptr<astock::database::QtMySQLDatabase> getDatabase() const { return db_; }

    friend class FactorInstanceManagerTestAccess;
    
private:
    std::shared_ptr<astock::database::QtMySQLDatabase> db_;
    std::shared_ptr<DataAvailabilityChecker> dataChecker_;
    std::shared_ptr<foundation::thread::ThreadPoolExecutor> threadPool_;
    
    // 实例缓存（线程安全）
    mutable std::mutex cacheMutex_;
    std::unordered_map<std::string, std::shared_ptr<BaseFactor>> instanceCache_;
    std::unordered_map<std::string, FactorInstanceInfo> infoCache_;
    
    // 从数据库加载实例配置
    FactorInstanceInfo loadInstanceFromDB(const std::string& instanceId);
    
    // 创建具体因子类型
    std::shared_ptr<BaseFactor> createMomentumFactor(
        const FactorInstanceInfo& info);
    std::shared_ptr<BaseFactor> createValueFactor(
        const FactorInstanceInfo& info);
    std::shared_ptr<BaseFactor> createQualityFactor(
        const FactorInstanceInfo& info);
    std::shared_ptr<BaseFactor> createSizeFactor(
        const FactorInstanceInfo& info);
    std::shared_ptr<BaseFactor> createLowVolFactor(
        const FactorInstanceInfo& info);
    std::shared_ptr<BaseFactor> createConfigurableFactor(
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
};

} // namespace factor