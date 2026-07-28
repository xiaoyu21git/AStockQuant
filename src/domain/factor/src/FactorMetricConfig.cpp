#include "domain/factor/include/FactorMetricConfig.h"
#include "domain/factor/include/BaseFactor.h"
#include "foundation/json/json_facade.h"

#include <stdexcept>

namespace factor {

void CommonParams::fromJson(const foundation::json::JsonFacade& json)
{
    if (json.has("frequency")) {
        const auto& value = json.get("frequency");
        if (!value.isNumber()) throw std::runtime_error("frequency 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(DataFrequency::Minute) || v > static_cast<int>(DataFrequency::Yearly))
            throw std::runtime_error("frequency 不是有效的枚举值");
        frequency = static_cast<DataFrequency>(v);
    }
    if (json.has("laggedEnabled")) lagEnabled = json.get("laggedEnabled").asBool();
    if (json.has("lagMode")) {
        const auto& value = json.get("lagMode");
        if (!value.isNumber()) throw std::runtime_error("lagMode 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(LagMode::None) || v > static_cast<int>(LagMode::Adaptive))
            throw std::runtime_error("lagMode 不是有效的枚举值");
        lagMode = static_cast<LagMode>(v);
    }
    if (json.has("lagPeriods")) lagPeriods = static_cast<uint8_t>(json.get("lagPeriods").asInt());
    if (json.has("standardization")) {
        const auto& value = json.get("standardization");
        if (!value.isNumber()) throw std::runtime_error("standardization 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(StandardizationMethod::None) || v > static_cast<int>(StandardizationMethod::Percentile))
            throw std::runtime_error("standardization 不是有效的枚举值");
        standardization = static_cast<StandardizationMethod>(v);
    }
    if (json.has("neutralizationEnabled")) neutralizationEnabled = json.get("neutralizationEnabled").asBool();
    if (json.has("neutralizationMethod")) {
        const auto& value = json.get("neutralizationMethod");
        if (!value.isNumber()) throw std::runtime_error("neutralizationMethod 不是枚举数值字段");
        const int v = value.asInt();
        if (v < static_cast<int>(NeutralizationMethod::None) || v > static_cast<int>(NeutralizationMethod::IndustryMarketCap))
            throw std::runtime_error("neutralizationMethod 不是有效的枚举值");
        neutralizationMethod = static_cast<NeutralizationMethod>(v);
    }
    if (json.has("window")) window = static_cast<uint16_t>(json.get("window").asInt());
    if (json.has("lookbackWindow")) lookbackWindow = static_cast<uint16_t>(json.get("lookbackWindow").asInt());
    if (json.has("ascending")) ascending = json.get("ascending").asBool();
}

} // namespace factor