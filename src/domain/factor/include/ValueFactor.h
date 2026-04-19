#pragma once

#include "BaseFactor.h"

#include <QString>

namespace astock {
namespace database {
class QtMySQLDatabase;
}
}

namespace factor {

class ValueFactor : public BaseFactor {
public:
    struct Params {
        std::string valuationType = "pe";
        bool usePercentile = false;
        bool industryNeutral = false;
        int lookbackPeriod = 252;
        bool laggedEnabled = true;
        std::string frequency = "daily";
        std::string standardization = "none";

        void fromJson(const foundation::json::JsonFacade& json) {
            if (json.has("valuation_type")) valuationType = json.get("valuation_type").asString();
            if (valuationType.empty() && json.has("valuationType")) valuationType = json.get("valuationType").asString();
            if (valuationType.empty() && json.has("valuationMetric")) valuationType = json.get("valuationMetric").asString();
            if (json.has("lookback_period")) lookbackPeriod = json.get("lookback_period").asInt();
            if (json.has("lookbackPeriod")) lookbackPeriod = json.get("lookbackPeriod").asInt();
            if (json.has("lagged_enabled")) laggedEnabled = json.get("lagged_enabled").asBool();
            if (json.has("laggedEnabled")) laggedEnabled = json.get("laggedEnabled").asBool();
            if (json.has("frequency")) frequency = json.get("frequency").asString();
            if (json.has("standardization")) standardization = json.get("standardization").asString();
            if (json.has("use_percentile")) usePercentile = json.get("use_percentile").asBool();
            if (json.has("usePercentile")) usePercentile = json.get("usePercentile").asBool();
            if (json.has("industry_neutral")) industryNeutral = json.get("industry_neutral").asBool();
            if (json.has("industryNeutral")) industryNeutral = json.get("industryNeutral").asBool();
            if (json.has("neutralizationEnabled")) industryNeutral = json.get("neutralizationEnabled").asBool();
        }
    };

    ValueFactor();
    ~ValueFactor() override = default;

    void initializeFromDatabase(const std::string& instanceId) override;
    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<ValueFactor> create(
        const std::string& instanceId,
        std::shared_ptr<astock::database::QtMySQLDatabase> db,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    friend class ValueFactorTestAccess;

private:
    Params params_;

    QString selectedColumn() const;
    double scoreFromRawValue(double rawValue) const;
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor