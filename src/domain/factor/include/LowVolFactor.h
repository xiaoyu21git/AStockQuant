#pragma once

#include "BaseFactor.h"

#include <optional>

namespace factor {

class LowVolFactorTestAccess;

class LowVolFactor : public BaseFactor {
public:
    struct Params {
        int window = 20;
        int lookbackPeriod = 252;
        bool laggedEnabled = false;
        std::string frequency = "daily";
        std::string standardization = "none";
        bool neutralizationEnabled = false;
        std::vector<std::string> components = {"volatility", "drawdown", "beta"};
        std::string volatilityType = "standard";
        std::string benchmarkSymbol = "000300.SH";
        double volatilityWeight = 33.4;
        double drawdownWeight = 33.3;
        double betaWeight = 33.3;

        void fromJson(const foundation::json::JsonFacade& json) {
            if (json.has("window")) window = json.get("window").asInt();
            if (json.has("lookbackPeriod")) lookbackPeriod = json.get("lookbackPeriod").asInt();
            if (json.has("laggedEnabled")) laggedEnabled = json.get("laggedEnabled").asBool();
            if (json.has("frequency")) frequency = json.get("frequency").asString();
            if (json.has("standardization")) standardization = json.get("standardization").asString();
            if (json.has("neutralizationEnabled")) neutralizationEnabled = json.get("neutralizationEnabled").asBool();
            if (json.has("volatilityType")) volatilityType = json.get("volatilityType").asString();
            if (json.has("benchmarkSymbol")) benchmarkSymbol = json.get("benchmarkSymbol").asString();
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

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<LowVolFactor> create(
        const FactorInstanceInfo& info,
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
        const std::vector<HistoricalDataPoint>& symbolSeries,
        const std::vector<HistoricalDataPoint>& benchmarkSeries) const;
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor