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

        void fromJson(const foundation::json::JsonFacade& json) {
            if (json.has("size_metric")) sizeMetric = json.get("size_metric").asString();
            if (json.has("log_transform")) logTransform = json.get("log_transform").asBool();
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

private:
    Params params_;

    QString selectedColumn() const;
    double scoreFromRawValue(double rawValue) const;
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor