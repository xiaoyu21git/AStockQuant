#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

namespace factor {

class IndustryFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        IndustryMetric industryMetricKind{IndustryMetric::UNKNOWN};
        ConfigurableSectorType sectorType{ConfigurableSectorType::SW_L1};

        void fromJson(const foundation::json::JsonFacade& json);
    };

    IndustryFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<IndustryFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    static constexpr const char* FIELD_INDUSTRY_PROSPERITY = "industry_prosperity";
    static constexpr const char* FIELD_INDUSTRY_MOMENTUM = "industry_momentum";
    static constexpr const char* FIELD_INDUSTRY_CONCENTRATION = "industry_concentration";

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor