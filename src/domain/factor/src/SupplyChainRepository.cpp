#include "SupplyChainRepository.h"

#include "database/ISqlDatabase.h"
#include "database/NativePgConnectionPool.h"
#include "foundation/log/logging.hpp"

namespace factor {

SupplyChainRepository::SupplyChainRepository()
    : m_db(astock::database::NativePgConnectionPool::instance().getConnection())
{
}

using SqlParam = astock::database::SqlParam;

std::vector<SupplyChainRepository::TopProduct>
SupplyChainRepository::queryTopProducts(const std::string& calcDate, int topN)
{
    std::vector<TopProduct> result;

    const char* kSql =
        "SELECT product_id, rank_num, score "
        "FROM alpha.commodity_daily_rank "
        "WHERE calc_date = ? AND rank_num <= ? "
        "ORDER BY rank_num";

    auto queryResult = m_db->executeQuery(kSql, {
        SqlParam{calcDate},
        SqlParam{static_cast<std::int32_t>(topN)}
    });

    for (std::size_t i = 0; i < queryResult.rowCount(); ++i) {
        const auto& row = queryResult.getRow(i);
        result.push_back({
            row.getString("product_id"),
            row.getInt("rank_num"),
            row.getDouble("score")
        });
    }

    if (result.empty()) {
        INTERNAL_WARN_STREAM << "[SupplyChainRepo] queryTopProducts empty: date="
                             << calcDate << " topN=" << topN
                             << " lastError=" << m_db->lastError();
    }

    return result;
}

std::vector<double>
SupplyChainRepository::queryPriceSeries(const std::string& productId,
                                         const std::string& priceDate, int lookback)
{
    std::vector<double> result;

    const char* kSql =
        "SELECT close_price "
        "FROM mkt.commodity_prices_daily "
        "WHERE product_id = ? AND trade_date <= ? "
        "ORDER BY trade_date DESC "
        "LIMIT ?";

    auto queryResult = m_db->executeQuery(kSql, {
        SqlParam{productId},
        SqlParam{priceDate},
        SqlParam{static_cast<std::int32_t>(lookback)}
    });

    for (std::size_t i = queryResult.rowCount(); i > 0; --i) {
        result.push_back(queryResult.getRow(i - 1).getDouble("close_price"));
    }

    return result;
}

std::vector<SupplyChainRepository::StockMapping>
SupplyChainRepository::queryStockMappings(const std::string& productId,
                                           const std::string& queryDate)
{
    std::vector<StockMapping> result;

    const char* kSql =
        "SELECT symbol, weight "
        "FROM ref.product_stock_mapping "
        "WHERE product_id = ? "
        "  AND effective_date <= ? "
        "  AND expired_date >= ?";

    auto queryResult = m_db->executeQuery(kSql, {
        SqlParam{productId},
        SqlParam{queryDate},
        SqlParam{queryDate}
    });

    for (std::size_t i = 0; i < queryResult.rowCount(); ++i) {
        const auto& row = queryResult.getRow(i);
        result.push_back({
            row.getString("symbol"),
            row.getDouble("weight", 1.0)
        });
    }

    if (result.empty()) {
        INTERNAL_WARN_STREAM << "[SupplyChainRepo] queryStockMappings empty: productId="
                             << productId << " date=" << queryDate;
    }

    return result;
}

std::optional<double> SupplyChainRepository::queryInventoryChange(
    const std::string& productId, const std::string& queryDate)
{
    const char* kSql =
        "SELECT change_wow FROM mkt.commodity_inventory "
        "WHERE product_id = ? AND trade_date <= ? "
        "ORDER BY trade_date DESC LIMIT 1";

    auto queryResult = m_db->executeQuery(kSql, {
        SqlParam{productId},
        SqlParam{queryDate}
    });

    if (queryResult.isEmpty()) {
        return std::nullopt;
    }

    double val = queryResult.getRow(0).getDouble("change_wow", 0.0);
    if (val == 0.0) {
        return std::nullopt;
    }
    return val;
}

} // namespace factor
