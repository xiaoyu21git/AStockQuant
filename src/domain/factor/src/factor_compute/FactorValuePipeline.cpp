#include "factor_compute/FactorValuePipeline.h"
#include "factor_compute/ArrowMarketDataView.h"
#include "factor_compute/DlExecutionGuard.h"
#include "factor_compute/FactorWorkerContext.h"
#include "factor_compute/MemoryBudgetGuard.h"
#include "FactorInstanceManager.h"
#include "BaseFactor.h"
#include "../../../../domain/cleaning/include/DataSourceRegistry.h"
#include "database/MarketDataRepository.h"
#include "database/NativePgConnectionPool.h"
#include "foundation/Utils/DateUtils.h"
#include "foundation/log/logging.hpp"
#include "foundation/perf/Stopwatch.h"
#include "foundation/thread/CancellationGuard.h"
#include "foundation/thread/ThreadPoolExecutor.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_set>

namespace factor::compute {

namespace {

/// @brief 分块大小 (交易日) — 内存上界 = 块交易日 × 标的数 × 列数 × 4B
constexpr int kChunkDates = 60;

/// @brief 块级并行总内存预算 (用户裁定 1GB) — 在飞块数上限经 MemoryBudgetGuard 收紧
constexpr std::size_t kTotalBudgetBytes = 1024ULL * 1024ULL * 1024ULL;

// ══════════════════════════════════════════════════════════════════════════════
// 块级并行数据载体 (纯数据传输, 无行为 — 对齐 ChunkOutput/MarketMatrixBatch 先例)
// ══════════════════════════════════════════════════════════════════════════════

/// @brief 块日期规划 (串行/并行共用同一规划 — 保证两路径视图布局位级一致)
struct ChunkPlan {
    std::vector<DateKey> chunkDates;        // 本块交易日 (不含回看/尾扩展)
    std::vector<DateKey> tailDates;         // 尾扩展交易日 (IC 前向收益用)
    int warmupDays = 0;                     // 首块 = maxLookback, 块 N>0 = 0
    std::vector<DateKey> arrowPrefixDates;  // 块 N>0 数据集内回看行
};

/// @brief 块计算结果 (worker 生产 → 主线程按块序 sink; 视图析构即内存归还)
struct ChunkResult {
    std::size_t chunkIndex = 0;
    std::size_t chunkBytes = 0;             // 提交时主线程预算记账值 (归还时对冲)
    std::vector<DateKey> chunkDates;
    std::vector<DateKey> tailDates;
    std::unique_ptr<IMarketDataView> chunkView;
    std::unordered_map<std::string, FactorValuePipeline::FactorValuesByDate> perFactorValues;
    bool skipped = false;                   // 构建失败防御路径 (正常不可达): 不 sink
    bool cancelled = false;                 // 软取消: 不 sink, 主线程丢弃
    std::exception_ptr error;               // 非取消异常: 主线程排水后重抛 (禁止吞)
    double buildMs = 0.0;
    double computeMs = 0.0;
    std::map<std::string, double> factorMs;
};

/// @brief 块日期规划 — 回看行: 首块 = 数据集起始前 DB 回看; 块 N>0 = 数据集内 Arrow 前缀
/// (块 N>0 缺回看行时 getSeries 起点被钳到块首 → 每块开头 maxLookback 日空洞)
ChunkPlan planChunk(const std::vector<DateKey>& dates, std::size_t ci,
                    int fwdDays, int maxLookback, std::size_t totalDates,
                    std::size_t totalChunks)
{
    ChunkPlan plan;
    const std::size_t chunkStart = ci * static_cast<std::size_t>(kChunkDates);
    const std::size_t ownEnd = std::min(chunkStart + static_cast<std::size_t>(kChunkDates),
                                        totalDates);
    const std::size_t tailEnd = std::min(ownEnd + static_cast<std::size_t>(fwdDays),
                                         totalDates);
    plan.chunkDates.assign(dates.begin() + chunkStart, dates.begin() + ownEnd);
    plan.tailDates.assign(dates.begin() + ownEnd, dates.begin() + tailEnd);
    plan.warmupDays = (ci == 0 && maxLookback > 0) ? maxLookback : 0;
    if (ci > 0 && maxLookback > 0) {
        const std::size_t prefixStart =
            (chunkStart > static_cast<std::size_t>(maxLookback))
                ? chunkStart - static_cast<std::size_t>(maxLookback)
                : 0;
        plan.arrowPrefixDates.assign(dates.begin() + prefixStart,
                                     dates.begin() + chunkStart);
    }
    INTERNAL_INFO_STREAM << "[因子值管线] 块 " << ci + 1 << "/" << totalChunks
        << ": own=" << plan.chunkDates.size()
        << " tail=" << plan.tailDates.size()
        << " dbWarmup=" << plan.warmupDays
        << " arrowPrefix=" << plan.arrowPrefixDates.size()
        << " 已规划";
    return plan;
}

/// @brief 块视图行数估计 — 内存预算输入 (warmup 以 maxLookback 保守上界:
/// 实际查库交易日可能更少, 高估方向安全)
std::size_t planRowCount(const ChunkPlan& plan)
{
    return static_cast<std::size_t>(plan.warmupDays)
        + plan.arrowPrefixDates.size() + plan.chunkDates.size() + plan.tailDates.size();
}

/// @brief 本块交易日集合 (过滤 compute 对回看/尾部扩展行的产出)
std::unordered_set<std::string> buildOwnDateSet(const std::vector<DateKey>& chunkDates)
{
    std::unordered_set<std::string> ownDateSet;
    ownDateSet.reserve(chunkDates.size());
    for (const auto& d : chunkDates)
        ownDateSet.insert(foundation::utils::formatTradingDay(d.value));
    return ownDateSet;
}

/// @brief 块内因子计算 (串行/并行 worker 共用唯一实现)
/// 因子逐个调 compute — DL 因子经 DlExecutionGuard 独占槽 (进程级, 任一时刻
/// 至多一个 DL compute), 普通因子不经 Guard 直接算, 不受 DL 拖累。
/// 软取消由 computeImpl 每日期检查点抛出 OperationCancelledException, 调用方捕获。
/// @param factorMsOut 每因子累计耗时台账 (调用方合并, worker 侧传入 ChunkResult)
[[nodiscard]] std::unordered_map<std::string, FactorValuePipeline::FactorValuesByDate>
computeChunkFactors(
    FactorEngine& engine,
    FactorWorkerContext& ctx,
    const IMarketDataView* chunkView,
    const std::unordered_set<std::string>& ownDateSet,
    const std::vector<std::string>& factorIds,
    std::size_t chunkIndex,
    std::size_t computeSkip,
    const BacktestDataService::DbFallbackFn& dbFn,
    const std::atomic<bool>* cancelFlag,
    std::map<std::string, double>& factorMsOut)
{
    std::unordered_map<std::string, FactorValuePipeline::FactorValuesByDate> perFactorValues;
    MarketMatrixBatch chunkBatch;
    chunkBatch.batchIndex = chunkIndex;
    chunkBatch.marketView = chunkView;

    for (const auto& factorId : factorIds) {
        auto factor = ctx.factor(factorId);
        if (!factor) continue;

        FactorCacheKey cacheKey;
        cacheKey.factorName = factorId;

        foundation::perf::Stopwatch swFactor;
        swFactor.start();
        FactorMatrix factorResult;
        if (factor->getFactorType() == factor::FactorType::DL) {
            DlExecutionGuard dlSlot;  // DL 独占槽 (等待不可中断, 单 DL 因子场景无阻塞)
            factorResult = engine.computeWithInstance(*factor, chunkBatch, cacheKey,
                                                      computeSkip, dbFn, cancelFlag);
        } else {
            factorResult = engine.computeWithInstance(*factor, chunkBatch, cacheKey,
                                                      computeSkip, dbFn, cancelFlag);
        }
        swFactor.stop();
        factorMsOut[factorId] += swFactor.elapsedMilliseconds();

        FactorValuePipeline::FactorValuesByDate ownValues;
        for (auto& [dateStr, symValues] : factorResult.factorValues) {
            if (ownDateSet.count(dateStr))
                ownValues[dateStr] = std::move(symValues);
        }
        perFactorValues[factorId] = std::move(ownValues);
    }
    return perFactorValues;
}

/// @brief 运行台账 — build/compute/sink/每因子 块级累计 (worker 侧数值经
/// ChunkResult 传回, 主线程合并; 全程主线程单写, 无原子)
struct RunLedger {
    double buildMs = 0.0;
    double computeMs = 0.0;
    double sinkMs = 0.0;
    std::map<std::string, double> factorMs;

    void mergeChunk(const ChunkResult& r)
    {
        buildMs += r.buildMs;
        computeMs += r.computeMs;
        for (const auto& [fid, ms] : r.factorMs)
            factorMs[fid] += ms;
    }

    void report() const
    {
        INTERNAL_INFO_STREAM << "[PERF] 管线 build 累计: " << buildMs << " ms";
        INTERNAL_INFO_STREAM << "[PERF] 管线 compute 累计: " << computeMs << " ms";
        INTERNAL_INFO_STREAM << "[PERF] 管线 sink 累计: " << sinkMs << " ms";
        for (const auto& [fid, ms] : factorMs)
            INTERNAL_INFO_STREAM << "[PERF] compute<" << fid << "> 累计: " << ms << " ms";
    }
};

/// @brief 主线程按块序 sink 一块 (串行/并行共用; sink 返回前块视图保持有效)
void sinkChunk(const ChunkResult& r, const FactorValuePipeline::ChunkSink& sink,
               RunLedger& ledger)
{
    FactorValuePipeline::ChunkOutput out;
    out.chunkIndex = r.chunkIndex;
    out.chunkDates = &r.chunkDates;
    out.tailDates = &r.tailDates;
    out.chunkView = r.chunkView.get();
    out.perFactorValues = &r.perFactorValues;
    ledger.mergeChunk(r);
    sink(out);
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════════
// WarmupDataLoader
// ══════════════════════════════════════════════════════════════════════════════

WarmupDataLoader::WarmupDataLoader(std::int32_t firstDateVal, int maxLookback)
    : m_dbCache(std::make_shared<CacheMap>())
{
    // minReportDate: 数据集首日 - (回看+60) 日历日, 覆盖周末节假日与财报公告滞后
    // cacheStartDate: 数据集首日 — DB 只拉到此处为止, 之后的日期由数据集本身提供
    m_minReportDate = foundation::utils::formatTradingDay(
        foundation::utils::backScrollCalendarDays(firstDateVal, maxLookback + 60));
    m_cacheStartDate = foundation::utils::formatTradingDay(firstDateVal);
}

WarmupDataLoader::FieldClass WarmupDataLoader::classifyField(const std::string& field)
{
    static const std::unordered_set<std::string> kSymInfoSet(
        cleaning::symbol_info_columns::names().begin(),
        cleaning::symbol_info_columns::names().end());
    static const std::unordered_set<std::string> kFinSet(
        cleaning::financial_columns::names().begin(),
        cleaning::financial_columns::names().end());
    static const std::unordered_set<std::string> kKlineSet(
        cleaning::kline_columns::names().begin(),
        cleaning::kline_columns::names().end());
    static const std::unordered_set<std::string> kIndexSet(
        cleaning::index_columns::names().begin(),
        cleaning::index_columns::names().end());
    static const std::unordered_set<std::string> kMinuteSet(
        cleaning::minute_daily_columns::names().begin(),
        cleaning::minute_daily_columns::names().end());

    if (kSymInfoSet.count(field)) return FieldClass::SymbolInfo;
    if (kFinSet.count(field))    return FieldClass::Financial;
    if (kKlineSet.count(field))  return FieldClass::Kline;
    if (kIndexSet.count(field))  return FieldClass::Index;
    if (kMinuteSet.count(field)) return FieldClass::Minute;
    return FieldClass::Unknown;
}

void WarmupDataLoader::ensureFieldLoaded(const std::string& field,
                                         const std::vector<std::string>& symbols)
{
    // 快速路径: 已加载 → 共享锁直接返回 (并行 worker 运行期兜底高频命中)
    {
        std::shared_lock<std::shared_mutex> readLock(m_cacheMutex);
        auto it = m_dbCache->find(field);
        if (it != m_dbCache->end() && it->second.count("__loaded__")) return;
    }
    // 慢路径: 写锁 + 双检 — PG 查询被写锁自然串行化 (并发兜底安全, 无需额外 Semaphore)
    std::unique_lock<std::shared_mutex> writeLock(m_cacheMutex);
    auto& fieldCache = (*m_dbCache)[field];
    if (fieldCache.count("__loaded__")) return;

    const FieldClass fc = classifyField(field);
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return;
    astock::infrastructure::database::MarketDataRepository repo(db);

    switch (fc) {
    case FieldClass::SymbolInfo:
    case FieldClass::Index: {
        auto rows = db->executeQuery(
            "SELECT s.symbol, s." + field + " FROM ref.symbol_info s");
        for (std::size_t i = 0; i < rows.rowCount(); ++i) {
            auto row = rows.getRow(i);
            std::string sym = row.getString("symbol");
            double val = row.getDouble(field);
            if (!sym.empty() && std::isfinite(val))
                fieldCache[sym]["_"] = val;
        }
        break;
    }
    case FieldClass::Financial: {
        auto rows = repo.queryFinancialFieldAllReports(field, m_minReportDate, m_cacheStartDate, symbols);
        for (const auto& r : rows)
            fieldCache[r.symbol][r.tradeDate] = r.value;
        break;
    }
    case FieldClass::Kline: {
        auto rows = repo.queryFieldCrossSectionRange(field, m_minReportDate, m_cacheStartDate, symbols);
        for (const auto& r : rows)
            fieldCache[r.symbol][r.tradeDate] = r.value;
        break;
    }
    case FieldClass::Minute: {
        auto rows = repo.queryMinuteDailyAgg(symbols, m_minReportDate, m_cacheStartDate);
        for (const auto& mr : rows) {
            const auto& mv = mr.getValues();
            auto si = mv.find("symbol");
            auto ti = mv.find("trade_date");
            auto fi = mv.find(field);
            if (si != mv.end() && ti != mv.end() && fi != mv.end())
                fieldCache[si->second][ti->second] = std::stod(fi->second);
        }
        break;
    }
    case FieldClass::Unknown:
        INTERNAL_WARN_STREAM << "[DB查库] 未知字段 '" << field
            << "' — 不在 kline/symbol_info/financial/minute_daily 中";
        break;
    }
    fieldCache["__loaded__"]["_"] = 1.0;
}

std::unordered_map<std::string, double> WarmupDataLoader::lookupField(
    const std::string& date, const std::string& field,
    const std::vector<std::string>& symbols) const
{
    std::unordered_map<std::string, double> result;
    // 读共享锁 — 与 ensureFieldLoaded 写独占锁配对 (缓存写后 __loaded__ 标记保证可见)
    std::shared_lock<std::shared_mutex> readLock(m_cacheMutex);
    auto cacheIt = m_dbCache->find(field);
    if (cacheIt == m_dbCache->end()) return result;
    const auto& fieldCache = cacheIt->second;

    const FieldClass fc = classifyField(field);
    const bool isStatic = (fc == FieldClass::SymbolInfo || fc == FieldClass::Index);
    const bool isFinLookup = (fc == FieldClass::Financial);

    for (const auto& sym : symbols) {
        auto si = fieldCache.find(sym);
        if (si == fieldCache.end()) continue;
        if (isStatic) {
            auto it = si->second.find("_");
            if (it != si->second.end()) result[sym] = it->second;
        } else if (isFinLookup) {
            auto it = si->second.upper_bound(date);
            if (it != si->second.begin()) { --it; result[sym] = it->second; }
        } else {
            auto it = si->second.find(date);
            if (it != si->second.end()) result[sym] = it->second;
        }
    }
    return result;
}

BacktestDataService::DbFallbackFn WarmupDataLoader::dbFallbackFn()
{
    std::shared_ptr<WarmupDataLoader> self = shared_from_this();
    return [self](const std::string& date, const std::string& field,
                  const std::vector<std::string>& symbols)
        -> std::unordered_map<std::string, double> {
        self->ensureFieldLoaded(field, symbols);
        return self->lookupField(date, field, symbols);
    };
}

// ══════════════════════════════════════════════════════════════════════════════
// WarmupViewBuilder
// ══════════════════════════════════════════════════════════════════════════════

WarmupViewBuilder::WarmupViewBuilder(const ArrowMarketDataView& arrowView,
                                     std::shared_ptr<WarmupDataLoader> loader)
    : m_arrowView(arrowView)
    , m_loader(std::move(loader))
{}

std::vector<DateKey> WarmupViewBuilder::queryWarmupDates(
    std::int32_t firstDateVal, int warmupDays) const
{
    std::vector<DateKey> warmupDates;
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return warmupDates;
    astock::infrastructure::database::MarketDataRepository repo(db);

    // 查询下界: 往前回滚 2×warmupDays 个日历日, 确保日历区间覆盖足够交易日
    const int lowerBound = foundation::utils::backScrollCalendarDays(firstDateVal, warmupDays * 2);
    const auto td = repo.queryTradeCalendar(
        foundation::utils::formatTradingDay(lowerBound),
        foundation::utils::formatTradingDay(firstDateVal));
    size_t start = (td.size() > static_cast<size_t>(warmupDays))
        ? (td.size() - static_cast<size_t>(warmupDays)) : 0;
    for (size_t i = start; i < td.size(); ++i)
        warmupDates.push_back(DateKey{foundation::utils::parseTradingDay(td[i])});
    return warmupDates;
}

void WarmupViewBuilder::overwriteWarmupRows(
    IMarketDataView& view,
    const std::vector<DateKey>& warmupDates,
    const std::vector<std::string>& fields) const
{
    const auto& dbFn = m_loader->dbFallbackFn();
    const auto syms = m_arrowView.symbolStrings();
    const int32_t nInsts = static_cast<int32_t>(syms.size());
    size_t totalCells = 0;
    for (size_t wi = 0; wi < warmupDates.size(); ++wi) {
        const std::string dateStr = foundation::utils::formatTradingDay(warmupDates[wi].value);
        for (const auto& col : fields) {
            auto* data = view.mutableFieldData(col);
            if (!data) continue;
            auto dbRes = dbFn(dateStr, col,
                std::vector<std::string>(syms.begin(), syms.end()));
            for (int32_t si = 0; si < nInsts; ++si) {
                auto it = dbRes.find(syms[static_cast<size_t>(si)]);
                double v = (it != dbRes.end()) ? it->second
                    : std::numeric_limits<double>::quiet_NaN();
                data[wi * static_cast<size_t>(nInsts) + static_cast<size_t>(si)]
                    = static_cast<signal_value_t>(v);
                if (std::isfinite(v)) ++totalCells;
            }
        }
    }
    INTERNAL_INFO_STREAM << "[DB补数据] 查库结束: dates=" << warmupDates.size()
        << " fields=" << fields.size()
        << " symbols=" << nInsts
        << " validCells=" << totalCells;
}

std::unique_ptr<IMarketDataView> WarmupViewBuilder::build(
    const std::vector<DateKey>& dates,
    const std::vector<DateKey>& tailDates,
    const std::vector<std::string>& fields,
    int warmupDays,
    const std::vector<DateKey>& arrowPrefixDates,
    std::size_t& prefixRowCountOut,
    const ArrowReaderHandle* readerHandle) const
{
    prefixRowCountOut = 0;
    if (dates.empty()) return nullptr;

    // ── 回看日期: 从目标首日往前, data.trade_calendar 查 warmupDays 个交易日 ──
    std::vector<DateKey> warmupDates;
    if (warmupDays > 0)
        warmupDates = queryWarmupDates(dates.front().value, warmupDays);
    prefixRowCountOut = warmupDates.size() + arrowPrefixDates.size();

    // ── 行布局: [DB回看行][Arrow前缀回看行][本块][尾扩展] ──
    auto extendedDates = warmupDates;
    extendedDates.insert(extendedDates.end(),
                         arrowPrefixDates.begin(), arrowPrefixDates.end());
    extendedDates.insert(extendedDates.end(), dates.begin(), dates.end());
    extendedDates.insert(extendedDates.end(), tailDates.begin(), tailDates.end());

    // readerHandle 非空 = 并行 worker 独立句柄路径; null = 串行共享 reader (主线程)
    auto chunkView = readerHandle
        ? m_arrowView.makeChunkViewConcurrent(extendedDates, fields, *readerHandle)
        : m_arrowView.makeChunkView(extendedDates, fields);
    if (!chunkView || warmupDates.empty()) return chunkView;

    // ── 回看行在数据集中不存在 (NaN) → 从 DB 逐日逐列覆写 ──
    foundation::perf::Stopwatch swWarm;
    swWarm.start();
    overwriteWarmupRows(*chunkView, warmupDates, fields);
    swWarm.stop();
    swWarm.report("DB 回看覆写");
    return chunkView;
}

// ══════════════════════════════════════════════════════════════════════════════
// FactorValuePipeline
// ══════════════════════════════════════════════════════════════════════════════

FactorValuePipeline::FactorValuePipeline(FactorEngine& engine,
                                         BacktestDataService& dataSvc)
    : m_engine(engine)
    , m_dataSvc(dataSvc)
{}

void FactorValuePipeline::collectFieldRequirements(
    const std::vector<std::string>& factorIds,
    std::vector<std::string>& extraFieldsOut,
    int& maxLookbackOut) const
{
    extraFieldsOut.clear();
    maxLookbackOut = 0;
    if (!m_engine.instanceManager()) return;
    for (const auto& fid : factorIds) {
        auto factor = m_engine.instanceManager()->createInstance(fid);
        if (!factor) continue;
        for (const auto& f : factor->getDataRequirements().requiredFields)
            extraFieldsOut.push_back(f);
        for (const auto& f : factor->getDataRequirements().optionalFields)
            extraFieldsOut.push_back(f);
        int lb = factor->getLookbackDays();
        if (lb > maxLookbackOut) maxLookbackOut = lb;
    }
    std::sort(extraFieldsOut.begin(), extraFieldsOut.end());
    extraFieldsOut.erase(
        std::unique(extraFieldsOut.begin(), extraFieldsOut.end()),
        extraFieldsOut.end());
    extraFieldsOut.erase(
        std::remove_if(extraFieldsOut.begin(), extraFieldsOut.end(),
            [](const std::string& f) {
                return f == "open" || f == "high" || f == "low" || f == "close"
                    || f == "volume" || f == "symbol" || f == "trade_date";
            }),
        extraFieldsOut.end());
}

std::vector<std::string> FactorValuePipeline::buildChunkColumns(
    const std::vector<std::string>& extraFields)
{
    std::vector<std::string> chunkColumns = {"open", "high", "low", "close", "volume"};
    for (const auto& f : extraFields)
        chunkColumns.push_back(f);
    for (const auto& f : factor::BaseFactor::neutralizationFields())
        if (std::find(chunkColumns.begin(), chunkColumns.end(), f) == chunkColumns.end())
            chunkColumns.push_back(f);
    return chunkColumns;
}

void FactorValuePipeline::run(
    const ArrowMarketDataView& arrowView,
    const std::vector<DateKey>& dates,
    const std::vector<std::string>& factorIds,
    int forwardDays,
    ChunkSink sink,
    ProgressFn onProgress,
    int workerThreads,
    const std::atomic<bool>* cancelFlag)
{
    if (dates.empty() || factorIds.empty() || !sink) return;
    if (!m_engine.instanceManager()) {
        INTERNAL_ERROR_STREAM << "[因子值管线] 中止: instanceManager 为空 (无因子实例可创建)";
        return;
    }

    // ── 收集因子所需额外字段 + 最大回看交易日 ──
    std::vector<std::string> neededExtraFields;
    int maxLookback = 0;
    collectFieldRequirements(factorIds, neededExtraFields, maxLookback);
    const std::vector<std::string> chunkColumns = buildChunkColumns(neededExtraFields);
    {
        std::ostringstream cols;
        for (size_t i = 0; i < chunkColumns.size(); ++i) {
            if (i > 0) cols << ",";
            cols << chunkColumns[i];
        }
        INTERNAL_INFO_STREAM << "[因子值管线] chunkColumns(" << chunkColumns.size() << "): " << cols.str();
    }

    const int fwdDays = std::max(0, forwardDays);
    const size_t totalDates = dates.size();
    const size_t totalChunks = (totalDates + kChunkDates - 1) / kChunkDates;

    // ── 主线程预载: 全部 chunkColumns 字段一次性查库 ──
    // 与旧流程同区间同查询 (旧: 块0 DB回看覆写 + compute dbFallback 首命中懒加载),
    // 预载后运行期 dbFallback 纯内存命中; 并行 worker 并发兜底由 ensureFieldLoaded
    // 内 shared_mutex 双检串行化。dbFallback 不再挂接 m_dataSvc 全局状态,
    // 改经 compute 参数显式传给块任务 (compute 优先参数, 空才回退 m_dataSvc)。
    auto loader = std::make_shared<WarmupDataLoader>(dates.front().value, maxLookback);
    const auto& allSymbols = arrowView.symbolStrings();
    foundation::perf::Stopwatch swPreload;
    swPreload.start();
    for (const auto& col : chunkColumns)
        loader->ensureFieldLoaded(col, allSymbols);
    swPreload.stop();
    swPreload.report("字段预载 (PG)");

    const BacktestDataService::DbFallbackFn dbFn = loader->dbFallbackFn();
    WarmupViewBuilder viewBuilder(arrowView, loader);
    RunLedger ledger;

    const bool useParallel = (workerThreads >= 2 && totalChunks >= 2);

    if (!useParallel) {
        // ── 串行路径 (workerThreads<2 或块数<2) — 与并行路径同一套实现 ──
        FactorWorkerContext ctx(*m_engine.instanceManager(), arrowView);
        foundation::thread::CancellationGuard mainCancelGuard(cancelFlag);

        for (std::size_t ci = 0; ci < totalChunks; ++ci) {
            ChunkPlan plan = planChunk(dates, ci, fwdDays, maxLookback, totalDates, totalChunks);
            if (plan.chunkDates.empty()) continue;
            const auto ownDateSet = buildOwnDateSet(plan.chunkDates);

            ChunkResult r;
            r.chunkIndex = ci;
            r.chunkDates = std::move(plan.chunkDates);
            r.tailDates = std::move(plan.tailDates);

            std::size_t prefixRowCount = 0;
            foundation::perf::Stopwatch swChunkBuild;
            swChunkBuild.start();
            r.chunkView = viewBuilder.build(r.chunkDates, r.tailDates, chunkColumns,
                                            plan.warmupDays, plan.arrowPrefixDates,
                                            prefixRowCount);
            swChunkBuild.stop();
            r.buildMs = swChunkBuild.elapsedMilliseconds();
            if (!r.chunkView) continue;

            foundation::perf::Stopwatch swChunkCompute;
            swChunkCompute.start();
            try {
                r.perFactorValues = computeChunkFactors(
                    m_engine, ctx, r.chunkView.get(), ownDateSet, factorIds,
                    ci, prefixRowCount, dbFn, cancelFlag, r.factorMs);
            } catch (const foundation::thread::OperationCancelledException&) {
                break;   // 软取消: 丢弃未 sink 块提前返回 (结果不发布, orchestrator 检查 cancelFlag)
            }
            swChunkCompute.stop();
            r.computeMs = swChunkCompute.elapsedMilliseconds();
            if (mainCancelGuard.isCancelled()) break;

            foundation::perf::Stopwatch swChunkSink;
            swChunkSink.start();
            sinkChunk(r, sink, ledger);
            swChunkSink.stop();
            ledger.sinkMs += swChunkSink.elapsedMilliseconds();
            if (onProgress) {
                onProgress(static_cast<double>(ci + 1) / static_cast<double>(totalChunks),
                           "chunk " + std::to_string(ci + 1) + "/" + std::to_string(totalChunks));
            }
        }
        ledger.report();
        INTERNAL_INFO_STREAM << "[因子值管线] 完成: totalChunks=" << totalChunks;
        return;
    }

    // ── 并行路径: 块级并行 — 提交 (预算预扣) 与排水 (sink 归还预算) 均主线程,
    //    交织推进无竞争; 在飞块数 ≤ min(workerThreads, 预算/最大单块) 保证
    //    每在飞块必有 worker 且在 1GB 预算内 ──
    MemoryBudgetGuard budgetGuard(allSymbols.size(), chunkColumns);
    const std::size_t worstRows = static_cast<std::size_t>(maxLookback)
        + static_cast<std::size_t>(kChunkDates) + static_cast<std::size_t>(fwdDays);
    const std::size_t maxChunkBytes = budgetGuard.chunkBytes(worstRows);
    const int maxInFlight = budgetGuard.getMaxInFlight(workerThreads, kTotalBudgetBytes, maxChunkBytes);
    INTERNAL_INFO_STREAM << "[内存预算] 单块上界=" << (maxChunkBytes >> 20)
        << "MB maxInFlight=" << maxInFlight << "/" << workerThreads;

    foundation::thread::ThreadPoolExecutor pool(static_cast<std::size_t>(workerThreads));

    std::mutex readyMutex;
    std::condition_variable readyCv;
    std::map<std::size_t, std::shared_ptr<ChunkResult>> readyQueue;  // 按块序就绪结果

    std::size_t nextSubmit = 0;      // 下一个待提交块
    std::size_t nextSink = 0;        // 下一个待 sink 块 (严格升序)
    std::size_t inFlightCount = 0;   // 已提交未 sink (预算记账, 仅主线程读写)
    std::size_t inFlightBytes = 0;
    std::size_t peakInFlightBytes = 0;
    std::size_t sunkCount = 0;
    bool terminate = false;          // 取消或失败: 停止提交, 丢弃未 sink 块
    std::exception_ptr failure;

    foundation::thread::CancellationGuard mainCancelGuard(cancelFlag);

    // worker 块任务: 建视图 + 块内因子计算 + 结果入队。
    // 异常全部落入 result 字段不外抛 (取消/失败由主线程统一处置)
    auto chunkTask = [&](std::size_t ci, ChunkPlan plan,
                         std::unordered_set<std::string> ownDateSet,
                         std::size_t chunkBytesEstimate) {
        auto result = std::make_shared<ChunkResult>();
        result->chunkIndex = ci;
        result->chunkBytes = chunkBytesEstimate;
        result->chunkDates = std::move(plan.chunkDates);
        result->tailDates = std::move(plan.tailDates);
        try {
            // thread_local 上下文: 每 pool 线程一份跨块复用 — 隔离实例记忆化缓存
            // 保温 (数值与共享实例位级一致, 仅影响速度), 独立 Arrow 读句柄并发安全
            thread_local std::shared_ptr<FactorWorkerContext> tlsCtx;
            if (!tlsCtx)
                tlsCtx = std::make_shared<FactorWorkerContext>(
                    *m_engine.instanceManager(), arrowView);

            std::size_t prefixRowCount = 0;
            foundation::perf::Stopwatch swChunkBuild;
            swChunkBuild.start();
            result->chunkView = viewBuilder.build(result->chunkDates, result->tailDates,
                                                  chunkColumns, plan.warmupDays,
                                                  plan.arrowPrefixDates, prefixRowCount,
                                                  &tlsCtx->readerHandle());
            swChunkBuild.stop();
            result->buildMs = swChunkBuild.elapsedMilliseconds();
            if (!result->chunkView) {
                result->skipped = true;   // 构建失败防御路径 (正常不可达): 不 sink
            } else {
                foundation::perf::Stopwatch swChunkCompute;
                swChunkCompute.start();
                result->perFactorValues = computeChunkFactors(
                    m_engine, *tlsCtx, result->chunkView.get(), ownDateSet,
                    factorIds, ci, prefixRowCount, dbFn, cancelFlag, result->factorMs);
                swChunkCompute.stop();
                result->computeMs = swChunkCompute.elapsedMilliseconds();
            }
        } catch (const foundation::thread::OperationCancelledException&) {
            result->cancelled = true;   // 软取消: 不 sink (其余异常落 error, 禁止吞)
        } catch (...) {
            result->error = std::current_exception();
        }
        {
            std::lock_guard<std::mutex> lock(readyMutex);
            readyQueue[ci] = std::move(result);
        }
        readyCv.notify_one();
    };

    while (true) {
        // ① 提交: 预算允许且未终止时, 预扣预算提交下一块
        while (!terminate && !mainCancelGuard.isCancelled()
               && nextSubmit < totalChunks
               && inFlightCount < static_cast<std::size_t>(maxInFlight)) {
            const std::size_t ci = nextSubmit++;
            ChunkPlan plan = planChunk(dates, ci, fwdDays, maxLookback, totalDates, totalChunks);
            if (plan.chunkDates.empty()) {
                // 防御路径 (正常不可达: chunkStart < totalDates 恒成立): 空块立即置就绪。
                // 仍计入在飞 (0 字节), 与排水侧的 --inFlightCount 对冲
                ++inFlightCount;
                auto empty = std::make_shared<ChunkResult>();
                empty->chunkIndex = ci;
                empty->skipped = true;
                {
                    std::lock_guard<std::mutex> lock(readyMutex);
                    readyQueue[ci] = std::move(empty);
                }
                readyCv.notify_one();
                continue;
            }
            const std::size_t bytes = budgetGuard.chunkBytes(planRowCount(plan));
            inFlightBytes += bytes;
            if (inFlightBytes > peakInFlightBytes) peakInFlightBytes = inFlightBytes;
            ++inFlightCount;
            auto ownDateSet = buildOwnDateSet(plan.chunkDates);
            pool.post([=]() mutable {
                chunkTask(ci, std::move(plan), std::move(ownDateSet), bytes);
            });
        }
        if (mainCancelGuard.isCancelled()) {
            terminate = true;   // 主线程取消检查点 (提交阶段)
            break;
        }

        // ② 排水: 按块序 sink 就绪块 (预算归还, 视图析构即内存释放)
        while (nextSink < nextSubmit) {
            std::shared_ptr<ChunkResult> res;
            {
                std::unique_lock<std::mutex> lock(readyMutex);
                readyCv.wait(lock, [&] {
                    return readyQueue.count(nextSink) > 0 || terminate;
                });
                if (terminate) break;
                auto it = readyQueue.find(nextSink);
                res = it->second;
                readyQueue.erase(it);
            }
            ++nextSink;
            --inFlightCount;
            inFlightBytes -= res->chunkBytes;

            if (res->error) {
                failure = res->error;   // 失败: 停提交停 sink, 终排丢弃剩余块
                terminate = true;
                break;
            }
            if (!res->cancelled && !res->skipped) {
                foundation::perf::Stopwatch swChunkSink;
                swChunkSink.start();
                sinkChunk(*res, sink, ledger);
                swChunkSink.stop();
                ledger.sinkMs += swChunkSink.elapsedMilliseconds();
                ++sunkCount;
                if (onProgress) {
                    onProgress(static_cast<double>(sunkCount) / static_cast<double>(totalChunks),
                               "chunk " + std::to_string(nextSink) + "/" + std::to_string(totalChunks));
                }
            }
            // res 出作用域: 块视图析构, 内存归还 (预算对冲已在上方记账)
        }

        // ③ 终止判定 (含排水阶段的迟到取消 — 已 sink 块由 orchestrator 检查
        //    cancelFlag 跳过 onComplete, 不发布)
        if (terminate) break;
        if (mainCancelGuard.isCancelled()) {
            terminate = true;
            break;
        }
        if (nextSubmit == totalChunks && nextSink == totalChunks) break;
    }

    if (terminate) {
        // ── 终排: 取消/失败 → 丢弃剩余在飞块 (不 sink)。全部返回后才可释放
        //    就绪队列等局部状态 (worker 任务引用了本栈变量) ──
        while (nextSink < nextSubmit) {
            std::shared_ptr<ChunkResult> res;
            {
                std::unique_lock<std::mutex> lock(readyMutex);
                readyCv.wait(lock, [&] { return readyQueue.count(nextSink) > 0; });
                auto it = readyQueue.find(nextSink);
                res = it->second;
                readyQueue.erase(it);
            }
            ++nextSink;
            --inFlightCount;
            inFlightBytes -= res->chunkBytes;
            if (res->error && !failure) failure = res->error;
        }
        pool.shutdown(true);   // 块全部返回 → 任务已尽, 等 worker 退出 (thread_local 上下文析构)
        if (failure) std::rethrow_exception(failure);   // 失败优先于取消: 原始异常上抛
        INTERNAL_INFO_STREAM << "[因子值管线] 已取消: sunk=" << sunkCount << "/" << totalChunks
            << ", 未 sink 块已丢弃 (结果不发布)";
        return;
    }

    pool.shutdown(true);
    ledger.report();
    INTERNAL_INFO_STREAM << "[MEM] 块级并行记账峰值: " << (peakInFlightBytes >> 20)
        << " MB (预算 " << (kTotalBudgetBytes >> 20)
        << " MB, maxInFlight=" << maxInFlight
        << ", 单块上界=" << (maxChunkBytes >> 20) << " MB)";
    INTERNAL_INFO_STREAM << "[因子值管线] 完成: totalChunks=" << totalChunks
        << " workerThreads=" << workerThreads;
}

} // namespace factor::compute
