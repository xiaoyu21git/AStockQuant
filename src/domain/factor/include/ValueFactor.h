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

        void fromJson(const foundation::json::JsonFacade& json) {
            if (json.has("valuation_type")) valuationType = json.get("valuation_type").asString();
            if (json.has("use_percentile")) usePercentile = json.get("use_percentile").asBool();
            if (json.has("industry_neutral")) industryNeutral = json.get("industry_neutral").asBool();
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

private:
    Params params_;

    QString selectedColumn() const;
    double scoreFromRawValue(double rawValue) const;
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor