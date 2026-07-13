// DataCacheParquet.cpp — Arrow IPC 持久化实现（纯 C++，Arrow C++ API）
// 文件扩展名 .arrow，格式 Arrow IPC File / Feather
#include "DataCache.h"

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>

#include "foundation/log/logging.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using J = foundation::json::JsonFacade;
using namespace cleaning;

namespace {

void scanFields(const std::vector<J>& rows,
                std::vector<std::string>& fieldNames,
                std::unordered_set<std::string>& numericFields)
{
    std::unordered_map<std::string, bool> fieldNumeric;
    // 已知字段名从 DataFieldKeys 聚合（唯一定义点），不再硬编码
    // 动态扫描：哪些字段实际存在于数据中、是数值还是字符串
    const std::vector<const char*> knownFields = []() {
        const auto mk = [](std::initializer_list<const char*> fields) {
            return std::vector<const char*>(fields);
        };
        // CF/MF/F_F/XF — 仅 DataFieldKeys.h 中定义了字段名常量
        auto v = mk({CF::SYMBOL, CF::TRADE_DATE, CF::DATA_SOURCE,
                     MF::OPEN, MF::HIGH, MF::LOW, MF::CLOSE, MF::PRE_CLOSE,
                     MF::VOLUME, MF::TURNOVER, MF::CHANGE_PCT, MF::CHANGE_AMT, MF::AMPLITUDE,
                     MF::TURNOVER_RATE, MF::PE_RATIO, MF::PB_RATIO, MF::MARKET_CAP,
                     MF::CIRCULATING_MARKET_CAP, MF::PRE_ADJ_FACTOR, MF::POST_ADJ_FACTOR,
                     MF::INDUSTRY_CODE});
        auto f = mk({F_F::REPORT_DATE, F_F::REPORT_TYPE, F_F::DISCLOSURE_DATE,
                     F_F::EFFECTIVE_DISCLOSURE_DATE, F_F::SYMBOL_ID, F_F::INDICATOR_ID,
                     F_F::EPS, F_F::BPS, F_F::ROA, F_F::ROE,
                     F_F::PROFIT_MARGIN, F_F::GROSS_MARGIN, F_F::OPERATING_MARGIN,
                     F_F::DEBT_TO_EQUITY, F_F::CURRENT_RATIO, F_F::QUICK_RATIO,
                     F_F::OPERATING_CASH_FLOW, F_F::INVESTING_CASH_FLOW, F_F::FINANCING_CASH_FLOW,
                     F_F::TOTAL_REVENUE, F_F::NET_PROFIT, F_F::TOTAL_ASSETS, F_F::TOTAL_LIABILITIES, F_F::EQUITY,
                     F_F::DIVIDEND_YIELD, F_F::PAYOUT_RATIO, F_F::DIVIDEND_STABILITY});
        auto x = mk({XF::NAME, XF::EXCHANGE, XF::STATUS, XF::LIST_DATE, XF::DELIST_DATE});
        // 兼容别名 — 不在 DataFieldKeys 中但实际数据可能出现
        std::vector<const char*> aliases = {"code", "stock_code", "industry", "asset_class", "trade_status"};
        v.insert(v.end(), f.begin(), f.end());
        v.insert(v.end(), x.begin(), x.end());
        v.insert(v.end(), aliases.begin(), aliases.end());
        return v;
    }();
    for (const auto& row : rows) {
        if (!row.isObject()) continue;
        for (const char* f : knownFields) {
            if (!row.has(f)) continue;
            auto it = fieldNumeric.find(f);
            if (it == fieldNumeric.end()) {
                fieldNumeric[f] = row.get(f).isNumber();
            } else if (!it->second && row.get(f).isNumber()) {
                it->second = true;
            }
        }
    }
    fieldNames = {CF::SYMBOL.c_str(), CF::TRADE_DATE.c_str()};
    for (const auto& [f, _] : fieldNumeric) {
        if (f != "symbol" && f != "trade_date") fieldNames.push_back(f);
    }
    for (const auto& f : fieldNames) {
        if (fieldNumeric.count(f) && fieldNumeric[f]) numericFields.insert(f);
    }
}

std::shared_ptr<arrow::Table> buildArrowTable(
    const std::vector<J>& rows,
    const std::vector<std::string>& fieldNames,
    const std::unordered_set<std::string>& numericFields)
{
    std::vector<std::shared_ptr<arrow::ChunkedArray>> columns;
    std::vector<std::shared_ptr<arrow::Field>> schemaFields;
    const int64_t n = static_cast<int64_t>(rows.size());

    for (const auto& fname : fieldNames) {
        if (numericFields.count(fname)) {
            arrow::DoubleBuilder builder;
            for (const auto& row : rows) {
                if (row.isObject() && row.has(fname.c_str())) {
                    auto v = row.get(fname.c_str());
                    if (v.isNumber()) {
                        builder.Append(v.asDouble());
                    } else if (v.isString()) {
                        try { builder.Append(std::stod(v.asString())); }
                        catch (...) { builder.Append(std::numeric_limits<double>::quiet_NaN()); }
                    } else {
                        builder.Append(std::numeric_limits<double>::quiet_NaN());
                    }
                } else {
                    builder.AppendNull();
                }
            }
            std::shared_ptr<arrow::Array> arr;
            builder.Finish(&arr);
            columns.push_back(std::make_shared<arrow::ChunkedArray>(arr));
            schemaFields.push_back(arrow::field(fname, arrow::float64()));
        } else {
            arrow::StringBuilder builder;
            for (const auto& row : rows) {
                if (row.isObject() && row.has(fname.c_str())) {
                    auto v = row.get(fname.c_str());
                    if (v.isString()) builder.Append(v.asString());
                    else if (v.isNumber()) { char buf[64]; snprintf(buf,64,"%.10g",v.asDouble()); builder.Append(std::string(buf)); }
                    else builder.AppendNull();
                } else {
                    builder.AppendNull();
                }
            }
            std::shared_ptr<arrow::Array> arr;
            builder.Finish(&arr);
            columns.push_back(std::make_shared<arrow::ChunkedArray>(arr));
            schemaFields.push_back(arrow::field(fname, arrow::utf8()));
        }
    }

    return arrow::Table::Make(arrow::schema(schemaFields), columns, n);
}

std::vector<J> tableToRows(const std::shared_ptr<arrow::Table>& table)
{
    std::vector<J> rows;
    const int64_t n = table->num_rows();
    rows.reserve(static_cast<size_t>(n));
    const auto& schema = table->schema();
    const int nCols = table->num_columns();

    // 预计算每列的 chunk 边界
    struct ColInfo { std::vector<int64_t> offsets; std::vector<std::shared_ptr<arrow::Array>> chunks; };
    std::vector<ColInfo> colInfos(nCols);
    for (int c = 0; c < nCols; ++c) {
        auto chunked = table->column(c);
        int64_t off = 0;
        for (int k = 0; k < chunked->num_chunks(); ++k) {
            auto arr = chunked->chunk(k);
            colInfos[c].offsets.push_back(off);
            colInfos[c].chunks.push_back(arr);
            off += arr->length();
        }
    }

    for (int64_t i = 0; i < n; ++i) {
        auto obj = J::createObject();
        for (int c = 0; c < nCols; ++c) {
            const auto& fname = schema->field(c)->name();
            const auto& info = colInfos[c];
            auto it = std::upper_bound(info.offsets.begin(), info.offsets.end(), i) - 1;
            int k = static_cast<int>(it - info.offsets.begin());
            int64_t off = i - info.offsets[k];
            auto arr = info.chunks[k];
            if (arr->IsNull(off)) continue;

            if (arr->type_id() == arrow::Type::DOUBLE) {
                auto dArr = std::static_pointer_cast<arrow::DoubleArray>(arr);
                obj.set(fname, J::createDouble(dArr->Value(off)));
            } else if (arr->type_id() == arrow::Type::STRING) {
                auto sArr = std::static_pointer_cast<arrow::StringArray>(arr);
                obj.set(fname, J::createString(sArr->GetString(off)));
            }
        }
        rows.push_back(std::move(obj));
    }
    return rows;
}

} // anonymous namespace

namespace cleaning {

void DataCache::saveDataSetFile(int dataId, const std::vector<J>& rows)
{
    if (rows.empty()) return;
    std::vector<std::string> fieldNames;
    std::unordered_set<std::string> numericFields;
    scanFields(rows, fieldNames, numericFields);
    saveDataSetFile(dataId, rows, fieldNames, numericFields);
}

void DataCache::saveDataSetFile(int dataId, const std::vector<J>& rows,
    const std::vector<std::string>& fieldNames,
    const std::unordered_set<std::string>& numericFields)
{
    if (rows.empty()) return;
    auto table = buildArrowTable(rows, fieldNames, numericFields);

    std::string path = dataFilePath(dataId);
    auto outResult = arrow::io::FileOutputStream::Open(path);
    if (!outResult.ok()) {
        INTERNAL_ERROR_STREAM << "[DataCache] saveArrow: cannot open " << path << ": " << outResult.status().ToString();
        return;
    }

    auto writerResult = arrow::ipc::MakeFileWriter(outResult.ValueOrDie(), table->schema());
    if (!writerResult.ok()) {
        INTERNAL_ERROR_STREAM << "[DataCache] saveArrow: writer error " << writerResult.status().ToString();
        return;
    }

    auto writer = writerResult.ValueOrDie();
    auto writeStatus = writer->WriteTable(*table);
    if (!writeStatus.ok()) {
        INTERNAL_ERROR_STREAM << "[DataCache] saveArrow: write error " << writeStatus.ToString();
        return;
    }
    writer->Close();

    INTERNAL_INFO_STREAM << "[DataCache] saved Arrow IPC " << path << ": " << table->num_rows() << " rows x " << table->num_columns() << " cols";
}

std::vector<J> DataCache::loadDataSetFile(int dataId)
{
    std::string path = dataFilePath(dataId);

    auto inResult = arrow::io::ReadableFile::Open(path);
    if (!inResult.ok()) {
        INTERNAL_ERROR_STREAM << "[DataCache] loadArrow: cannot open " << path << ": " << inResult.status().ToString();
        return {};
    }

    auto readerResult = arrow::ipc::RecordBatchFileReader::Open(inResult.ValueOrDie());
    if (!readerResult.ok()) {
        INTERNAL_ERROR_STREAM << "[DataCache] loadArrow: open error " << readerResult.status().ToString();
        return {};
    }

    auto reader = readerResult.ValueOrDie();
    std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
    for (int i = 0; i < reader->num_record_batches(); ++i) {
        auto batchResult = reader->ReadRecordBatch(i);
        if (!batchResult.ok()) continue;
        batches.push_back(batchResult.ValueOrDie());
    }

    if (batches.empty()) return {};

    auto tableResult = arrow::Table::FromRecordBatches(batches);
    if (!tableResult.ok()) {
        INTERNAL_ERROR_STREAM << "[DataCache] loadArrow: table error " << tableResult.status().ToString();
        return {};
    }

    auto table = tableResult.ValueOrDie();
    INTERNAL_INFO_STREAM << "[DataCache] loaded Arrow IPC " << path << ": " << table->num_rows() << " rows x " << table->num_columns() << " cols";

    return tableToRows(table);
}

std::shared_ptr<arrow::Table> DataCache::loadDataSetTable(int dataId)
{
    std::string path = dataFilePath(dataId);

    auto inResult = arrow::io::ReadableFile::Open(path);
    if (!inResult.ok()) {
        INTERNAL_ERROR_STREAM << "[DataCache] loadTable: cannot open " << path;
        return nullptr;
    }

    auto readerResult = arrow::ipc::RecordBatchFileReader::Open(inResult.ValueOrDie());
    if (!readerResult.ok()) return nullptr;

    auto reader = readerResult.ValueOrDie();
    std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
    for (int i = 0; i < reader->num_record_batches(); ++i) {
        auto batchResult = reader->ReadRecordBatch(i);
        if (!batchResult.ok()) continue;
        batches.push_back(batchResult.ValueOrDie());
    }
    if (batches.empty()) return nullptr;

    auto tableResult = arrow::Table::FromRecordBatches(batches);
    if (!tableResult.ok()) return nullptr;

    auto table = tableResult.ValueOrDie();
    INTERNAL_INFO_STREAM << "[DataCache] loaded Arrow Table " << path << ": " << table->num_rows() << " rows x " << table->num_columns() << " cols";
    return table;
}

std::vector<std::string> DataCache::loadDataSetSchemaFields(int dataId)
{
    std::string path = dataFilePath(dataId);

    auto inResult = arrow::io::ReadableFile::Open(path);
    if (!inResult.ok()) return {};

    auto readerResult = arrow::ipc::RecordBatchFileReader::Open(inResult.ValueOrDie());
    if (!readerResult.ok()) return {};

    auto reader = readerResult.ValueOrDie();
    auto schema = reader->schema();
    if (!schema) return {};

    std::vector<std::string> fields;
    fields.reserve(static_cast<size_t>(schema->num_fields()));
    for (int i = 0; i < schema->num_fields(); ++i) {
        fields.push_back(schema->field(i)->name());
    }
    return fields;
}

// ── 增量：扫描 trade_date 列取真实最大日期（以文件为准）──

std::string DataCache::getMaxTradeDate(int dataId)
{
    std::string path = dataFilePath(dataId);

    auto inResult = arrow::io::ReadableFile::Open(path);
    if (!inResult.ok()) {
        INTERNAL_WARN_STREAM << "[DataCache] getMaxTradeDate: cannot open " << path;
        return {};
    }
    auto readerResult = arrow::ipc::RecordBatchFileReader::Open(inResult.ValueOrDie());
    if (!readerResult.ok()) return {};
    auto reader = readerResult.ValueOrDie();
    auto schema = reader->schema();
    if (!schema) return {};

    int tdIdx = schema->GetFieldIndex(std::string(CF::TRADE_DATE.c_str()));
    if (tdIdx < 0) {
        INTERNAL_WARN_STREAM << "[DataCache] getMaxTradeDate: no trade_date column in " << path;
        return {};
    }

    std::string maxTd;
    for (int i = 0; i < reader->num_record_batches(); ++i) {
        auto batchResult = reader->ReadRecordBatch(i);
        if (!batchResult.ok()) continue;
        auto col = batchResult.ValueOrDie()->column(tdIdx);
        if (!col || col->type_id() != arrow::Type::STRING) continue;
        auto sArr = std::static_pointer_cast<arrow::StringArray>(col);
        for (int64_t r = 0; r < sArr->length(); ++r) {
            if (sArr->IsNull(r)) continue;
            std::string v = sArr->GetString(r);
            if (v > maxTd) maxTd = std::move(v);
        }
    }
    return maxTd;
}

// ── 查看：加载指定 symbol 的所有行（完整列，按文件原有顺序）──

std::vector<J> DataCache::loadDataSetRowsBySymbol(int dataId, const std::string& symbol)
{
    std::string path = dataFilePath(dataId);
    auto inResult = arrow::io::ReadableFile::Open(path);
    if (!inResult.ok()) return {};
    auto readerResult = arrow::ipc::RecordBatchFileReader::Open(inResult.ValueOrDie());
    if (!readerResult.ok()) return {};
    auto reader = readerResult.ValueOrDie();
    auto schema = reader->schema();
    if (!schema) return {};

    const int nCols = schema->num_fields();
    const int symIdx = schema->GetFieldIndex(std::string(CF::SYMBOL.c_str()));
    if (symIdx < 0) return {};
    std::vector<std::string> names(static_cast<size_t>(nCols));
    for (int c = 0; c < nCols; ++c) names[static_cast<size_t>(c)] = schema->field(c)->name();

    std::vector<J> out;
    for (int i = 0; i < reader->num_record_batches(); ++i) {
        auto bR = reader->ReadRecordBatch(i);
        if (!bR.ok()) continue;
        auto batch = bR.ValueOrDie();
        auto symCol = batch->column(symIdx);
        if (!symCol || symCol->type_id() != arrow::Type::STRING) continue;
        auto symArr = std::static_pointer_cast<arrow::StringArray>(symCol);

        // 预取每列 typed 指针
        std::vector<const arrow::DoubleArray*> dbl(static_cast<size_t>(nCols), nullptr);
        std::vector<const arrow::StringArray*> str(static_cast<size_t>(nCols), nullptr);
        std::vector<std::shared_ptr<arrow::Array>> owners(static_cast<size_t>(nCols));
        for (int c = 0; c < nCols; ++c) {
            owners[static_cast<size_t>(c)] = batch->column(c);
            auto& o = owners[static_cast<size_t>(c)];
            if (o->type_id() == arrow::Type::DOUBLE) dbl[static_cast<size_t>(c)] = static_cast<const arrow::DoubleArray*>(o.get());
            else if (o->type_id() == arrow::Type::STRING) str[static_cast<size_t>(c)] = static_cast<const arrow::StringArray*>(o.get());
        }

        const int64_t n = batch->num_rows();
        for (int64_t r = 0; r < n; ++r) {
            if (symArr->IsNull(r) || symArr->GetString(r) != symbol) continue;
            auto obj = J::createObject();
            for (size_t c = 0; c < static_cast<size_t>(nCols); ++c) {
                if (dbl[c]) { if (!dbl[c]->IsNull(r)) obj.set(names[c], J::createDouble(dbl[c]->Value(r))); }
                else if (str[c]) { if (!str[c]->IsNull(r)) obj.set(names[c], J::createString(str[c]->GetString(r))); }
            }
            out.push_back(std::move(obj));
        }
    }
    return out;
}

// ── 增量：按日期范围加载完整行 ──
std::vector<J> DataCache::loadDataSetRange(int dataId, const std::string& sinceDate)
{
    std::string path = dataFilePath(dataId);

    auto inResult = arrow::io::ReadableFile::Open(path);
    if (!inResult.ok()) {
        INTERNAL_ERROR_STREAM << "[DataCache] loadRange: cannot open " << path << ": " << inResult.status().ToString();
        return {};
    }

    auto readerResult = arrow::ipc::RecordBatchFileReader::Open(inResult.ValueOrDie());
    if (!readerResult.ok()) {
        INTERNAL_ERROR_STREAM << "[DataCache] loadRange: open error " << readerResult.status().ToString();
        return {};
    }

    auto reader = readerResult.ValueOrDie();
    std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
    for (int i = 0; i < reader->num_record_batches(); ++i) {
        auto batchResult = reader->ReadRecordBatch(i);
        if (!batchResult.ok()) continue;
        batches.push_back(batchResult.ValueOrDie());
    }
    if (batches.empty()) return {};

    auto tableResult = arrow::Table::FromRecordBatches(batches);
    if (!tableResult.ok()) {
        INTERNAL_ERROR_STREAM << "[DataCache] loadRange: table error " << tableResult.status().ToString();
        return {};
    }

    auto allRows = tableToRows(tableResult.ValueOrDie());

    // 过滤 trade_date >= sinceDate（trade_date 为 "YYYY-MM-DD" 字符串，字典序即时间序）
    std::vector<J> result;
    result.reserve(allRows.size());
    const char* kTradeDate = CF::TRADE_DATE.c_str();
    for (auto& row : allRows) {
        if (!row.isObject() || !row.has(kTradeDate)) continue;
        auto v = row.get(kTradeDate);
        if (!v.isString()) continue;
        if (v.asString() >= sinceDate) result.push_back(std::move(row));
    }
    INTERNAL_INFO_STREAM << "[DataCache] loadRange " << path << ": " << result.size() << " rows since " << sinceDate;
    return result;
}

// ── 增量：原子追加写入 ──

int DataCache::appendDataSetFile(int dataId, const std::vector<J>& newRows,
    const std::vector<std::string>& fieldNames,
    const std::unordered_set<std::string>& numericFields)
{
    if (newRows.empty()) {
        INTERNAL_WARN_STREAM << "[DataCache] append: no new rows for dataset " << dataId;
        return -1;
    }

    const std::string path = dataFilePath(dataId);
    const std::string tmpPath = path + ".tmp";

    // 0. 清理上次崩溃残留的临时文件
    std::error_code rmEc;
    std::filesystem::remove(tmpPath, rmEc);

    // 1. 打开旧文件，读取 schema（并延迟到写完新 batch 后再读旧 batch）
    // 1~3. 读旧文件 + 构建新表校验 schema + 写临时文件。
    //   所有 arrow 读写句柄都限制在此作用域内，作用域结束(且显式 Close 旧文件)后再 rename，
    //   避免 Windows 下 Result 对象残留的 ReadableFile 句柄未释放导致 rename "拒绝访问"。
    int64_t oldRows = 0;
    int64_t newRowCount = 0;
    {
        auto inResult = arrow::io::ReadableFile::Open(path);
        if (!inResult.ok()) {
            INTERNAL_ERROR_STREAM << "[DataCache] append: cannot open old file " << path << ": " << inResult.status().ToString();
            return -1;
        }
        auto inFile = inResult.ValueOrDie();
        auto readerResult = arrow::ipc::RecordBatchFileReader::Open(inFile);
        if (!readerResult.ok()) {
            INTERNAL_ERROR_STREAM << "[DataCache] append: read old error " << readerResult.status().ToString();
            (void)inFile->Close();
            return -1;
        }
        auto reader = readerResult.ValueOrDie();
        auto oldSchema = reader->schema();

        // 构建新数据 Table，校验 schema 与旧文件一致
        auto newTable = buildArrowTable(newRows, fieldNames, numericFields);
        if (!newTable->schema()->Equals(*oldSchema)) {
            INTERNAL_ERROR_STREAM << "[DataCache] append: schema mismatch for dataset " << dataId << ", abort (old file kept)";
            (void)inFile->Close();
            return -1;
        }
        newRowCount = newTable->num_rows();

        // 写临时文件：旧 batch 全量 + 新 Table。任一步失败即清理 tmp、保留旧文件
        auto outResult = arrow::io::FileOutputStream::Open(tmpPath);
        if (!outResult.ok()) {
            INTERNAL_ERROR_STREAM << "[DataCache] append: cannot open tmp " << tmpPath << ": " << outResult.status().ToString();
            (void)inFile->Close();
            return -1;
        }
        auto stream = outResult.ValueOrDie();
        auto writerResult = arrow::ipc::MakeFileWriter(stream, oldSchema);
        if (!writerResult.ok()) {
            INTERNAL_ERROR_STREAM << "[DataCache] append: writer error " << writerResult.status().ToString();
            stream.reset(); (void)inFile->Close();
            std::filesystem::remove(tmpPath, rmEc);
            return -1;
        }
        auto writer = writerResult.ValueOrDie();

        auto fail = [&](const std::string& what) -> int {
            INTERNAL_ERROR_STREAM << "[DataCache] append: " << what << " (old file kept)";
            writer->Close(); stream.reset(); (void)inFile->Close();
            std::filesystem::remove(tmpPath, rmEc);
            return -1;
        };

        for (int i = 0; i < reader->num_record_batches(); ++i) {
            auto batchResult = reader->ReadRecordBatch(i);
            if (!batchResult.ok()) return fail("read old batch failed: " + batchResult.status().ToString());
            auto batch = batchResult.ValueOrDie();
            auto st = writer->WriteRecordBatch(*batch);
            if (!st.ok()) return fail("write old batch failed: " + st.ToString());
            oldRows += batch->num_rows();
        }

        auto wst = writer->WriteTable(*newTable);
        if (!wst.ok()) return fail("write new table failed: " + wst.ToString());

        auto cst = writer->Close();
        if (!cst.ok()) return fail("close writer failed: " + cst.ToString());
        stream.reset();          // 关闭 tmp 输出流（flush 到磁盘）
        (void)inFile->Close();   // 显式关闭旧文件读句柄
        // reader/readerResult/inFile/inResult/writer/stream 均在此作用域结束时析构
    }
    // 5. 原子替换：tmp → path。带退避重试，应对读端短暂占用 .arrow
    constexpr int kMaxRenameRetries = 5;
    constexpr int kRetryDelayMs = 100;
    std::error_code ec;
    bool renamed = false;
    for (int attempt = 0; attempt < kMaxRenameRetries; ++attempt) {
        std::filesystem::rename(tmpPath, path, ec);
        if (!ec) { renamed = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(kRetryDelayMs));
    }
    if (!renamed) {
        INTERNAL_ERROR_STREAM << "[DataCache] append: atomic replace failed after retries: " << ec.message() << " (old file kept)";
        std::filesystem::remove(tmpPath, rmEc);
        return -1;
    }

    // 6. 更新元数据：rowCount 与 endDate
    const int64_t totalRows = oldRows + newRowCount;
    std::string maxTradeDate;
    for (const auto& row : newRows) {
        if (!row.isObject() || !row.has(CF::TRADE_DATE.c_str())) continue;
        auto v = row.get(CF::TRADE_DATE.c_str());
        if (v.isString() && v.asString() > maxTradeDate) maxTradeDate = v.asString();
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_index.find(dataId);
        if (it != m_index.end()) {
            it->second.rowCount = static_cast<int>(totalRows);
            if (!maxTradeDate.empty() && maxTradeDate > it->second.endDate)
                it->second.endDate = maxTradeDate;
            saveCatalog();
        }
    }

    INTERNAL_INFO_STREAM << "[DataCache] appended dataset " << dataId << ": +" << newRowCount
                         << " rows (old=" << oldRows << ", total=" << totalRows << "), endDate<=" << maxTradeDate;
    return static_cast<int>(totalRows);
}

// ── 批量写入 ──

struct ArrowWriteSession : public DataCache::WriteSession {
    std::shared_ptr<arrow::Schema> schema;
    std::shared_ptr<arrow::ipc::RecordBatchWriter> writer;
    std::shared_ptr<arrow::io::FileOutputStream> stream;
    std::vector<std::string> fieldNames;
    std::unordered_set<std::string> numericFields;
};

DataCache::ArrowWriteToken DataCache::beginArrowWrite(int dataId)
{
    auto session = new ArrowWriteSession();
    session->dataId = dataId;
    ensureDir(datasetDir(dataId));
    ensureDir(datasetDir(dataId) + "/raw");
    session->stream = arrow::io::FileOutputStream::Open(dataFilePath(dataId)).ValueOrDie();
    return session;
}

DataCache::ArrowWriteToken DataCache::beginArrowWrite(int dataId,
    const std::vector<std::string>& fieldNames,
    const std::unordered_set<std::string>& numericFields)
{
    auto session = new ArrowWriteSession();
    session->dataId = dataId;
    session->fieldNames = fieldNames;
    session->numericFields = numericFields;
    ensureDir(datasetDir(dataId));
    ensureDir(datasetDir(dataId) + "/raw");
    session->stream = arrow::io::FileOutputStream::Open(dataFilePath(dataId)).ValueOrDie();
    return session;
}

void DataCache::appendArrowBatch(ArrowWriteToken token, const std::vector<J>& rows)
{
    if (!token || rows.empty()) return;
    auto* s = static_cast<ArrowWriteSession*>(token);

    // 首次写入：若 schema 未预设则扫描字段
    if (!s->writer) {
        if (s->fieldNames.empty()) scanFields(rows, s->fieldNames, s->numericFields);
        auto table = buildArrowTable(rows, s->fieldNames, s->numericFields);
        s->schema = table->schema();
        s->writer = arrow::ipc::MakeFileWriter(s->stream, s->schema).ValueOrDie();
        s->writer->WriteTable(*table);
        s->totalRows += table->num_rows();
        return;
    }

    // 后续批次：复用 schema
    auto table = buildArrowTable(rows, s->fieldNames, s->numericFields);
    if (!table->schema()->Equals(*s->schema)) return; // schema 不匹配，跳过
    s->writer->WriteTable(*table);
    s->totalRows += table->num_rows();
}

void DataCache::appendArrowTable(ArrowWriteToken token,
                                  const std::shared_ptr<arrow::Table>& table)
{
    if (!token || !table) return;
    auto* s = static_cast<ArrowWriteSession*>(token);
    if (!s->writer) {
        s->schema = table->schema();
        s->writer = arrow::ipc::MakeFileWriter(s->stream, s->schema).ValueOrDie();
    }
    if (!table->schema()->Equals(*s->schema)) return;
    s->writer->WriteTable(*table);
    s->totalRows += table->num_rows();
}

void DataCache::finishArrowWrite(ArrowWriteToken token)
{
    if (!token) return;
    auto* s = static_cast<ArrowWriteSession*>(token);
    if (s->writer) s->writer->Close();
    s->stream.reset();
    INTERNAL_INFO_STREAM << "[DataCache] saved Arrow IPC " << dataFilePath(s->dataId) << ": " << s->totalRows << " rows x " << s->fieldNames.size() << " cols";
    delete s;
}

} // namespace cleaning
