#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"

#include <string>

namespace factor {

class SizeFactor : public BaseFactor {
public:
    static constexpr const char* F_MARKET_CAP = "market_cap";
    static constexpr const char* F_CIRCULATING_MARKET_CAP = "circulating_market_cap";

    struct Params : CommonParams {
        SizeMetric sizeMetric = SizeMetric::MARKET_CAP;
        bool logTransform = true;
    };

    SizeFactor();
    ~SizeFactor() override = default;

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return params_.lookbackWindow; }

    static std::shared_ptr<SizeFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    friend class SizeFactorTestAccess;

private:
    Params params_;

    std::string selectedColumn() const;
    double scoreFromRawValue(double rawValue) const;
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor