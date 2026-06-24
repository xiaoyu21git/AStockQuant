#include "../include/RuntimeFactorSvc.h"
#include "../../factor/include/FactorInstanceManager.h"
#include "../../factor/include/factor_compute/FactorEngine.h"
#include "../../factor/include/factor_compute/CachedMarketDataView.h"
#include "../../infrastructure/include/database/ISqlDatabase.h"
#include "foundation/log/logging.hpp"

#include <string>
#include <vector>

namespace domain::strategy {

RuntimeFactorSvc::~RuntimeFactorSvc() = default;

RuntimeFactorSvc::RuntimeFactorSvc(
    factor::FactorInstanceManager& instanceManager,
    SymbolResolver symbolResolver,
    FactorNameResolver factorNameResolver)
    : m_instanceManager(instanceManager)
    , m_symbolResolver(std::move(symbolResolver))
    , m_factorNameResolver(std::move(factorNameResolver))
{
    m_engine = std::make_unique<factor::compute::FactorEngine>(0ULL);
    m_engine->setInstanceManager(&m_instanceManager);
    fprintf(stderr, "[RFS] ctor: engine=%p instanceMgr=%p symbolResolver=%s factorNameResolver=%s\n",
            static_cast<void*>(m_engine.get()),
            static_cast<void*>(&m_instanceManager),
            m_symbolResolver ? "yes" : "NULL",
            m_factorNameResolver ? "yes" : "NULL");
    fflush(stderr);
}

void RuntimeFactorSvc::setMarketView(const factor::compute::IMarketDataView*) {
    // 实盘路径使用 setLiveMarketView() 替代；此接口保留向后兼容
}

void RuntimeFactorSvc::setLiveMarketView(const factor::compute::IMarketDataView* view) {
    m_liveMarketView = view;
    // 重建符号解析：tick 侧 InstrumentId 用股票代码段(000001→1, 600000→600000)
    // 解析后直接映射到 view 中的股票代码字符串，不再走顺序 ID
    auto* cachedView = dynamic_cast<const factor::compute::CachedMarketDataView*>(view);
    if (cachedView && !cachedView->symbolStrings().empty()) {
        const auto& symbols = cachedView->symbolStrings();
        auto idToSym = std::make_shared<std::unordered_map<std::uint32_t, std::string>>();
        for (size_t i = 0; i < symbols.size(); ++i) {
            const std::string& sym = symbols[i];
            if (sym.empty()) continue;
            std::uint32_t codeId = 0;
            try { codeId = static_cast<std::uint32_t>(std::stoul(sym)); }
            catch (...) { continue; }
            (*idToSym)[codeId] = sym;
        }
        m_symbolResolver = [idToSym](std::uint32_t id) -> std::string {
            auto it = idToSym->find(id);
            return it != idToSym->end() ? it->second : std::string();
        };
        fprintf(stderr, "[RFS] setLiveMarketView: rebuilt symbol resolver (%zu symbols)\n", symbols.size());
    } else {
        fprintf(stderr, "[RFS] setLiveMarketView: view=%p (no symbol strings available)\n", static_cast<const void*>(view));
    }
    fflush(stderr);
}

void RuntimeFactorSvc::setDataService(factor::compute::BacktestDataService* svc) {
    m_dataSvc = svc;
    m_engine->setDataService(svc);
    fprintf(stderr, "[RFS] setDataService: svc=%p\n", static_cast<void*>(svc));
    fflush(stderr);

    // 用 MarketView 中的真实股票代码替换硬编码解析器
    if (svc) {
        auto batch = svc->loadBatch(0);
        if (batch.marketView && !batch.marketView->instruments().empty()) {
            // 尝试从 CachedMarketDataView 获取真实股票代码
            auto* cachedView = dynamic_cast<const factor::compute::CachedMarketDataView*>(batch.marketView);
            if (cachedView && !cachedView->symbolStrings().empty()) {
                const auto& symbols = cachedView->symbolStrings();
                auto idToSym = std::make_shared<std::unordered_map<std::uint32_t, std::string>>();
                for (size_t i = 0; i < symbols.size(); ++i) {
                    const std::string& sym = symbols[i];
                    if (sym.empty()) continue;
                    std::uint32_t codeId = 0;
                    try { codeId = static_cast<std::uint32_t>(std::stoul(sym)); }
                    catch (...) { continue; }
                    (*idToSym)[codeId] = sym;
                }
                m_symbolResolver = [idToSym](std::uint32_t id) -> std::string {
                    auto it = idToSym->find(id);
                    return it != idToSym->end() ? it->second : std::string();
                };
                fprintf(stderr, "[RFS] setDataService: rebuilt symbol resolver (%zu symbols)\n", symbols.size());
                fflush(stderr);
            }
        }
    }
}

void RuntimeFactorSvc::setFactorIds(const std::vector<std::string>& factorIds) {
    const std::lock_guard<std::mutex> lock(m_stateMutex);
    m_factorIds = factorIds;
}

std::vector<std::string> RuntimeFactorSvc::getRequiredFields() const {
    std::vector<std::string> fields;
    for (const auto& fid : m_factorIds) {
        if (fid.empty()) continue;
        try {
            auto factor = m_instanceManager.createInstance(fid);
            if (factor) {
                auto reqs = factor->getDataRequirements();
                for (auto& f : reqs.requiredFields)
                    fields.push_back(f);
                for (auto& f : reqs.optionalFields)
                    fields.push_back(f);
            }
        } catch (...) {}
    }
    std::sort(fields.begin(), fields.end());
    fields.erase(std::unique(fields.begin(), fields.end()), fields.end());
    return fields;
}

int RuntimeFactorSvc::getMaxLookbackDays() const {
    int maxDays = 0;
    for (const auto& fid : m_factorIds) {
        if (fid.empty()) continue;
        try {
            auto factor = m_instanceManager.createInstance(fid);
            if (factor) {
                int minPts = factor->getBoundaryRules().minDataPoints;
                if (minPts > maxDays) maxDays = minPts;
            }
        } catch (...) {}
    }
    // 交易日 → 日历日：×1.5 覆盖周末节假日，最少 30 天
    int calendarDays = std::max(30, static_cast<int>(maxDays * 1.5));
    return calendarDays;
}

void RuntimeFactorSvc::buildLiveView(
    const std::vector<astock::database::SqlQueryResultRow>& rows,
    const std::vector<std::string>& extraFields)
{
    auto view = factor::compute::CachedMarketDataView::fromSqlRows(rows, extraFields);
    if (view) setLiveMarketView(view.get());
    m_ownedLiveView = std::move(view);
}

// ── IFactorSvc: 统一的因子值计算入口 ──
std::unordered_map<std::uint32_t, double> RuntimeFactorSvc::getValues(
    const std::string& instanceId,
    std::int32_t date,
    const std::vector<std::uint32_t>& symbolIds)
{
    std::unordered_map<std::uint32_t, double> result;
    if (!m_symbolResolver) return result;

    std::vector<std::string> symbolStrList;
    for (std::uint32_t id : symbolIds) {
        std::string sym = m_symbolResolver(id);
        if (sym.empty()) continue;
        // 去掉 .SH / .SZ 后缀，对齐 view 的 symbolStrings
        auto dot = sym.find('.');
        if (dot != std::string::npos) sym.resize(dot);
        symbolStrList.push_back(std::move(sym));
    }
    if (symbolStrList.empty()) return result;

    const int y = date / 10000, m = (date / 100) % 100, d = date % 100;
    char dateBuf[16];
    std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", y, m, d);

    // ── 回测: FactorEngine 全量算一次, 缓存 ──
    if (m_dataSvc) {
        fprintf(stderr, "[RFS] getValues BACKTEST: instance=%s date=%s symbols=%zu\n",
                instanceId.c_str(), dateBuf, symbolStrList.size());
        fflush(stderr);
        if (m_factorCache.find(instanceId) == m_factorCache.end()) {
            fprintf(stderr, "[RFS] getValues BACKTEST cache MISS, calling engine->compute...\n");
            fflush(stderr);
            factor::compute::FactorCacheKey key;
            key.factorName = instanceId;
            factor::compute::MarketMatrixBatch batch;
            batch.batchIndex = 0;
            m_factorCache[instanceId] = m_engine->compute(batch, key).factorValues;
            auto& cached = m_factorCache[instanceId];
            fprintf(stderr, "[RFS] getValues BACKTEST cache FILLED: dates=%zu\n", cached.size());
            if (!cached.empty()) {
                auto firstDate = cached.begin()->first;
                auto lastDate = cached.rbegin()->first;
                fprintf(stderr, "[RFS]   cache range: %s ~ %s\n", firstDate.c_str(), lastDate.c_str());
            }
            fflush(stderr);
        }
        auto it = m_factorCache[instanceId].find(dateBuf);
        if (it != m_factorCache[instanceId].end()) {
            for (const auto& [sym, val] : it->second) {
                for (uint32_t id : symbolIds)
                    if (m_symbolResolver(id) == sym) { result[id] = val; break; }
            }
        } else {
            fprintf(stderr, "[RFS] getValues BACKTEST: date %s NOT FOUND in cache (cache has %zu dates)\n",
                    dateBuf, m_factorCache[instanceId].size());
            // 打印缓存中随机一个日期的前5个key，确认格式
            if (!m_factorCache[instanceId].empty()) {
                auto& sampleDate = m_factorCache[instanceId].begin()->second;
                fprintf(stderr, "[RFS]   cache sample keys: ");
                int n=0; for (auto& [k,v] : sampleDate) { if (++n>5) break; fprintf(stderr, "%s ", k.c_str()); }
                fprintf(stderr, "\n");
            }
            fflush(stderr);
        }
        fprintf(stderr, "[RFS] getValues BACKTEST result: %zu values\n", result.size());
        fflush(stderr);
        return result;
    }

    // ── 实盘: 用 view 中最新日期（昨天）而不是 tick 日期（今天）做因子计算 ──
    if (m_liveMarketView) {
        const auto& dates = m_liveMarketView->dates();
        if (!dates.empty()) {
            int lastDate = dates.back().value;
            int ly = lastDate / 10000, lm = (lastDate / 100) % 100, ld = lastDate % 100;
            std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", ly, lm, ld);
        }
        auto factorValues = m_engine->computeSingleDate(instanceId, dateBuf, symbolStrList, m_liveMarketView);
        for (const auto& [sym, val] : factorValues) {
            for (uint32_t id : symbolIds)
                if (m_symbolResolver(id) == sym) { result[id] = val; break; }
        }
        if (result.empty())
            INTERNAL_WARN_STREAM << "[RFS] 因子值空: id=" << instanceId << " date=" << dateBuf
                                 << " sym=" << (symbolStrList.empty() ? "?" : symbolStrList[0])
                                 << " viewLastDate=" << (m_liveMarketView->dates().empty() ? "none"
                                     : std::to_string(m_liveMarketView->dates().back().value));
        return result;
    }

    INTERNAL_WARN_STREAM << "[RFS] 无数据源: id=" << instanceId
                         << " dataSvc=" << static_cast<void*>(m_dataSvc)
                         << " liveView=" << static_cast<const void*>(m_liveMarketView);
    return result;
}

// ── IRuntimeFactorService: 状态累积（原 buildFactorCallbacks 闭包逻辑）──

StrategyServiceFlowResult RuntimeFactorSvc::updateIncremental(
    const MarketDataPoint& point)
{
    if (!point.isValid())
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    const std::lock_guard<std::mutex> lock(m_stateMutex);
    m_latestTradeDay = point.tradingDay();
    m_latestSymbols = { point.instrumentId().value };
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceFlowResult RuntimeFactorSvc::updateBatch(
    const std::vector<MarketDataPoint>& batch)
{
    if (batch.empty())
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    std::int32_t latestDay{0};
    std::vector<std::uint32_t> syms;
    for (const auto& p : batch) {
        if (!p.isValid()) continue;
        latestDay = p.tradingDay();
        syms.push_back(p.instrumentId().value);
    }
    if (latestDay == 0 || syms.empty())
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    const std::lock_guard<std::mutex> lock(m_stateMutex);
    m_latestTradeDay = latestDay;
    m_latestSymbols = std::move(syms);
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

void RuntimeFactorSvc::copySnapshots(std::vector<RuntimeFactorSnapshot>& output) const
{
    output.clear();
    std::int32_t tradeDay{0};
    std::vector<std::uint32_t> syms;
    std::vector<std::string> instanceIds;
    {
        const std::lock_guard<std::mutex> lock(m_stateMutex);
        tradeDay = m_latestTradeDay;
        syms = m_latestSymbols;
        instanceIds = m_factorIds;
    }
    if (tradeDay == 0 || syms.empty() || instanceIds.empty()) {
        return;
    }

    auto* self = const_cast<RuntimeFactorSvc*>(this);
    for (const auto& iid : instanceIds) {
        auto values = self->getValues(iid, tradeDay, syms);
        for (auto& [sym, score] : values) {
            output.push_back(RuntimeFactorSnapshot{ sym, iid, score, 1 });
        }
    }
    if (output.empty()) {
        static int emptySnapCount = 0;
        if (++emptySnapCount % 50 == 0)
            INTERNAL_WARN_STREAM << "[RFS] 快照连续空: count=" << emptySnapCount
                                 << " day=" << tradeDay << " ids=" << instanceIds.size();
    }
}

} // namespace domain::strategy
