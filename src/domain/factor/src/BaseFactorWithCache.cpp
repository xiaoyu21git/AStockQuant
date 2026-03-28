#include "domain/factor/include/BaseFactorWithCache.h"
#include "infrastructure/include/database/DatabaseConnection.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace factor {

BaseFactorWithCache::BaseFactorWithCache() 
    : instanceId_(foundation::utils::Uuid::generate_v4()) {
}

void BaseFactorWithCache::setCacheManager(std::shared_ptr<FactorCacheManager> cacheManager) {
    cacheManager_ = cacheManager;
}

void BaseFactorWithCache::initializeFromDatabase(const std::string& instanceId) {
    loadConfigFromDB(instanceId);
}

CalculationResult BaseFactorWithCache::calculate(const CalculationContext& context) {
    // 检查缓存是否可用
    if (cacheManager_ && cacheManager_->isCacheAvailable()) {
        // 尝试从缓存获取
        foundation::json::JsonFacade cachedResult;
        if (cacheManager_->getFactorResult(instanceId_.to_string(), 
                                          context.date, 
                                          cachedResult)) {
            auto result = CalculationResult::fromJson(cachedResult);
            
            // 检查缓存结果是否有效
            if (!result.isEmpty() && result.dataStatus.isValid()) {
                result.metadata.set("from_cache", true);
                return result;
            }
        }
    }
    
    // 缓存未命中或不可用，执行实际计算
    auto result = calculateWithoutCache(context);
    
    // 如果计算成功，缓存结果
    if (cacheManager_ && cacheManager_->isCacheAvailable() && 
        !result.isEmpty() && result.dataStatus.isValid()) {
        
        cacheManager_->setFactorResult(instanceId_.to_string(), 
                                      context.date, 
                                      result.toJson());
        result.metadata.set("from_cache", false);
    }
    
    return result;
}

std::vector<CalculationResult> BaseFactorWithCache::calculateBatch(
    const std::vector<CalculationContext>& contexts) {
    
    std::vector<CalculationResult> results;
    results.reserve(contexts.size());
    
    if (cacheManager_ && cacheManager_->isCacheAvailable()) {
        // 批量获取缓存
        std::vector<std::string> dates;
        for (const auto& context : contexts) {
            dates.push_back(context.date);
        }
        
        auto cachedResults = cacheManager_->getBatchFactorResults(
            instanceId_.to_string(), dates
        );
        
        // 处理每个上下文
        for (size_t i = 0; i < contexts.size(); i++) {
            const auto& context = contexts[i];
            const auto& date = dates[i];
            
            auto it = cachedResults.find(date);
            if (it != cachedResults.end()) {
                // 缓存命中
                auto result = CalculationResult::fromJson(it->second);
                result.metadata.set("from_cache", true);
                results.push_back(result);
            } else {
                // 缓存未命中，计算并缓存
                auto result = calculateWithoutCache(context);
                if (!result.isEmpty() && result.dataStatus.isValid()) {
                    cacheManager_->setFactorResult(
                        instanceId_.to_string(), 
                        date, 
                        result.toJson()
                    );
                    result.metadata.set("from_cache", false);
                }
                results.push_back(result);
            }
        }
    } else {
        // 无缓存支持，直接计算
        for (const auto& context : contexts) {
            results.push_back(calculateWithoutCache(context));
        }
    }
    
    return results;
}

DataStatus BaseFactorWithCache::checkDataAvailability(const std::string& date) const {
    if (!dataChecker_) {
        DataStatus status;
        status.availability = DataAvailability::UNAVAILABLE;
        status.message = "数据检查器未初始化";
        return status;
    }
    
    // 检查所有必需字段
    DataStatus overallStatus;
    overallStatus.availability = DataAvailability::AVAILABLE;
    overallStatus.coverage = 1.0;
    
    for (const auto& field : dataRequirements_.requiredFields) {
        // 简化：只检查close字段
        if (field == "close" || field == "adj_factor") {
            auto status = dataChecker_->checkPriceData(date);
            if (!status.isValid()) {
                overallStatus.availability = DataAvailability::UNAVAILABLE;
                overallStatus.message = status.message;
                overallStatus.missingFields.insert(
                    overallStatus.missingFields.end(),
                    status.missingFields.begin(),
                    status.missingFields.end()
                );
                break;
            }
        } else if (field == "pe_ratio" || field == "pb_ratio" || field == "market_cap") {
            auto status = dataChecker_->checkValuationData(date);
            if (!status.isValid()) {
                overallStatus.availability = DataAvailability::UNAVAILABLE;
                overallStatus.message = status.message;
                overallStatus.missingFields.insert(
                    overallStatus.missingFields.end(),
                    status.missingFields.begin(),
                    status.missingFields.end()
                );
                break;
            }
        }
    }
    
    return overallStatus;
}

foundation::json::JsonFacade BaseFactorWithCache::toJson() const {
    auto json = foundation::json::JsonFacade::createObject();
    
    json.set("instance_id", instanceId_.to_string());
    json.set("name", name_);
    json.set("description", description_);
    json.set("factor_type", factorType_);
    json.set("data_requirements", dataRequirements_.toJson());
    json.set("boundary_rules", boundaryRules_.toJson());
    
    return json;
}

void BaseFactorWithCache::fromJson(const foundation::json::JsonFacade& json) {
    if (json.has("instance_id")) {
        instanceId_ = foundation::utils::Uuid::from_string(json.get("instance_id").asString());
    }
    
    if (json.has("name")) {
        name_ = json.get("name").asString();
    }
    
    if (json.has("description")) {
        description_ = json.get("description").asString();
    }
    
    if (json.has("factor_type")) {
        factorType_ = json.get("factor_type").asString();
    }
    
    if (json.has("data_requirements")) {
        auto dataReq = json.get("data_requirements");
        if (dataReq.has("required")) {
            auto required = dataReq.get("required");
            for (size_t i = 0; i < required.size(); i++) {
                dataRequirements_.requiredFields.push_back(required.at(i).asString());
            }
        }
        
        if (dataReq.has("optional")) {
            auto optional = dataReq.get("optional");
            for (size_t i = 0; i < optional.size(); i++) {
                dataRequirements_.optionalFields.push_back(optional.at(i).asString());
            }
        }
        
        if (dataReq.has("alternative")) {
            auto alternative = dataReq.get("alternative");
            for (size_t i = 0; i < alternative.size(); i++) {
                dataRequirements_.alternativeFields.push_back(alternative.at(i).asString());
            }
        }
    }
    
    if (json.has("boundary_rules")) {
        auto rules = json.get("boundary_rules");
        if (rules.has("min_data_points")) {
            boundaryRules_.minDataPoints = rules.get("min_data_points").asInt();
        }
        
        if (rules.has("handle_new_stock")) {
            boundaryRules_.handleNewStock = rules.get("handle_new_stock").asString();
        }
        
        if (rules.has("handle_suspended")) {
            boundaryRules_.handleSuspended = rules.get("handle_suspended").asString();
        }
        
        if (rules.has("handle_delisted")) {
            boundaryRules_.handleDelisted = rules.get("handle_delisted").asString();
        }
        
        if (rules.has("handle_outliers")) {
            boundaryRules_.handleOutliers = rules.get("handle_outliers").asString();
        }
    }
}

std::shared_ptr<BaseFactorWithCache> BaseFactorWithCache::createFromDatabase(
    const std::string& instanceId,
    std::shared_ptr<DatabaseConnection> db,
    std::shared_ptr<DataAvailabilityCheckerWithCache> dataChecker,
    std::shared_ptr<FactorCacheManager> cacheManager) {
    
    // 查询因子类型
    auto result = db->executeQuery(
        "SELECT fi.instance_name, fi.description, fi.full_config, "
        "f.major_category as factor_type "
        "FROM factor_instance fi "
        "JOIN factors f ON fi.factor_id = f.factor_id "
        "WHERE fi.instance_id = ?",
        {instanceId}
    );
    
    if (result.empty()) {
        throw std::runtime_error("因子实例不存在: " + instanceId);
    }
    
    std::string factorType = result.getString("factor_type");
    std::shared_ptr<BaseFactorWithCache> factor;
    
    // 根据因子类型创建具体实例
    // 注意：这里需要包含具体的因子头文件
    // 暂时返回nullptr，实际实现时需要包含具体因子类
    
    return factor;
}

std::unordered_map<std::string, double> BaseFactorWithCache::applyBoundaryRules(
    const std::unordered_map<std::string, double>& rawValues,
    const CalculationContext& context) {
    
    // 简化实现：直接返回原始值
    // 实际实现需要根据boundaryRules_处理新股、停牌等
    return rawValues;
}

std::unordered_map<std::string, double> BaseFactorWithCache::handleOutliers(
    const std::unordered_map<std::string, double>& values) {
    
    if (values.empty() || boundaryRules_.handleOutliers == "keep") {
        return values;
    }
    
    if (boundaryRules_.handleOutliers == "exclude") {
        // 排除异常值：这里简化处理，实际需要计算统计量
        return values;
    }
    
    // winsorize处理
    if (boundaryRules_.handleOutliers == "winsorize_3sigma") {
        // 计算均值和标准差
        std::vector<double> valueList;
        for (const auto& [symbol, value] : values) {
            valueList.push_back(value);
        }
        
        double sum = std::accumulate(valueList.begin(), valueList.end(), 0.0);
        double mean = sum / valueList.size();
        
        double sq_sum = std::inner_product(valueList.begin(), valueList.end(), 
                                          valueList.begin(), 0.0);
        double stdev = std::sqrt(sq_sum / valueList.size() - mean * mean);
        
        double lower = mean - 3 * stdev;
        double upper = mean + 3 * stdev;
        
        std::unordered_map<std::string, double> winsorized;
        for (const auto& [symbol, value] : values) {
            double newValue = value;
            if (value < lower) newValue = lower;
            if (value > upper) newValue = upper;
            winsorized[symbol] = newValue;
        }
        
        return winsorized;
    }
    
    return values;
}

void BaseFactorWithCache::loadConfig(const foundation::json::JsonFacade& config) {
    // 解析配置
    if (config.has("data_requirements")) {
        auto dataReq = config.get("data_requirements");
        if (dataReq.has("required")) {
            auto required = dataReq.get("required");
            for (size_t i = 0; i < required.size(); i++) {
                dataRequirements_.requiredFields.push_back(required.at(i).asString());
            }
        }
    }
    
    if (config.has("boundary_rules")) {
        auto rules = config.get("boundary_rules");
        if (rules.has("min_data_points")) {
            boundaryRules_.minDataPoints = rules.get("min_data_points").asInt();
        }
    }
}

void BaseFactorWithCache::loadConfigFromDB(const std::string& instanceId) {
    if (!db_) {
        throw std::runtime_error("数据库连接未初始化");
    }
    
    auto result = db_->executeQuery(
        "SELECT instance_name, description, full_config FROM factor_instance WHERE instance_id = ?",
        {instanceId}
    );
    
    if (result.empty()) {
        throw std::runtime_error("因子实例不存在: " + instanceId);
    }
    
    name_ = result.getString("instance_name");
    description_ = result.getString("description");
    instanceId_ = foundation::utils::Uuid::from_string(instanceId);
    
    // 解析配置
    auto config = foundation::json::JsonFacade::parse(result.getString("full_config"));
    loadConfig(config);
}

} // namespace factor