#include "FactorInstanceManager.h"
#include "CompositeFactor.h"
#include "CustomFactor.h"
#include "DividendFactor.h"
#include "GrowthFactor.h"
#include "IndustryFactor.h"
#include "LiquidityFactor.h"
#include "MacroFactor.h"
#include "factor_enums.h"
#include "LowVolFactor.h"
#include "MomentumFactor.h"
#include "QualityFactor.h"
#include "SentimentFactor.h"
#include "SizeFactor.h"
#include "TechnicalFactor.h"
#include "ValueFactor.h"
#include "FactorConfigAccess.h"
#include "IFactorResolver.h"
#include "infrastructure/include/database/ISqlDatabase.h"
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <sstream>

namespace factor {

namespace {

std::vector<astock::database::SqlParam> buildParams(const std::string& a)
{
    return { astock::database::SqlParam{std::string(a)} };
}

std::vector<astock::database::SqlParam> buildParams(const std::string& a, const std::string& b)
{
    return { astock::database::SqlParam{std::string(a)}, astock::database::SqlParam{std::string(b)} };
}

class FactorResolverAdapter final : public IFactorResolver {
public:
    explicit FactorResolverAdapter(FactorInstanceManager& manager)
        : manager_(manager)
    {
    }

    std::shared_ptr<BaseFactor> createIsolated(const std::string& instanceId) override
    {
        return manager_.createIsolatedInstance(instanceId);
    }

    FactorInstanceInfo getInfo(const std::string& instanceId) override
    {
        return manager_.getInstanceInfo(instanceId);
    }

private:
    FactorInstanceManager& manager_;
};

using FactorCreator = std::shared_ptr<BaseFactor> (*)(const FactorInstanceInfo&, std::shared_ptr<DataAvailabilityChecker>, FactorInstanceManager&);

std::shared_ptr<BaseFactor> createMomentumFactorEntry(const FactorInstanceInfo& info,
                                                       std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                       FactorInstanceManager&)
{
    return MomentumFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createValueFactorEntry(const FactorInstanceInfo& info,
                                                    std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                    FactorInstanceManager&)
{
    return ValueFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createQualityFactorEntry(const FactorInstanceInfo& info,
                                                      std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                      FactorInstanceManager&)
{
    return QualityFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createSizeFactorEntry(const FactorInstanceInfo& info,
                                                   std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                   FactorInstanceManager&)
{
    return SizeFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createLowVolFactorEntry(const FactorInstanceInfo& info,
                                                     std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                     FactorInstanceManager&)
{
    return LowVolFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createGrowthFactorEntry(const FactorInstanceInfo& info,
                                                     std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                     FactorInstanceManager&)
{
    return GrowthFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createDividendFactorEntry(const FactorInstanceInfo& info,
                                                       std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                       FactorInstanceManager&)
{
    return DividendFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createTechnicalFactorEntry(const FactorInstanceInfo& info,
                                                        std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                        FactorInstanceManager&)
{
    return TechnicalFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createLiquidityFactorEntry(const FactorInstanceInfo& info,
                                                        std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                        FactorInstanceManager&)
{
    return LiquidityFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createMacroFactorEntry(const FactorInstanceInfo& info,
                                                    std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                    FactorInstanceManager&)
{
    return MacroFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createIndustryFactorEntry(const FactorInstanceInfo& info,
                                                       std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                       FactorInstanceManager&)
{
    return IndustryFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createSentimentFactorEntry(const FactorInstanceInfo& info,
                                                        std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                        FactorInstanceManager&)
{
    return SentimentFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createCustomFactorEntry(const FactorInstanceInfo& info,
                                                     std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                     FactorInstanceManager&)
{
    return CustomFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createCompositeFactorEntry(const FactorInstanceInfo& info,
                                                        std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                        FactorInstanceManager& manager)
{
    return CompositeFactor::create(
        info,
        std::move(dataChecker),
        std::make_shared<FactorResolverAdapter>(manager));
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
    {FactorType::COMPOSITE, &createCompositeFactorEntry},
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
    std::shared_ptr<astock::database::ISqlDatabase> db,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
    : db_(db), dataChecker_(dataChecker) {

    fprintf(stderr, "[FIM] ctor START\n"); fflush(stderr);
    threadPool_ = std::make_shared<foundation::thread::ThreadPoolExecutor>(4);
    fprintf(stderr, "[FIM] threadpool OK\n"); fflush(stderr);

    // refreshCache() 移除 — 策略需要的因子由 createInstance() 按需加载+缓存
    fprintf(stderr, "[FIM] ctor DONE (lazy load)\n"); fflush(stderr);
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
        return creator(info, dataChecker_, *this);
    } catch (const std::exception& e) {
        fprintf(stderr, "[FIM] createFactorFromInfo failed: %s type=%d error=%s\n",
                instanceId.c_str(), static_cast<int>(factorType), e.what());
        fflush(stderr);
        return nullptr;
    } catch (...) {
        fprintf(stderr, "[FIM] createFactorFromInfo failed: %s type=%d error=unknown\n",
                instanceId.c_str(), static_cast<int>(factorType));
        fflush(stderr);
        return nullptr;
    }
}

std::shared_ptr<BaseFactor> FactorInstanceManager::createInstance(
    const std::string& instanceId) {

    std::lock_guard<std::mutex> lock(cacheMutex_);

    auto it = instanceCache_.find(instanceId);
    if (it != instanceCache_.end()) {
        return it->second;
    }

    auto info = loadInstanceFromDB(instanceId);
    if (info.instanceId.empty()) {
        return nullptr;
    }

    std::shared_ptr<BaseFactor> factor = createFactorFromInfo(instanceId, info);

    if (factor) {
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

    auto it = infoCache_.find(instanceId);
    if (it != infoCache_.end()) {
        return it->second;
    }

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

    // 延迟加载: QML 因子选择窗口需要全量列表
    if (infoCache_.empty()) {
        auto all = loadAllInstancesFromDB();
        for (auto& info : all) infoCache_[info.instanceId] = info;
    }

    std::vector<FactorInstanceInfo> allInstances;
    allInstances.reserve(infoCache_.size());
    for (const auto& [id, info] : infoCache_) allInstances.push_back(info);
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
            buildParams(newConfig.toString(), instanceId)
        );

        if (affectedRows > 0) {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            instanceCache_.erase(instanceId);
            infoCache_.erase(instanceId);

            return true;
        }

    } catch (const std::exception& e) {
        fprintf(stderr, "[FIM] updateInstanceConfig EXCEPTION: id=%s error=%s\n",
                instanceId.c_str(), e.what());
        fflush(stderr);
    } catch (...) {
        fprintf(stderr, "[FIM] updateInstanceConfig UNKNOWN EXCEPTION: id=%s\n",
                instanceId.c_str());
        fflush(stderr);
    }

    return false;
}

void FactorInstanceManager::refreshCache() {
    fprintf(stderr, "[FIM] refreshCache START\n"); fflush(stderr);
    try {
        std::lock_guard<std::mutex> lock(cacheMutex_);

        instanceCache_.clear();
        infoCache_.clear();

        fprintf(stderr, "[FIM] refreshCache calling loadAllInstancesFromDB\n"); fflush(stderr);
        auto allInstances = loadAllInstancesFromDB();
        fprintf(stderr, "[FIM] refreshCache loaded %zu instances\n", allInstances.size()); fflush(stderr);
        for (auto& info : allInstances) {
            infoCache_[info.instanceId] = info;
        }
        fprintf(stderr, "[FIM] refreshCache DONE\n"); fflush(stderr);
    } catch (const std::exception& e) {
        fprintf(stderr, "[FIM] refreshCache EXCEPTION: %s\n", e.what()); fflush(stderr);
    } catch (...) {
        fprintf(stderr, "[FIM] refreshCache UNKNOWN EXCEPTION (likely Foundation logger crash)\n"); fflush(stderr);
        // 崩溃源：updateInstanceAvailability → DataAvailabilityChecker → Foundation logger
        // 缓存刷新失败不影响主流程，继续运行
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

FactorInstanceInfo FactorInstanceManager::loadInstanceFromDB(
    const std::string& instanceId) {

    FactorInstanceInfo info;
    if (!db_) {
        return info;
    }

    try {
        auto result = db_->executeQuery(
            "SELECT fi.instance_id, fi.instance_name, fi.description, "
            "fi.full_config::text AS full_config, fi.status, fi.factor_id, f.major_category "
            "FROM factor_instance fi "
            "LEFT JOIN factors f ON fi.factor_id = f.factor_id "
            "WHERE fi.instance_id = ?",
            buildParams(instanceId)
        );

        if (result.isEmpty()) {
            return info;
        }

        const auto& row = result.getRow(0);

        info.instanceId = row.getString("instance_id");
        info.instanceName = row.getString("instance_name");
        info.description = row.getString("description");
        info.config = foundation::json::JsonFacade::parse(row.getString("full_config"));
        info.factorType = config::factorTypeFromConfig(info.config);

        updateInstanceAvailability(info);

    } catch (const std::exception& e) {
        fprintf(stderr, "[FIM] loadInstanceFromDB EXCEPTION: id=%s error=%s\n",
                instanceId.c_str(), e.what());
        fflush(stderr);
    } catch (...) {
        fprintf(stderr, "[FIM] loadInstanceFromDB UNKNOWN EXCEPTION: id=%s\n",
                instanceId.c_str());
        fflush(stderr);
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

    std::string checkDate = date;
    if (checkDate.empty()) {
        try {
            auto latestResult = db_->executeQuery(
                "SELECT MAX(trade_date) as latest_date FROM daily_bar"
            );

            if (!latestResult.isEmpty()) {
                checkDate = latestResult.getRow(0).getString("latest_date");
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "[FIM] updateInstanceAvailability daily_bar query EXCEPTION: %s\n",
                    e.what());
            fflush(stderr);
        } catch (...) {
            fprintf(stderr, "[FIM] updateInstanceAvailability daily_bar query UNKNOWN EXCEPTION\n");
            fflush(stderr);
        }
    }

    if (checkDate.empty()) {
        info.isAvailable = false;
        info.dataStatus.message = "无法确定检查日期";
        return;
    }

    // 使用已解析的 config 直接检查，避免 checkFactorData 内部重复查询 factor_instance 表
    info.dataStatus = dataChecker_->checkFactorData(
        info.config,
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
            "fi.full_config::text AS full_config, fi.status, fi.factor_id, f.major_category "
            "FROM factor_instance fi "
            "LEFT JOIN factors f ON fi.factor_id = f.factor_id "
            "WHERE fi.status = 'ACTIVE' "
            "ORDER BY fi.created_at DESC"
        );


        for (size_t i = 0; i < result.rowCount(); i++) {
            auto row = result.getRow(i);

            FactorInstanceInfo info;
            info.instanceId = row.getString("instance_id");
            info.instanceName = row.getString("instance_name");
            info.description = row.getString("description");
            info.config = foundation::json::JsonFacade::parse(row.getString("full_config"));
            info.factorType = config::factorTypeFromConfig(info.config);

            updateInstanceAvailability(info);

            instances.push_back(info);
        }

    } catch (const std::exception& e) {
        fprintf(stderr, "[FIM] loadAllInstancesFromDB EXCEPTION: %s\n", e.what());
        fflush(stderr);
    } catch (...) {
        fprintf(stderr, "[FIM] loadAllInstancesFromDB UNKNOWN EXCEPTION\n");
        fflush(stderr);
    }

    return instances;
}

} // namespace factor
