#include "domain/factor/include/FactorConfigAccess.h"

#include "JsonFacadeHelpers.h"

#include <stdexcept>

namespace factor::config {

namespace {

constexpr const char* kFactorTypeKey = "factorType";
constexpr const char* kInstanceIdKey = "instance_id";
constexpr const char* kInstanceNameKey = "instance_name";
constexpr const char* kNameKey = "name";
constexpr const char* kDescriptionKey = "description";
constexpr const char* kDataStatusKey = "data_status";
constexpr const char* kAvailabilityKey = "is_available";
constexpr const char* kConfigKey = "config";
constexpr const char* kCalculationKey = "calculation";
constexpr const char* kDataRequirementsKey = "dataRequirements";
constexpr const char* kBoundaryRulesKey = "boundaryRules";

std::string requiredStringField(const foundation::json::JsonFacade& config,
                                const char* key,
                                const char* errorText)
{
    const auto value = config.get(key);
    if (!value.isString()) {
        throw std::runtime_error(errorText);
    }
    return value.asString();
}

} // namespace

bool hasFactorType(const foundation::json::JsonFacade& config)
{
    return config.has(kFactorTypeKey);
}

FactorType factorTypeFromConfig(const foundation::json::JsonFacade& config)
{
    if (!hasFactorType(config)) {
        return FactorType::UNKNOWN;
    }

    const auto factorTypeValue = config.get(kFactorTypeKey);
    if (!factorTypeValue.isNumber()) {
        return FactorType::UNKNOWN;
    }

    const FactorType type = factorTypeFromIndex(factorTypeValue.asInt());
    return type == FactorType::UNKNOWN ? FactorType::UNKNOWN : type;
}

FactorType requiredFactorTypeFromConfig(const foundation::json::JsonFacade& config)
{
    if (!hasFactorType(config)) {
        return FactorType::UNKNOWN;
    }

    const auto factorTypeValue = config.get(kFactorTypeKey);
    if (!factorTypeValue.isNumber()) {
        throw std::runtime_error("factorType 不是数字字段");
    }

    return factorTypeFromIndex(factorTypeValue.asInt());
}

void setFactorType(foundation::json::JsonFacade& config, FactorType factorType)
{
    config.set(kFactorTypeKey, json_helper::toJsonValue(factorTypeIndex(factorType)));
}

void setSerializedInstanceId(foundation::json::JsonFacade& config, const std::string& instanceId)
{
    config.set(kInstanceIdKey, json_helper::toJsonValue(instanceId));
}

bool hasSerializedInstanceId(const foundation::json::JsonFacade& config)
{
    return config.has(kInstanceIdKey);
}

std::string requiredSerializedInstanceId(const foundation::json::JsonFacade& config)
{
    return requiredStringField(config, kInstanceIdKey, "instance_id 不是字符串字段");
}

void setSerializedInstanceName(foundation::json::JsonFacade& config, const std::string& instanceName)
{
    config.set(kInstanceNameKey, json_helper::toJsonValue(instanceName));
}

bool hasSerializedInstanceName(const foundation::json::JsonFacade& config)
{
    return config.has(kInstanceNameKey);
}

std::string requiredSerializedInstanceName(const foundation::json::JsonFacade& config)
{
    return requiredStringField(config, kInstanceNameKey, "instance_name 不是字符串字段");
}

void setSerializedFactorName(foundation::json::JsonFacade& config, const std::string& name)
{
    config.set(kNameKey, json_helper::toJsonValue(name));
}

bool hasSerializedFactorName(const foundation::json::JsonFacade& config)
{
    return config.has(kNameKey);
}

std::string requiredSerializedFactorName(const foundation::json::JsonFacade& config)
{
    return requiredStringField(config, kNameKey, "name 不是字符串字段");
}

void setSerializedDescription(foundation::json::JsonFacade& config, const std::string& description)
{
    config.set(kDescriptionKey, json_helper::toJsonValue(description));
}

bool hasSerializedDescription(const foundation::json::JsonFacade& config)
{
    return config.has(kDescriptionKey);
}

std::string requiredSerializedDescription(const foundation::json::JsonFacade& config)
{
    return requiredStringField(config, kDescriptionKey, "description 不是字符串字段");
}

void setSerializedDataStatus(foundation::json::JsonFacade& config, const foundation::json::JsonFacade& dataStatus)
{
    config.set(kDataStatusKey, dataStatus);
}

void setSerializedAvailability(foundation::json::JsonFacade& config, bool isAvailable)
{
    config.set(kAvailabilityKey, json_helper::toJsonValue(isAvailable));
}

void setSerializedConfig(foundation::json::JsonFacade& config, const foundation::json::JsonFacade& nestedConfig)
{
    config.set(kConfigKey, nestedConfig);
}

bool hasCalculationConfig(const foundation::json::JsonFacade& config)
{
    return config.has(kCalculationKey);
}

foundation::json::JsonFacade calculationConfig(const foundation::json::JsonFacade& config)
{
    return config.get(kCalculationKey);
}

void setDataRequirementsConfig(foundation::json::JsonFacade& config, const foundation::json::JsonFacade& dataRequirements)
{
    config.set(kDataRequirementsKey, dataRequirements);
}

bool hasDataRequirementsConfig(const foundation::json::JsonFacade& config)
{
    return config.has(kDataRequirementsKey);
}

foundation::json::JsonFacade dataRequirementsConfig(const foundation::json::JsonFacade& config)
{
    return config.get(kDataRequirementsKey);
}

void setBoundaryRulesConfig(foundation::json::JsonFacade& config, const foundation::json::JsonFacade& boundaryRules)
{
    config.set(kBoundaryRulesKey, boundaryRules);
}

bool hasBoundaryRulesConfig(const foundation::json::JsonFacade& config)
{
    return config.has(kBoundaryRulesKey);
}

foundation::json::JsonFacade boundaryRulesConfig(const foundation::json::JsonFacade& config)
{
    return config.get(kBoundaryRulesKey);
}

} // namespace factor::config