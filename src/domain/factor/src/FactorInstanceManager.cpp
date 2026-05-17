#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/CustomFactor.h"
#include "domain/factor/include/DividendFactor.h"
#include "domain/factor/include/GrowthFactor.h"
#include "domain/factor/include/IndustryFactor.h"
#include "domain/factor/include/LiquidityFactor.h"
#include "domain/factor/include/MacroFactor.h"
#include "domain/factor/include/factor_enums.h"
#include "domain/factor/include/LowVolFactor.h"
#include "domain/factor/include/MomentumFactor.h"
#include "domain/factor/include/QualityFactor.h"
#include "domain/factor/include/SentimentFactor.h"
#include "domain/factor/include/SizeFactor.h"
#include "domain/factor/include/TechnicalFactor.h"
#include "domain/factor/include/ValueFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"
#include <QDebug>
#include <QVariant>
#include <algorithm>
#include <chrono>

namespace factor {

namespace {

QString positionalParamKey(int index)
{
    return QStringLiteral("__pos_%1").arg(index, 6, 10, QLatin1Char('0'));
}

std::map<QString, QVariant> makePositionalParams(std::initializer_list<QVariant> values)
{
    std::map<QString, QVariant> params;
    int index = 0;
    for (const QVariant& value : values) {
        params.emplace(positionalParamKey(index++), value);
    }
    return params;
}

using FactorCreator = std::shared_ptr<BaseFactor> (*)(const FactorInstanceInfo&, std::shared_ptr<DataAvailabilityChecker>);

std::shared_ptr<BaseFactor> createMomentumFactorEntry(const FactorInstanceInfo& info,
                                                      std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    return MomentumFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createValueFactorEntry(const FactorInstanceInfo& info,
                                                   std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    return ValueFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createQualityFactorEntry(const FactorInstanceInfo& info,
                                                     std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    return QualityFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createSizeFactorEntry(const FactorInstanceInfo& info,
                                                  std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    return SizeFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createLowVolFactorEntry(const FactorInstanceInfo& info,
                                                    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    return LowVolFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createGrowthFactorEntry(const FactorInstanceInfo& info,
                                                    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    return GrowthFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createDividendFactorEntry(const FactorInstanceInfo& info,
                                                      std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    return DividendFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createTechnicalFactorEntry(const FactorInstanceInfo& info,
                                                       std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    return TechnicalFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createLiquidityFactorEntry(const FactorInstanceInfo& info,
                                                       std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    return LiquidityFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createMacroFactorEntry(const FactorInstanceInfo& info,
                                                   std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    return MacroFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createIndustryFactorEntry(const FactorInstanceInfo& info,
                                                      std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    return IndustryFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createSentimentFactorEntry(const FactorInstanceInfo& info,
                                                       std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    return SentimentFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createCustomFactorEntry(const FactorInstanceInfo& info,
                                                    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    return CustomFactor::create(info, std::move(dataChecker));
}

const std::pair<FactorType, FactorCreator> kFactorCreators[] = {
    {FactorType::MOMENTUM, &createMomentumFactorEntry},
    {FactorType::VALUE, &createValueFactorEntry},
    {FactorType::QUALITY, &createQualityFactorEntry},
    {FactorType::SIZE, &createSizeFactorEntry},
    {FactorType::LOW_VOLATILITY, &createLowVolFactorEntry},
    {FactorType::GROWTH, &createGrowthFactorEntry},
    {FactorType::DIVIDEND, &createDividendFactorEntry},
    {FactorType::TECHNICAL, &createTechnicalFactorEntry},
    {FactorType::LIQUIDITY, &createLiquidityFactorEntry},
    {FactorType::MACRO, &createMacroFactorEntry},
    {FactorType::INDUSTRY, &createIndustryFactorEntry},
    {FactorType::SENTIMENT, &createSentimentFactorEntry},
    {FactorType::CUSTOM, &createCustomFactorEntry},
};

FactorCreator resolveFactorCreator(FactorType factorType)
{
    const auto* it = std::find_if(
        std::begin(kFactorCreators),
        std::end(kFactorCreators),
        [factorType](const auto& entry) {
            return entry.first == factorType;
        });
    return it != std::end(kFactorCreators) ? it->second : nullptr;
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

std::shared_ptr<BaseFactor> FactorInstanceManager::createFactorFromInfo(
    const std::string& instanceId,
    const FactorInstanceInfo& info)
{
    const FactorType factorType = info.factorType;
    const FactorCreator creator = resolveFactorCreator(factorType);
    if (!creator) {
        return nullptr;
    }

    try {
        return creator(info, dataChecker_);
    } catch (const std::exception& e) {
        qWarning() << "FactorInstanceManager::createFactorFromInfo: failed to create instance"
                   << QString::fromStdString(instanceId)
                   << "factorType=" << static_cast<int>(factorType)
                   << "error=" << e.what();
        return nullptr;
    } catch (...) {
        qWarning() << "FactorInstanceManager::createFactorFromInfo: failed to create instance"
                   << QString::fromStdString(instanceId)
                   << "factorType=" << static_cast<int>(factorType)
                   << "error=unknown";
        return nullptr;
    }
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
    
    std::shared_ptr<BaseFactor> factor = createFactorFromInfo(instanceId, info);
    
    if (factor) {
        // 缓存实例
        instanceCache_[instanceId] = factor;
        infoCache_[instanceId] = info;
    }
    
    return factor;
}

std::shared_ptr<BaseFactor> FactorInstanceManager::createIsolatedInstance(
    const std::string& instanceId)
{
    FactorInstanceInfo info;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto cachedInfoIt = infoCache_.find(instanceId);
        if (cachedInfoIt != infoCache_.end()) {
            info = cachedInfoIt->second;
        }
    }

    if (info.instanceId.empty()) {
        info = loadInstanceFromDB(instanceId);
        if (info.instanceId.empty()) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(cacheMutex_);
        infoCache_[instanceId] = info;
    }

    return createFactorFromInfo(instanceId, info);
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
        
        stats.instancesByType[factorTypeIndex(info.factorType)]++;
    }
    
    return stats;
}

// ============ 私有方法实现 ============

FactorInstanceInfo FactorInstanceManager::loadInstanceFromDB(
    const std::string& instanceId) {
    
    FactorInstanceInfo info;
    if (!db_) {
        return info;
    }
    
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
        info.factorType = config::factorTypeFromConfig(info.config);
        
        // 检查数据可用性
        updateInstanceAvailability(info);
        
    } catch (const std::exception&) {
        // 加载失败，返回空信息
    }
    
    return info;
}

void FactorInstanceManager::updateInstanceAvailability(
    FactorInstanceInfo& info,
    const std::string& date) {
    
    if (!dataChecker_) {
        info.isAvailable = false;
        return;
    }

    if (!db_) {
        info.isAvailable = false;
        info.dataStatus.message = "数据库未初始化";
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
    if (!db_) {
        return instances;
    }
    
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
            info.factorType = config::factorTypeFromConfig(info.config);

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
