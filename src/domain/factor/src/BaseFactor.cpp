#include "domain/factor/include/BaseFactor.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace factor {

BaseFactor::BaseFactor() 
    : instanceId_(foundation::utils::Uuid::generate_v4().to_string()) {
}

std::vector<CalculationResult> BaseFactor::calculateBatch(
    const std::vector<CalculationContext>& contexts) {
    
    std::vector<CalculationResult> results;
    results.reserve(contexts.size());
    
    for (const auto& context : contexts) {
        results.push_back(calculate(context));
    }
    
    return results;
}

DataStatus BaseFactor::checkDataAvailability(const std::string& date) const {
    if (!dataChecker_) {
        DataStatus status;
        status.availability = DataAvailability::UNAVAILABLE;
        status.message = "数据检查器未初始化";
        return status;
    }

    return dataChecker_->checkFactorData(instanceId_, date, date);
}

foundation::json::JsonFacade BaseFactor::toJson() const {
    auto json = foundation::json::JsonFacade::createObject();
    
    json.set("instance_id", json_helper::toJsonValue(instanceId_));
    json.set("name", json_helper::toJsonValue(name_));
    json.set("description", json_helper::toJsonValue(description_));
    json.set("factorType", json_helper::toJsonValue(factorType_));
    json.set("dataRequirements", dataRequirements_.toJson());
    json.set("boundaryRules", boundaryRules_.toJson());
    
    return json;
}

void BaseFactor::fromJson(const foundation::json::JsonFacade& json) {
    if (json.has("instance_id")) {
        const auto value = json.get("instance_id");
        if (!value.isString()) {
            throw std::runtime_error("instance_id 不是字符串字段");
        }
        instanceId_ = value.asString();
    }
    
    if (json.has("name")) {
        const auto value = json.get("name");
        if (!value.isString()) {
            throw std::runtime_error("name 不是字符串字段");
        }
        name_ = value.asString();
    }
    
    if (json.has("description")) {
        const auto value = json.get("description");
        if (!value.isString()) {
            throw std::runtime_error("description 不是字符串字段");
        }
        description_ = value.asString();
    }
    
    if (json.has("factorType")) {
        const auto value = json.get("factorType");
        if (!value.isString()) {
            throw std::runtime_error("factorType 不是字符串字段");
        }
        factorType_ = value.asString();
    }
    
    if (json.has("dataRequirements")) {
        auto dataReq = json.get("dataRequirements");
        if (dataReq.has("required")) {
            auto required = dataReq.get("required");
            for (size_t i = 0; i < required.size(); i++) {
                const auto item = required.at(i);
                if (!item.isString()) {
                    throw std::runtime_error("dataRequirements.required 不是字符串字段");
                }
                dataRequirements_.requiredFields.push_back(item.asString());
            }
        }
        
        if (dataReq.has("optional")) {
            auto optional = dataReq.get("optional");
            for (size_t i = 0; i < optional.size(); i++) {
                const auto item = optional.at(i);
                if (!item.isString()) {
                    throw std::runtime_error("dataRequirements.optional 不是字符串字段");
                }
                dataRequirements_.optionalFields.push_back(item.asString());
            }
        }
        
        if (dataReq.has("alternative")) {
            auto alternative = dataReq.get("alternative");
            for (size_t i = 0; i < alternative.size(); i++) {
                const auto item = alternative.at(i);
                if (!item.isString()) {
                    throw std::runtime_error("dataRequirements.alternative 不是字符串字段");
                }
                dataRequirements_.alternativeFields.push_back(item.asString());
            }
        }
    }
    
    if (json.has("boundaryRules")) {
        auto rules = json.get("boundaryRules");
        if (rules.has("minDataPoints")) {
            boundaryRules_.minDataPoints = rules.get("minDataPoints").asInt();
        }
        
        if (rules.has("handleNewStock")) {
            const auto value = rules.get("handleNewStock");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleNewStock 不是字符串字段");
            }
            boundaryRules_.handleNewStock = value.asString();
        }
        
        if (rules.has("handleSuspended")) {
            const auto value = rules.get("handleSuspended");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleSuspended 不是字符串字段");
            }
            boundaryRules_.handleSuspended = value.asString();
        }
        
        if (rules.has("handleDelisted")) {
            const auto value = rules.get("handleDelisted");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleDelisted 不是字符串字段");
            }
            boundaryRules_.handleDelisted = value.asString();
        }
        
        if (rules.has("handleOutliers")) {
            const auto value = rules.get("handleOutliers");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleOutliers 不是字符串字段");
            }
            boundaryRules_.handleOutliers = value.asString();
        }
    }
}

    bool BaseFactor::isHistoricalViewRuntime(const CalculationContext& context) const {
        return static_cast<bool>(context.historicalView);
    }

    CalculationResult BaseFactor::createHistoricalViewRuntimeError(const CalculationContext& context,
                                                                  const std::string& errorMsg) const {
        CalculationResult result;
        result.calculationId = foundation::utils::Uuid::generate_v4();
        result.date = context.date;
        result.dataStatus = CalculationResult::createError(errorMsg).dataStatus;
        result.metadata.set("error", json_helper::toJsonValue(errorMsg));
        return result;
    }

std::unordered_map<std::string, double> BaseFactor::applyBoundaryRules(
    const std::unordered_map<std::string, double>& rawValues,
    const CalculationContext& context) {
    
    // 简化实现：直接返回原始值
    // 实际实现需要根据boundaryRules_处理新股、停牌等
    return rawValues;
}

std::unordered_map<std::string, double> BaseFactor::handleOutliers(
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

void BaseFactor::loadConfig(const foundation::json::JsonFacade& config) {
    dataRequirements_.requiredFields.clear();
    dataRequirements_.optionalFields.clear();
    dataRequirements_.alternativeFields.clear();

    // 解析配置
    if (config.has("dataRequirements")) {
        auto dataReq = config.get("dataRequirements");
        if (dataReq.has("required")) {
            auto required = dataReq.get("required");
            for (size_t i = 0; i < required.size(); i++) {
                const auto item = required.at(i);
                if (!item.isString()) {
                    throw std::runtime_error("dataRequirements.required 不是字符串字段");
                }
                dataRequirements_.requiredFields.push_back(item.asString());
            }
        }

        if (dataReq.has("optional")) {
            auto optional = dataReq.get("optional");
            for (size_t i = 0; i < optional.size(); i++) {
                const auto item = optional.at(i);
                if (!item.isString()) {
                    throw std::runtime_error("dataRequirements.optional 不是字符串字段");
                }
                dataRequirements_.optionalFields.push_back(item.asString());
            }
        }

        if (dataReq.has("alternative")) {
            auto alternative = dataReq.get("alternative");
            for (size_t i = 0; i < alternative.size(); i++) {
                const auto item = alternative.at(i);
                if (!item.isString()) {
                    throw std::runtime_error("dataRequirements.alternative 不是字符串字段");
                }
                dataRequirements_.alternativeFields.push_back(item.asString());
            }
        }
    }
    
    if (config.has("boundaryRules")) {
        auto rules = config.get("boundaryRules");
        if (rules.has("minDataPoints")) {
            boundaryRules_.minDataPoints = rules.get("minDataPoints").asInt();
        }

        if (rules.has("handleNewStock")) {
            const auto value = rules.get("handleNewStock");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleNewStock 不是字符串字段");
            }
            boundaryRules_.handleNewStock = value.asString();
        }

        if (rules.has("handleSuspended")) {
            const auto value = rules.get("handleSuspended");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleSuspended 不是字符串字段");
            }
            boundaryRules_.handleSuspended = value.asString();
        }

        if (rules.has("handleDelisted") || rules.has("handle_delisted")) {
            const auto value = rules.has("handleDelisted") ? rules.get("handleDelisted") : rules.get("handle_delisted");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleDelisted 不是字符串字段");
            }
            boundaryRules_.handleDelisted = value.asString();
        }

        if (rules.has("handleOutliers") || rules.has("handle_outliers")) {
            const auto value = rules.has("handleOutliers") ? rules.get("handleOutliers") : rules.get("handle_outliers");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleOutliers 不是字符串字段");
            }
            boundaryRules_.handleOutliers = value.asString();
        }
    }
}

} // namespace factor