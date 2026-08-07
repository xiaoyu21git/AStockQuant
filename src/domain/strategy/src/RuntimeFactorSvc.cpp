#include "../include/RuntimeFactorSvc.h"
#include "../../factor/include/FactorInstanceManager.h"
#include "../../factor/include/EventDrivenFactor.h"
#include "../../factor/include/factor_compute/FactorEngine.h"
#include "../../factor/include/factor_compute/CachedMarketDataView.h"
#include "../../infrastructure/include/database/ISqlDatabase.h"
#include "foundation/log/logging.hpp"
#include "foundation/market/AStockSymbol.h"

#include <string>
#include <sstream>
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
    INTERNAL_INFO_STREAM << "[RFS] ctor: engine=" << static_cast<void*>(m_engine.get()) << " instanceMgr=" << static_cast<void*>(&m_instanceManager) << " symbolResolver=" << (m_symbolResolver ? "yes" : "NULL") << " factorNameResolver=" << (m_factorNameResolver ? "yes" : "NULL");
}

void RuntimeFactorSvc::setMarketView(const factor::compute::IMarketDataView*) {
    // 实盘路径使用 setLiveMarketView() 替代；此接口保留向后兼容
}

void RuntimeFactorSvc::setLiveMarketView(const factor::compute::IMarketDataView* view) {
    m_liveMarketView = view;
    // 重建符号解析：tick 侧 InstrumentId 用股票代码段(000001→1, 600000→600000)
    // 解析后直接映射到 view 中的股票代码字符串，不再走顺序 ID
    if (view && !view->symbolStrings().empty()) {
        const auto& symbols = view->symbolStrings();
        auto idToSym = std::make_shared<std::unordered_map<std::uint32_t, std::string>>();
        for (size_t i = 0; i < symbols.size(); ++i) {
            const std::string& sym = symbols[i];
            if (sym.empty()) continue;
            std::string codeOnly = foundation::market::AStockSymbol::codeOnly(sym);
            std::uint32_t codeId = 0;
            try { codeId = static_cast<std::uint32_t>(std::stoul(codeOnly)); }
            catch (...) { continue; }
            (*idToSym)[codeId] = sym;
        }
        m_symbolResolver = [idToSym](std::uint32_t id) -> std::string {
            auto it = idToSym->find(id);
            return it != idToSym->end() ? it->second : std::string();
        };
        INTERNAL_INFO_STREAM << "[RFS] setLiveMarketView: rebuilt symbol resolver (" << symbols.size() << " symbols)";
    } else {
        INTERNAL_INFO_STREAM << "[RFS] setLiveMarketView: view=" << static_cast<const void*>(view) << " (no symbol strings available)";
    }
}

void RuntimeFactorSvc::setDataService(factor::compute::BacktestDataService* svc) {
    m_dataSvc = svc;
    m_engine->setDataService(svc);
    INTERNAL_INFO_STREAM << "[RFS] setDataService: svc=" << static_cast<void*>(svc);

    // 用 MarketView 中的真实股票代码替换硬编码解析器
    if (svc) {
        auto batch = svc->loadBatch(0);
        if (batch.marketView && !batch.marketView->instruments().empty()) {
            // 从 IMarketDataView 接口获取股票代码
            if (batch.marketView && !batch.marketView->symbolStrings().empty()) {
                const auto& symbols = batch.marketView->symbolStrings();
                auto idToSym = std::make_shared<std::unordered_map<std::uint32_t, std::string>>();
                for (size_t i = 0; i < symbols.size(); ++i) {
                    const std::string& sym = symbols[i];
                    if (sym.empty()) continue;
                    std::string codeOnly = foundation::market::AStockSymbol::codeOnly(sym);
                    std::uint32_t codeId = 0;
                    try { codeId = static_cast<std::uint32_t>(std::stoul(codeOnly)); }
                    catch (...) { continue; }
                    (*idToSym)[codeId] = sym;
                }
                m_symbolResolver = [idToSym](std::uint32_t id) -> std::string {
                    auto it = idToSym->find(id);
                    return it != idToSym->end() ? it->second : std::string();
                };
                INTERNAL_INFO_STREAM << "[RFS] setDataService: rebuilt symbol resolver (" << symbols.size() << " symbols)";
            }
        }
    }
}

void RuntimeFactorSvc::setFactorIds(const std::vector<std::string>& factorIds) {
    const std::lock_guard<std::mutex> lock(m_stateMutex);
    m_factorIds = factorIds;
}

const std::map<std::string, double>* RuntimeFactorSvc::backtestValuesBySymbol(
    const std::string& instanceId, std::int32_t date) const
{
    if (!m_dataSvc || !m_engine) return nullptr;
    // 与 getValues 回测分支共享同一份缓存: 首次访问全量计算
    if (m_factorCache.find(instanceId) == m_factorCache.end()) {
        factor::compute::FactorCacheKey key;
        key.factorName = instanceId;
        factor::compute::MarketMatrixBatch batch;
        batch.batchIndex = 0;
        m_factorCache[instanceId] = m_engine->compute(batch, key).factorValues;
        INTERNAL_INFO_STREAM << "[RFS] backtestValuesBySymbol cache FILLED: id=" << instanceId
                             << " dates=" << m_factorCache[instanceId].size();
    }
    char dateBuf[16];
    std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
                  date / 10000, (date / 100) % 100, date % 100);
    const auto& cache = m_factorCache[instanceId];
    const auto it = cache.find(dateBuf);
    return it != cache.end() ? &it->second : nullptr;
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

void RuntimeFactorSvc::clearSignalCache() {
    if (m_engine) m_engine->clearSignalCache();
}

void RuntimeFactorSvc::loadCommodityEvents(const std::string& startDate,
                                            const std::string& endDate)
{
    for (const auto& fid : m_factorIds) {
        if (fid.empty()) continue;
        try {
            auto factor = m_instanceManager.createInstance(fid);
            if (!factor) continue;
            auto* edf = dynamic_cast<factor::EventDrivenFactor*>(factor.get());
            if (edf) edf->loadEventsFromDb(startDate, endDate);
        } catch (...) {}
    }
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
        symbolStrList.push_back(foundation::market::AStockSymbol::codeOnly(sym));
    }
    if (symbolStrList.empty()) return result;

    const int y = date / 10000, m = (date / 100) % 100, d = date % 100;
    char dateBuf[16];
    std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", y, m, d);

    // ── 回测: FactorEngine 全量算一次, 缓存 ──
    if (m_dataSvc) {
        INTERNAL_INFO_STREAM << "[RFS] getValues BACKTEST: instance=" << instanceId << " date=" << dateBuf << " symbols=" << symbolStrList.size();
        if (m_factorCache.find(instanceId) == m_factorCache.end()) {
            INTERNAL_INFO_STREAM << "[RFS] getValues BACKTEST cache MISS, calling engine->compute...";
            factor::compute::FactorCacheKey key;
            key.factorName = instanceId;
            factor::compute::MarketMatrixBatch batch;
            batch.batchIndex = 0;
            m_factorCache[instanceId] = m_engine->compute(batch, key).factorValues;
            auto& cached = m_factorCache[instanceId];
            INTERNAL_INFO_STREAM << "[RFS] getValues BACKTEST cache FILLED: dates=" << cached.size();
            if (!cached.empty()) {
                auto firstDate = cached.begin()->first;
                auto lastDate = cached.rbegin()->first;
                INTERNAL_INFO_STREAM << "[RFS]   cache range: " << firstDate << " ~ " << lastDate;
            }
        }
        auto it = m_factorCache[instanceId].find(dateBuf);
        if (it != m_factorCache[instanceId].end()) {
            for (const auto& [sym, val] : it->second) {
                // 缓存 key 可能带后缀 (如 "000001.SZ"), 统一去掉后缀再匹配
                std::string codeOnly = foundation::market::AStockSymbol::codeOnly(sym);
                for (uint32_t id : symbolIds) {
                    std::string resolved = foundation::market::AStockSymbol::codeOnly(m_symbolResolver(id));
                    if (resolved == codeOnly) { result[id] = val; break; }
                }
            }
        } else {
            INTERNAL_WARN_STREAM << "[RFS] getValues BACKTEST: date " << dateBuf << " NOT FOUND in cache (cache has " << m_factorCache[instanceId].size() << " dates)";
            // 打印缓存中随机一个日期的前5个key，确认格式
            if (!m_factorCache[instanceId].empty()) {
                auto& sampleDate = m_factorCache[instanceId].begin()->second;
                std::ostringstream sampleOss;
                sampleOss << "[RFS]   cache sample keys: ";
                int n=0; for (auto& [k,v] : sampleDate) { if (++n>5) break; sampleOss << k << " "; }
                INTERNAL_WARN_STREAM << sampleOss.str();
            }
        }
        INTERNAL_INFO_STREAM << "[RFS] getValues BACKTEST result: " << result.size() << " values";
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
    if (tradeDay == 0 || instanceIds.empty()) {
        return;
    }

    char dateBuf[16];
    std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
                  tradeDay / 10000, (tradeDay / 100) % 100, tradeDay % 100);

    // 回测路径: 直接从 m_factorCache 读值，用 view symbolStrings 做字符串匹配
    // (与 FactorBacktestOrchestrator / backtestValuesBySymbol 保持一致，不经过 resolver)
    if (m_dataSvc) {
        // 从 view 构建 symbol → instrumentId 映射
        std::unordered_map<std::string, std::uint32_t> symbolToId;
        auto batch = m_dataSvc->loadBatch(0);
        if (batch.marketView) {
            const auto& viewSyms = batch.marketView->symbolStrings();
            const auto& insts = batch.marketView->instruments();
            for (size_t i = 0; i < viewSyms.size() && i < insts.size(); ++i)
                symbolToId[viewSyms[i]] = insts[i].value;
        }

        for (const auto& iid : instanceIds) {
            auto cacheIt = m_factorCache.find(iid);
            if (cacheIt == m_factorCache.end()) continue;
            auto dateIt = cacheIt->second.find(dateBuf);
            if (dateIt == cacheIt->second.end()) continue;

            size_t matched = 0;
            double sampleVal = 0.0;
            for (const auto& [sym, val] : dateIt->second) {
                auto idIt = symbolToId.find(sym);
                if (idIt == symbolToId.end()) continue;
                if (matched == 0) sampleVal = val;
                ++matched;
                output.push_back(RuntimeFactorSnapshot{ idIt->second, iid, val, 1 });
            }
            static int diag = 0;
            if (++diag <= 3)
                INTERNAL_INFO_STREAM << "[RFS] copySnapshots cacheRead: iid=" << iid
                                     << " date=" << dateBuf
                                     << " cacheEntries=" << dateIt->second.size()
                                     << " matched=" << matched
                                     << " sampleVal=" << sampleVal;
        }
        return;
    }

    // 实盘路径: EOD/补单需要全市场截面因子做 Z-score 归一化
    if (tradeDay == 0 || syms.empty()) return;

    // 截面因子: updateIncremental 只传了当前 tick 的单个标的,
    // 此处从 liveMarketView 展开为全量标的, 一次计算全截面并缓存
    if (m_liveMarketView && m_engine) {
        const auto& instruments = m_liveMarketView->instruments();
        if (instruments.size() > syms.size()) {
            syms.clear();
            syms.reserve(instruments.size());
            for (const auto& inst : instruments)
                syms.push_back(inst.value);
        }
    }

    auto* self = const_cast<RuntimeFactorSvc*>(this);

    // 构建 codeOnly → instrumentId 映射 (缓存 key 用无后缀码, 统一查找)
    // 同时构建 fullSymbol → instrumentId 映射 (因子计算结果 key 是完整 symbol)
    std::unordered_map<std::string, std::uint32_t> codeOnlyToId;
    std::unordered_map<std::string, std::uint32_t> fullSymToId;
    if (m_liveMarketView) {
        const auto& viewSyms = m_liveMarketView->symbolStrings();
        const auto& viewInsts = m_liveMarketView->instruments();
        for (size_t i = 0; i < viewSyms.size() && i < viewInsts.size(); ++i) {
            const auto& fullSym = viewSyms[i];
            std::uint32_t instId = viewInsts[i].value;
            fullSymToId[fullSym] = instId;
            std::string code = foundation::market::AStockSymbol::codeOnly(fullSym);
            codeOnlyToId[code] = instId;
        }
    }

    // 工具 lambda: 完整 symbol → 去后缀码 (委托 AStockSymbol)
    auto stripSuffix = [](const std::string& s) {
        return foundation::market::AStockSymbol::codeOnly(s);
    };

    for (const auto& iid : instanceIds) {
        // ── 缓存读: 首次计算后后续 step() 调用直接读缓存 ──
        auto cacheIt = m_factorCache.find(iid);
        if (cacheIt != m_factorCache.end()) {
            auto dateIt = cacheIt->second.find(std::string(dateBuf));
            if (dateIt != cacheIt->second.end()) {
                for (const auto& [sym, val] : dateIt->second) {
                    auto idIt = codeOnlyToId.find(sym);
                    if (idIt != codeOnlyToId.end())
                        output.push_back(RuntimeFactorSnapshot{ idIt->second, iid, val, 1 });
                }
                static int cacheDiag = 0;
                if (++cacheDiag <= 3)
                    INTERNAL_INFO_STREAM << "[RFS] copySnapshots liveCache: iid=" << iid
                                         << " date=" << dateBuf
                                         << " entries=" << dateIt->second.size();
                continue;
            }
        }

        // ── 缓存未命中: 批量计算全截面因子值 ──
        // 关键: 必须传完整 symbol (如 "000001.SZ") 给 computeSingleDate,
        // MarketDataViewHistoricalAdapter 内部存的是带后缀的 symbol, 去后缀会导致 findSymbolIndex 失败
        std::vector<std::string> symbolStrs;
        symbolStrs.reserve(syms.size());
        for (uint32_t id : syms) {
            std::string resolved = m_symbolResolver ? m_symbolResolver(id) : std::string();
            if (resolved.empty()) continue;
            symbolStrs.push_back(std::move(resolved));  // 保留完整 symbol, 不去后缀
        }

        auto factorValues = self->m_engine->computeSingleDate(
            iid, std::string(dateBuf), symbolStrs, m_liveMarketView);

        // 写入缓存: key 用无后缀码, 与 codeOnlyToId 一致
        std::map<std::string, double> dateCache;
        for (const auto& [sym, val] : factorValues)
            dateCache[stripSuffix(sym)] = val;
        m_factorCache[iid][std::string(dateBuf)] = std::move(dateCache);

        // 输出快照: 通过 fullSymbol 查找 instrumentId
        for (const auto& [sym, val] : factorValues) {
            auto idIt = fullSymToId.find(sym);
            if (idIt != fullSymToId.end())
                output.push_back(RuntimeFactorSnapshot{ idIt->second, iid, val, 1 });
        }

        static int computeDiag = 0;
        if (++computeDiag <= 3)
            INTERNAL_INFO_STREAM << "[RFS] copySnapshots liveCompute: iid=" << iid
                                 << " date=" << dateBuf
                                 << " computed=" << factorValues.size()
                                 << " totalSyms=" << symbolStrs.size();
    }
}

} // namespace domain::strategy
