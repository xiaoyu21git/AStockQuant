#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

#include <string>

namespace factor {

class ReversalFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        ReversalSplitMethod splitMethod{ReversalSplitMethod::NONE};
        int window = 20;
        std::string splitMetric = "avg_trade_amount";
        bool useHighOnly = false;

        void fromJson(const foundation::json::JsonFacade& json);
    };

    ReversalFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return params_.window; }

    static std::shared_ptr<ReversalFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;

    // W式切割实现
    void calculateWCut(const CalculationContext& context,
                       const std::string& effectiveDate,
                       const std::vector<std::string>& symbols,
                       std::unordered_map<std::string, double>& outValues) const;
};

} // namespace factor
