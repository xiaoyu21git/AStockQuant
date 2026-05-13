#pragma once

#include "BaseFactor.h"
#include "factor_enums.h"

#include <QDate>
#include <QStringList>

#include <unordered_map>

namespace factor {

// 动量因子
class MomentumFactor : public BaseFactor {
private:
    static QString adjustPriceTypeToString(AdjustPriceType type);
    static AdjustPriceType adjustPriceTypeFromString(const QString& rawType);

public:
    struct Params {
        int window = 20;
        int lookbackPeriod = 252;
        bool laggedEnabled = false;
        std::string frequency = "daily";
        std::string standardization = "none";
        bool neutralizationEnabled = false;
        std::string type = "simple";  // simple, rank, normalized
        AdjustPriceType adjustPriceType = AdjustPriceType::POST_ADJUST_FACTOR;  // 复权类型：pre_adjust_factor（前复权）或 post_adjust_factor（后复权）
        bool useVolume = false;
        int skipRecent = 0;  // 跳过最近N天（避免未来函数）
        
        foundation::json::JsonFacade toJson() const {
            auto json = foundation::json::JsonFacade::createObject();
            json.set("window", json_helper::toJsonValue(window));
            json.set("lookbackPeriod", json_helper::toJsonValue(lookbackPeriod));
            json.set("laggedEnabled", json_helper::toJsonValue(laggedEnabled));
            json.set("frequency", json_helper::toJsonValue(frequency));
            json.set("standardization", json_helper::toJsonValue(standardization));
            json.set("neutralizationEnabled", json_helper::toJsonValue(neutralizationEnabled));
            json.set("type", json_helper::toJsonValue(type));
            json.set("adjustPriceType", json_helper::toJsonValue(MomentumFactor::adjustPriceTypeToString(adjustPriceType).toStdString()));
            json.set("useVolume", json_helper::toJsonValue(useVolume));
            json.set("skipRecent", json_helper::toJsonValue(skipRecent));
            return json;
        }
        
        void fromJson(const foundation::json::JsonFacade& json) {
            if (json.has("window")) window = json.get("window").asInt();
            if (json.has("lookbackPeriod")) lookbackPeriod = json.get("lookbackPeriod").asInt();
            if (json.has("laggedEnabled")) laggedEnabled = json.get("laggedEnabled").asBool();
            if (json.has("frequency")) frequency = json.get("frequency").asString();
            if (json.has("standardization")) standardization = json.get("standardization").asString();
            if (json.has("neutralizationEnabled")) neutralizationEnabled = json.get("neutralizationEnabled").asBool();
            if (json.has("type")) type = json.get("type").asString();
            if (json.has("adjustPriceType")) adjustPriceType = MomentumFactor::adjustPriceTypeFromString(QString::fromStdString(json.get("adjustPriceType").asString()));
            if (json.has("useVolume")) useVolume = json.get("useVolume").asBool();
            if (json.has("skipRecent")) skipRecent = json.get("skipRecent").asInt();
        }
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

    static QString earliestMomentumSeriesDate(const QDate& anchorDate, int window, int skipRecent);
    static QString normalizeMomentumType(const std::string& rawType);
    static QString resolveAdjustFieldName(AdjustPriceType priceType);
    static double volumeConfirmationMultiplier(const std::vector<double>& volumes);
    
    // 计算逻辑
    double calculateSymbolMomentum(const std::string& symbol,
                                   const CalculationContext& context);
    
    // 不同类型动量计算
    std::unordered_map<std::string, double> calculateSimpleMomentum(
        const CalculationContext& context);
    
    std::unordered_map<std::string, double> calculateRankMomentum(
        const CalculationContext& context);
    
    std::unordered_map<std::string, double> calculateNormalizedMomentum(
        const CalculationContext& context);
    
    // 辅助方法
    std::pair<double, double> getPriceData(const std::string& symbol,
                                           const CalculationContext& context);
    
    // 加载配置
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor