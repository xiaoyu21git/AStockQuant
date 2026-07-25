#include "factor_compute/ArrowMarketDataView.h"
#include "factor_compute/SubMarketDataView.h"

#include "foundation/Utils/Timestamp.h"
#include "foundation/log/logging.hpp"

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace factor::compute {

namespace {

/// @brief 解析日期字符串 → YYYYMMDD int（直接字符运算，避免 Timestamp::from_string 开销）
inline int32_t parseDateInt(const std::string& s) {
    if (s.size() < 8) return 0;
    if (s.size() >= 10 && s[4] == '-') {
        return (s[0]-'0')*10000000 + (s[1]-'0')*1000000 + (s[2]-'0')*100000 + (s[3]-'0')*10000
             + (s[5]-'0')*1000 + (s[6]-'0')*100 + (s[8]-'0')*10 + (s[9]-'0');
    }
    return (s[0]-'0')*10000000 + (s[1]-'0')*1000000 + (s[2]-'0')*100000 + (s[3]-'0')*10000
         + (s[4]-'0')*1000 + (s[5]-'0')*100 + (s[6]-'0')*10 + (s[7]-'0');
}

NumericConstMatrixView buildMatrixView(
    const signal_value_t* data, int32_t rows, int32_t cols) noexcept
{
    NumericConstMatrixView v;
    v.data = data;
    v.rowCount = rows;
    v.columnCount = cols;
    v.rowStride = cols;
    return v;
}

struct ColumnData {
    std::vector<signal_value_t> values;
    const signal_value_t* ptr = nullptr;
    int32_t length = 0;
};

/// @brief 从单个 RecordBatch 提取指定双精度列
std::shared_ptr<arrow::DoubleArray> batchDoubleColumn(
    const std::shared_ptr<arrow::RecordBatch>& batch, const std::string& name)
{
    int idx = batch->schema()->GetFieldIndex(name);
    if (idx < 0) return nullptr;
    auto arr = batch->column(idx);
    if (!arr) return nullptr;
    return std::static_pointer_cast<arrow::DoubleArray>(arr);
}

/// @brief 从单个 RecordBatch 提取指定字符串列
std::shared_ptr<arrow::StringArray> batchStringColumn(
    const std::shared_ptr<arrow::RecordBatch>& batch, const std::string& name)
{
    int idx = batch->schema()->GetFieldIndex(name);
    if (idx < 0) return nullptr;
    auto arr = batch->column(idx);
    if (!arr) return nullptr;
    return std::static_pointer_cast<arrow::StringArray>(arr);
}

// ══════════════════════════════════════════════════════════════════════════════
// DenseChunkView — 分块自包含视图，独立持有列数据
// ══════════════════════════════════════════════════════════════════════════════
class DenseChunkView final : public IMarketDataView {
public:
    DenseChunkView(std::vector<DateKey> dates,
                   std::vector<InstrumentId> instruments,
                   std::vector<std::string> symbols)
        : dates_(std::move(dates))
        , instruments_(std::move(instruments))
        , symbols_(std::move(symbols))
    {}

    void setColumn(const std::string& name, ColumnData data) {
        columns_[name] = std::move(data);
    }

    [[nodiscard]] NumericConstMatrixView open() const override {
        return columnView("open");
    }
    [[nodiscard]] NumericConstMatrixView high() const override {
        return columnView("high");
    }
    [[nodiscard]] NumericConstMatrixView low() const override {
        return columnView("low");
    }
    [[nodiscard]] NumericConstMatrixView close() const override {
        return columnView("close");
    }
    [[nodiscard]] NumericConstMatrixView volume() const override {
        return columnView("volume");
    }

    [[nodiscard]] std::optional<NumericConstMatrixView>
    getField(const std::string& fieldName) const override {
        auto it = columns_.find(fieldName);
        if (it == columns_.end()) return std::nullopt;
        return buildMatrixView(it->second.ptr,
                               static_cast<int32_t>(dates_.size()),
                               static_cast<int32_t>(instruments_.size()));
    }

    [[nodiscard]] const std::vector<DateKey>& dates() const override { return dates_; }
    [[nodiscard]] const std::vector<InstrumentId>& instruments() const override { return instruments_; }
    [[nodiscard]] const std::vector<std::string>& symbolStrings() const override { return symbols_; }

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(DateRange dateRange) const override {
        auto ds = std::vector<DateKey>();
        for (const auto& d : dates_)
            if (d.value >= dateRange.from.value && d.value <= dateRange.to.value) ds.push_back(d);
        return std::make_unique<SubMarketDataView>(*this, std::move(ds), instruments_);
    }

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(const std::vector<InstrumentId>& ids) const override {
        return std::make_unique<SubMarketDataView>(*this, dates_, ids);
    }

    signal_value_t* mutableFieldData(const std::string& fieldName) override {
        auto it = columns_.find(fieldName);
        return (it != columns_.end()) ? it->second.values.data() : nullptr;
    }
    int32_t fieldDataLength() const override {
        return static_cast<int32_t>(dates_.size()) * static_cast<int32_t>(instruments_.size());
    }
    std::vector<std::string> fieldNames() const override {
        std::vector<std::string> names;
        names.reserve(columns_.size());
        for (const auto& [k, _] : columns_) names.push_back(k);
        return names;
    }

private:
    [[nodiscard]] NumericConstMatrixView columnView(const std::string& name) const {
        auto it = columns_.find(name);
        if (it == columns_.end()) {
            // 返回空视图
            static const signal_value_t s_dummy = 0;
            NumericConstMatrixView v;
            v.data = &s_dummy;
            v.rowCount = 0;
            v.columnCount = 0;
            v.rowStride = 0;
            return v;
        }
        return buildMatrixView(it->second.ptr,
                               static_cast<int32_t>(dates_.size()),
                               static_cast<int32_t>(instruments_.size()));
    }

    std::vector<DateKey> dates_;
    std::vector<InstrumentId> instruments_;
    std::vector<std::string> symbols_;
    std::unordered_map<std::string, ColumnData> columns_;
};

} // anonymous namespace

// ══════════════════════════════════════════════════════════════════════════════
// ArrowMarketDataView::Impl
// ══════════════════════════════════════════════════════════════════════════════
class ArrowMarketDataView::Impl {
public:
    explicit Impl(const std::string& path) {
        // ── 打开文件，建立 mmap + reader ──
        auto inResult = arrow::io::MemoryMappedFile::Open(path, arrow::io::FileMode::READ);
        if (!inResult.ok()) {
            INTERNAL_ERROR_STREAM << "[ArrowView] mmap failed: " << inResult.status().ToString();
            return;
        }
        input_ = inResult.ValueOrDie();

        auto readerResult = arrow::ipc::RecordBatchFileReader::Open(input_);
        if (!readerResult.ok()) {
            INTERNAL_ERROR_STREAM << "[ArrowView] reader open failed: " << readerResult.status().ToString();
            return;
        }
        reader_ = readerResult.ValueOrDie();
        const int nBatches = reader_->num_record_batches();
        if (nBatches == 0) {
            INTERNAL_ERROR_STREAM << "[ArrowView] file has 0 batches";
            return;
        }

        // ── 扫描 date + symbol 列，构建索引（逐 batch 流式，不积累全量）──
        std::unordered_map<std::string, int> dateToIdx;
        int nextInst = 0;
        int64_t totalRows = 0;

        for (int bi = 0; bi < nBatches; ++bi) {
            auto batch = reader_->ReadRecordBatch(bi);
            if (!batch.ok()) continue;
            auto b = batch.ValueOrDie();
            auto symArr = batchStringColumn(b, "symbol");
            auto dateArr = batchStringColumn(b, "trade_date");
            if (!symArr || !dateArr) continue;

            for (int64_t j = 0; j < b->num_rows(); ++j) {
                std::string sym = symArr->IsNull(j) ? "" : symArr->GetString(j);
                std::string dt = dateArr->IsNull(j) ? "" : dateArr->GetString(j);
                if (!sym.empty() && localSymbolToInst_.find(sym) == localSymbolToInst_.end())
                    localSymbolToInst_[sym] = nextInst++;
                if (!dt.empty() && dateToIdx.find(dt) == dateToIdx.end()) {
                    dateToIdx[dt] = static_cast<int>(dateToIdx.size());
                    dateKeys_.push_back(DateKey{parseDateInt(dt)});
                }
            }
            totalRows += b->num_rows();
        }

        nDates_ = static_cast<int>(dateKeys_.size());
        nInsts_ = static_cast<int>(localSymbolToInst_.size());
        if (nDates_ == 0 || nInsts_ == 0) {
            INTERNAL_WARN_STREAM << "[ArrowView] empty index: dates=" << nDates_ << " insts=" << nInsts_;
            return;
        }

        // ── 构建 instruments / symbols ──
        instruments_.resize(nInsts_);
        symbolStrings_.resize(nInsts_);
        for (const auto& [sym, id] : localSymbolToInst_) {
            instruments_[id] = InstrumentId{static_cast<uint32_t>(id)};
            symbolStrings_[id] = sym;
        }

        // 按日期排序
        std::sort(dateKeys_.begin(), dateKeys_.end(),
            [](const DateKey& a, const DateKey& b) { return a.value < b.value; });

        // ── 记录可用字段名 ──
        {
            auto firstBatch = reader_->ReadRecordBatch(0).ValueOrDie();
            for (const auto& f : firstBatch->schema()->field_names()) {
                if (f != "symbol" && f != "trade_date" && f != "__index_level_0__")
                    availableFields_.insert(f);
            }
        }

        indexReady_ = true;

        INTERNAL_INFO_STREAM << "[ArrowView] " << path << ": " << nDates_ << " dates x " << nInsts_
            << " insts, " << availableFields_.size() << " fields, batches=" << nBatches
            << ", rows=" << static_cast<long long>(totalRows) << " (lazy load)";
    }

    // ── 懒加载核心列（全量，首次访问触发）──
    void ensureCoreColumn(const std::string& name) const {
        if (coreLoaded_.count(name)) return;
        coreLoaded_.insert(name);

        auto& cd = coreColumns_[name];
        cd.values.resize(static_cast<size_t>(nDates_) * nInsts_,
                         std::numeric_limits<signal_value_t>::quiet_NaN());
        cd.length = nDates_ * nInsts_;
        cd.ptr = cd.values.data();

        const int nBatches = reader_->num_record_batches();
        for (int bi = 0; bi < nBatches; ++bi) {
            auto batchRes = reader_->ReadRecordBatch(bi);
            if (!batchRes.ok()) continue;
            auto batch = batchRes.ValueOrDie();

            auto symArr = batchStringColumn(batch, "symbol");
            auto dateArr = batchStringColumn(batch, "trade_date");
            auto colArr = batchDoubleColumn(batch, name);
            if (!symArr || !dateArr || !colArr) continue;

            for (int64_t j = 0; j < batch->num_rows(); ++j) {
                if (colArr->IsNull(j)) continue;
                std::string sym = symArr->IsNull(j) ? "" : symArr->GetString(j);
                std::string dt = dateArr->IsNull(j) ? "" : dateArr->GetString(j);
                auto si = localSymbolToInst_.find(sym);
                auto di = dateIndex(dt);
                if (si != localSymbolToInst_.end() && di >= 0)
                    cd.values[static_cast<size_t>(di) * nInsts_ + si->second] =
                        static_cast<signal_value_t>(colArr->Value(j));
            }
        }
    }

    // ── 懒加载额外字段（全量）──
    ColumnData loadLazyField(const std::string& name) const {
        ColumnData cd;
        if (nDates_ == 0 || nInsts_ == 0) return cd;
        cd.values.resize(static_cast<size_t>(nDates_) * nInsts_,
                         std::numeric_limits<signal_value_t>::quiet_NaN());
        cd.length = nDates_ * nInsts_;
        cd.ptr = cd.values.data();

        const int nBatches = reader_->num_record_batches();
        for (int bi = 0; bi < nBatches; ++bi) {
            auto batchRes = reader_->ReadRecordBatch(bi);
            if (!batchRes.ok()) continue;
            auto batch = batchRes.ValueOrDie();

            auto symArr = batchStringColumn(batch, "symbol");
            auto dateArr = batchStringColumn(batch, "trade_date");
            auto colArr = batchDoubleColumn(batch, name);
            if (!symArr || !dateArr || !colArr) continue;

            for (int64_t j = 0; j < batch->num_rows(); ++j) {
                if (colArr->IsNull(j)) continue;
                std::string sym = symArr->IsNull(j) ? "" : symArr->GetString(j);
                std::string dt = dateArr->IsNull(j) ? "" : dateArr->GetString(j);
                auto si = localSymbolToInst_.find(sym);
                auto di = dateIndex(dt);
                if (si != localSymbolToInst_.end() && di >= 0)
                    cd.values[static_cast<size_t>(di) * nInsts_ + si->second] =
                        static_cast<signal_value_t>(colArr->Value(j));
            }
        }
        return cd;
    }

    // ── 分块视图：只加载指定日期区间 + 指定列 ──
    std::unique_ptr<IMarketDataView> makeChunkView(
        const std::vector<DateKey>& dateRange,
        const std::vector<std::string>& columns) const
    {
        if (!indexReady_ || dateRange.empty() || columns.empty())
            return nullptr;

        // 构建 dateRange → 行索引 映射（按值查找，因为 dateRange 是 dateKeys_ 的子集）
        std::unordered_map<int32_t, int> dateValToChunkRow;
        for (size_t i = 0; i < dateRange.size(); ++i)
            dateValToChunkRow[dateRange[i].value] = static_cast<int>(i);

        const int chunkDates = static_cast<int>(dateRange.size());
        const int chunkInsts = nInsts_;

        auto view = std::make_unique<DenseChunkView>(
            std::vector<DateKey>(dateRange),
            std::vector<InstrumentId>(instruments_),
            std::vector<std::string>(symbolStrings_));

        const int nBatches = reader_->num_record_batches();
        for (const auto& colName : columns) {
            ColumnData cd;
            cd.values.resize(static_cast<size_t>(chunkDates) * chunkInsts,
                             std::numeric_limits<signal_value_t>::quiet_NaN());
            cd.length = chunkDates * chunkInsts;
            cd.ptr = cd.values.data();

            for (int bi = 0; bi < nBatches; ++bi) {
                auto batchRes = reader_->ReadRecordBatch(bi);
                if (!batchRes.ok()) continue;
                auto batch = batchRes.ValueOrDie();

                auto symArr = batchStringColumn(batch, "symbol");
                auto dateArr = batchStringColumn(batch, "trade_date");
                auto colArr = batchDoubleColumn(batch, colName);
                if (!symArr || !dateArr || !colArr) continue;

                for (int64_t j = 0; j < batch->num_rows(); ++j) {
                    if (colArr->IsNull(j)) continue;
                    std::string sym = symArr->IsNull(j) ? "" : symArr->GetString(j);
                    std::string dt = dateArr->IsNull(j) ? "" : dateArr->GetString(j);
                    auto si = localSymbolToInst_.find(sym);
                    if (si == localSymbolToInst_.end()) continue;

                    int32_t dateVal = parseDateInt(dt);

                    auto ri = dateValToChunkRow.find(dateVal);
                    if (ri == dateValToChunkRow.end()) continue;

                    cd.values[static_cast<size_t>(ri->second) * chunkInsts + si->second] =
                        static_cast<signal_value_t>(colArr->Value(j));
                }
            }
            view->setColumn(colName, std::move(cd));
        }
        return view;
    }

    // ── 清除懒加载列缓存 ──
    void clearColumnCaches() {
        size_t coreCols = coreColumns_.size();
        size_t extraCols = extraFields_.size();
        size_t coreLoadedCount = coreLoaded_.size();

        // 估算释放内存
        size_t freedBytes = 0;
        for (auto& [name, cd] : coreColumns_) {
            freedBytes += cd.values.size() * sizeof(signal_value_t);
        }
        for (auto& [name, cd] : extraFields_) {
            freedBytes += cd.values.size() * sizeof(signal_value_t);
        }

        coreLoaded_.clear();
        coreColumns_.clear();
        extraFields_.clear();

        INTERNAL_INFO_STREAM << "[MEM] ArrowView::clearColumnCaches: freed "
            << (freedBytes / (1024.0 * 1024.0)) << " MB"
            << " (coreCols=" << coreCols << " extraCols=" << extraCols
            << " coreLoaded=" << coreLoadedCount << ")";
    }

    // ── 日期查找辅助 ──
    int dateIndex(const std::string& dateStr) const {
        int32_t dateVal = parseDateInt(dateStr);
        auto it = std::lower_bound(dateKeys_.begin(), dateKeys_.end(), dateVal,
            [](const DateKey& dk, int32_t v) { return dk.value < v; });
        if (it == dateKeys_.end() || it->value != dateVal) return -1;
        return static_cast<int>(it - dateKeys_.begin());
    }

    // ── 成员 ──
    std::shared_ptr<arrow::io::RandomAccessFile> input_;
    std::shared_ptr<arrow::ipc::RecordBatchFileReader> reader_;

    bool indexReady_{false};
    std::vector<DateKey> dateKeys_;
    std::vector<InstrumentId> instruments_;
    std::vector<std::string> symbolStrings_;
    std::unordered_map<std::string, int> localSymbolToInst_;
    int nDates_ = 0, nInsts_ = 0;

    std::unordered_set<std::string> availableFields_;

    // 懒加载缓存（全量）
    mutable std::unordered_set<std::string> coreLoaded_;
    mutable std::unordered_map<std::string, ColumnData> coreColumns_;
    mutable std::unordered_map<std::string, ColumnData> extraFields_;
};

// ══════════════════════════════════════════════════════════════════════════════
// ArrowMarketDataView 公共接口
// ══════════════════════════════════════════════════════════════════════════════

ArrowMarketDataView::ArrowMarketDataView(const std::string& path)
    : impl_(std::make_unique<Impl>(path)) {}
ArrowMarketDataView::~ArrowMarketDataView() = default;

NumericConstMatrixView ArrowMarketDataView::open() const {
    impl_->ensureCoreColumn("open");
    auto& cd = impl_->coreColumns_["open"];
    return buildMatrixView(cd.ptr, impl_->nDates_, impl_->nInsts_);
}
NumericConstMatrixView ArrowMarketDataView::high() const {
    impl_->ensureCoreColumn("high");
    auto& cd = impl_->coreColumns_["high"];
    return buildMatrixView(cd.ptr, impl_->nDates_, impl_->nInsts_);
}
NumericConstMatrixView ArrowMarketDataView::low() const {
    impl_->ensureCoreColumn("low");
    auto& cd = impl_->coreColumns_["low"];
    return buildMatrixView(cd.ptr, impl_->nDates_, impl_->nInsts_);
}
NumericConstMatrixView ArrowMarketDataView::close() const {
    impl_->ensureCoreColumn("close");
    auto& cd = impl_->coreColumns_["close"];
    return buildMatrixView(cd.ptr, impl_->nDates_, impl_->nInsts_);
}
NumericConstMatrixView ArrowMarketDataView::volume() const {
    impl_->ensureCoreColumn("volume");
    auto& cd = impl_->coreColumns_["volume"];
    return buildMatrixView(cd.ptr, impl_->nDates_, impl_->nInsts_);
}

std::optional<NumericConstMatrixView>
ArrowMarketDataView::getField(const std::string& name) const {
    if (name == "open")  return open();
    if (name == "high")  return high();
    if (name == "low")   return low();
    if (name == "close") return close();
    if (name == "volume")return volume();

    auto it = impl_->extraFields_.find(name);
    if (it != impl_->extraFields_.end())
        return buildMatrixView(it->second.ptr, impl_->nDates_, impl_->nInsts_);

    if (impl_->availableFields_.count(name)) {
        impl_->extraFields_[name] = impl_->loadLazyField(name);
        impl_->availableFields_.erase(name);
        auto jt = impl_->extraFields_.find(name);
        if (jt != impl_->extraFields_.end())
            return buildMatrixView(jt->second.ptr, impl_->nDates_, impl_->nInsts_);
    }
    return std::nullopt;
}

void ArrowMarketDataView::ensureColumns(const std::vector<std::string>& names) const {
    for (const auto& n : names) {
        if (n == "open" || n == "high" || n == "low" || n == "close" || n == "volume") {
            impl_->ensureCoreColumn(n);
        } else if (impl_->availableFields_.count(n)) {
            impl_->extraFields_[n] = impl_->loadLazyField(n);
            impl_->availableFields_.erase(n);
        }
    }
}

void ArrowMarketDataView::clearColumnCaches() const {
    impl_->clearColumnCaches();
}

std::vector<std::string> ArrowMarketDataView::fieldNames() const {
    std::vector<std::string> names = {"open","high","low","close","volume"};
    for (const auto& f : impl_->availableFields_)
        if (f != "open" && f != "high" && f != "low" && f != "close" && f != "volume")
            names.push_back(f);
    return names;
}

std::unique_ptr<IMarketDataView>
ArrowMarketDataView::makeChunkView(const std::vector<DateKey>& dateRange,
                                    const std::vector<std::string>& columns) const {
    return impl_->makeChunkView(dateRange, columns);
}

const std::vector<DateKey>& ArrowMarketDataView::dates() const { return impl_->dateKeys_; }
const std::vector<InstrumentId>& ArrowMarketDataView::instruments() const { return impl_->instruments_; }
const std::vector<std::string>& ArrowMarketDataView::symbolStrings() const { return impl_->symbolStrings_; }

std::unique_ptr<IMarketDataView> ArrowMarketDataView::slice(DateRange range) const {
    auto ds = std::vector<DateKey>();
    for (const auto& d : impl_->dateKeys_)
        if (d.value >= range.from.value && d.value <= range.to.value) ds.push_back(d);
    return std::make_unique<SubMarketDataView>(*this, std::move(ds), impl_->instruments_);
}

std::unique_ptr<IMarketDataView> ArrowMarketDataView::slice(
    const std::vector<InstrumentId>& ids) const {
    return std::make_unique<SubMarketDataView>(*this, impl_->dateKeys_, ids);
}

} // namespace factor::compute
