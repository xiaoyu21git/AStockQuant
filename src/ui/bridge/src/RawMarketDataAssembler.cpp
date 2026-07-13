// RawMarketDataAssembler.cpp — 组装逻辑实现
// 逻辑原样搬自 DataFetchController::fetchDataTypesBySource 的下载装配段（行为不变）
#include "RawMarketDataAssembler.h"

#include "database/MarketDataRepository.h"
#include "database/NativePgConnectionPool.h"
#include "DataTableAssembler.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace bridge {

using astock::infrastructure::database::MarketDataRepository;

RawMarketDataAssembler::Result RawMarketDataAssembler::assemble(
    const std::vector<std::string>& dataTypes,
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate,
    const TableSink& onTable,
    const ProgressFn& onProgress)
{
    Result result;
    if (startDate.empty() || endDate.empty()) { result.error = "日期未设置"; return result; }
    if (dataTypes.empty()) { result.error = "未选择数据类型"; return result; }
    if (symbols.empty()) { result.ok = true; return result; }  // 无标的：空结果，非错误

    // ── 构建统一 Schema（DataSourceRegistry 唯一定义点）──
    auto mergedSchema = cleaning::fullSchemaForTypes(dataTypes);
    const auto& allFields = mergedSchema.names;
    const auto& numericFields = mergedSchema.numeric;
    if (allFields.empty()) { result.error = "Schema 为空"; return result; }

    // 财务字段名集合（用于财务缓存过滤）
    const auto& finCols = cleaning::financial_columns::names();
    std::unordered_set<std::string> finColSet(finCols.begin(), finCols.end());

    const std::vector<std::string>& allSymbolsVec = symbols;
    static const int symbolChunkSize = 200;

    // ── 第一步：全量加载财务数据到内存，symbol → [(report_date,{col:val})]，report_date 升序 ──
    domain::data::DataTableAssembler::FinancialCache finCache;
    {
        auto dbFin = astock::database::NativePgConnectionPool::instance().getConnection();
        if (dbFin && dbFin->isOpen()) {
            MarketDataRepository finRepo(std::move(dbFin));
            // 财务数据需覆盖缓存起始之前的报告期，确保首个交易日能对齐最近财报
            std::string finSd = "2014-01-01"; // 数据库最早 report_date=2014-12-31
            for (size_t fs = 0; fs < allSymbolsVec.size(); fs += symbolChunkSize) {
                size_t fe = (std::min)(fs + static_cast<size_t>(symbolChunkSize), allSymbolsVec.size());
                std::vector<std::string> fchunk(allSymbolsVec.begin() + fs, allSymbolsVec.begin() + fe);
                auto frows = finRepo.queryFinancialData(fchunk, finSd, endDate);
                for (const auto& row : frows) {
                    const auto& vals = row.getValues();
                    auto symIt = vals.find("symbol");
                    auto rptIt = vals.find("report_date");
                    if (symIt == vals.end() || rptIt == vals.end() || rptIt->second.empty()) continue;
                    std::unordered_map<std::string, std::string> fv;
                    for (const auto& [col, val] : vals) {
                        if (!val.empty() && finColSet.count(col)) fv[col] = val;
                    }
                    finCache[symIt->second].emplace_back(rptIt->second, std::move(fv));
                }
            }
        }
        for (auto& [sym, vec] : finCache)
            std::sort(vec.begin(), vec.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    // index_code 映射（直接 Arrow 构建时用）
    domain::data::DataTableAssembler::IndexCodeMap indexMap;
    {
        auto idxDb = astock::database::NativePgConnectionPool::instance().getConnection();
        if (idxDb && idxDb->isOpen()) {
            MarketDataRepository idxRepo(std::move(idxDb));
            indexMap = idxRepo.queryIndexCodeMap(endDate);
        }
    }

    // ── 第二步：按月分片下载 K线，每行即时合并财务数据 ──
    int y1 = std::stoi(startDate.substr(0, 4)), m1 = std::stoi(startDate.substr(5, 2)), d1 = std::stoi(startDate.substr(8, 2));
    int y2 = std::stoi(endDate.substr(0, 4)),   m2 = std::stoi(endDate.substr(5, 2)),   d2 = std::stoi(endDate.substr(8, 2));
    int totalMonths = (y2 - y1) * 12 + (m2 - m1) + 1;
    int doneMonths = 0;
    int totalRows = 0;
    static const int dtab[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};

    for (int mi = 0; mi < totalMonths; ++mi) {
        int cm = m1 + mi, cy = y1 + (cm - 1) / 12; cm = (cm - 1) % 12 + 1;
        int cs = (cy == y1 && cm == m1) ? d1 : 1;
        int ce = (cy == y2 && cm == m2) ? d2 : dtab[cm] + (cm == 2 && cy % 4 == 0 && (cy % 100 != 0 || cy % 400 == 0) ? 1 : 0);
        char buf[32];
        snprintf(buf, 32, "%04d-%02d-%02d", cy, cm, cs); std::string ms = buf;
        snprintf(buf, 32, "%04d-%02d-%02d", cy, cm, ce); std::string me = buf;

        for (size_t start = 0; start < allSymbolsVec.size(); start += symbolChunkSize) {
            size_t end = (std::min)(start + static_cast<size_t>(symbolChunkSize), allSymbolsVec.size());
            std::vector<std::string> chunk(allSymbolsVec.begin() + start, allSymbolsVec.begin() + end);

            auto db2 = astock::database::NativePgConnectionPool::instance().getConnection();
            if (!db2 || !db2->isOpen()) {
                // 连接失败：停止装配，返回已产出部分（与原逻辑 goto dl_end 一致，交调用方决定）
                result.error = "数据库连接失败"; result.totalRows = totalRows; result.ok = false;
                return result;
            }
            MarketDataRepository repo(std::move(db2));
            auto rows = repo.queryDailyBarJoined(chunk, ms, me);
            if (rows.empty()) continue;
            int64_t nRows = static_cast<int64_t>(rows.size());

            // ── 委托领域层构建 Arrow Table ──
            auto table = domain::data::DataTableAssembler::buildFromSqlRows(
                rows, allFields, numericFields, finCache, indexMap);
            rows.clear(); rows.shrink_to_fit();

            if (table && onTable) onTable(table);
            totalRows += static_cast<int>(nRows);
        }

        // 本月 K线已写完，清除不会再被后续月份引用的旧财报
        for (auto it = finCache.begin(); it != finCache.end(); ) {
            auto& vec = it->second;
            auto cut = std::lower_bound(vec.begin(), vec.end(), ms,
                [](const auto& rp, const std::string& c) { return rp.first < c; });
            if (cut != vec.begin()) --cut;
            if (cut != vec.begin()) { vec.erase(vec.begin(), cut); vec.shrink_to_fit(); }
            if (vec.empty()) it = finCache.erase(it);
            else ++it;
        }

        ++doneMonths;
        if (onProgress) onProgress(doneMonths, totalMonths, totalRows);
    }

    result.totalRows = totalRows;
    result.ok = true;
    return result;
}

} // namespace bridge
