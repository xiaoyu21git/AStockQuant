#pragma once

#include "ConfigurableFactor.h"
#include "factor_enums.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace factor {

// 动量因子
class MomentumFactor : public BaseFactor {
private:
public:
    struct Params : ConfigurableFactorBase::CommonParams {
        int window = 20;
        MomentumCalculationType type = MomentumCalculationType::SIMPLE;
        AdjustPriceType adjustPriceType = AdjustPriceType::POST_ADJUST_FACTOR;  // 复权类型：pre_adjust_factor（前复权）或 post_adjust_factor（后复权）
        bool useVolume = false;
        int skipRecent = 0;  // 跳过最近N天（避免未来函数）
    };
    
    MomentumFactor();
    ~MomentumFactor() override = default;
    
    // 重写基类方法
    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    
    // 参数访问
    Params getParams() const { return params_; }
    void setParams(const Params& params) { params_ = params; }
    
    // 工厂方法
    static std::shared_ptr<MomentumFactor> create(const FactorInstanceInfo& info,
                                                  std::shared_ptr<DataAvailabilityChecker> dataChecker);
    
private:
    Params params_;

    static std::string earliestMomentumSeriesDate(const std::string& anchorDate, int window, int skipRecent);
    static std::string resolveAdjustFieldName(AdjustPriceType priceType);
    static double volumeConfirmationMultiplier(const std::vector<double>& volumes);
    
    // 计算逻辑
    double calculateSymbolMomentum(const std::string& symbol,
                                   const CalculationContext& context,
                                   MomentumCalculationType calculationType);
    
    // 不同类型动量计算
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
    
    // 加载配置
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor