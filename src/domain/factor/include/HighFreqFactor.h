#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

namespace factor {

class HighFreqFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        int frequency = 5;                  // 分钟频率
        int lookbackDays = 10;              // 回顾天数
        int window = 20;                    // 聚合天数（月度因子）
        HFAggregation aggregation{HFAggregation::MEAN};
        double threshold = 0.0;
        double percentile = 0.2;            // 聪明钱阈值
        HFMomentType momentType{HFMomentType::VARIANCE};

        void fromJson(const foundation::json::JsonFacade& json);
    };

    HighFreqFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return params_.window + params_.lookbackDays; }

    static std::shared_ptr<HighFreqFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor
