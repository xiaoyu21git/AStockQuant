#include "../include/RuntimeFactorSvc.h"
#include "../../factor/include/FactorInstanceManager.h"
#include "../../factor/include/EventDrivenFactor.h"
#include "../../factor/include/factor_compute/FactorEngine.h"
#include "../../factor/include/factor_compute/ArrowMarketDataView.h"
#include "../../factor/include/factor_compute/FactorValuePipeline.h"
#include "../../factor/include/factor_compute/CachedMarketDataView.h"
#include "../../infrastructure/include/database/ISqlDatabase.h"
#include "foundation/Utils/DateUtils.h"
#include "foundation/log/logging.hpp"
#include "foundation/market/AStockSymbol.h"

#include <atomic>
#include <cmath>
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

void RuntimeFactorSvc::setLiveMarketView(
    std::shared_ptr<const factor::compute::IMarketDataView> view)
{
    m_liveMarketView = std::move(view);  // P3: 发布即持有 (shared_ptr 直接入库)
    // 重建符号解析：tick 侧 InstrumentId 用股票代码段(000001→1, 600000→600000)
    // 解析后直接映射到 view 中的股票代码字符串，不再走顺序 ID
    if (m_liveMarketView && !m_liveMarketView->symbolStrings().empty()) {
        const auto& symbols = m_liveMarketView->symbolStrings();
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
        INTERNAL_INFO_STREAM << "[RFS] setLiveMarketView: 已重建符号解析器 (" << symbols.size() << " 只标的)";
    } else {
        INTERNAL_INFO_STREAM << "[RFS] setLiveMarketView: view=" << static_cast<const void*>(m_liveMarketView.get()) << " (无标的符号串可用)";
    }
}

void RuntimeFactorSvc::setDataService(factor::compute::BacktestDataService* svc) {
    m_dataSvc = svc;
    m_engine->setDataService(svc);
    // 统一因子值管线入口: 缓存 Arrow 视图 (backtestValuesBySymbol 经 FactorValuePipeline 分块计算)
    m_arrowView = (svc && svc->getView())
        ? static_cast<const factor::compute::ArrowMarketDataView*>(svc->getView())
        : nullptr;
    INTERNAL_INFO_STREAM << "[RFS] setDataService: svc=" << static_cast<void*>(svc)
                         << " arrowView=" << static_cast<const void*>(m_arrowView);

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
                INTERNAL_INFO_STREAM << "[RFS] setDataService: 已重建符号解析器 (" << symbols.size() << " 只标的)";
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
    if (!m_dataSvc || !m_engine || !m_arrowView) return nullptr;
    auto& cache = activeTier();
    // 与 getValues 回测分支共享同一份缓存: 首次访问经统一因子值管线全量计算
    if (cache.find(instanceId) == cache.end()) {
        // 统一管线 — 与因子回测同一份计算实现 (回看/分块/DB补齐全部内聚在管线内)
        const std::vector<std::string> factorIds{instanceId};
        factor::compute::FactorValuePipeline pipeline(*m_engine, *m_dataSvc);
        pipeline.run(*m_arrowView, m_arrowView->dates(), factorIds, /*forwardDays=*/0,
            [&](const factor::compute::FactorValuePipeline::ChunkOutput& out) {
                auto it = out.perFactorValues->find(instanceId);
                if (it == out.perFactorValues->end()) return;
                for (const auto& [dateStr, symValues] : it->second) {
                    auto& dst = cache[instanceId][dateStr];
                    for (const auto& [sym, val] : symValues)
                        dst[sym] = val;
                }
            });
        INTERNAL_INFO_STREAM << "[RFS] backtestValuesBySymbol 缓存已填充: id=" << instanceId
                             << " 日期数=" << cache[instanceId].size();
    }
    char dateBuf[16];
    foundation::utils::formatTradingDayTo(date, dateBuf, sizeof(dateBuf));
    const auto it = cache[instanceId].find(dateBuf);
    return it != cache[instanceId].end() ? &it->second : nullptr;
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
    // P3 发布顺序: 先持有后发布 — shared_ptr 构造完成即所有权确定,
    // setLiveMarketView 发布与持有同一步, 消除旧实现 "先发布裸指针后入库" 的间隙
    std::shared_ptr<factor::compute::CachedMarketDataView> view =
        factor::compute::CachedMarketDataView::fromSqlRows(rows, extraFields);
    setLiveMarketView(std::move(view));
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

    const std::string dateKey = foundation::utils::formatTradingDay(date);
    char dateBuf[16];
    foundation::utils::formatTradingDayTo(date, dateBuf, sizeof(dateBuf));

    // ── 回测: 委托 backtestValuesBySymbol (统一因子值管线, 活跃周期分区; 回测恒 Daily) ──
    if (m_dataSvc) {
        INTERNAL_INFO_STREAM << "[RFS] getValues BACKTEST: instance=" << instanceId << " 日期=" << dateBuf << " 标的=" << symbolStrList.size();
        const std::map<std::string, double>* dateValues = backtestValuesBySymbol(instanceId, date);
        if (dateValues) {
            for (const auto& [sym, val] : *dateValues) {
                // 缓存 key 可能带后缀 (如 "000001.SZ"), 统一去掉后缀再匹配
                std::string codeOnly = foundation::market::AStockSymbol::codeOnly(sym);
                for (uint32_t id : symbolIds) {
                    std::string resolved = foundation::market::AStockSymbol::codeOnly(m_symbolResolver(id));
                    if (resolved == codeOnly) { result[id] = val; break; }
                }
            }
        } else {
            auto& cache = activeTier();
            const auto cacheIt = cache.find(instanceId);
            INTERNAL_WARN_STREAM << "[RFS] getValues BACKTEST: date " << dateBuf << " 在缓存中未找到 (缓存有 "
                << ((cacheIt != cache.end()) ? cacheIt->second.size() : 0) << " 条日期)";
            // 打印缓存中随机一个日期的前5个key，确认格式
            if (cacheIt != cache.end() && !cacheIt->second.empty()) {
                const auto& sampleDate = cacheIt->second.begin()->second;
                std::ostringstream sampleOss;
                sampleOss << "[RFS]   缓存样本键: ";
                int n=0; for (const auto& [k,v] : sampleDate) { if (++n>5) break; sampleOss << k << " "; }
                INTERNAL_WARN_STREAM << sampleOss.str();
            }
        }
        INTERNAL_INFO_STREAM << "[RFS] getValues BACKTEST result: " << result.size() << " 个值";
        return result;
    }

    // ── 实盘: 用 view 中最新日期（昨天）而不是 tick 日期（今天）做因子计算 ──
    if (m_liveMarketView) {
        const auto& dates = m_liveMarketView->dates();
        if (!dates.empty()) {
            int lastDate = dates.back().value;
            foundation::utils::formatTradingDayTo(lastDate, dateBuf, sizeof(dateBuf));
        }
        auto factorValues = m_engine->computeSingleDate(instanceId, dateBuf, symbolStrList, m_liveMarketView.get());
        for (const auto& [sym, val] : factorValues) {
            for (uint32_t id : symbolIds)
                if (m_symbolResolver(id) == sym) { result[id] = val; break; }
        }
        if (result.empty())
            INTERNAL_WARN_STREAM << "[RFS] 因子值空: id=" << instanceId << " 日期=" << dateBuf
                                 << " sym=" << (symbolStrList.empty() ? "?" : symbolStrList[0])
                                 << " viewLastDate=" << (m_liveMarketView->dates().empty() ? "none"
                                     : std::to_string(m_liveMarketView->dates().back().value));
        return result;
    }

    INTERNAL_WARN_STREAM << "[RFS] 无数据源: id=" << instanceId
                         << " dataSvc=" << static_cast<void*>(m_dataSvc)
                         << " liveView=" << static_cast<const void*>(m_liveMarketView.get());
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

        auto& cache = activeTier();
        size_t bShareSkipped = 0;
        for (const auto& iid : instanceIds) {
            auto cacheIt = cache.find(iid);
            if (cacheIt == cache.end()) continue;
            auto dateIt = cacheIt->second.find(dateBuf);
            if (dateIt == cacheIt->second.end()) continue;

            size_t matched = 0;
            double sampleVal = 0.0;
            for (const auto& [sym, val] : dateIt->second) {
                if (foundation::market::AStockSymbol::isBShareSymbol(sym)) {  // B股不进截面快照
                    ++bShareSkipped;
                    continue;
                }
                auto idIt = symbolToId.find(sym);
                if (idIt == symbolToId.end()) continue;
                if (matched == 0) sampleVal = val;
                ++matched;
                output.push_back(RuntimeFactorSnapshot{ idIt->second, iid, val, 1 });
            }
            static std::atomic<int> diag{0};
            if (diag.fetch_add(1, std::memory_order_relaxed) < 3)
                INTERNAL_INFO_STREAM << "[RFS] copySnapshots 缓存读取: iid=" << iid
                                     << " 日期=" << dateBuf
                                     << " 缓存条目=" << dateIt->second.size()
                                     << " matched=" << matched
                                     << " sampleVal=" << sampleVal;
        }
        if (bShareSkipped > 0)
            INTERNAL_DEBUG_STREAM << "[RFS] copySnapshots B股剔除: " << bShareSkipped << " 标的";
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

    auto& cache = activeTier();
    size_t bShareSkipped = 0;
    for (const auto& iid : instanceIds) {
        // ── 缓存读: 首次计算后后续 step() 调用直接读缓存 ──
        auto cacheIt = cache.find(iid);
        if (cacheIt != cache.end()) {
            auto dateIt = cacheIt->second.find(std::string(dateBuf));
            if (dateIt != cacheIt->second.end()) {
                for (const auto& [sym, val] : dateIt->second) {
                    if (foundation::market::AStockSymbol::isBShareSymbol(sym)) {  // B股不进截面快照
                        ++bShareSkipped;
                        continue;
                    }
                    auto idIt = codeOnlyToId.find(sym);
                    if (idIt != codeOnlyToId.end())
                        output.push_back(RuntimeFactorSnapshot{ idIt->second, iid, val, 1 });
                }
                static std::atomic<int> cacheDiag{0};
                if (cacheDiag.fetch_add(1, std::memory_order_relaxed) < 3)
                    INTERNAL_INFO_STREAM << "[RFS] copySnapshots 实时缓存: iid=" << iid
                                         << " 日期=" << dateBuf
                                         << " 条目=" << dateIt->second.size();
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
            if (foundation::market::AStockSymbol::isBShareSymbol(resolved)) {  // B股不进全截面计算 (Z-score 统计零污染)
                ++bShareSkipped;
                continue;
            }
            symbolStrs.push_back(std::move(resolved));  // 保留完整 symbol, 不去后缀
        }

        auto factorValues = self->m_engine->computeSingleDate(
            iid, std::string(dateBuf), symbolStrs, m_liveMarketView.get());

        // 写入缓存: key 用无后缀码, 与 codeOnlyToId 一致
        std::map<std::string, double> dateCache;
        for (const auto& [sym, val] : factorValues)
            dateCache[stripSuffix(sym)] = val;
        cache[iid][std::string(dateBuf)] = std::move(dateCache);

        // 输出快照: 通过 fullSymbol 查找 instrumentId
        for (const auto& [sym, val] : factorValues) {
            auto idIt = fullSymToId.find(sym);
            if (idIt != fullSymToId.end())
                output.push_back(RuntimeFactorSnapshot{ idIt->second, iid, val, 1 });
        }

        static std::atomic<int> computeDiag{0};
        if (computeDiag.fetch_add(1, std::memory_order_relaxed) < 3)
            INTERNAL_INFO_STREAM << "[RFS] copySnapshots 实时计算: iid=" << iid
                                 << " 日期=" << dateBuf
                                 << " computed=" << factorValues.size()
                                 << " totalSyms=" << symbolStrs.size();
    }
    if (bShareSkipped > 0)
        INTERNAL_DEBUG_STREAM << "[RFS] copySnapshots B股剔除: " << bShareSkipped << " 标的";
}

// ── preflight P2 探测: 评估日因子全截面是否有有限值 (§9, 终审 4.1) ──

bool RuntimeFactorSvc::probeFactorCrossSection(
    const std::vector<std::string>& symbols, std::int32_t tradingDayInt) const
{
    // 非因子策略: 无因子实例 → 无因子依赖, 探测通过
    if (m_factorIds.empty()) return true;
    if (!m_liveMarketView || !m_engine) return false;

    char dateBuf[16];
    foundation::utils::formatTradingDayTo(tradingDayInt, dateBuf, sizeof(dateBuf));

    // 完整 symbol (copySnapshots 实盘计算路径口径: 去后缀会导致 findSymbolIndex 失败)
    std::vector<std::string> fullSymbols;
    fullSymbols.reserve(symbols.size());
    for (const auto& sym : symbols)
        if (!sym.empty()) fullSymbols.push_back(sym);
    if (fullSymbols.empty()) return false;

    auto* self = const_cast<RuntimeFactorSvc*>(this);

    // 全截面判定: 任一因子实例在评估日存在 >=1 个有限值 → 通过
    auto& cache = activeTier();
    for (const auto& iid : m_factorIds) {
        if (iid.empty()) continue;
        // 缓存已命中 (copySnapshots 已算过): 只读判定
        auto cacheIt = cache.find(iid);
        if (cacheIt != cache.end()) {
            auto dateIt = cacheIt->second.find(std::string(dateBuf));
            if (dateIt != cacheIt->second.end()) {
                for (const auto& [sym, val] : dateIt->second)
                    if (std::isfinite(val)) return true;
                continue;  // 该实例当日全空, 换下一实例
            }
        }
        // 纯计算 (不写缓存, 无副作用)
        auto values = self->computeFactor(iid, std::string(dateBuf), fullSymbols);
        for (const auto& [sym, val] : values)
            if (std::isfinite(val)) return true;
    }
    INTERNAL_WARN_STREAM << "[RFS] probe: 因子全截面无有限值: 实例数=" << m_factorIds.size()
                         << " 评估日=" << dateBuf << " 标的=" << fullSymbols.size();
    return false;
}

std::unordered_map<std::string, double> RuntimeFactorSvc::computeFactor(
    const std::string& instanceId, const std::string& dateBuf,
    const std::vector<std::string>& fullSymbols)
{
    if (!m_engine || !m_liveMarketView) return {};
    return m_engine->computeSingleDate(instanceId, dateBuf, fullSymbols, m_liveMarketView.get());
}

void RuntimeFactorSvc::warmUpCache(const std::string& strategyId, BarPeriod period,
                                   const std::vector<std::string>& symbols)
{
    // 纯预热: 视图末行(锚点日)全因子全截面计算并写入 period 分区, copySnapshots 后续直接命中
    // 终审 4.1 + C11 (P3 接线): 由调度器 start() 经 setWarmUpFn 显式调用, 不做引擎全局预热
    if (m_factorIds.empty() || !m_liveMarketView || !m_engine) return;
    const auto& dates = m_liveMarketView->dates();
    if (dates.empty()) return;
    char dateBuf[16];
    foundation::utils::formatTradingDayTo(dates.back().value, dateBuf, sizeof(dateBuf));

    std::vector<std::string> fullSymbols;
    fullSymbols.reserve(symbols.size());
    for (const auto& sym : symbols)
        if (!sym.empty() && !foundation::market::AStockSymbol::isBShareSymbol(sym))  // 入口统一过滤: B股不进预热缓存
            fullSymbols.push_back(sym);
    if (fullSymbols.empty()) return;

    auto& cache = tier(period);  // ADR-004: 按 period 物理分区, 跨频零共享
    for (const auto& iid : m_factorIds) {
        if (iid.empty()) continue;
        auto values = computeFactor(iid, std::string(dateBuf), fullSymbols);
        std::map<std::string, double> dateCache;
        for (const auto& [sym, val] : values)
            dateCache[foundation::market::AStockSymbol::codeOnly(sym)] = val;
        cache[iid][std::string(dateBuf)] = std::move(dateCache);
    }
    INTERNAL_INFO_STREAM << "[RFS] warmUpCache: strategyId=" << strategyId
                         << " period=" << BarPeriodNaming::suffix(period)
                         << " 实例数=" << m_factorIds.size()
                         << " 锚点=" << dateBuf << " 标的=" << fullSymbols.size();
}

} // namespace domain::strategy
