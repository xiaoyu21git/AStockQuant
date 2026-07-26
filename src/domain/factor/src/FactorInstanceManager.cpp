#include "FactorInstanceManager.h"
#include "CompositeFactor.h"
#include "CustomFactor.h"
#include "EventDrivenFactor.h"
#include "DividendFactor.h"
#include "ReversalFactor.h"
#include "HighFreqFactor.h"
#include "DLFactor.h"
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
#include "foundation/log/logging.hpp"
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

std::shared_ptr<BaseFactor> createEventDrivenFactorEntry(const FactorInstanceInfo& info,
                                                          std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                          FactorInstanceManager&)
{
    return EventDrivenFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createReversalFactorEntry(const FactorInstanceInfo& info,
                                                       std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                       FactorInstanceManager&)
{
    return ReversalFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createHighFreqFactorEntry(const FactorInstanceInfo& info,
                                                       std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                       FactorInstanceManager&)
{
    return HighFreqFactor::create(info, std::move(dataChecker));
}

std::shared_ptr<BaseFactor> createDLFactorEntry(const FactorInstanceInfo& info,
                                                 std::shared_ptr<DataAvailabilityChecker> dataChecker,
                                                 FactorInstanceManager&)
{
    return DLFactor::create(info, std::move(dataChecker));
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
    {FactorType::EVENT_DRIVEN, &createEventDrivenFactorEntry},
    {FactorType::REVERSAL, &createReversalFactorEntry},
    {FactorType::HIGH_FREQ, &createHighFreqFactorEntry},
    {FactorType::DL, &createDLFactorEntry},
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

    INTERNAL_INFO_STREAM << "[FIM] ctor START";
    threadPool_ = std::make_shared<foundation::thread::ThreadPoolExecutor>(4);
    INTERNAL_INFO_STREAM << "[FIM] threadpool OK";

    // refreshCache() 移除 — 策略需要的因子由 createInstance() 按需加载+缓存
    INTERNAL_INFO_STREAM << "[FIM] ctor DONE (lazy load)";
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
        INTERNAL_ERROR_STREAM << "[FIM] createFactorFromInfo failed: " << instanceId << " type=" << static_cast<int>(factorType) << " error=" << e.what();
        return nullptr;
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[FIM] createFactorFromInfo failed: " << instanceId << " type=" << static_cast<int>(factorType) << " error=unknown";
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

    // 全量加载: 必须从 DB 拉取, infoCache_ 可能只有部分条目
    auto all = loadAllInstancesFromDB();
    for (auto& info : all) infoCache_[info.instanceId] = info;

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
        // 提取 instance_name
        std::string instanceName = newConfig.has("instance_name")
            ? newConfig.get("instance_name").asString() : instanceId;
        std::string description = newConfig.has("description")
            ? newConfig.get("description").asString() : "";

        using P = astock::database::SqlParam;
        std::string configStr = newConfig.toString();
        // 先确保 alpha.factors 存在 (FK 约束)
        db_->executeUpdate(
            "INSERT INTO alpha.factors (factor_id, factor_name, display_name) "
            "VALUES ($1, $2, $2) "
            "ON CONFLICT (factor_id) DO NOTHING",
            {P{instanceId}, P{instanceName}}
        );
        const int affectedRows = db_->executeUpdate(
            "INSERT INTO alpha.factor_instance (instance_id, factor_id, instance_name, description, full_config) "
            "VALUES ($1, $2, $3, $4, $5::jsonb) "
            "ON CONFLICT (instance_id) DO UPDATE SET "
            "  full_config = EXCLUDED.full_config, "
            "  instance_name = EXCLUDED.instance_name, "
            "  description = EXCLUDED.description, "
            "  updated_at = CURRENT_TIMESTAMP",
            {P{instanceId}, P{instanceId}, P{instanceName}, P{description}, P{configStr}}
        );

        if (affectedRows > 0) {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            instanceCache_.erase(instanceId);
            infoCache_.erase(instanceId);

            return true;
        }

    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[FIM] updateInstanceConfig EXCEPTION: id=" << instanceId << " error=" << e.what();
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[FIM] updateInstanceConfig UNKNOWN EXCEPTION: id=" << instanceId;
    }

    return false;
}

void FactorInstanceManager::refreshCache() {
    INTERNAL_INFO_STREAM << "[FIM] refreshCache START";
    try {
        std::lock_guard<std::mutex> lock(cacheMutex_);

        instanceCache_.clear();
        infoCache_.clear();

        INTERNAL_INFO_STREAM << "[FIM] refreshCache calling loadAllInstancesFromDB";
        auto allInstances = loadAllInstancesFromDB();
        INTERNAL_INFO_STREAM << "[FIM] refreshCache loaded " << allInstances.size() << " instances";
        for (auto& info : allInstances) {
            infoCache_[info.instanceId] = info;
        }
        INTERNAL_INFO_STREAM << "[FIM] refreshCache DONE";
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[FIM] refreshCache EXCEPTION: " << e.what();
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[FIM] refreshCache UNKNOWN EXCEPTION (likely Foundation logger crash)";
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
            "FROM alpha.factor_instance fi "
            "LEFT JOIN alpha.factors f ON fi.factor_id = f.factor_id "
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
        INTERNAL_ERROR_STREAM << "[FIM] loadInstanceFromDB EXCEPTION: id=" << instanceId << " error=" << e.what();
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[FIM] loadInstanceFromDB UNKNOWN EXCEPTION: id=" << instanceId;
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
                "SELECT MAX(trade_date) as latest_date FROM mkt.daily_bar"
            );

            if (!latestResult.isEmpty()) {
                checkDate = latestResult.getRow(0).getString("latest_date");
            }
        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "[FIM] updateInstanceAvailability daily_bar query EXCEPTION: " << e.what();
        } catch (...) {
            INTERNAL_ERROR_STREAM << "[FIM] updateInstanceAvailability daily_bar query UNKNOWN EXCEPTION";
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
            "FROM alpha.factor_instance fi "
            "LEFT JOIN alpha.factors f ON fi.factor_id = f.factor_id "
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
        INTERNAL_ERROR_STREAM << "[FIM] loadAllInstancesFromDB EXCEPTION: " << e.what();
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[FIM] loadAllInstancesFromDB UNKNOWN EXCEPTION";
    }

    return instances;
}

} // namespace factor
