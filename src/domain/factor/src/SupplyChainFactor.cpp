#include "SupplyChainFactor.h"
#include "FactorInstanceManager.h"
#include "JsonFacadeHelpers.h"
#include "foundation/log/logging.hpp"

#include <algorithm>

namespace factor {

SupplyChainFactor::SupplyChainFactor()
    : m_repo(std::make_unique<SupplyChainRepository>())
{
    factorType_ = FactorType::SUPPLY_CHAIN;
}

CalculationResult SupplyChainFactor::calculate(const CalculationContext& context)
{
    CalculationResult result;
    result.date = context.date;
    result.dataStatus.availability = DataAvailability::UNAVAILABLE;

    if (context.symbols.empty()) {
        result.metadata.set("emptyReason",
            json_helper::toJsonValue("symbols为空"));
        return result;
    }

    // 1. 查询 Top 池
    auto topProducts = m_repo->queryTopProducts(context.date, params_.maxHoldings);

    if (topProducts.empty()) {
        result.metadata.set("emptyReason",
            json_helper::toJsonValue("商品排名数据为空"));
        return result;
    }

    // 1.5 过滤：score 低于阈值的商品不选（默认 0=只选正动量）
    //    避免在商品全线下跌时强行选出"跌得最少"的来推股票
    topProducts.erase(
        std::remove_if(topProducts.begin(), topProducts.end(),
                       [this](const auto& p) { return p.score <= params_.minScore; }),
        topProducts.end());

    if (topProducts.empty()) {
        result.metadata.set("emptyReason",
            json_helper::toJsonValue("所有商品score低于阈值"));
        return result;
    }

    // 2. 按商品 score 直接赋权（不做归一化！）
    //    归一化会抹平行情强度：score=0.05 和 score=0.40 归一化后都是 1.0
    //    直接用绝对 score 保留信号强弱差异
    std::unordered_map<std::string, double> values;
    for (const auto& p : topProducts) {
        auto mappings = m_repo->queryStockMappings(p.productId, context.date);

        // 按 weight 降序，只取 top K
        std::sort(mappings.begin(), mappings.end(),
                  [](const auto& a, const auto& b) { return a.weight > b.weight; });
        int take = std::min(params_.maxStocksPerProduct,
                            static_cast<int>(mappings.size()));

        for (int i = 0; i < take; ++i) {
            const auto& m = mappings[i];
            double& val = values[m.symbol];
            // 因子值 = score × stockWeight（保留行情强度的绝对量级）
            double contributed = p.score * m.weight;
            // 同一股票可能映射到多个商品 → 取最大贡献值（最强的传导信号）
            if (contributed > val) val = contributed;
        }
    }

    if (values.empty()) {
        result.metadata.set("emptyReason",
            json_helper::toJsonValue("无股票映射"));
        return result;
    }

    // 6. 返回：标记跳过标准化（同组值相同，标准化会抹平信号）
    result.values = std::move(values);
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.metadata.set("skipStandardization", json_helper::toJsonValue(true));
    result.metadata.set("reason",
        json_helper::toJsonValue("同组股票值相同，标准化会抹平信号"));

    return result;
}

BoundaryRules SupplyChainFactor::getBoundaryRules() const
{
    BoundaryRules rules;
    rules.minDataPoints = 2;
    rules.handleNewStock = NewStockHandling::INCLUDE;
    rules.handleSuspended = SuspendedHandling::SET_NULL;
    rules.handleDelisted = DelistedHandling::EXCLUDE;
    rules.handleOutliers = OutlierHandling::KEEP;
    return rules;
}

std::shared_ptr<SupplyChainFactor> SupplyChainFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<SupplyChainFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

void SupplyChainFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);

    foundation::json::JsonFacade calcCfg = config.has("calculation")
        ? config.get("calculation")
        : config;

    params_.fromJson(calcCfg);
}

} // namespace factor
