#pragma once

#include "BaseFactor.h"

namespace astock {
namespace database {
class QtMySQLDatabase;
}
}

namespace factor {

// 动量因子
class MomentumFactor : public BaseFactor {
public:
    struct Params {
        int window = 20;
        std::string type = "simple";  // simple, rank, normalized
        std::string priceType = "adj_close";
        bool useVolume = false;
        int skipRecent = 0;  // 跳过最近N天（避免未来函数）
        
        foundation::json::JsonFacade toJson() const {
            auto json = foundation::json::JsonFacade::createObject();
            json.set("window", json_helper::toJsonValue(window));
            json.set("type", json_helper::toJsonValue(type));
            json.set("price_type", json_helper::toJsonValue(priceType));
            json.set("use_volume", json_helper::toJsonValue(useVolume));
            json.set("skip_recent", json_helper::toJsonValue(skipRecent));
            return json;
        }
        
        void fromJson(const foundation::json::JsonFacade& json) {
            if (json.has("window")) window = json.get("window").asInt();
            if (json.has("type")) type = json.get("type").asString();
            if (json.has("price_type")) priceType = json.get("price_type").asString();
            if (json.has("use_volume")) useVolume = json.get("use_volume").asBool();
            if (json.has("skip_recent")) skipRecent = json.get("skip_recent").asInt();
        }
    };
    
    MomentumFactor();
    ~MomentumFactor() override = default;
    
    // 重写基类方法
    void initializeFromDatabase(const std::string& instanceId) override;
    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    
    // 参数访问
    Params getParams() const { return params_; }
    void setParams(const Params& params) { params_ = params; }
    
    // 工厂方法
    static std::shared_ptr<MomentumFactor> create(const std::string& instanceId,
                                                  std::shared_ptr<astock::database::QtMySQLDatabase> db,
                                                  std::shared_ptr<DataAvailabilityChecker> dataChecker);
    
private:
    Params params_;
    
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