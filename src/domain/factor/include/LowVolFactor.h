#pragma once

#include "BaseFactor.h"

#include <optional>

namespace astock {
namespace database {
class QtMySQLDatabase;
}
}

namespace factor {

class LowVolFactorTestAccess;

class LowVolFactor : public BaseFactor {
public:
    struct Params {
        int window = 20;
        std::vector<std::string> components = {"volatility", "drawdown", "beta"};
        std::string volatilityType = "standard";
        double volatilityWeight = 33.4;
        double drawdownWeight = 33.3;
        double betaWeight = 33.3;

        void fromJson(const foundation::json::JsonFacade& json) {
            if (json.has("window")) window = json.get("window").asInt();
            if (json.has("volatilityType")) volatilityType = json.get("volatilityType").asString();
            if (json.has("volatility_type")) volatilityType = json.get("volatility_type").asString();
            if (json.has("components")) {
                components.clear();
                const auto componentList = json.get("components");
                for (size_t index = 0; index < componentList.size(); ++index) {
                    const std::string component = componentList.at(index).asString();
                    if (component == "volatility" || component == "drawdown" || component == "beta") {
                        components.push_back(component);
                    }
                }
                if (components.empty()) {
                    components = {"volatility", "drawdown", "beta"};
                }
            }
            if (json.has("volatilityWeight")) volatilityWeight = json.get("volatilityWeight").asDouble();
            if (json.has("drawdownWeight")) drawdownWeight = json.get("drawdownWeight").asDouble();
            if (json.has("betaWeight")) betaWeight = json.get("betaWeight").asDouble();
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
    friend class LowVolFactorTestAccess;

    struct SymbolMetrics {
        std::optional<double> volatility;
        std::optional<double> maxDrawdown;
        std::optional<double> beta;
    };

    Params params_;

    std::optional<double> computeVolatility(const std::vector<double>& closes) const;
    std::optional<double> computeMaxDrawdown(const std::vector<double>& closes) const;
    std::optional<double> computeBeta(
        const std::vector<FactorDataPoint>& symbolSeries,
        const std::vector<FactorDataPoint>& benchmarkSeries) const;
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor