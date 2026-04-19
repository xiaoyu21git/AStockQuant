#pragma once

#include "BaseFactor.h"

#include <QString>

namespace astock {
namespace database {
class QtMySQLDatabase;
}
}

namespace factor {

class SizeFactor : public BaseFactor {
public:
    struct Params {
        std::string sizeMetric = "market_cap";
        bool logTransform = true;
        bool usePercentile = false;
        bool industryNeutral = false;
        std::string standardization = "none";

        void fromJson(const foundation::json::JsonFacade& json) {
            if (json.has("size_metric")) {
                sizeMetric = json.get("size_metric").asString();
            } else if (json.has("sizeMetric")) {
                sizeMetric = json.get("sizeMetric").asString();
            }
            if (json.has("log_transform")) {
                logTransform = json.get("log_transform").asBool();
            } else if (json.has("logTransform")) {
                logTransform = json.get("logTransform").asBool();
            }
            if (json.has("use_percentile")) {
                usePercentile = json.get("use_percentile").asBool();
            } else if (json.has("usePercentile")) {
                usePercentile = json.get("usePercentile").asBool();
            }
            if (json.has("industry_neutral")) {
                industryNeutral = json.get("industry_neutral").asBool();
            } else if (json.has("industryNeutral")) {
                industryNeutral = json.get("industryNeutral").asBool();
            } else if (json.has("neutralizationEnabled")) {
                industryNeutral = json.get("neutralizationEnabled").asBool();
            }
            if (json.has("standardization")) {
                standardization = json.get("standardization").asString();
            }
        }
    };

    SizeFactor();
    ~SizeFactor() override = default;

    void initializeFromDatabase(const std::string& instanceId) override;
    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<SizeFactor> create(
        const std::string& instanceId,
        std::shared_ptr<astock::database::QtMySQLDatabase> db,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    friend class SizeFactorTestAccess;

private:
    Params params_;

    QString selectedColumn() const;
    double scoreFromRawValue(double rawValue) const;
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor