#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"

#include <optional>

namespace factor {

class LowVolFactorTestAccess;

class LowVolFactor : public BaseFactor {
public:
    static constexpr const char* F_CLOSE = "close";
    static constexpr const char* F_HIGH = "high";
    static constexpr const char* F_LOW = "low";

    struct Params : CommonParams {
        int window = 20;
        std::vector<LowVolComponent> components = {LowVolComponent::VOLATILITY, LowVolComponent::DRAWDOWN, LowVolComponent::BETA};
        std::string benchmarkSymbol = "000300.SH";
        double volatilityWeight = 33.4;
        double drawdownWeight = 33.3;
        double betaWeight = 33.3;
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