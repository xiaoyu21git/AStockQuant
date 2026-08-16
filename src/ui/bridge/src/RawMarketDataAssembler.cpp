// RawMarketDataAssembler.cpp — 组装逻辑实现
// 逻辑原样搬自 DataFetchController::fetchDataTypesBySource 的下载装配段（行为不变）
#include "RawMarketDataAssembler.h"

#include "database/MarketDataRepository.h"
#include "database/NativePgConnectionPool.h"
#include "DataTableAssembler.h"
#include "foundation/log/logging.hpp"
#include "foundation/market/AStockSymbol.h"

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

    // ── 板块日频聚合：全日期范围一次查询，构建 sectorIdx ──
    // sector_relative_strength 由 SQL 直接产出（sqlSectorDailyAgg），C++ 侧无需再算
    std::map<std::string, std::unordered_map<std::string, std::string>> sectorIdx;
    {
        auto dbSector = astock::database::NativePgConnectionPool::instance().getConnection();
        if (dbSector && dbSector->isOpen()) {
            MarketDataRepository sectorRepo(std::move(dbSector));
            auto sRows = sectorRepo.querySectorDailyAgg(startDate, endDate);
            auto mfRows = sectorRepo.querySectorMoneyFlowAgg(startDate, endDate);
            const auto& secCols = cleaning::sector_daily_columns::names();

            for (const auto& sr : sRows) {
                const auto& sv = sr.getValues();
                auto ii = sv.find("industry_code");
                auto ti = sv.find("trade_date");
                if (ii == sv.end() || ti == sv.end()) continue;
                std::string key;
                key += ii->second;
                key += '|';
                std::string date = ti->second.size() >= 10 ? ti->second.substr(0, 10) : ti->second;
                for (char c : date) if (c != '-') key += c;
                std::unordered_map<std::string, std::string> colVals;
                for (const auto& cn : secCols) {
                    auto it = sv.find(cn);
                    if (it != sv.end() && !it->second.empty()
                        && it->second != "NULL" && it->second != "null") {
                        colVals[cn] = it->second;
                    }
                }
                if (!colVals.empty()) sectorIdx[std::move(key)] = std::move(colVals);
            }

            for (const auto& mr : mfRows) {
                const auto& mv = mr.getValues();
                auto ii = mv.find("industry_code");
                auto ti = mv.find("trade_date");
                if (ii == mv.end() || ti == mv.end()) continue;
                std::string key;
                key += ii->second;
                key += '|';
                std::string date = ti->second.size() >= 10 ? ti->second.substr(0, 10) : ti->second;
                for (char c : date) if (c != '-') key += c;
                auto it = sectorIdx.find(key);
                if (it == sectorIdx.end()) continue;
                for (const auto& cn : {"sector_money_flow_net","sector_money_flow_ratio"}) {
                    auto mit = mv.find(cn);
                    if (mit != mv.end() && !mit->second.empty())
                        it->second[cn] = mit->second;
                }
            }

            auto concRows = sectorRepo.querySectorConcentration(startDate, endDate);
            for (const auto& cr : concRows) {
                const auto& cv = cr.getValues();
                auto ii = cv.find("industry_code");
                auto ti = cv.find("trade_date");
                if (ii == cv.end() || ti == cv.end()) continue;
                std::string key;
                key += ii->second;
                key += '|';
                std::string date = ti->second.size() >= 10 ? ti->second.substr(0, 10) : ti->second;
                for (char c : date) if (c != '-') key += c;
                auto it = sectorIdx.find(key);
                if (it == sectorIdx.end()) continue;
                auto ci = cv.find("sector_concentration");
                if (ci != cv.end() && !ci->second.empty()
                    && ci->second != "NULL" && ci->second != "null")
                    it->second["sector_concentration"] = ci->second;
            }

        }
    }

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

            // ── 辅助：构建归一化 key = FULL_SYMBOL|YYYYMMDD ──
            auto normKey = [](const std::string& symbol, const std::string& rawDate) -> std::string {
                std::string key = foundation::market::AStockSymbol::normalizeToFullSymbol(symbol);
                key += '|';
                std::string date = rawDate.size() >= 10 ? rawDate.substr(0, 10) : rawDate;
                for (char c : date) if (c != '-') key += c;
                return key;
            };

            // ── 分钟日聚合：如果选了 minute_data，按 (symbol, trade_date) 注入分钟列 ──
            bool hasMinute = std::find(dataTypes.begin(), dataTypes.end(), "minute_data") != dataTypes.end();
            if (hasMinute) {
                auto mRows = repo.queryMinuteDailyAgg(chunk, ms, me);
                std::map<std::string, std::unordered_map<std::string, std::string>> minIdx;
                const auto& minCols = cleaning::minute_daily_columns::names();
                for (const auto& mr : mRows) {
                    const auto& mv = mr.getValues();
                    auto si = mv.find("symbol");
                    auto ti = mv.find("trade_date");
                    if (si == mv.end() || ti == mv.end()) continue;
                    std::unordered_map<std::string, std::string> colVals;
                    for (const auto& cn : minCols) {
                        auto it = mv.find(cn);
                        if (it != mv.end()) colVals[cn] = it->second;
                    }
                    minIdx[normKey(si->second, ti->second)] = std::move(colVals);
                }
                for (auto& row : rows) {
                    const auto& rv = row.getValues();
                    auto si = rv.find("symbol");
                    auto ti = rv.find("trade_date");
                    if (si == rv.end() || ti == rv.end()) continue;
                    auto it = minIdx.find(normKey(si->second, ti->second));
                    if (it == minIdx.end()) continue;
                    for (const auto& [cn, cv] : it->second)
                        row.setValue(cn, cv);
                }
            }

            // ── 资金流注入：独立 queryMoneyFlow 路径，避免 LEFT JOIN 扫描全表 ──
            bool hasMoneyFlow = std::find(dataTypes.begin(), dataTypes.end(), "money_flow") != dataTypes.end();
            if (hasMoneyFlow) {
                auto mfRows = repo.queryMoneyFlow(chunk, ms, me);
                std::map<std::string, std::unordered_map<std::string, std::string>> mfIdx;
                const auto& mfCols = cleaning::money_flow_columns::names();
                for (const auto& mr : mfRows) {
                    const auto& mv = mr.getValues();
                    auto si = mv.find("symbol");
                    auto ti = mv.find("trade_date");
                    if (si == mv.end() || ti == mv.end()) continue;
                    std::unordered_map<std::string, std::string> colVals;
                    for (const auto& cn : mfCols) {
                        auto it = mv.find(cn);
                        if (it != mv.end() && !it->second.empty())
                            colVals[cn] = it->second;
                    }
                    mfIdx[normKey(si->second, ti->second)] = std::move(colVals);
                }
                for (auto& row : rows) {
                    const auto& rv = row.getValues();
                    auto si = rv.find("symbol");
                    auto ti = rv.find("trade_date");
                    if (si == rv.end() || ti == rv.end()) continue;
                    auto it = mfIdx.find(normKey(si->second, ti->second));
                    if (it == mfIdx.end()) continue;
                    for (const auto& [cn, cv] : it->second)
                        row.setValue(cn, cv);
                }
            }

            // ── 板块列注入：使用月级预构建的 sectorIdx（O(1) 查表，不再每 chunk 查 SQL）──
            if (!sectorIdx.empty()) {
                for (auto& row : rows) {
                    const auto& rv = row.getValues();
                    auto ii = rv.find("industry_code");
                    auto ti = rv.find("trade_date");
                    if (ii == rv.end() || ti == rv.end()) continue;
                    std::string key;
                    key += ii->second;
                    key += '|';
                    std::string date = ti->second.size() >= 10 ? ti->second.substr(0, 10) : ti->second;
                    for (char c : date) if (c != '-') key += c;
                    auto it = sectorIdx.find(key);
                    if (it == sectorIdx.end()) continue;
                    for (const auto& [cn, cv] : it->second)
                        row.setValue(cn, cv);
                }
            }

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
