#pragma once

#include "BaseFactor.h"

namespace factor {

class QualityFactorTestAccess;

class QualityFactor : public BaseFactor {
public:
    struct Params {
        std::string metric = "roe";
        std::string frequency = "daily";
        int lookbackPeriod = 252;
        std::string standardization = "none";
        bool laggedEnabled = false;
        bool neutralizationEnabled = false;
        double qualityThreshold = 0.1;

        void fromJson(const foundation::json::JsonFacade& json) {
            if (json.has("metric")) metric = json.get("metric").asString();
            if (json.has("frequency")) frequency = json.get("frequency").asString();
            if (json.has("lookbackPeriod")) lookbackPeriod = json.get("lookbackPeriod").asInt();
            if (json.has("standardization")) standardization = json.get("standardization").asString();
            if (json.has("laggedEnabled")) laggedEnabled = json.get("laggedEnabled").asBool();
            if (json.has("neutralizationEnabled")) neutralizationEnabled = json.get("neutralizationEnabled").asBool();
            if (json.has("qualityThreshold")) qualityThreshold = json.get("qualityThreshold").asDouble();
        }
    };

    QualityFactor();
    ~QualityFactor() override = default;

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<QualityFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

private:
    friend class QualityFactorTestAccess;

    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor