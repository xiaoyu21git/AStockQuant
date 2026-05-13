#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/factor_enums.h"
#include "domain/factor/include/LowVolFactor.h"
#include "domain/factor/include/MomentumFactor.h"
#include "domain/factor/include/QualityFactor.h"
#include "domain/factor/include/SizeFactor.h"
#include "domain/factor/include/ConfigurableFactor.h"
#include "domain/factor/include/ValueFactor.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QVariant>
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

bool hasMeaningfulVariantValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return false;
    }

    if (value.typeId() == QMetaType::QString) {
        return !value.toString().trimmed().isEmpty();
    }

    if (value.typeId() == QMetaType::QVariantList) {
        return !value.toList().isEmpty();
    }

    if (value.typeId() == QMetaType::QVariantMap) {
        return !value.toMap().isEmpty();
    }

    return true;
}

QVariant canonicalizeAliasVariant(const QVariant& value);

QVariantMap canonicalizeParameterAliases(const QVariantMap& rawParameters)
{
    static const QHash<QString, QString> aliasToCanonical = {
        {QStringLiteral("transactionCost"), QStringLiteral("transactionCost")},
        {QStringLiteral("slippageRate"), QStringLiteral("slippageRate")},
        {QStringLiteral("riskFreeRate"), QStringLiteral("riskFreeRate")},
        {QStringLiteral("benchmarkSymbol"), QStringLiteral("benchmarkSymbol")},
        {QStringLiteral("window"), QStringLiteral("window")},
        {QStringLiteral("lookbackPeriod"), QStringLiteral("lookbackPeriod")}
    };

    QVariantMap canonicalized;
    for (auto it = rawParameters.begin(); it != rawParameters.end(); ++it) {
        const QString canonicalKey = aliasToCanonical.value(it.key(), it.key());
        const QVariant currentValue = canonicalizeAliasVariant(it.value());

        if (!canonicalized.contains(canonicalKey)) {
            canonicalized.insert(canonicalKey, currentValue);
            continue;
        }

        const QVariant existingValue = canonicalized.value(canonicalKey);
        if (!hasMeaningfulVariantValue(existingValue) && hasMeaningfulVariantValue(currentValue)) {
            canonicalized.insert(canonicalKey, currentValue);
        }
    }

    return canonicalized;
}

QVariant canonicalizeAliasVariant(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return value;
    }

    if (value.typeId() == QMetaType::QVariantMap) {
        return canonicalizeParameterAliases(value.toMap());
    }

    if (value.typeId() == QMetaType::QVariantList) {
        QVariantList normalized;
        const QVariantList list = value.toList();
        normalized.reserve(list.size());
        for (const QVariant& item : list) {
            normalized.append(canonicalizeAliasVariant(item));
        }
        return normalized;
    }

    return value;
}

foundation::json::JsonFacade canonicalizeFullConfigAliases(const foundation::json::JsonFacade& rawConfig)
{
    const QByteArray jsonBytes = QByteArray::fromStdString(rawConfig.toString());
    if (jsonBytes.trimmed().isEmpty()) {
        return rawConfig;
    }

    const QJsonDocument parsed = QJsonDocument::fromJson(jsonBytes);
    if (!parsed.isObject()) {
        return rawConfig;
    }

    QVariantMap root = parsed.object().toVariantMap();

    const QVariantMap normalizedRoot = canonicalizeParameterAliases(root);
    root = normalizedRoot;

    if (root.contains(QStringLiteral("parameters")) && root.value(QStringLiteral("parameters")).canConvert<QVariantMap>()) {
        root.insert(
            QStringLiteral("parameters"),
            canonicalizeParameterAliases(root.value(QStringLiteral("parameters")).toMap())
        );
    }

    QVariantMap calculation = root.value(QStringLiteral("calculation")).toMap();
    if (!calculation.isEmpty()) {
        calculation = canonicalizeParameterAliases(calculation);

        if (calculation.contains(QStringLiteral("params")) && calculation.value(QStringLiteral("params")).canConvert<QVariantMap>()) {
            calculation.insert(
                QStringLiteral("params"),
                canonicalizeParameterAliases(calculation.value(QStringLiteral("params")).toMap())
            );
        }

        root.insert(QStringLiteral("calculation"), calculation);
    }

    const QByteArray normalizedJson = QJsonDocument::fromVariant(root).toJson(QJsonDocument::Compact);
    return foundation::json::JsonFacade::parse(normalizedJson.toStdString());
}

FactorType parseFactorTypeText(const QString& rawType)
{
    const QString normalized = rawType.trimmed().toLower();
    if (normalized.isEmpty()) {
        return FactorType::UNKNOWN;
    }

    if (normalized == QStringLiteral("value") ) {
        return FactorType::VALUE;
    }
    if (normalized == QStringLiteral("momentum") ) {
        return FactorType::MOMENTUM;
    }
    if (normalized == QStringLiteral("size") ) {
        return FactorType::SIZE;
    }
    if (normalized == QStringLiteral("quality") ) {
        return FactorType::QUALITY;
    }
    if (normalized == QStringLiteral("growth") ) {
        return FactorType::GROWTH;
    }
    if (normalized == QStringLiteral("dividend") ) {
        return FactorType::DIVIDEND;
    }
    if (normalized == QStringLiteral("technical") ) {
        return FactorType::TECHNICAL;
    }
    if (normalized == QStringLiteral("liquidity") ) {
        return FactorType::LIQUIDITY;
    }
    if (normalized == QStringLiteral("macro") ) {
        return FactorType::MACRO;
    }
    if (normalized == QStringLiteral("industry") ) {
        return FactorType::INDUSTRY;
    }
    if (normalized == QStringLiteral("sentiment") ) {
        return FactorType::SENTIMENT;
    }
    if (normalized == QStringLiteral("custom") ) {
        return FactorType::CUSTOM;
    }
    if (normalized == QStringLiteral("low_volatility") ) {
        return FactorType::LOW_VOLATILITY;
    }
    return FactorType::UNKNOWN;
}

FactorType resolveFactorType(const foundation::json::JsonFacade& config, const QString& fallbackType)
{
    if (config.has("factorType")) {
        const auto factorTypeValue = config.get("factorType");
        if (factorTypeValue.isString()) {
            const FactorType type = parseFactorTypeText(QString::fromStdString(factorTypeValue.asString()));
            if (type != FactorType::UNKNOWN) {
                return type;
            }
        } else {
            const FactorType type = factorTypeFromIndex(factorTypeValue.asInt());
            if (type != FactorType::UNKNOWN) {
                return type;
            }
        }
    }

    {
        const FactorType type = parseFactorTypeText(fallbackType);
        if (type != FactorType::UNKNOWN) {
            return type;
        }
    }

    if (config.has("calculation")) {
        auto calculation = config.get("calculation");
        if (calculation.isObject() && calculation.has("type")) {
            const auto calculationType = calculation.get("type");
            if (calculationType.isString()) {
                const FactorType type = parseFactorTypeText(QString::fromStdString(calculationType.asString()));
                if (type != FactorType::UNKNOWN) {
                    return type;
                }
            } else {
                const FactorType type = factorTypeFromIndex(calculationType.asInt());
                if (type != FactorType::UNKNOWN) {
                    return type;
                }
            }
        }
    }

    return FactorType::UNKNOWN;
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
    const FactorType factorType = info.factorType;

    switch (factorType) {
    case FactorType::MOMENTUM:
        factor = createMomentumFactor(info);
        break;
    case FactorType::VALUE:
        factor = createValueFactor(info);
        break;
    case FactorType::QUALITY:
        factor = createQualityFactor(info);
        break;
    case FactorType::SIZE:
        factor = createSizeFactor(info);
        break;
    case FactorType::LOW_VOLATILITY:
        factor = createLowVolFactor(info);
        break;
    case FactorType::GROWTH:
    case FactorType::DIVIDEND:
    case FactorType::TECHNICAL:
    case FactorType::LIQUIDITY:
    case FactorType::MACRO:
    case FactorType::INDUSTRY:
    case FactorType::SENTIMENT:
    case FactorType::CUSTOM:
        factor = createConfigurableFactor(info);
        break;
    default:
        break;
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
        const foundation::json::JsonFacade normalizedConfig = canonicalizeFullConfigAliases(newConfig);
        const int affectedRows = db_->executeUpdate(
            "UPDATE factor_instance SET full_config = ?, updated_at = CURRENT_TIMESTAMP "
            "WHERE instance_id = ?",
            makePositionalParams({QString::fromStdString(normalizedConfig.toString()), QString::fromStdString(instanceId)})
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
        info,
        dataChecker_
    );
}

std::shared_ptr<BaseFactor> FactorInstanceManager::createValueFactor(
    const FactorInstanceInfo& info) {
    return ValueFactor::create(info, dataChecker_);
}

std::shared_ptr<BaseFactor> FactorInstanceManager::createQualityFactor(
    const FactorInstanceInfo& info) {
    return QualityFactor::create(info, dataChecker_);
}

std::shared_ptr<BaseFactor> FactorInstanceManager::createSizeFactor(
    const FactorInstanceInfo& info) {
    return SizeFactor::create(info, dataChecker_);
}

std::shared_ptr<BaseFactor> FactorInstanceManager::createLowVolFactor(
    const FactorInstanceInfo& info) {
    return LowVolFactor::create(info, dataChecker_);
}

std::shared_ptr<BaseFactor> FactorInstanceManager::createConfigurableFactor(
    const FactorInstanceInfo& info) {
    return ConfigurableFactor::create(info, dataChecker_);
}

FactorInstanceManager::ParsedConfig FactorInstanceManager::parseConfig(
    const foundation::json::JsonFacade& config) {
    
    ParsedConfig parsed;
    
    if (config.has("dataRequirements")) {
        auto dataReq = config.get("dataRequirements");
        
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
    
    if (config.has("boundaryRules")) {
        auto rules = config.get("boundaryRules");
        
        if (rules.has("minDataPoints")) {
            parsed.boundaryRules.minDataPoints = rules.get("minDataPoints").asInt();
        }
        
        if (rules.has("handleNewStock")) {
            parsed.boundaryRules.handleNewStock = rules.get("handleNewStock").asString();
        }
        
        if (rules.has("handleSuspended")) {
            parsed.boundaryRules.handleSuspended = rules.get("handleSuspended").asString();
        }
        
        if (rules.has("handleDelisted")) {
            parsed.boundaryRules.handleDelisted = rules.get("handleDelisted").asString();
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