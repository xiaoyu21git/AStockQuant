#include "IPriceProvider.h"
#include "../../../engine/include/GmSessionEngine.h"
#include "../../market/include/MarketDataService.h"
#include "../../../infrastructure/include/database/ISqlDatabase.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "foundation/log/logging.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace domain::strategy {
namespace {

/// DB 日线数据源 (补单窗口单级源, 回退链末级)
/// 原 StrategyEngine::fetchTodayPrices 补单分支迁移, SQL 保持不变
class DbDailyBarPriceProvider final : public IPriceProvider {
public:
    const char* sourceName() const noexcept override { return "db"; }

    std::map<std::string, EodDayBar> fetchPrices(
        const std::vector<std::string>&, const std::string& endDateStr,
        BarPeriod) const override
    {
        std::map<std::string, EodDayBar> bars;
        auto& pool = astock::database::NativePgConnectionPool::instance();
        auto db = pool.getConnection();
        if (db && db->isOpen()) {
            auto res = db->executeQuery(
                "SELECT si.symbol, d.close, d.volume, d.pre_close "
                "FROM mkt.daily_bar d "
                "JOIN ref.symbol_info si ON d.symbol_id = si.id "
                "WHERE d.trade_date = $1::date",
                {astock::database::SqlParam{endDateStr}});
            for (auto& row : res.getRows()) {
                std::string sym = row.getString("symbol");
                double c = row.getDouble("close");
                if (!sym.empty() && c > 0)
                    bars[sym] = {c,
                                 row.getDouble("volume"),
                                 row.getDouble("pre_close")};
            }
        }
        return bars;
    }
};

/// GM tick 缓存数据源 (盘中首级)
/// 原 StrategyEngine::fetchTodayPrices 盘中分支迁移, 收盘前日线未生成时用实时价
class GmTickCachePriceProvider final : public IPriceProvider {
public:
    const char* sourceName() const noexcept override { return "tickCache"; }

    std::map<std::string, EodDayBar> fetchPrices(
        const std::vector<std::string>& symbols, const std::string&,
        BarPeriod) const override
    {
        std::map<std::string, EodDayBar> bars;
        auto cachedQuotes = engine::GmSessionEngine::instance().getCachedQuotes();
        for (const auto& sym : symbols) {
            auto it = cachedQuotes.find(sym);
            if (it != cachedQuotes.end()) {
                const auto& q = it->second;
                bars[sym] = {q.price, q.volume, q.preClose};
            }
        }
        return bars;
    }
};

/// LiveData 数据源 (tick 聚合日K, 盘中回退级)
/// 无 preClose 时该值可能为 0 → 涨跌停过滤对 preClose<=0 自动跳过, 语义安全
class LiveDataDailyBarPriceProvider final : public IPriceProvider {
public:
    const char* sourceName() const noexcept override { return "liveData"; }

    std::map<std::string, EodDayBar> fetchPrices(
        const std::vector<std::string>& symbols, const std::string&,
        BarPeriod) const override
    {
        std::map<std::string, EodDayBar> bars;
        auto& svc = domain::market::MarketDataService::instance();
        for (const auto& sym : symbols) {
            const auto& ld = svc.liveData(sym);
            const auto& d = ld.dailyBar();
            if (d.close() > 0.0)
                bars[sym] = {d.close(), d.volume(), ld.preClose()};
        }
        return bars;
    }
};

/// 回退链: 逐级取价, 仅补缺标的; 逐级 INFO 日志供运维诊断
class FallbackPriceProvider final : public IPriceProvider {
public:
    explicit FallbackPriceProvider(std::vector<std::unique_ptr<IPriceProvider>> chain)
        : m_chain(std::move(chain)) {}

    const char* sourceName() const noexcept override { return "fallback"; }

    std::map<std::string, EodDayBar> fetchPrices(
        const std::vector<std::string>& symbols, const std::string& endDateStr,
        BarPeriod period) const override
    {
        std::map<std::string, EodDayBar> merged;
        for (const auto& provider : m_chain) {
            // 仅向该级请求尚未命中的标的
            std::vector<std::string> missing;
            for (const auto& sym : symbols)
                if (merged.find(sym) == merged.end()) missing.push_back(sym);
            if (missing.empty()) break;

            auto batch = provider->fetchPrices(missing, endDateStr, period);
            for (auto& [sym, bar] : batch)
                merged.emplace(sym, bar);
            INTERNAL_INFO_STREAM << "[Price] 源=" << provider->sourceName()
                                 << " 命中=" << batch.size() << "/" << missing.size();
        }
        return merged;
    }

private:
    std::vector<std::unique_ptr<IPriceProvider>> m_chain;
};

} // namespace

std::unique_ptr<IPriceProvider> PriceProviderFactory::createProvider(
    BarPeriod period, EvalMode mode)
{
    switch (period) {
    case BarPeriod::Daily:
        if (mode == EvalMode::Compensation) {
            // 补单: DB 单级 (回看历史), 空即 PriceDataEmpty (不静默)
            return std::make_unique<DbDailyBarPriceProvider>();
        }
        // 盘中: tick缓存 → LiveData 回退链 (澄清 C10: DB 不参与盘中当日价格兜底,
        // 数据库唯一职责=回看数据; 盘中全空 = tick 介入失败 → P4 Error 截断)
        {
            std::vector<std::unique_ptr<IPriceProvider>> chain;
            chain.push_back(std::make_unique<GmTickCachePriceProvider>());
            chain.push_back(std::make_unique<LiveDataDailyBarPriceProvider>());
            return std::make_unique<FallbackPriceProvider>(std::move(chain));
        }
    case BarPeriod::Minute1:
    case BarPeriod::Minute5:
        // 接口位: 分钟数据源未实现 (§12 明确不做), 激活前返回 nullptr
        return nullptr;
    }
    return nullptr;
}

} // namespace domain::strategy
