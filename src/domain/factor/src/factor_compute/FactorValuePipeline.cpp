#include "factor_compute/FactorValuePipeline.h"
#include "factor_compute/ArrowMarketDataView.h"
#include "FactorInstanceManager.h"
#include "BaseFactor.h"
#include "../../../../domain/cleaning/include/DataSourceRegistry.h"
#include "database/MarketDataRepository.h"
#include "database/NativePgConnectionPool.h"
#include "foundation/Utils/DateUtils.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace factor::compute {

namespace {

/// @brief 分块大小 (交易日) — 内存上界 = 块交易日 × 标的数 × 列数 × 4B
constexpr int kChunkDates = 60;

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
    std::size_t& warmupRowCountOut) const
{
    warmupRowCountOut = 0;
    if (dates.empty()) return nullptr;

    // ── 回看日期: 从目标首日往前, data.trade_calendar 查 warmupDays 个交易日 ──
    std::vector<DateKey> warmupDates;
    if (warmupDays > 0)
        warmupDates = queryWarmupDates(dates.front().value, warmupDays);
    warmupRowCountOut = warmupDates.size();

    auto extendedDates = warmupDates;
    extendedDates.insert(extendedDates.end(), dates.begin(), dates.end());
    extendedDates.insert(extendedDates.end(), tailDates.begin(), tailDates.end());

    auto chunkView = m_arrowView.makeChunkView(extendedDates, fields);
    if (!chunkView || warmupDates.empty()) return chunkView;

    // ── 回看行在数据集中不存在 (NaN) → 从 DB 逐日逐列覆写 ──
    overwriteWarmupRows(*chunkView, warmupDates, fields);
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
    ProgressFn onProgress)
{
    if (dates.empty() || factorIds.empty() || !sink) return;

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

    // ── dbFallback 挂接/摘除由管线内部管理 ──
    // loader 必须先于 guard 析构 (guard 先析构摘除回调, 再释放 loader 与缓存)
    auto loader = std::make_shared<WarmupDataLoader>(dates.front().value, maxLookback);
    struct DbFallbackGuard {
        BacktestDataService* svc;
        ~DbFallbackGuard() {
            if (svc) svc->setDbFallback({});
        }
    } dbGuard{&m_dataSvc};
    m_dataSvc.setDbFallback(loader->dbFallbackFn());

    WarmupViewBuilder viewBuilder(arrowView, loader);

    for (size_t ci = 0; ci < totalChunks; ++ci) {
        if (onProgress) {
            onProgress(static_cast<double>(ci) / static_cast<double>(totalChunks),
                       "chunk " + std::to_string(ci + 1) + "/" + std::to_string(totalChunks));
        }

        const size_t chunkStart = ci * kChunkDates;
        const size_t ownEnd = std::min(chunkStart + kChunkDates, totalDates);
        const size_t tailEnd = std::min(ownEnd + static_cast<size_t>(fwdDays), totalDates);
        std::vector<DateKey> chunkDates(dates.begin() + chunkStart, dates.begin() + ownEnd);
        std::vector<DateKey> tailDates(dates.begin() + ownEnd, dates.begin() + tailEnd);
        if (chunkDates.empty()) continue;

        // ── 首块: 从 DB 一次性补齐回看数据 ──
        const int warmupDays = (ci == 0 && maxLookback > 0) ? maxLookback : 0;
        std::size_t warmupRowCount = 0;
        auto chunkView = viewBuilder.build(chunkDates, tailDates, chunkColumns,
                                           warmupDays, warmupRowCount);
        if (!chunkView) continue;
        const size_t computeSkip = warmupRowCount;

        INTERNAL_INFO_STREAM << "[因子值管线] 块 " << ci + 1 << "/" << totalChunks
            << ": own=" << chunkDates.size()
            << " tail=" << tailDates.size()
            << " warmup=" << warmupRowCount
            << " fields=" << chunkColumns.size();

        // ── 本块交易日集合 (过滤 compute 对尾部扩展日的产出) ──
        std::unordered_set<std::string> ownDateSet;
        ownDateSet.reserve(chunkDates.size());
        for (const auto& d : chunkDates)
            ownDateSet.insert(foundation::utils::formatTradingDay(d.value));

        std::unordered_map<std::string, FactorValuesByDate> perFactorValues;
        MarketMatrixBatch chunkBatch;
        chunkBatch.batchIndex = ci;
        chunkBatch.marketView = chunkView.get();

        for (const auto& factorId : factorIds) {
            FactorCacheKey cacheKey;
            cacheKey.factorName = factorId;
            auto factorResult = m_engine.compute(chunkBatch, cacheKey, computeSkip);
            FactorValuesByDate ownValues;
            for (auto& [dateStr, symValues] : factorResult.factorValues) {
                if (ownDateSet.count(dateStr))
                    ownValues[dateStr] = std::move(symValues);
            }
            perFactorValues[factorId] = std::move(ownValues);
        }

        ChunkOutput out;
        out.chunkIndex = ci;
        out.chunkDates = &chunkDates;
        out.tailDates = &tailDates;
        out.chunkView = chunkView.get();
        out.perFactorValues = &perFactorValues;
        sink(out);

        // chunkView 在此出作用域 → 该块数据释放
    }
    INTERNAL_INFO_STREAM << "[因子值管线] 完成: totalChunks=" << totalChunks;
}

} // namespace factor::compute
