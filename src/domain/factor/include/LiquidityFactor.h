#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

namespace factor {

class LiquidityFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        LiquidityMetric liquidityMetric{LiquidityMetric::UNKNOWN};

        void fromJson(const foundation::json::JsonFacade& json);
    };

    LiquidityFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return params_.window; }

    static std::shared_ptr<LiquidityFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    static constexpr const char* FIELD_AMIHUD_ILLIQUIDITY = "amihud_illiquidity";
    static constexpr const char* FIELD_CLOSE = "close";
    static constexpr const char* FIELD_VOLUME = "volume";
    static constexpr const char* FIELD_AMPLITUDE = "amplitude";
    static constexpr const char* FIELD_TURNOVER_RATE = "turnover_rate";

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor