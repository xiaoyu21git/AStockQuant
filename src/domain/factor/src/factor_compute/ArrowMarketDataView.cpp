#include "factor_compute/ArrowMarketDataView.h"
#include "factor_compute/SubMarketDataView.h"

#include "foundation/Utils/Timestamp.h"
#include "foundation/log/logging.hpp"
#include "foundation/perf/Stopwatch.h"

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>

#include <algorithm>
#include <cmath>
#include <limits>
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

/// @brief 单个 record batch 的日期值域 — makeChunkView 跳读索引
/// (批内日期未排序、相邻批范围重叠, 故记录 [min,max] 而非首尾日期)
class BatchDateRange {
public:
    BatchDateRange(int32_t minDate, int32_t maxDate)
        : m_minDate(minDate), m_maxDate(maxDate) {}

    /// @brief 与 [chunkMin, chunkMax] 不相交时整批可跳过
    [[nodiscard]] bool disjoint(int32_t chunkMin, int32_t chunkMax) const {
        return m_maxDate < chunkMin || m_minDate > chunkMax;
    }

private:
    int32_t m_minDate = 0;
    int32_t m_maxDate = 0;
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
// ArrowReaderHandle
// ══════════════════════════════════════════════════════════════════════════════

std::shared_ptr<ArrowReaderHandle> ArrowReaderHandle::open(const std::string& path)
{
    // 打开逻辑唯一实现 — ArrowMarketDataView::Impl 构造与 createReaderHandle 共用
    auto handle = std::make_shared<ArrowReaderHandle>();
    auto inResult = arrow::io::MemoryMappedFile::Open(path, arrow::io::FileMode::READ);
    if (!inResult.ok()) {
        handle->m_lastError = "mmap 失败: " + inResult.status().ToString();
        return handle;
    }
    handle->m_input = inResult.ValueOrDie();

    auto readerResult = arrow::ipc::RecordBatchFileReader::Open(handle->m_input);
    if (!readerResult.ok()) {
        handle->m_lastError = "reader 打开失败: " + readerResult.status().ToString();
        handle->m_input.reset();
        return handle;
    }
    handle->m_reader = readerResult.ValueOrDie();
    return handle;
}

// ══════════════════════════════════════════════════════════════════════════════
// ArrowMarketDataView::Impl
// ══════════════════════════════════════════════════════════════════════════════
class ArrowMarketDataView::Impl {
public:
    explicit Impl(const std::string& path)
        : m_path(path)
    {
        // ── 打开文件，建立 mmap + reader (与 ArrowReaderHandle::open 共用同一打开实现) ──
        auto handle = ArrowReaderHandle::open(path);
        if (!handle || !handle->valid()) {
            INTERNAL_ERROR_STREAM << "[ArrowView] " << (handle ? handle->lastError() : "open failed");
            return;
        }
        input_ = handle->input();
        reader_ = handle->reader();
        const int nBatches = reader_->num_record_batches();
        if (nBatches == 0) {
            INTERNAL_ERROR_STREAM << "[ArrowView] 文件有 0 个批次";
            return;
        }

        // ── 扫描 date + symbol 列，构建索引（逐 batch 流式，不积累全量）──
        // 顺带记录每批日期值域 (batchDateRange_), 供 makeChunkView 跳读不相交批
        std::unordered_map<std::string, int> dateToIdx;
        int nextInst = 0;
        int64_t totalRows = 0;
        batchDateRange_.reserve(nBatches);

        for (int bi = 0; bi < nBatches; ++bi) {
            int32_t batchMin = std::numeric_limits<int32_t>::max();
            int32_t batchMax = std::numeric_limits<int32_t>::min();
            auto batch = reader_->ReadRecordBatch(bi);
            if (batch.ok()) {
                auto b = batch.ValueOrDie();
                auto symArr = batchStringColumn(b, "symbol");
                auto dateArr = batchStringColumn(b, "trade_date");
                if (symArr && dateArr) {
                    for (int64_t j = 0; j < b->num_rows(); ++j) {
                        std::string sym = symArr->IsNull(j) ? "" : symArr->GetString(j);
                        std::string dt = dateArr->IsNull(j) ? "" : dateArr->GetString(j);
                        if (!sym.empty() && localSymbolToInst_.find(sym) == localSymbolToInst_.end())
                            localSymbolToInst_[sym] = nextInst++;
                        if (!dt.empty()) {
                            const int32_t dv = parseDateInt(dt);
                            if (dv > 0) {
                                if (dv < batchMin) batchMin = dv;
                                if (dv > batchMax) batchMax = dv;
                            }
                            if (dateToIdx.find(dt) == dateToIdx.end()) {
                                dateToIdx[dt] = static_cast<int>(dateToIdx.size());
                                dateKeys_.push_back(DateKey{dv});
                            }
                        }
                    }
                }
                totalRows += b->num_rows();
            }
            batchDateRange_.push_back(BatchDateRange(
                (batchMin != std::numeric_limits<int32_t>::max()) ? batchMin : 0,
                (batchMax != std::numeric_limits<int32_t>::min()) ? batchMax : 0));
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

        // ── 排序后重建 O(1) 日期索引 (值 → 行号) ──
        // emplace 保留首个出现下标, 与排序向量中 lower_bound 命中元素一致
        dateIndexMap_.reserve(dateKeys_.size());
        for (std::size_t i = 0; i < dateKeys_.size(); ++i)
            dateIndexMap_.emplace(dateKeys_[i].value, static_cast<int>(i));

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

    // ── 单格取值 (统一取数入口): 空值 → NaN ──
    static double extractCell(const std::shared_ptr<arrow::DoubleArray>& arr, int64_t row)
    {
        if (!arr || arr->IsNull(row)) return std::numeric_limits<double>::quiet_NaN();
        return arr->Value(row);
    }

    /// @brief 列名是否为核心 5 列
    static bool isCoreColumn(const std::string& name)
    {
        return name == "open" || name == "high" || name == "low"
            || name == "close" || name == "volume";
    }

    // ── 多列单扫加载 (核心列与额外列统一处理): 一次全文件扫描同时填充多列 ──
    // 单列加载 (ensureCoreColumn/loadLazyField) 也经此入口, 不按列类型写两套扫描代码
    void loadColumnsOnce(const std::vector<std::string>& names) const
    {
        if (names.empty() || nDates_ == 0 || nInsts_ == 0) return;

        // 过滤未加载列 (核心列看 coreLoaded_, 额外列看 availableFields_)
        std::vector<std::string> pending;
        pending.reserve(names.size());
        for (const auto& n : names) {
            if (isCoreColumn(n)) {
                if (!coreLoaded_.count(n)) { coreLoaded_.insert(n); pending.push_back(n); }
            } else if (availableFields_.count(n)) {
                pending.push_back(n);
            }
        }
        if (pending.empty()) return;

        // 预分配所有目标列 (全 NaN)
        for (const auto& n : pending) {
            auto& cd = isCoreColumn(n) ? coreColumns_[n] : extraFields_[n];
            cd.values.assign(static_cast<size_t>(nDates_) * nInsts_,
                             std::numeric_limits<signal_value_t>::quiet_NaN());
            cd.length = nDates_ * nInsts_;
            cd.ptr = cd.values.data();
        }

        // 单次 batch 扫描, 所有列同时填充 (symbol/date 每行只解析一次)
        const int nBatches = reader_->num_record_batches();
        for (int bi = 0; bi < nBatches; ++bi) {
            auto batchRes = reader_->ReadRecordBatch(bi);
            if (!batchRes.ok()) continue;
            auto batch = batchRes.ValueOrDie();

            auto symArr = batchStringColumn(batch, "symbol");
            auto dateArr = batchStringColumn(batch, "trade_date");
            if (!symArr || !dateArr) continue;

            std::vector<std::shared_ptr<arrow::DoubleArray>> colArrs(pending.size());
            for (size_t ci = 0; ci < pending.size(); ++ci)
                colArrs[ci] = batchDoubleColumn(batch, pending[ci]);

            for (int64_t j = 0; j < batch->num_rows(); ++j) {
                std::string sym = symArr->IsNull(j) ? "" : symArr->GetString(j);
                std::string dt = dateArr->IsNull(j) ? "" : dateArr->GetString(j);
                auto si = localSymbolToInst_.find(sym);
                if (si == localSymbolToInst_.end()) continue;
                const int di = dateIndex(dt);
                if (di < 0) continue;

                for (size_t ci = 0; ci < pending.size(); ++ci) {
                    const double v = extractCell(colArrs[ci], j);
                    if (!std::isfinite(v)) continue;
                    auto& cd = isCoreColumn(pending[ci])
                        ? coreColumns_[pending[ci]] : extraFields_[pending[ci]];
                    cd.values[static_cast<size_t>(di) * nInsts_ + si->second] =
                        static_cast<signal_value_t>(v);
                }
            }
        }
        for (const auto& n : pending)
            if (!isCoreColumn(n)) availableFields_.erase(n);
    }

    // ── 懒加载核心列（全量，首次访问触发）──
    void ensureCoreColumn(const std::string& name) const {
        loadColumnsOnce({name});
    }

    // ── 懒加载额外字段（全量）──
    ColumnData loadLazyField(const std::string& name) const {
        loadColumnsOnce({name});
        auto it = extraFields_.find(name);
        return (it != extraFields_.end()) ? it->second : ColumnData{};
    }

    // ── 分块视图：只加载指定日期区间 + 指定列 (串行路径 — 共享 reader_) ──
    std::unique_ptr<IMarketDataView> makeChunkView(
        const std::vector<DateKey>& dateRange,
        const std::vector<std::string>& columns) const
    {
        return makeChunkViewWithReader(dateRange, columns, reader_);
    }

    // ── 分块视图填充 (指定 reader — 串行共享 reader_, 并行 worker 用独立句柄) ──
    // makeChunkView 与 makeChunkViewConcurrent 共用此唯一实现; 索引 (日期/标的/
    // 跳读) 只读共享, 仅 RecordBatch 读取经各自 reader, 无全局锁。
    std::unique_ptr<IMarketDataView> makeChunkViewWithReader(
        const std::vector<DateKey>& dateRange,
        const std::vector<std::string>& columns,
        const std::shared_ptr<arrow::ipc::RecordBatchFileReader>& reader) const
    {
        if (!indexReady_ || dateRange.empty() || columns.empty())
            return nullptr;

        foundation::perf::Stopwatch swScan;   // 块级扫描计时台账
        swScan.start();

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

        // ── 预分配所有列 ──
        struct ColBuf {
            std::string name;
            ColumnData cd;
            std::shared_ptr<arrow::DoubleArray> arr;
        };
        std::vector<ColBuf> colBufs;
        colBufs.reserve(columns.size());
        for (const auto& colName : columns) {
            ColBuf cb;
            cb.name = colName;
            cb.cd.values.resize(static_cast<size_t>(chunkDates) * chunkInsts,
                                std::numeric_limits<signal_value_t>::quiet_NaN());
            cb.cd.length = chunkDates * chunkInsts;
            cb.cd.ptr = cb.cd.values.data();
            colBufs.push_back(std::move(cb));
        }

        // ── 单次 batch 扫描，所有列同时填充（symbol/date 每行只解析一次）──
        // 跳读: 与块值域不相交的批不可能命中任何块日期 → 整批跳过 (位级一致)
        int32_t chunkMin = std::numeric_limits<int32_t>::max();
        int32_t chunkMax = std::numeric_limits<int32_t>::min();
        for (const auto& d : dateRange) {
            if (d.value < chunkMin) chunkMin = d.value;
            if (d.value > chunkMax) chunkMax = d.value;
        }
        const int nBatches = reader->num_record_batches();
        int scannedBatches = 0;
        int skippedBatches = 0;
        for (int bi = 0; bi < nBatches; ++bi) {
            if (bi < static_cast<int>(batchDateRange_.size())
                && batchDateRange_[static_cast<size_t>(bi)].disjoint(chunkMin, chunkMax)) {
                ++skippedBatches;
                continue;
            }
            ++scannedBatches;
            auto batchRes = reader->ReadRecordBatch(bi);
            if (!batchRes.ok()) continue;
            auto batch = batchRes.ValueOrDie();

            auto symArr = batchStringColumn(batch, "symbol");
            auto dateArr = batchStringColumn(batch, "trade_date");
            if (!symArr || !dateArr) continue;

            // 预取本 batch 的所有列数组（缺失列本 batch 保持 NaN）
            for (auto& cb : colBufs)
                cb.arr = batchDoubleColumn(batch, cb.name);

            for (int64_t j = 0; j < batch->num_rows(); ++j) {
                std::string sym = symArr->IsNull(j) ? "" : symArr->GetString(j);
                std::string dt = dateArr->IsNull(j) ? "" : dateArr->GetString(j);
                auto si = localSymbolToInst_.find(sym);
                if (si == localSymbolToInst_.end()) continue;

                int32_t dateVal = parseDateInt(dt);

                auto ri = dateValToChunkRow.find(dateVal);
                if (ri == dateValToChunkRow.end()) continue;

                for (auto& cb : colBufs) {
                    if (!cb.arr || cb.arr->IsNull(j)) continue;
                    cb.cd.values[static_cast<size_t>(ri->second) * chunkInsts + si->second] =
                        static_cast<signal_value_t>(cb.arr->Value(j));
                }
            }
        }
        for (auto& cb : colBufs)
            view->setColumn(std::move(cb.name), std::move(cb.cd));
        swScan.stop();
        swScan.report("makeChunkView [dates=" + std::to_string(chunkDates)
            + " cols=" + std::to_string(columns.size())
            + " scanned=" + std::to_string(scannedBatches)
            + " skipped=" + std::to_string(skippedBatches) + "]");
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

        INTERNAL_INFO_STREAM << "[MEM] ArrowView::clearColumnCaches: 已释放 "
            << (freedBytes / (1024.0 * 1024.0)) << " MB"
            << " (coreCols=" << coreCols << " extraCols=" << extraCols
            << " coreLoaded=" << coreLoadedCount << ")";
    }

    // ── 日期查找辅助 (O(1) 哈希) ──
    int dateIndex(const std::string& dateStr) const {
        const int32_t dateVal = parseDateInt(dateStr);
        const auto it = dateIndexMap_.find(dateVal);
        return (it != dateIndexMap_.end()) ? it->second : -1;
    }

    // ── 成员 ──
    std::string m_path;                                  // 数据集文件路径 (worker 创建独立句柄用)
    std::shared_ptr<arrow::io::RandomAccessFile> input_;
    std::shared_ptr<arrow::ipc::RecordBatchFileReader> reader_;

    bool indexReady_{false};
    std::vector<DateKey> dateKeys_;
    std::vector<InstrumentId> instruments_;
    std::vector<std::string> symbolStrings_;
    std::unordered_map<std::string, int> localSymbolToInst_;
    std::vector<BatchDateRange> batchDateRange_;   // 每批日期值域 (跳读索引)
    std::unordered_map<int32_t, int> dateIndexMap_; // 排序后日期值 → 行号 (O(1) 查找)
    int nDates_ = 0, nInsts_ = 0;

    // 尚未加载的额外字段集合 (加载后移除 — 懒加载状态, 与 coreLoaded_ 同为 mutable)
    mutable std::unordered_set<std::string> availableFields_;

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
    // 多列单扫: 一次全文件扫描同时填充全部目标列 (核心列与额外列统一处理)
    impl_->loadColumnsOnce(names);
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

std::unique_ptr<IMarketDataView>
ArrowMarketDataView::makeChunkViewConcurrent(const std::vector<DateKey>& dateRange,
                                             const std::vector<std::string>& columns,
                                             const ArrowReaderHandle& handle) const {
    // 并行 worker 路径: 经独立句柄读 RecordBatch, 索引与串行路径共享 (只读)
    return impl_->makeChunkViewWithReader(dateRange, columns, handle.reader());
}

const std::string& ArrowMarketDataView::arrowPath() const {
    return impl_->m_path;
}

std::shared_ptr<ArrowReaderHandle>
ArrowMarketDataView::createReaderHandle(const std::string& path) {
    return ArrowReaderHandle::open(path);
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
