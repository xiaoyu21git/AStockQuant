#pragma once

#include "factor_enums.h"
#include "foundation/json/json_facade.h"

namespace factor::config {

bool hasFactorType(const foundation::json::JsonFacade& config);
FactorType factorTypeFromConfig(const foundation::json::JsonFacade& config);
FactorType requiredFactorTypeFromConfig(const foundation::json::JsonFacade& config);
void setFactorType(foundation::json::JsonFacade& config, FactorType factorType);

void setSerializedInstanceId(foundation::json::JsonFacade& config, const std::string& instanceId);
bool hasSerializedInstanceId(const foundation::json::JsonFacade& config);
std::string requiredSerializedInstanceId(const foundation::json::JsonFacade& config);
void setSerializedInstanceName(foundation::json::JsonFacade& config, const std::string& instanceName);
bool hasSerializedInstanceName(const foundation::json::JsonFacade& config);
std::string requiredSerializedInstanceName(const foundation::json::JsonFacade& config);
void setSerializedFactorName(foundation::json::JsonFacade& config, const std::string& name);
bool hasSerializedFactorName(const foundation::json::JsonFacade& config);
std::string requiredSerializedFactorName(const foundation::json::JsonFacade& config);
void setSerializedDescription(foundation::json::JsonFacade& config, const std::string& description);
bool hasSerializedDescription(const foundation::json::JsonFacade& config);
std::string requiredSerializedDescription(const foundation::json::JsonFacade& config);
void setSerializedDataStatus(foundation::json::JsonFacade& config, const foundation::json::JsonFacade& dataStatus);
void setSerializedAvailability(foundation::json::JsonFacade& config, bool isAvailable);
void setSerializedConfig(foundation::json::JsonFacade& config, const foundation::json::JsonFacade& nestedConfig);
bool hasCalculationConfig(const foundation::json::JsonFacade& config);
foundation::json::JsonFacade calculationConfig(const foundation::json::JsonFacade& config);
void setDataRequirementsConfig(foundation::json::JsonFacade& config, const foundation::json::JsonFacade& dataRequirements);
bool hasDataRequirementsConfig(const foundation::json::JsonFacade& config);
foundation::json::JsonFacade dataRequirementsConfig(const foundation::json::JsonFacade& config);
void setBoundaryRulesConfig(foundation::json::JsonFacade& config, const foundation::json::JsonFacade& boundaryRules);
bool hasBoundaryRulesConfig(const foundation::json::JsonFacade& config);
foundation::json::JsonFacade boundaryRulesConfig(const foundation::json::JsonFacade& config);

} // namespace factor::config