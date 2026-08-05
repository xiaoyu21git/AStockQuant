#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

namespace factor {

class HighFreqFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        Params() { CommonParams::frequency = DataFrequency::Minute; }
        int barFrequency = 5;               // 分钟K线频率（分钟/根）
        int lookbackDays = 10;              // 回顾天数
        int window = 20;                    // 聚合天数（月度因子）
        HFAggregation aggregation{HFAggregation::MEAN};
        double threshold = 0.0;
        double percentile = 0.2;            // 聪明钱阈值
        HFMomentType momentType{HFMomentType::VARIANCE};
        HFMethod method{HFMethod::SMART_MONEY};       // 因子计算方法（非交易策略）

        void fromJson(const foundation::json::JsonFacade& json);
    };

    HighFreqFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override {
        const int freqScale = (std::max)(1, params_.barFrequency / 5);
        return (params_.window + params_.lookbackDays) * freqScale;
    }

    static std::shared_ptr<HighFreqFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    // ── 计算方法（公开，便于单测验证）──
    double smartMoneySignal(const std::vector<double>& returns,
                            const std::vector<double>& volumes) const;
    double realizedMoment(const std::vector<double>& returns) const;
    double volumePriceSignal(const std::vector<double>& returns,
                             const std::vector<double>& volumes,
                             const std::vector<double>& opens,
                             const std::vector<double>& highs,
                             const std::vector<double>& lows) const;

    // 辅助函数
    static bool isCumulative(const std::vector<double>& series);
    static std::vector<double> diff(const std::vector<double>& series);
    static double movingAverage(const std::vector<double>& series, int window, int endIdx);

    void loadConfig(const foundation::json::JsonFacade& config) override;

private:
    Params params_;
};

} // namespace factor
