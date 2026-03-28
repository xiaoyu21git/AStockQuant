#pragma once

#include "BaseFactor.h"

namespace astock {
namespace database {
class QtMySQLDatabase;
}
}

namespace factor {

class LowVolFactor : public BaseFactor {
public:
    struct Params {
        int window = 20;
        std::string volatilityType = "standard";

        void fromJson(const foundation::json::JsonFacade& json) {
            if (json.has("window")) window = json.get("window").asInt();
            if (json.has("volatility_type")) volatilityType = json.get("volatility_type").asString();
        }
    };

    LowVolFactor();
    ~LowVolFactor() override = default;

    void initializeFromDatabase(const std::string& instanceId) override;
    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<LowVolFactor> create(
        const std::string& instanceId,
        std::shared_ptr<astock::database::QtMySQLDatabase> db,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

private:
    Params params_;

    double computeVolatility(const std::vector<double>& closes) const;
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor