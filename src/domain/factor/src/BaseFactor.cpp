#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/LowVolFactor.h"
#include "domain/factor/include/MomentumFactor.h"
#include "domain/factor/include/QualityFactor.h"
#include "domain/factor/include/SizeFactor.h"
#include "domain/factor/include/ValueFactor.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"
#include <algorithm>
#include <cmath>
#include <numeric>

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
    if (normalized == QString::fromUtf8("规模因子") || normalized == "size") {
        return "规模因子";
    }
    if (normalized == QString::fromUtf8("低波因子") || normalized == "low_vol" || normalized == "lowvol") {
        return "低波因子";
    }
    if (normalized == QString::fromUtf8("质量因子") || normalized == "quality") {
        return "质量因子";
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

BaseFactor::BaseFactor() 
    : instanceId_(foundation::utils::Uuid::generate_v4().to_string()) {
}

void BaseFactor::initializeFromDatabase(const std::string& instanceId) {
    loadConfigFromDB(instanceId);
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
    json.set("factor_type", json_helper::toJsonValue(factorType_));
    json.set("data_requirements", dataRequirements_.toJson());
    json.set("boundary_rules", boundaryRules_.toJson());
    
    return json;
}

void BaseFactor::fromJson(const foundation::json::JsonFacade& json) {
    if (json.has("instance_id")) {
        instanceId_ = json.get("instance_id").asString();
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

std::shared_ptr<BaseFactor> BaseFactor::createFromDatabase(
    const std::string& instanceId,
    std::shared_ptr<astock::database::QtMySQLDatabase> db,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {
    
    // 查询因子类型
    auto result = db->executeQuery(
        "SELECT fi.instance_name, fi.description, CAST(fi.full_config AS CHAR) AS full_config, f.major_category "
        "FROM factor_instance fi "
        "LEFT JOIN factors f ON fi.factor_id = f.factor_id "
        "WHERE fi.instance_id = ?",
        makePositionalParams({QString::fromStdString(instanceId)})
    );
    
    if (result.isEmpty()) {
        throw std::runtime_error("因子实例不存在: " + instanceId);
    }

    const auto& row = result.getRow(0);
    
    auto config = foundation::json::JsonFacade::parse(row.getString("full_config").toStdString());
    std::string factorType = resolveFactorType(config, row.getString("major_category"));
    std::shared_ptr<BaseFactor> factor;
    
    if (factorType == "动量因子") {
        factor = MomentumFactor::create(instanceId, db, dataChecker);
    } else if (factorType == "价值因子") {
        factor = ValueFactor::create(instanceId, db, dataChecker);
    } else if (factorType == "规模因子") {
        factor = SizeFactor::create(instanceId, db, dataChecker);
    } else if (factorType == "低波因子") {
        factor = LowVolFactor::create(instanceId, db, dataChecker);
    } else if (factorType == "质量因子") {
        factor = QualityFactor::create(instanceId, db, dataChecker);
    }
    
    return factor;
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
    if (config.has("data_requirements")) {
        auto dataReq = config.get("data_requirements");
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
    
    if (config.has("boundary_rules")) {
        auto rules = config.get("boundary_rules");
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

void BaseFactor::loadConfigFromDB(const std::string& instanceId) {
    if (!db_) {
        throw std::runtime_error("数据库连接未初始化");
    }
    
    auto result = db_->executeQuery(
        "SELECT instance_name, description, CAST(full_config AS CHAR) AS full_config FROM factor_instance WHERE instance_id = ?",
        makePositionalParams({QString::fromStdString(instanceId)})
    );
    
    if (result.isEmpty()) {
        throw std::runtime_error("因子实例不存在: " + instanceId);
    }

    const auto& row = result.getRow(0);
    
    name_ = row.getString("instance_name").toStdString();
    description_ = row.getString("description").toStdString();
    instanceId_ = instanceId;
    
    // 解析配置
    auto config = foundation::json::JsonFacade::parse(row.getString("full_config").toStdString());
    loadConfig(config);
}

} // namespace factor