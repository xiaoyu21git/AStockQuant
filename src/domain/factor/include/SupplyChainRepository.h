#pragma once
// SupplyChainRepository — 传导链因子 PG 数据访问层
// 职责: 查询商品排名、价格序列、股票映射，不包含业务逻辑
// 零 Qt 依赖，纯 C++17

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace astock::database {
class ISqlDatabase;
}

namespace factor {

class SupplyChainRepository final {
public:
    SupplyChainRepository();

    struct TopProduct {
        std::string productId;
        int rankNum{0};
        double score{0.0};
    };

    struct StockMapping {
        std::string symbol;
        double weight{1.0};
    };

    /// 查询 calc_date 日排名前 topN 的商品
    std::vector<TopProduct> queryTopProducts(const std::string& calcDate, int topN);

    /// 查询商品 priceDate 及之前最近 lookback 条收盘价（按日期倒序）
    std::vector<double> queryPriceSeries(const std::string& productId,
                                         const std::string& priceDate, int lookback);

    /// 查询 productId 在 queryDate 有效期内所有股票映射
    std::vector<StockMapping> queryStockMappings(const std::string& productId,
                                                  const std::string& queryDate);

    /// 查询商品最新库存周环比 (change_wow), 库存↓=负值
    std::optional<double> queryInventoryChange(const std::string& productId,
                                                const std::string& queryDate);

private:
    std::shared_ptr<astock::database::ISqlDatabase> m_db;
};

} // namespace factor
