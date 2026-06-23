#include "../include/RuntimeFactorSvc.h"
#include "../../factor/include/FactorInstanceManager.h"
#include "../../factor/include/factor_compute/FactorEngine.h"
#include "../../factor/include/factor_compute/CachedMarketDataView.h"
#include "../../factor/include/factor_compute/ArrowMarketDataView.h"

#include <cstdio>
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

    // 重建符号解析器：优先 CachedMarketDataView，其次 ArrowMarketDataView
    auto buildResolver = [this](const auto& symbols, const auto& instruments) {
        auto idToSym = std::make_shared<std::unordered_map<std::uint32_t, std::string>>();
        for (size_t i = 0; i < instruments.size() && i < symbols.size(); ++i)
            (*idToSym)[instruments[i].value] = symbols[i];
        m_symbolResolver = [idToSym](std::uint32_t id) -> std::string {
            auto it = idToSym->find(id);
            return it != idToSym->end() ? it->second : std::string();
        };
        fprintf(stderr, "[RFS] setLiveMarketView: rebuilt symbol resolver (%zu symbols)\n", symbols.size());
        fflush(stderr);
    };

    if (auto* cv = dynamic_cast<const factor::compute::CachedMarketDataView*>(view)) {
        if (!cv->symbolStrings().empty())
            buildResolver(cv->symbolStrings(), view->instruments());
    } else if (auto* av = dynamic_cast<const factor::compute::ArrowMarketDataView*>(view)) {
        if (!av->symbolStrings().empty())
            buildResolver(av->symbolStrings(), view->instruments());
    } else {
        fprintf(stderr, "[RFS] setLiveMarketView: view=%p (no symbol strings available)\n",
                static_cast<const void*>(view));
        fflush(stderr);
    }
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
            const auto& instruments = batch.marketView->instruments();
            std::vector<std::string> symbols;
            // 尝试从 CachedMarketDataView 或 ArrowMarketDataView 获取真实股票代码
            if (auto* cv = dynamic_cast<const factor::compute::CachedMarketDataView*>(batch.marketView)) {
                symbols = cv->symbolStrings();
            } else if (auto* av = dynamic_cast<const factor::compute::ArrowMarketDataView*>(batch.marketView)) {
                symbols = av->symbolStrings();
            }
            if (!symbols.empty()) {
                auto idToSym = std::make_shared<std::unordered_map<std::uint32_t, std::string>>();
                for (size_t i = 0; i < instruments.size() && i < symbols.size(); ++i) {
                    (*idToSym)[instruments[i].value] = symbols[i];
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
    fprintf(stderr, "[RFS] setFactorIds: count=%zu first=%s\n",
            factorIds.size(), factorIds.empty() ? "(none)" : factorIds[0].c_str());
    fflush(stderr);
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
        const std::string sym = m_symbolResolver(id);
        if (!sym.empty()) symbolStrList.push_back(sym);
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

    // ── 实盘: 统一走 FactorEngine::computeSingleDate（HistoricalView 由 m_liveMarketView 提供）──
    if (m_liveMarketView) {
        fprintf(stderr, "[RFS] getValues LIVE: instance=%s date=%s symbols=%zu view=%p\n",
                instanceId.c_str(), dateBuf, symbolStrList.size(),
                static_cast<const void*>(m_liveMarketView));
        fflush(stderr);
        auto factorValues = m_engine->computeSingleDate(instanceId, dateBuf, symbolStrList, m_liveMarketView);
        for (const auto& [sym, val] : factorValues) {
            for (uint32_t id : symbolIds)
                if (m_symbolResolver(id) == sym) { result[id] = val; break; }
        }
        fprintf(stderr, "[RFS] getValues LIVE result: %zu values\n", result.size());
        fflush(stderr);
        return result;
    }

    // ── 回退: 无 MarketView 时无法计算。实盘启动前必须调用 setLiveMarketView()，回测须注入 setDataService() ──
    fprintf(stderr, "[RFS] getValues NO-SOURCE: instance=%s m_dataSvc=%p m_liveMarketView=%p — 因子值全部返回空!\n",
            instanceId.c_str(), static_cast<void*>(m_dataSvc),
            static_cast<const void*>(m_liveMarketView));
    fflush(stderr);
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
    fprintf(stderr, "[RFS] copySnapshots: day=%d syms=%zu factorIds=%zu m_dataSvc=%p m_liveMarketView=%p\n",
            tradeDay, syms.size(), instanceIds.size(),
            static_cast<void*>(m_dataSvc),
            static_cast<const void*>(m_liveMarketView));
    fflush(stderr);
    if (tradeDay == 0 || syms.empty() || instanceIds.empty()) {
        fprintf(stderr, "[RFS] copySnapshots SKIP: no data (day=%d syms=%zu ids=%zu)\n",
                tradeDay, syms.size(), instanceIds.size());
        fflush(stderr);
        return;
    }

    auto* self = const_cast<RuntimeFactorSvc*>(this);
    for (const auto& iid : instanceIds) {
        auto values = self->getValues(iid, tradeDay, syms);
        for (auto& [sym, score] : values) {
            output.push_back(RuntimeFactorSnapshot{ sym, iid, score, 1 });
        }
    }
    fprintf(stderr, "[RFS] copySnapshots DONE: %zu snapshots\n", output.size());
    fflush(stderr);
}

} // namespace domain::strategy
