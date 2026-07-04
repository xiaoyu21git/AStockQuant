#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

#include <vector>

namespace factor {

class TechnicalFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        std::vector<TechnicalIndicator> technicalIndicators;
        TechnicalPriceType technicalPriceType{TechnicalPriceType::UNKNOWN};
        TechnicalCombinationMode technicalCombinationMode{TechnicalCombinationMode::EqualWeight};
        int rsiWindow = 14;
        int maWindow = 20;
        int emaWindow = 20;
        int bollWindow = 20;
        double bollStdDev = 2.0;
        int kdjWindow = 9;
        int kdjKPeriod = 3;
        int kdjDPeriod = 3;
        int atrWindow = 14;
        int macdFastPeriod = 12;
        int macdSlowPeriod = 26;
        int macdSignalPeriod = 9;
        int obvWindow = 20;
        int vwapWindow = 20;
        int volumeRatioWindow = 20;
        int turnoverStabilityWindow = 60;
        LiquidityMetric turnoverStabilityMetric{LiquidityMetric::TURNOVER_RATE};
        bool useVolume = false;

        void fromJson(const foundation::json::JsonFacade& json);
    };

    TechnicalFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return params_.window; }

    static std::shared_ptr<TechnicalFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor