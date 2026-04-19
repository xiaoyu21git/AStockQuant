#pragma once

#include "BaseFactor.h"

namespace astock {
namespace database {
class QtMySQLDatabase;
}
}

namespace factor {

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
            if (json.has("quality_threshold")) qualityThreshold = json.get("quality_threshold").asDouble();
            if (json.has("qualityThreshold")) qualityThreshold = json.get("qualityThreshold").asDouble();
        }
    };

    QualityFactor();
    ~QualityFactor() override = default;

    void initializeFromDatabase(const std::string& instanceId) override;
    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<QualityFactor> create(
        const std::string& instanceId,
        std::shared_ptr<astock::database::QtMySQLDatabase> db,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor