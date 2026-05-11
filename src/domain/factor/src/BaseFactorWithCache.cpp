#include "domain/factor/include/BaseFactorWithCache.h"
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
                result.metadata.set("fromCache", true);
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
        result.metadata.set("fromCache", false);
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
                result.metadata.set("fromCache", true);
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
                    result.metadata.set("fromCache", false);
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

    return dataChecker_->checkFactorData(instanceId_.to_string(), date, date);
}

foundation::json::JsonFacade BaseFactorWithCache::toJson() const {
    auto json = foundation::json::JsonFacade::createObject();
    
    json.set("instance_id", instanceId_.to_string());
    json.set("name", name_);
    json.set("description", description_);
    json.set("factorType", factorType_);
    json.set("dataRequirements", dataRequirements_.toJson());
    json.set("boundaryRules", boundaryRules_.toJson());
    
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
    
    if (json.has("factorType")) {
        factorType_ = json.get("factorType").asString();
    }
    
    if (json.has("dataRequirements")) {
        auto dataReq = json.get("dataRequirements");
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
    
    if (json.has("boundaryRules")) {
        auto rules = json.get("boundaryRules");
        if (rules.has("minDataPoints")) {
            boundaryRules_.minDataPoints = rules.get("minDataPoints").asInt();
        }
        
        if (rules.has("handleNewStock")) {
            boundaryRules_.handleNewStock = rules.get("handleNewStock").asString();
        }
        
        if (rules.has("handleSuspended")) {
            boundaryRules_.handleSuspended = rules.get("handleSuspended").asString();
        }
        
        if (rules.has("handleDelisted")) {
            boundaryRules_.handleDelisted = rules.get("handleDelisted").asString();
        }
        
        if (rules.has("handleOutliers")) {
            boundaryRules_.handleOutliers = rules.get("handleOutliers").asString();
        }
    }
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
    if (config.has("dataRequirements")) {
        auto dataReq = config.get("dataRequirements");
        if (dataReq.has("required")) {
            auto required = dataReq.get("required");
            for (size_t i = 0; i < required.size(); i++) {
                dataRequirements_.requiredFields.push_back(required.at(i).asString());
            }
        }
    }
    
    if (config.has("boundaryRules")) {
        auto rules = config.get("boundaryRules");
        if (rules.has("minDataPoints")) {
            boundaryRules_.minDataPoints = rules.get("minDataPoints").asInt();
        }
    }
}

} // namespace factor