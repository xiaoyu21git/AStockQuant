#pragma once

#include "BaseFactor.h"

#include <QString>

namespace factor {

class SizeFactor : public BaseFactor {
public:
    struct Params {
        std::string sizeMetric = "market_cap";
        bool logTransform = true;
        int lookbackPeriod = 252;
        bool laggedEnabled = false;
        std::string frequency = "daily";
        std::string standardization = "none";
        bool neutralizationEnabled = false;

        void fromJson(const foundation::json::JsonFacade& json) {
            if (json.has("sizeMetric")) {
                sizeMetric = json.get("sizeMetric").asString();
            }
            if (json.has("logTransform")) {
                logTransform = json.get("logTransform").asBool();
            }
            if (json.has("lookbackPeriod")) {
                lookbackPeriod = json.get("lookbackPeriod").asInt();
            }
            if (json.has("laggedEnabled")) {
                laggedEnabled = json.get("laggedEnabled").asBool();
            }
            if (json.has("frequency")) {
                frequency = json.get("frequency").asString();
            }
            if (json.has("standardization")) {
                standardization = json.get("standardization").asString();
            }
            if (json.has("neutralizationEnabled")) {
                neutralizationEnabled = json.get("neutralizationEnabled").asBool();
            }
        }
    };

    SizeFactor();
    ~SizeFactor() override = default;

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<SizeFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    friend class SizeFactorTestAccess;

private:
    Params params_;

    QString selectedColumn() const;
    double scoreFromRawValue(double rawValue) const;
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor