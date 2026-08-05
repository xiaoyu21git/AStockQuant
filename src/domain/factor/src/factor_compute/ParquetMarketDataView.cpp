#include "factor_compute/ParquetMarketDataView.h"
#include "factor_compute/SubMarketDataView.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/exception.h>

namespace factor::compute {

namespace {

/// @brief 将原始 float32 数组封装为 NumericConstMatrixView
NumericConstMatrixView buildMatrixView(
    const signal_value_t* data,
    int32_t rowCount,
    int32_t columnCount) noexcept
{
    NumericConstMatrixView view;
    view.data = data;
    view.rowCount = rowCount;
    view.columnCount = columnCount;
    view.rowStride = columnCount;
    return view;
}

/// @brief 设计文档 Section 5.2：
/// 从 Arrow Table 中按列名提取 float32 数组并接管所有权。
/// Arrow 源数据为 float64，读取时转换为 float32 以节省内存。
/// 返回指向内部数据的 const float* 指针。
struct ColumnData final {
    std::vector<signal_value_t> values;  // float32 存储
    const signal_value_t* rawValues{nullptr};
    int32_t length{0};
};

ColumnData extractColumn(
    const std::shared_ptr<arrow::Table>& table,
    const std::string& columnName)
{
    ColumnData result;
    const auto columnIndex = table->schema()->GetFieldIndex(columnName);
    if (columnIndex < 0) {
        throw std::runtime_error(
            "ParquetMarketDataView: column '" + columnName + "' not found in Parquet schema");
    }

    const auto chunkedArray = table->column(columnIndex);
    if (!chunkedArray) {
        throw std::runtime_error(
            "ParquetMarketDataView: column '" + columnName + "' is null");
    }

    // 合并所有 chunk 为单个 double 数组
    const auto consolidatedResult = chunkedArray->View(
        arrow::float64());
    if (!consolidatedResult.ok()) {
        throw std::runtime_error(
            "ParquetMarketDataView: failed to consolidate column '" + columnName
            + "': " + consolidatedResult.status().ToString());
    }

    // 转换为 float32 存储
    const auto doubleArray = std::static_pointer_cast<arrow::DoubleArray>(
        consolidatedResult.ValueOrDie());
    result.length = static_cast<int32_t>(doubleArray->length());
    result.values.resize(static_cast<size_t>(result.length));
    const double* src = doubleArray->raw_values();
    for (int32_t i = 0; i < result.length; ++i) {
        result.values[static_cast<size_t>(i)] = static_cast<signal_value_t>(src[i]);
    }
    result.rawValues = result.values.data();
    return result;
}

/// @brief 从 Arrow Table 中提取日期列（int32 → DateKey）
std::vector<DateKey> extractDateKeys(
    const std::shared_ptr<arrow::Table>& table,
    const std::string& dateColumnName)
{
    std::vector<DateKey> dateKeys;
    const auto columnIndex = table->schema()->GetFieldIndex(dateColumnName);
    if (columnIndex < 0) {
        return dateKeys;
    }

    const auto chunkedArray = table->column(columnIndex);
    if (!chunkedArray) {
        return dateKeys;
    }

    const auto consolidatedResult = chunkedArray->View(arrow::int32());
    if (!consolidatedResult.ok()) {
        return dateKeys;
    }

    const auto intArray = std::static_pointer_cast<arrow::Int32Array>(
        consolidatedResult.ValueOrDie());
    dateKeys.reserve(static_cast<size_t>(intArray->length()));
    for (int64_t i = 0; i < intArray->length(); ++i) {
        DateKey key;
        key.value = intArray->Value(i);
        dateKeys.push_back(key);
    }
    return dateKeys;
}

/// @brief 从 Arrow Table 中提取标的列（int32 → InstrumentId）
std::vector<InstrumentId> extractInstrumentIds(
    const std::shared_ptr<arrow::Table>& table,
    const std::string& instrumentColumnName)
{
    std::vector<InstrumentId> instruments;
    const auto columnIndex = table->schema()->GetFieldIndex(instrumentColumnName);
    if (columnIndex < 0) {
        return instruments;
    }

    const auto chunkedArray = table->column(columnIndex);
    if (!chunkedArray) {
        return instruments;
    }

    const auto consolidatedResult = chunkedArray->View(arrow::int32());
    if (!consolidatedResult.ok()) {
        return instruments;
    }

    const auto intArray = std::static_pointer_cast<arrow::Int32Array>(
        consolidatedResult.ValueOrDie());
    instruments.reserve(static_cast<size_t>(intArray->length()));
    for (int64_t i = 0; i < intArray->length(); ++i) {
        InstrumentId id;
        id.value = static_cast<uint32_t>(intArray->Value(i));
        instruments.push_back(id);
    }
    return instruments;
}

} // namespace

class ParquetMarketDataView::Impl final {
public:
    std::shared_ptr<arrow::Table> table;
    ColumnData openColumn;
    ColumnData highColumn;
    ColumnData lowColumn;
    ColumnData closeColumn;
    ColumnData volumeColumn;
    std::vector<DateKey> datesOwned;
    std::vector<InstrumentId> instrumentsOwned;
    std::vector<std::string> symbolStringsOwned;
    int32_t rowCount{0};
    int32_t columnCount{0};
};

ParquetMarketDataView::ParquetMarketDataView(const std::string& parquetPath)
    : impl_(std::make_unique<Impl>())
{
    try {
        auto maybeMmap = arrow::io::MemoryMappedFile::Open(
            parquetPath, arrow::io::FileMode::READ);
        if (!maybeMmap.ok()) {
            throw std::runtime_error(
                "ParquetMarketDataView: failed to mmap '" + parquetPath
                + "': " + maybeMmap.status().ToString());
        }
        std::shared_ptr<arrow::io::MemoryMappedFile> mmapFile = maybeMmap.ValueOrDie();

        std::unique_ptr<parquet::arrow::FileReader> reader;
        auto readerStatus = parquet::arrow::OpenFile(
            mmapFile, arrow::default_memory_pool(), &reader);
        if (!readerStatus.ok()) {
            throw std::runtime_error(
                "ParquetMarketDataView: failed to create Parquet reader for '"
                + parquetPath + "': " + readerStatus.ToString());
        }

        std::shared_ptr<arrow::Table> table;
        auto readStatus = reader->ReadTable(&table);
        if (!readStatus.ok()) {
            throw std::runtime_error(
                "ParquetMarketDataView: failed to read table from '"
                + parquetPath + "': " + readStatus.ToString());
        }
        impl_->table = table;

        impl_->openColumn = extractColumn(impl_->table, "open");
        impl_->highColumn = extractColumn(impl_->table, "high");
        impl_->lowColumn = extractColumn(impl_->table, "low");
        impl_->closeColumn = extractColumn(impl_->table, "close");
        impl_->volumeColumn = extractColumn(impl_->table, "volume");

        const int32_t rowCount = impl_->closeColumn.length;
        if (impl_->openColumn.length != rowCount
            || impl_->highColumn.length != rowCount
            || impl_->lowColumn.length != rowCount
            || impl_->volumeColumn.length != rowCount) {
            throw std::runtime_error(
                "ParquetMarketDataView: OHLCV column lengths mismatch in '"
                + parquetPath + "'");
        }

        impl_->datesOwned = extractDateKeys(impl_->table, "date");
        impl_->instrumentsOwned = extractInstrumentIds(impl_->table, "instrument");
        // 尝试读取 symbol 字符串列
        {
            int symIdx = impl_->table->schema()->GetFieldIndex("symbol");
            if (symIdx >= 0) {
                auto symCol = impl_->table->column(symIdx);
                if (symCol && symCol->type_id() == arrow::Type::STRING) {
                    auto strArr = std::static_pointer_cast<arrow::StringArray>(
                        symCol->chunk(0));
                    impl_->symbolStringsOwned.reserve(strArr->length());
                    for (int64_t i = 0; i < strArr->length(); ++i)
                        impl_->symbolStringsOwned.push_back(
                            strArr->IsNull(i) ? "" : strArr->GetString(i));
                }
            }
        }

        if (impl_->datesOwned.empty() && impl_->instrumentsOwned.empty()) {
            impl_->rowCount = rowCount;
            impl_->columnCount = 1;
            return;
        }

        const int32_t instCount = impl_->instrumentsOwned.empty()
            ? 1
            : static_cast<int32_t>(impl_->instrumentsOwned.size());
        const int32_t dateCount = impl_->datesOwned.empty()
            ? (rowCount / instCount)
            : static_cast<int32_t>(impl_->datesOwned.size());

        impl_->rowCount = dateCount;
        impl_->columnCount = instCount;

    } catch (const parquet::ParquetException& e) {
        throw std::runtime_error(
            "ParquetMarketDataView: Parquet exception for '"
            + parquetPath + "': " + std::string(e.what()));
    } catch (const std::exception& e) {
        throw std::runtime_error(
            "ParquetMarketDataView: failed to open '"
            + parquetPath + "': " + std::string(e.what()));
    }
}

ParquetMarketDataView::~ParquetMarketDataView() = default;

NumericConstMatrixView ParquetMarketDataView::open() const
{
    return buildMatrixView(impl_->openColumn.rawValues, impl_->rowCount, impl_->columnCount);
}

NumericConstMatrixView ParquetMarketDataView::high() const
{
    return buildMatrixView(impl_->highColumn.rawValues, impl_->rowCount, impl_->columnCount);
}

NumericConstMatrixView ParquetMarketDataView::low() const
{
    return buildMatrixView(impl_->lowColumn.rawValues, impl_->rowCount, impl_->columnCount);
}

NumericConstMatrixView ParquetMarketDataView::close() const
{
    return buildMatrixView(impl_->closeColumn.rawValues, impl_->rowCount, impl_->columnCount);
}

NumericConstMatrixView ParquetMarketDataView::volume() const
{
    return buildMatrixView(impl_->volumeColumn.rawValues, impl_->rowCount, impl_->columnCount);
}

std::optional<NumericConstMatrixView>
ParquetMarketDataView::getField(const std::string& fieldName) const
{
    try {
        ColumnData column = extractColumn(impl_->table, fieldName);
        if (column.rawValues == nullptr || column.length <= 0) {
            return std::nullopt;
        }
        return buildMatrixView(column.rawValues, impl_->rowCount, impl_->columnCount);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

const std::vector<DateKey>& ParquetMarketDataView::dates() const { return impl_->datesOwned; }
const std::vector<InstrumentId>& ParquetMarketDataView::instruments() const { return impl_->instrumentsOwned; }
const std::vector<std::string>& ParquetMarketDataView::symbolStrings() const { return impl_->symbolStringsOwned; }

std::unique_ptr<IMarketDataView>
ParquetMarketDataView::slice(DateRange dateRange) const
{
    const auto& allDates = dates();
    std::vector<DateKey> dateSubset;
    for (const auto& d : allDates) {
        if (d.value >= dateRange.from.value && d.value <= dateRange.to.value) {
            dateSubset.push_back(d);
        }
    }
    if (dateSubset.empty()) {
        throw std::runtime_error("ParquetMarketDataView::slice(dateRange): empty date subset");
    }
    const auto& allInstruments = instruments();
    return std::make_unique<SubMarketDataView>(
        *this, std::move(dateSubset), std::vector<InstrumentId>(allInstruments));
}

std::unique_ptr<IMarketDataView>
ParquetMarketDataView::slice(const std::vector<InstrumentId>& instrumentIds) const
{
    std::unordered_set<uint32_t> idSet;
    for (const auto& id : instrumentIds) {
        idSet.insert(id.value);
    }
    std::vector<InstrumentId> instSubset;
    const auto& allInstruments = instruments();
    for (const auto& inst : allInstruments) {
        if (idSet.count(inst.value)) {
            instSubset.push_back(inst);
        }
    }
    if (instSubset.empty()) {
        throw std::runtime_error("ParquetMarketDataView::slice(instrumentIds): empty instrument subset");
    }
    const auto& allDates = dates();
    return std::make_unique<SubMarketDataView>(
        *this, std::vector<DateKey>(allDates), std::move(instSubset));
}

} // namespace factor::compute