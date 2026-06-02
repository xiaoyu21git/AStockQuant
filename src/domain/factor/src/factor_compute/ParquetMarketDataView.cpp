#include "factor_compute/ParquetMarketDataView.h"

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

/// @brief 将原始 double 数组封装为 NumericConstMatrixView
NumericConstMatrixView buildMatrixView(
    const double* data,
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
/// 从 Arrow Table 中按列名提取 double 数组并接管所有权。
/// 返回指向内部数据的 const double* 指针。
struct ColumnData final {
    std::shared_ptr<arrow::DoubleArray> array;
    const double* rawValues{nullptr};
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

    // 合并所有 chunk 为单个数组
    const auto consolidatedResult = chunkedArray->View(
        arrow::float64());
    if (!consolidatedResult.ok()) {
        throw std::runtime_error(
            "ParquetMarketDataView: failed to consolidate column '" + columnName
            + "': " + consolidatedResult.status().ToString());
    }

    const auto consolidatedArray = consolidatedResult.ValueOrDie();
    result.array = std::static_pointer_cast<arrow::DoubleArray>(consolidatedArray);
    result.rawValues = result.array->raw_values();
    result.length = static_cast<int32_t>(result.array->length());
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
        // 如果无日期列，生成默认序列
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
    // Arrow Table 持有所有列数据的所有权，保证 raw_values 指针有效
    std::shared_ptr<arrow::Table> table;

    // 各列数据的包装（持有 array 引用，保证 raw_values 生命周期）
    ColumnData openColumn;
    ColumnData highColumn;
    ColumnData lowColumn;
    ColumnData closeColumn;
    ColumnData volumeColumn;

    // 维度的拥有性副本
    std::vector<DateKey> datesOwned;
    std::vector<InstrumentId> instrumentsOwned;

    // 维度信息
    int32_t rowCount{0};
    int32_t columnCount{0};
};

ParquetMarketDataView::ParquetMarketDataView(const std::string& parquetPath)
    : impl_(std::make_unique<Impl>())
{
    // P4-T2：列式 mmap 与零拷贝读路径
    //
    // 设计文档 Section 5.2 预期流程：
    // 1. arrow::io::MemoryMappedFile::Open(parquetPath, arrow::io::FileMode::READ)
    // 2. parquet::arrow::FileReader::Make(arrow::default_memory_pool(), ...)
    // 3. 按列读取为 arrow::Table
    // 4. 通过 arrow::DoubleArray::raw_values() 获取 const double* 指针
    // 5. 构造 NumericConstMatrixView 时绑定这些指针

    try {
        // Step 1: 打开 Parquet 文件的内存映射
        auto maybeMmap = arrow::io::MemoryMappedFile::Open(
            parquetPath, arrow::io::FileMode::READ);
        if (!maybeMmap.ok()) {
            throw std::runtime_error(
                "ParquetMarketDataView: failed to mmap '" + parquetPath
                + "': " + maybeMmap.status().ToString());
        }
        std::shared_ptr<arrow::io::MemoryMappedFile> mmapFile =
            maybeMmap.ValueOrDie();

        // Step 2: 创建 Parquet reader
        std::unique_ptr<parquet::arrow::FileReader> reader;
        auto readerStatus = parquet::arrow::OpenFile(
            mmapFile, arrow::default_memory_pool(), &reader);
        if (!readerStatus.ok()) {
            throw std::runtime_error(
                "ParquetMarketDataView: failed to create Parquet reader for '"
                + parquetPath + "': " + readerStatus.ToString());
        }

        // Step 3: 读取整个表（按列）
        std::shared_ptr<arrow::Table> table;
        auto readStatus = reader->ReadTable(&table);
        if (!readStatus.ok()) {
            throw std::runtime_error(
                "ParquetMarketDataView: failed to read table from '"
                + parquetPath + "': " + readStatus.ToString());
        }
        impl_->table = table;

        // Step 4: 提取 OHLCV 五列
        impl_->openColumn = extractColumn(impl_->table, "open");
        impl_->highColumn = extractColumn(impl_->table, "high");
        impl_->lowColumn = extractColumn(impl_->table, "low");
        impl_->closeColumn = extractColumn(impl_->table, "close");
        impl_->volumeColumn = extractColumn(impl_->table, "volume");

        // 验证列长度一致性
        const int32_t rowCount = impl_->closeColumn.length;
        if (impl_->openColumn.length != rowCount
            || impl_->highColumn.length != rowCount
            || impl_->lowColumn.length != rowCount
            || impl_->volumeColumn.length != rowCount) {
            throw std::runtime_error(
                "ParquetMarketDataView: OHLCV column lengths mismatch in '"
                + parquetPath + "'");
        }

        // Step 5: 提取维度信息
        impl_->datesOwned = extractDateKeys(impl_->table, "date");
        impl_->instrumentsOwned = extractInstrumentIds(impl_->table, "instrument");

        // 如果 Parquet 中无显式日期/标的列，从维度推断
        if (impl_->datesOwned.empty() && impl_->instrumentsOwned.empty()) {
            // 从 Parquet 行数推断：假设行优先格式
            // 实际使用时由外部注入维度信息
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
    return buildMatrixView(
        impl_->openColumn.rawValues,
        impl_->rowCount,
        impl_->columnCount);
}

NumericConstMatrixView ParquetMarketDataView::high() const
{
    return buildMatrixView(
        impl_->highColumn.rawValues,
        impl_->rowCount,
        impl_->columnCount);
}

NumericConstMatrixView ParquetMarketDataView::low() const
{
    return buildMatrixView(
        impl_->lowColumn.rawValues,
        impl_->rowCount,
        impl_->columnCount);
}

NumericConstMatrixView ParquetMarketDataView::close() const
{
    return buildMatrixView(
        impl_->closeColumn.rawValues,
        impl_->rowCount,
        impl_->columnCount);
}

NumericConstMatrixView ParquetMarketDataView::volume() const
{
    return buildMatrixView(
        impl_->volumeColumn.rawValues,
        impl_->rowCount,
        impl_->columnCount);
}

const std::vector<DateKey>& ParquetMarketDataView::dates() const
{
    return impl_->datesOwned;
}

const std::vector<InstrumentId>& ParquetMarketDataView::instruments() const
{
    return impl_->instrumentsOwned;
}

} // namespace factor::compute