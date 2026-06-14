#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace factor {

class MomentumFactor : public BaseFactor {
public:
    static constexpr const char* F_OPEN = "open";
    static constexpr const char* F_HIGH = "high";
    static constexpr const char* F_LOW = "low";
    static constexpr const char* F_CLOSE = "close";
    static constexpr const char* F_PRE_CLOSE = "pre_close";
    static constexpr const char* F_VOLUME = "volume";
    static constexpr const char* F_TURNOVER = "turnover";
    static constexpr const char* F_CHANGE_PCT = "change_pct";
    static constexpr const char* F_AMPLITUDE = "amplitude";
    static constexpr const char* F_TURNOVER_RATE = "turnover_rate";
    static constexpr const char* F_PRE_ADJ_FACTOR = "pre_adjust_factor";
    static constexpr const char* F_POST_ADJ_FACTOR = "post_adjust_factor";

    struct Params : CommonParams {
        int window = 20;
        MomentumCalculationType type = MomentumCalculationType::SIMPLE;
        AdjustPriceType adjustPriceType = AdjustPriceType::POST_ADJUST_FACTOR;
        bool useVolume = false;
        int skipRecent = 0;
    };
    
    MomentumFactor();
    ~MomentumFactor() override = default;
    
    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    
    Params getParams() const { return params_; }
    void setParams(const Params& params) { params_ = params; }
    
    static std::shared_ptr<MomentumFactor> create(const FactorInstanceInfo& info,
                                                  std::shared_ptr<DataAvailabilityChecker> dataChecker);
    
private:
    Params params_;

    static std::string earliestMomentumSeriesDate(const std::string& anchorDate, int window, int skipRecent);
    static std::string resolveAdjustFieldName(AdjustPriceType priceType);
    static double volumeConfirmationMultiplier(const std::vector<double>& volumes);
    
    double calculateSymbolMomentum(const std::string& symbol,
                                   const CalculationContext& context,
                                   MomentumCalculationType calculationType);
    
    std::unordered_map<std::string, double> calculateSimpleMomentum(
        const CalculationContext& context);
    
    std::unordered_map<std::string, double> calculateRankMomentum(
        const CalculationContext& context);
    
    std::unordered_map<std::string, double> calculateNormalizedMomentum(
        const CalculationContext& context);

    std::vector<double> getAdjustedPriceSeries(const std::string& symbol,
                                               const CalculationContext& context);
    double calculateTaLibRocMomentum(const std::vector<double>& adjustedSeries) const;
    double calculateTaLibExponentialMomentum(const std::vector<double>& adjustedSeries) const;
    
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor