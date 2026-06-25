// DataCacheParquet.cpp — Arrow IPC 持久化实现（纯 C++，Arrow C++ API）
// 文件扩展名 .arrow，格式 Arrow IPC File / Feather
#include "DataCache.h"

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>

#include "foundation/log/logging.hpp"
#include <algorithm>
#include <memory>
#include <string>
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
    // 按数据类型定义字段
    // 从数据中动态检测字段（避免硬编码）
    static const int KNOWN_COUNT_KLINE = 30;
    static const int KNOWN_COUNT_FINANCIAL = 22;
    static const char* knownFieldsData[] = {
        // 通用
        CF::SYMBOL, "name", "code", "stock_code", CF::TRADE_DATE,
        "industry", MF::INDUSTRY_CODE,
        // K线数值
        MF::OPEN, MF::HIGH, MF::LOW, MF::CLOSE, MF::PRE_CLOSE,
        MF::VOLUME, MF::TURNOVER, MF::CHANGE_PCT, MF::CHANGE_AMT, MF::AMPLITUDE,
        MF::TURNOVER_RATE, MF::PE_RATIO, MF::PB_RATIO, MF::MARKET_CAP,
        MF::CIRCULATING_MARKET_CAP, MF::PRE_ADJ_FACTOR, MF::POST_ADJ_FACTOR,
        // K线字符串
        "asset_class", "status", CF::DATA_SOURCE, "trade_status",
        F_F::DISCLOSURE_DATE, F_F::REPORT_TYPE,
        // 财务
        F_F::REPORT_DATE, F_F::SYMBOL_ID, F_F::INDICATOR_ID,
        F_F::EPS, F_F::BPS, F_F::ROA, F_F::ROE, F_F::PROFIT_MARGIN, F_F::GROSS_MARGIN, F_F::OPERATING_MARGIN,
        F_F::DEBT_TO_EQUITY, F_F::CURRENT_RATIO, F_F::QUICK_RATIO,
        F_F::OPERATING_CASH_FLOW, F_F::INVESTING_CASH_FLOW, F_F::FINANCING_CASH_FLOW,
        F_F::TOTAL_REVENUE, F_F::NET_PROFIT, F_F::TOTAL_ASSETS, F_F::TOTAL_LIABILITIES, F_F::EQUITY,
        F_F::DIVIDEND_YIELD, F_F::PAYOUT_RATIO, F_F::DIVIDEND_STABILITY,
        F_F::EFFECTIVE_DISCLOSURE_DATE
    };
    std::vector<const char*> knownFields(knownFieldsData, knownFieldsData + sizeof(knownFieldsData)/sizeof(knownFieldsData[0]));
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
                    builder.Append(v.isNumber() ? v.asDouble() : std::numeric_limits<double>::quiet_NaN());
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
