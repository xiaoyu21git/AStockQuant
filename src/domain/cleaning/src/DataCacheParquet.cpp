// DataCacheParquet.cpp — Arrow IPC 持久化实现（纯 C++，Arrow C++ API）
// 文件扩展名 .arrow，格式 Arrow IPC File / Feather
#include "DataCache.h"

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>

#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using J = foundation::json::JsonFacade;

namespace {

void scanFields(const std::vector<J>& rows,
                std::vector<std::string>& fieldNames,
                std::unordered_set<std::string>& numericFields)
{
    std::unordered_map<std::string, bool> fieldNumeric;
    static const char* knownFields[] = {
        "symbol", "code", "stock_code", "name", "asset_class", "status",
        "trade_date", "report_date", "disclosure_date",
        "open", "high", "low", "close", "pre_close",
        "volume", "turnover", "change_pct", "change_amt", "amplitude",
        "turnover_rate", "pe_ratio", "pb_ratio", "market_cap",
        "circulating_market_cap", "pre_adj_factor", "post_adj_factor",
        "data_source", "industry_code", "industry", "trade_status"
    };
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
    fieldNames = {"symbol", "trade_date"};
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
                    builder.Append(v.asString());
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

    for (int64_t i = 0; i < n; ++i) {
        auto obj = J::createObject();
        for (int c = 0; c < table->num_columns(); ++c) {
            const auto& fname = schema->field(c)->name();
            auto chunked = table->column(c);
            auto arrResult = chunked->chunk(0);
            if (!arrResult) continue;

            if (arrResult->type_id() == arrow::Type::DOUBLE) {
                auto dArr = std::static_pointer_cast<arrow::DoubleArray>(arrResult);
                if (!dArr->IsNull(i)) obj.set(fname, J::createDouble(dArr->Value(i)));
            } else if (arrResult->type_id() == arrow::Type::STRING) {
                auto sArr = std::static_pointer_cast<arrow::StringArray>(arrResult);
                if (!sArr->IsNull(i)) obj.set(fname, J::createString(sArr->GetString(i)));
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

    auto table = buildArrowTable(rows, fieldNames, numericFields);

    std::string path = dataFilePath(dataId);
    auto outResult = arrow::io::FileOutputStream::Open(path);
    if (!outResult.ok()) {
        fprintf(stderr, "[DataCache] saveArrow: cannot open %s: %s\n",
                path.c_str(), outResult.status().ToString().c_str());
        fflush(stderr);
        return;
    }

    auto writerResult = arrow::ipc::MakeFileWriter(outResult.ValueOrDie(), table->schema());
    if (!writerResult.ok()) {
        fprintf(stderr, "[DataCache] saveArrow: writer error %s\n",
                writerResult.status().ToString().c_str());
        fflush(stderr);
        return;
    }

    auto writer = writerResult.ValueOrDie();
    auto writeStatus = writer->WriteTable(*table);
    if (!writeStatus.ok()) {
        fprintf(stderr, "[DataCache] saveArrow: write error %s\n",
                writeStatus.ToString().c_str());
        fflush(stderr);
        return;
    }
    writer->Close();

    fprintf(stderr, "[DataCache] saved Arrow IPC %s: %lld rows x %d cols\n",
            path.c_str(), table->num_rows(), table->num_columns());
    fflush(stderr);
}

std::vector<J> DataCache::loadDataSetFile(int dataId)
{
    std::string path = dataFilePath(dataId);

    auto inResult = arrow::io::ReadableFile::Open(path);
    if (!inResult.ok()) {
        fprintf(stderr, "[DataCache] loadArrow: cannot open %s: %s\n",
                path.c_str(), inResult.status().ToString().c_str());
        fflush(stderr);
        return {};
    }

    auto readerResult = arrow::ipc::RecordBatchFileReader::Open(inResult.ValueOrDie());
    if (!readerResult.ok()) {
        fprintf(stderr, "[DataCache] loadArrow: open error %s\n",
                readerResult.status().ToString().c_str());
        fflush(stderr);
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
        fprintf(stderr, "[DataCache] loadArrow: table error %s\n",
                tableResult.status().ToString().c_str());
        fflush(stderr);
        return {};
    }

    auto table = tableResult.ValueOrDie();
    fprintf(stderr, "[DataCache] loaded Arrow IPC %s: %lld rows x %d cols\n",
            path.c_str(), table->num_rows(), table->num_columns());
    fflush(stderr);

    return tableToRows(table);
}

} // namespace cleaning
