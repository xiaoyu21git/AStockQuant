#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/LowVolFactor.h"
#include "domain/factor/include/MomentumFactor.h"
#include "domain/factor/include/QualityFactor.h"
#include "domain/factor/include/SizeFactor.h"
#include "domain/factor/include/ValueFactor.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"
#include <algorithm>
#include <chrono>

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

std::string normalizeFactorType(const QString& rawType)
{
    const QString normalized = rawType.trimmed().toLower();
    if (normalized == QString::fromUtf8("动量因子") || normalized == "momentum") {
        return "动量因子";
    }
    if (normalized == QString::fromUtf8("价值因子") || normalized == "value") {
        return "价值因子";
    }
    if (normalized == QString::fromUtf8("质量因子") || normalized == "quality") {
        return "质量因子";
    }
    if (normalized == QString::fromUtf8("规模因子") || normalized == "size") {
        return "规模因子";
    }
    if (normalized == QString::fromUtf8("低波因子") || normalized == "low_vol" || normalized == "lowvol") {
        return "低波因子";
    }
    return {};
}

std::string resolveFactorType(const foundation::json::JsonFacade& config, const QString& fallbackType)
{
    if (config.has("factor_type")) {
        const std::string type = normalizeFactorType(QString::fromStdString(config.get("factor_type").asString()));
        if (!type.empty()) {
            return type;
        }
    }

    {
        const std::string type = normalizeFactorType(fallbackType);
        if (!type.empty()) {
            return type;
        }
    }

    if (config.has("calculation")) {
        auto calculation = config.get("calculation");
        if (calculation.isObject() && calculation.has("type")) {
            const std::string type = normalizeFactorType(QString::fromStdString(calculation.get("type").asString()));
            if (!type.empty()) {
                return type;
            }
        }
    }

    return {};
}

}

FactorInstanceManager::FactorInstanceManager(
    std::shared_ptr<astock::database::QtMySQLDatabase> db,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
    : db_(db), dataChecker_(dataChecker) {
    
    // 创建线程池
    threadPool_ = std::make_shared<foundation::thread::ThreadPoolExecutor>(4);
    
    // 预加载所有实例信息
    refreshCache();
}

std::shared_ptr<BaseFactor> FactorInstanceManager::createInstance(
    const std::string& instanceId) {
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    // 检查缓存
    auto it = instanceCache_.find(instanceId);
    if (it != instanceCache_.end()) {
        return it->second;
    }
    
    // 加载实例信息
    auto info = loadInstanceFromDB(instanceId);
    if (info.instanceId.empty()) {
        return nullptr;
    }
    
    // 根据因子类型创建具体实例
    std::shared_ptr<BaseFactor> factor;
    
    if (info.factorType == "动量因子") {
        factor = createMomentumFactor(info);
    } else if (info.factorType == "价值因子") {
        factor = createValueFactor(info);
    } else if (info.factorType == "质量因子") {
        factor = createQualityFactor(info);
    } else if (info.factorType == "规模因子") {
        factor = createSizeFactor(info);
    } else if (info.factorType == "低波因子") {
        factor = createLowVolFactor(info);
    } else {
        factor = BaseFactor::createFromDatabase(instanceId, db_, dataChecker_);
    }
    
    if (factor) {
        // 缓存实例
        instanceCache_[instanceId] = factor;
        infoCache_[instanceId] = info;
    }
    
    return factor;
}

FactorInstanceInfo FactorInstanceManager::getInstanceInfo(
    const std::string& instanceId) {
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    // 检查缓存
    auto it = infoCache_.find(instanceId);
    if (it != infoCache_.end()) {
        return it->second;
    }
    
    // 从数据库加载
    auto info = loadInstanceFromDB(instanceId);
    if (!info.instanceId.empty()) {
        infoCache_[instanceId] = info;
    }
    
    return info;
}

std::vector<FactorInstanceInfo> FactorInstanceManager::listAvailableInstances() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::vector<FactorInstanceInfo> availableInstances;
    
    for (const auto& [id, info] : infoCache_) {
        if (info.isAvailable) {
            availableInstances.push_back(info);
        }
    }
    
    return availableInstances;
}

std::vector<FactorInstanceInfo> FactorInstanceManager::listAllInstances() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    std::vector<FactorInstanceInfo> allInstances;
    allInstances.reserve(infoCache_.size());
    
    for (const auto& [id, info] : infoCache_) {
        allInstances.push_back(info);
    }
    
    return allInstances;
}

std::map<std::string, DataStatus> FactorInstanceManager::batchCheckAvailability(
    const std::vector<std::string>& instanceIds,
    const std::string& date) {
    
    std::map<std::string, DataStatus> results;
    
    for (const auto& instanceId : instanceIds) {
        try {
            auto info = getInstanceInfo(instanceId);
            if (!info.instanceId.empty()) {
                results[instanceId] = dataChecker_->checkFactorData(instanceId, date, date);
            } else {
                results[instanceId] = DataStatus();
            }
        } catch (const std::exception&) {
            DataStatus status;
            status.availability = DataAvailability::UNAVAILABLE;
            status.message = "检查失败";
            results[instanceId] = status;
        }
    }
    
    return results;
}

bool FactorInstanceManager::updateInstanceConfig(
    const std::string& instanceId,
    const foundation::json::JsonFacade& newConfig) {
    
    try {
        const int affectedRows = db_->executeUpdate(
            "UPDATE factor_instance SET full_config = ?, updated_at = CURRENT_TIMESTAMP "
            "WHERE instance_id = ?",
            makePositionalParams({QString::fromStdString(newConfig.toString()), QString::fromStdString(instanceId)})
        );
        
        if (affectedRows > 0) {
            // 清除缓存
            std::lock_guard<std::mutex> lock(cacheMutex_);
            instanceCache_.erase(instanceId);
            infoCache_.erase(instanceId);
            
            return true;
        }
        
    } catch (const std::exception&) {
        // 更新失败
    }
    
    return false;
}

void FactorInstanceManager::refreshCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    // 清空缓存
    instanceCache_.clear();
    infoCache_.clear();
    
    // 重新加载所有实例
    auto allInstances = loadAllInstancesFromDB();
    for (auto& info : allInstances) {
        infoCache_[info.instanceId] = info;
    }
}

FactorInstanceManager::Statistics FactorInstanceManager::getStatistics() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    Statistics stats;
    stats.totalInstances = static_cast<int>(infoCache_.size());
    
    for (const auto& [id, info] : infoCache_) {
        if (info.isAvailable) {
            stats.availableInstances++;
        } else {
            stats.unavailableInstances++;
        }
        
        stats.instancesByType[info.factorType]++;
    }
    
    return stats;
}

// ============ 私有方法实现 ============

FactorInstanceInfo FactorInstanceManager::loadInstanceFromDB(
    const std::string& instanceId) {
    
    FactorInstanceInfo info;
    
    try {
        auto result = db_->executeQuery(
            "SELECT fi.instance_id, fi.instance_name, fi.description, "
            "CAST(fi.full_config AS CHAR) AS full_config, fi.status, fi.factor_id, f.major_category "
            "FROM factor_instance fi "
            "LEFT JOIN factors f ON fi.factor_id = f.factor_id "
            "WHERE fi.instance_id = ?",
            makePositionalParams({QString::fromStdString(instanceId)})
        );
        
        if (result.isEmpty()) {
            return info;  // 返回空信息
        }

        const auto& row = result.getRow(0);
        
        info.instanceId = row.getString("instance_id").toStdString();
        info.instanceName = row.getString("instance_name").toStdString();
        info.description = row.getString("description").toStdString();
        info.config = foundation::json::JsonFacade::parse(row.getString("full_config").toStdString());
        info.factorType = resolveFactorType(info.config, row.getString("major_category"));
        
        // 检查数据可用性
        updateInstanceAvailability(info);
        
    } catch (const std::exception&) {
        // 加载失败，返回空信息
    }
    
    return info;
}

std::shared_ptr<BaseFactor> FactorInstanceManager::createMomentumFactor(
    const FactorInstanceInfo& info) {
    
    return MomentumFactor::create(
        info.instanceId,
        db_,
        dataChecker_
    );
}

std::shared_ptr<BaseFactor> FactorInstanceManager::createValueFactor(
    const FactorInstanceInfo& info) {
    return ValueFactor::create(info.instanceId, db_, dataChecker_);
}

std::shared_ptr<BaseFactor> FactorInstanceManager::createQualityFactor(
    const FactorInstanceInfo& info) {
    return QualityFactor::create(info.instanceId, db_, dataChecker_);
}

std::shared_ptr<BaseFactor> FactorInstanceManager::createSizeFactor(
    const FactorInstanceInfo& info) {
    return SizeFactor::create(info.instanceId, db_, dataChecker_);
}

std::shared_ptr<BaseFactor> FactorInstanceManager::createLowVolFactor(
    const FactorInstanceInfo& info) {
    return LowVolFactor::create(info.instanceId, db_, dataChecker_);
}

FactorInstanceManager::ParsedConfig FactorInstanceManager::parseConfig(
    const foundation::json::JsonFacade& config) {
    
    ParsedConfig parsed;
    
    if (config.has("data_requirements")) {
        auto dataReq = config.get("data_requirements");
        
        if (dataReq.has("required")) {
            auto required = dataReq.get("required");
            for (size_t i = 0; i < required.size(); i++) {
                parsed.dataRequirements.required.push_back(required.at(i).asString());
            }
        }
        
        if (dataReq.has("optional")) {
            auto optional = dataReq.get("optional");
            for (size_t i = 0; i < optional.size(); i++) {
                parsed.dataRequirements.optional.push_back(optional.at(i).asString());
            }
        }
    }
    
    if (config.has("calculation")) {
        auto calc = config.get("calculation");
        if (calc.isObject()) {
            // 解析计算参数
            // 这里简化处理，实际需要根据具体因子类型解析
        }
    }
    
    if (config.has("boundary_rules")) {
        auto rules = config.get("boundary_rules");
        
        if (rules.has("min_data_points")) {
            parsed.boundaryRules.minDataPoints = rules.get("min_data_points").asInt();
        }
        
        if (rules.has("handle_new_stock")) {
            parsed.boundaryRules.handleNewStock = rules.get("handle_new_stock").asString();
        }
        
        if (rules.has("handle_suspended")) {
            parsed.boundaryRules.handleSuspended = rules.get("handle_suspended").asString();
        }
        
        if (rules.has("handle_delisted")) {
            parsed.boundaryRules.handleDelisted = rules.get("handle_delisted").asString();
        }
    }
    
    return parsed;
}

void FactorInstanceManager::updateInstanceAvailability(
    FactorInstanceInfo& info,
    const std::string& date) {
    
    if (!dataChecker_) {
        info.isAvailable = false;
        info.dataStatus.message = "数据检查器未初始化";
        return;
    }
    
    // 使用当前日期或指定日期
    std::string checkDate = date;
    if (checkDate.empty()) {
        // 获取最近交易日
        try {
            auto latestResult = db_->executeQuery(
                "SELECT MAX(trade_date) as latest_date FROM daily_bar"
            );
            
            if (!latestResult.isEmpty()) {
                checkDate = latestResult.getRow(0).getString("latest_date").toStdString();
            }
        } catch (const std::exception&) {
            // 获取失败，使用空日期
        }
    }
    
    if (checkDate.empty()) {
        info.isAvailable = false;
        info.dataStatus.message = "无法确定检查日期";
        return;
    }
    
    // 检查数据可用性
    info.dataStatus = dataChecker_->checkFactorData(
        info.instanceId,
        checkDate,
        checkDate
    );
    
    info.isAvailable = info.dataStatus.isValid();
}

std::vector<FactorInstanceInfo> FactorInstanceManager::loadAllInstancesFromDB() {
    std::vector<FactorInstanceInfo> instances;
    
    try {
        auto result = db_->executeQuery(
            "SELECT fi.instance_id, fi.instance_name, fi.description, "
            "CAST(fi.full_config AS CHAR) AS full_config, fi.status, fi.factor_id, f.major_category "
            "FROM factor_instance fi "
            "LEFT JOIN factors f ON fi.factor_id = f.factor_id "
            "WHERE fi.status = 'ACTIVE' "
            "ORDER BY fi.created_at DESC"
        );
        
        for (size_t i = 0; i < result.rowCount(); i++) {
            auto row = result.getRow(i);
            
            FactorInstanceInfo info;
            info.instanceId = row.getString("instance_id").toStdString();
            info.instanceName = row.getString("instance_name").toStdString();
            info.description = row.getString("description").toStdString();
            info.config = foundation::json::JsonFacade::parse(row.getString("full_config").toStdString());
            info.factorType = resolveFactorType(info.config, row.getString("major_category"));
            
            // 检查数据可用性
            updateInstanceAvailability(info);
            
            instances.push_back(info);
        }
        
    } catch (const std::exception&) {
        // 加载失败，返回空列表
    }
    
    return instances;
}

} // namespace factor