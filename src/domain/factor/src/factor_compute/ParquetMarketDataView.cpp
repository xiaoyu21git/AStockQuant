#include "factor_compute/ParquetMarketDataView.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace factor::compute {

namespace {

/// @brief 将原始 double 数组封装为 NumericConstMatrixView
///
/// 约束：调用方确保 data 生命周期在视图使用期间有效。
NumericConstMatrixView buildMatrixView(
    const double* data,
    int32_t rowCount,
    int32_t columnCount) noexcept
{
    NumericConstMatrixView view;
    view.data = data;
    view.rowCount = rowCount;
    view.columnCount = columnCount;
    return view;
}

/// @brief 将日期数组转换为 DateKey 数组（简单映射：YYYYMMDD int → DateKey）
std::vector<DateKey> buildDateKeys(const std::vector<int32_t>& rawDates)
{
    std::vector<DateKey> dateKeys;
    dateKeys.reserve(rawDates.size());
    for (int32_t rawDate : rawDates) {
        DateKey key;
        key.value = rawDate;
        dateKeys.push_back(key);
    }
    return dateKeys;
}

/// @brief 将标的 int ID 数组转换为 InstrumentId 数组
std::vector<InstrumentId> buildInstrumentIds(const std::vector<int32_t>& rawIds)
{
    std::vector<InstrumentId> instruments;
    instruments.reserve(rawIds.size());
    for (int32_t rawId : rawIds) {
        InstrumentId id;
        id.value = static_cast<uint32_t>(rawId);
        instruments.push_back(id);
    }
    return instruments;
}

} // namespace

class ParquetMarketDataView::Impl final {
public:
    // 列式数据存储（拥有所有权）
    std::vector<double> openData;
    std::vector<double> highData;
    std::vector<double> lowData;
    std::vector<double> closeData;
    std::vector<double> volumeData;

    // 维度的拥有性副本
    std::vector<DateKey> datesOwned;
    std::vector<InstrumentId> instrumentsOwned;

    // 行数 = 日期数，列数 = 标的数
    int32_t rowCount{0};
    int32_t columnCount{0};
};

ParquetMarketDataView::ParquetMarketDataView(const std::string& parquetPath)
    : impl_(std::make_unique<Impl>())
{
    // 设计文档 Section 5.2：
    // 实际实现应使用 Arrow/Parquet C++ API 进行内存映射（mmap）零拷贝读取。
    // 当前为占位实现，等待 Arrow 集成。
    //
    // 预期流程：
    // 1. arrow::io::MemoryMappedFile::Open(parquetPath, arrow::io::FileMode::READ)
    // 2. parquet::arrow::FileReader::Make(arrow::default_memory_pool(), ...)
    // 3. 按列读取为 arrow::ChunkedArray
    // 4. 通过 arrow::DoubleArray::raw_values() 获取 const double* 指针
    // 5. 构造 NumericConstMatrixView 时绑定这些指针

    (void)parquetPath;
    throw std::runtime_error(
        "ParquetMarketDataView: Arrow/Parquet integration not yet implemented. "
        "File: " + parquetPath);
}

ParquetMarketDataView::~ParquetMarketDataView() = default;

NumericConstMatrixView ParquetMarketDataView::open() const
{
    return buildMatrixView(
        impl_->openData.data(),
        impl_->rowCount,
        impl_->columnCount);
}

NumericConstMatrixView ParquetMarketDataView::high() const
{
    return buildMatrixView(
        impl_->highData.data(),
        impl_->rowCount,
        impl_->columnCount);
}

NumericConstMatrixView ParquetMarketDataView::low() const
{
    return buildMatrixView(
        impl_->lowData.data(),
        impl_->rowCount,
        impl_->columnCount);
}

NumericConstMatrixView ParquetMarketDataView::close() const
{
    return buildMatrixView(
        impl_->closeData.data(),
        impl_->rowCount,
        impl_->columnCount);
}

NumericConstMatrixView ParquetMarketDataView::volume() const
{
    return buildMatrixView(
        impl_->volumeData.data(),
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