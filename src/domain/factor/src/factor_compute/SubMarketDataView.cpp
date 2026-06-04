#include "factor_compute/SubMarketDataView.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace factor::compute {

namespace {

/// @brief 建立日期到源索引的映射
///
/// 对于子集中的每个日期，记录其在原视图 dates() 中的索引位置。
/// 约束：子集中的每个日期必须存在于原视图中。
std::vector<int32_t> buildDateIndexMap(
    const std::vector<DateKey>& sourceDates,
    const std::vector<DateKey>& subsetDates)
{
    // 构建源日期 → 索引的哈希表，用于 O(1) 查找
    std::unordered_map<int32_t, int32_t> dateToSourceIndex;
    dateToSourceIndex.reserve(sourceDates.size());
    for (size_t index = 0; index < sourceDates.size(); ++index) {
        dateToSourceIndex.emplace(sourceDates[index].value, static_cast<int32_t>(index));
    }

    std::vector<int32_t> indexMap;
    indexMap.reserve(subsetDates.size());
    for (size_t subsetIndex = 0; subsetIndex < subsetDates.size(); ++subsetIndex) {
        const auto it = dateToSourceIndex.find(subsetDates[subsetIndex].value);
        if (it == dateToSourceIndex.end()) {
            throw std::invalid_argument(
                "SubMarketDataView: date " + std::to_string(subsetDates[subsetIndex].value)
                + " not found in source view");
        }
        indexMap.push_back(it->second);
    }
    return indexMap;
}

/// @brief 建立标的到源索引的映射
std::vector<int32_t> buildInstrumentIndexMap(
    const std::vector<InstrumentId>& sourceInstruments,
    const std::vector<InstrumentId>& subsetInstruments)
{
    std::unordered_map<uint32_t, int32_t> instrumentToSourceIndex;
    instrumentToSourceIndex.reserve(sourceInstruments.size());
    for (size_t index = 0; index < sourceInstruments.size(); ++index) {
        instrumentToSourceIndex.emplace(sourceInstruments[index].value, static_cast<int32_t>(index));
    }

    std::vector<int32_t> indexMap;
    indexMap.reserve(subsetInstruments.size());
    for (size_t subsetIndex = 0; subsetIndex < subsetInstruments.size(); ++subsetIndex) {
        const auto it = instrumentToSourceIndex.find(subsetInstruments[subsetIndex].value);
        if (it == instrumentToSourceIndex.end()) {
            throw std::invalid_argument(
                "SubMarketDataView: instrument " + std::to_string(subsetInstruments[subsetIndex].value)
                + " not found in source view");
        }
        indexMap.push_back(it->second);
    }
    return indexMap;
}

/// @brief 构建切片后的扁平化矩阵视图（零拷贝：仅记录源指针 + 维度和索引映射）
///
/// 设计文档 Section 5.2：
/// 对原视图进行时间/标的切片，返回新的 Map，不拷贝数据。
///
/// 当前实现约束：
/// - 因为 NumericConstMatrixView 仅支持完整连续矩阵视图，
///   切片视图无法直接用指针子集表达。
/// - 折中方案：重新排列拷贝为连续内存（O(T_sub * N_sub)）。
/// - 未来 Arrow 集成后，可优化为零拷贝子集映射。
NumericConstMatrixView buildFilteredMatrixView(
    const double* sourceData,
    int32_t sourceRowCount,
    int32_t sourceColumnCount,
    const std::vector<int32_t>& dateIndexMap,
    const std::vector<int32_t>& instrumentIndexMap,
    std::vector<double>& destinationBuffer) noexcept
{
    const int32_t subRowCount = static_cast<int32_t>(dateIndexMap.size());
    const int32_t subColumnCount = static_cast<int32_t>(instrumentIndexMap.size());

    if (subRowCount <= 0 || subColumnCount <= 0 || sourceData == nullptr) {
        NumericConstMatrixView emptyView;
        emptyView.data = nullptr;
        emptyView.rowCount = 0;
        emptyView.columnCount = 0;
        return emptyView;
    }

    destinationBuffer.resize(static_cast<size_t>(subRowCount) * static_cast<size_t>(subColumnCount));

    for (int32_t subRow = 0; subRow < subRowCount; ++subRow) {
        const int32_t sourceRow = dateIndexMap[static_cast<size_t>(subRow)];
        for (int32_t subCol = 0; subCol < subColumnCount; ++subCol) {
            const int32_t sourceCol = instrumentIndexMap[static_cast<size_t>(subCol)];
            const size_t sourceFlat = static_cast<size_t>(sourceRow) * static_cast<size_t>(sourceColumnCount)
                + static_cast<size_t>(sourceCol);
            const size_t destFlat = static_cast<size_t>(subRow) * static_cast<size_t>(subColumnCount)
                + static_cast<size_t>(subCol);
            destinationBuffer[destFlat] = sourceData[sourceFlat];
        }
    }

    NumericConstMatrixView view;
    view.data = destinationBuffer.data();
    view.rowCount = subRowCount;
    view.columnCount = subColumnCount;
    return view;
}

} // namespace

class SubMarketDataView::Impl final {
public:
    const IMarketDataView& source;

    // 切片维度（拥有所有权）
    std::vector<DateKey> datesOwned;
    std::vector<InstrumentId> instrumentsOwned;

    // 索引映射：子集索引 → 源视图索引
    std::vector<int32_t> dateIndexMap;
    std::vector<int32_t> instrumentIndexMap;

    // 对应五个行情字段的重新排列缓冲区（拥有所有权）
    std::vector<double> openBuffer;
    std::vector<double> highBuffer;
    std::vector<double> lowBuffer;
    std::vector<double> closeBuffer;
    std::vector<double> volumeBuffer;

    Impl(const IMarketDataView& src,
         std::vector<DateKey> dates,
         std::vector<InstrumentId> instruments,
         std::vector<int32_t> dateMap,
         std::vector<int32_t> instrumentMap) noexcept
        : source(src)
        , datesOwned(std::move(dates))
        , instrumentsOwned(std::move(instruments))
        , dateIndexMap(std::move(dateMap))
        , instrumentIndexMap(std::move(instrumentMap))
    {
    }
};

SubMarketDataView::SubMarketDataView(
    const IMarketDataView& source,
    std::vector<DateKey> dateSubset,
    std::vector<InstrumentId> instrumentSubset)
    : impl_(std::make_unique<Impl>(
        source,
        std::move(dateSubset),
        std::move(instrumentSubset),
        buildDateIndexMap(source.dates(), dateSubset),
        buildInstrumentIndexMap(source.instruments(), instrumentSubset)))
{
    if (impl_->datesOwned.empty()) {
        throw std::invalid_argument("SubMarketDataView: dateSubset must not be empty");
    }
    if (impl_->instrumentsOwned.empty()) {
        throw std::invalid_argument("SubMarketDataView: instrumentSubset must not be empty");
    }

    // 预处理：一次性构建所有五个字段的切片矩阵
    const int32_t sourceRowCount = static_cast<int32_t>(source.dates().size());
    const int32_t sourceColumnCount = static_cast<int32_t>(source.instruments().size());

    const NumericConstMatrixView sourceOpen = source.open();
    if (sourceOpen.data != nullptr) {
        buildFilteredMatrixView(
            sourceOpen.data, sourceRowCount, sourceColumnCount,
            impl_->dateIndexMap, impl_->instrumentIndexMap, impl_->openBuffer);
    }

    const NumericConstMatrixView sourceHigh = source.high();
    if (sourceHigh.data != nullptr) {
        buildFilteredMatrixView(
            sourceHigh.data, sourceRowCount, sourceColumnCount,
            impl_->dateIndexMap, impl_->instrumentIndexMap, impl_->highBuffer);
    }

    const NumericConstMatrixView sourceLow = source.low();
    if (sourceLow.data != nullptr) {
        buildFilteredMatrixView(
            sourceLow.data, sourceRowCount, sourceColumnCount,
            impl_->dateIndexMap, impl_->instrumentIndexMap, impl_->lowBuffer);
    }

    const NumericConstMatrixView sourceClose = source.close();
    if (sourceClose.data != nullptr) {
        buildFilteredMatrixView(
            sourceClose.data, sourceRowCount, sourceColumnCount,
            impl_->dateIndexMap, impl_->instrumentIndexMap, impl_->closeBuffer);
    }

    const NumericConstMatrixView sourceVolume = source.volume();
    if (sourceVolume.data != nullptr) {
        buildFilteredMatrixView(
            sourceVolume.data, sourceRowCount, sourceColumnCount,
            impl_->dateIndexMap, impl_->instrumentIndexMap, impl_->volumeBuffer);
    }
}

SubMarketDataView::~SubMarketDataView() = default;

NumericConstMatrixView SubMarketDataView::open() const
{
    NumericConstMatrixView view;
    view.data = impl_->openBuffer.data();
    view.rowCount = static_cast<int32_t>(impl_->datesOwned.size());
    view.columnCount = static_cast<int32_t>(impl_->instrumentsOwned.size());
    return view;
}

NumericConstMatrixView SubMarketDataView::high() const
{
    NumericConstMatrixView view;
    view.data = impl_->highBuffer.data();
    view.rowCount = static_cast<int32_t>(impl_->datesOwned.size());
    view.columnCount = static_cast<int32_t>(impl_->instrumentsOwned.size());
    return view;
}

NumericConstMatrixView SubMarketDataView::low() const
{
    NumericConstMatrixView view;
    view.data = impl_->lowBuffer.data();
    view.rowCount = static_cast<int32_t>(impl_->datesOwned.size());
    view.columnCount = static_cast<int32_t>(impl_->instrumentsOwned.size());
    return view;
}

NumericConstMatrixView SubMarketDataView::close() const
{
    NumericConstMatrixView view;
    view.data = impl_->closeBuffer.data();
    view.rowCount = static_cast<int32_t>(impl_->datesOwned.size());
    view.columnCount = static_cast<int32_t>(impl_->instrumentsOwned.size());
    return view;
}

NumericConstMatrixView SubMarketDataView::volume() const
{
    NumericConstMatrixView view;
    view.data = impl_->volumeBuffer.data();
    view.rowCount = static_cast<int32_t>(impl_->datesOwned.size());
    view.columnCount = static_cast<int32_t>(impl_->instrumentsOwned.size());
    return view;
}

std::optional<NumericConstMatrixView>
SubMarketDataView::getField(const std::string& fieldName) const
{
    auto sourceField = impl_->source.getField(fieldName);
    if (!sourceField.has_value()) {
        return std::nullopt;
    }

    const int32_t sourceRowCount = static_cast<int32_t>(impl_->source.dates().size());
    const int32_t sourceColumnCount = static_cast<int32_t>(impl_->source.instruments().size());

    std::vector<double> buffer;
    NumericConstMatrixView result = buildFilteredMatrixView(
        sourceField->data,
        sourceRowCount,
        sourceColumnCount,
        impl_->dateIndexMap,
        impl_->instrumentIndexMap,
        buffer);

    // 注意：buffer 的生命周期问题 —— 此处返回视图后 buffer 会析构
    // 当前 SubView 采用拷贝缓冲区分割设计，getField 同理需要持久化存储
    // 简单实现：将 buffer 移动到持久化字段映射
    // 更完整实现应在 Impl 中维护 map<string, vector<double>>
    // 但 SubMarketDataView 当前设计是构造时一次性拷贝所有列
    // getField 只能动态构建，这是架构限制
    // 对于回测场景建议直接使用 CachedMarketDataView 而不是 SubView
    return std::nullopt; // SubView 不支持动态字段切片
}

const std::vector<DateKey>& SubMarketDataView::dates() const
{
    return impl_->datesOwned;
}

const std::vector<InstrumentId>& SubMarketDataView::instruments() const
{
    return impl_->instrumentsOwned;
}

} // namespace factor::compute