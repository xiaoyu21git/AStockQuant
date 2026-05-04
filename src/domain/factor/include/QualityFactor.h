#pragma once

#include "BaseFactor.h"

namespace factor {

class QualityFactorTestAccess;

class QualityFactor : public BaseFactor {
public:
    struct Params {
        std::string metric = "roe";
        std::string timeframe = "quarterly";
        double qualityThreshold = 0.1;

        void fromJson(const foundation::json::JsonFacade& json) {
            if (json.has("metric")) metric = json.get("metric").asString();
            if (metric.empty() && json.has("qualityMetric")) metric = json.get("qualityMetric").asString();
            if (json.has("timeframe")) timeframe = json.get("timeframe").asString();
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