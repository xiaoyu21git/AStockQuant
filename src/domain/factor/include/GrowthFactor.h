#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

#include <vector>
#include <string>

namespace factor {

class GrowthFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        std::vector<GrowthMetric> growthMetrics;
        std::vector<double> growthWeights;

        void fromJson(const foundation::json::JsonFacade& json);
    };

    GrowthFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return params_.lookbackWindow; }

    static std::shared_ptr<GrowthFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    static constexpr const char* FIELD_PAYOUT_RATIO = "payout_ratio";
    static constexpr const char* FIELD_DIVIDEND_STABILITY = "dividend_stability";
    static constexpr const char* FIELD_INVESTING_CASH_FLOW = "investing_cash_flow";
    static constexpr const char* FIELD_FINANCING_CASH_FLOW = "financing_cash_flow";

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor
