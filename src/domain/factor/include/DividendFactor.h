#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

#include <vector>

namespace factor {

class DividendFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        DividendMetric dividendMetric{DividendMetric::UNKNOWN};
        std::vector<DividendMetric> dividendMetrics;
        double minDividendYield = 0.0;

        void fromJson(const foundation::json::JsonFacade& json);
    };

    DividendFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return params_.lookbackWindow; }

    static std::shared_ptr<DividendFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    struct Fields {
        // DividendFactor reuses base field_names::DIVIDEND_YIELD for yield; no exclusive fields beyond that.
    };

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor