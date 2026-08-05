#pragma once
// SupplyChainFactor — 传导链因子
// 商品价格波动 → 产业链传导 → A股个股因子信号
// 核心特征: PG直连(不走HistoricalView) + 同组同值(硬关截面标准化)

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "SupplyChainRepository.h"
#include "factor_enums.h"

#include <memory>
#include <string>

namespace factor {

class SupplyChainFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        bool dynamicMode = true;          // true=动态Top-N, false=固定商品
        std::string fixedProductId;       // 固定模式下指定的商品ID
        int lookbackWindow = 20;          // 价格动量窗口（交易日）
        int maxHoldings = 3;              // 动态模式 Top-N 商品数
        int maxStocksPerProduct = 3;      // 每个商品只取权重最高的前 K 只龙头股
        double minScore = 0.0;            // 商品最低 score 阈值，低于此值不选（0=只选正动量商品）

        void fromJson(const foundation::json::JsonFacade& json);
    };

    SupplyChainFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override { return {}; }
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return params_.lookbackWindow + 5; }

    static std::shared_ptr<SupplyChainFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

private:
    Params params_;
    std::unique_ptr<SupplyChainRepository> m_repo;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor
