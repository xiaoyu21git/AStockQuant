#include "factor_compute/ArrowMarketDataView.h"
#include "factor_compute/SubMarketDataView.h"

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>

namespace factor::compute {

namespace {

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

ColumnData extractFloatColumn(const std::shared_ptr<arrow::Table>& table,
                               const std::string& name)
{
    ColumnData result;
    int idx = table->schema()->GetFieldIndex(name);
    if (idx < 0) return result;

    auto col = table->column(idx);
    if (!col) return result;

    auto arr = std::static_pointer_cast<arrow::DoubleArray>(
        col->chunk(0));
    if (!arr) return result;

    result.length = static_cast<int32_t>(arr->length());
    result.values.resize(result.length);
    const double* src = arr->raw_values();
    for (int32_t i = 0; i < result.length; ++i)
        result.values[i] = static_cast<signal_value_t>(src[i]);
    result.ptr = result.values.data();
    return result;
}

std::vector<std::string> extractStrings(const std::shared_ptr<arrow::Table>& table,
                                         const std::string& name)
{
    int idx = table->schema()->GetFieldIndex(name);
    if (idx < 0) return {};
    auto col = table->column(idx);
    if (!col) return {};
    auto arr = std::static_pointer_cast<arrow::StringArray>(col->chunk(0));
    if (!arr) return {};
    std::vector<std::string> result(arr->length());
    for (int64_t i = 0; i < arr->length(); ++i)
        result[i] = arr->GetString(i);
    return result;
}

} // anonymous namespace

class ArrowMarketDataView::Impl {
public:
    explicit Impl(const std::string& path) {
        auto in = arrow::io::ReadableFile::Open(path).ValueOrDie();
        auto reader = arrow::ipc::RecordBatchFileReader::Open(in).ValueOrDie();
        std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
        for (int i = 0; i < reader->num_record_batches(); ++i)
            batches.push_back(reader->ReadRecordBatch(i).ValueOrDie());
        auto table = arrow::Table::FromRecordBatches(batches).ValueOrDie();

        int64_t nRows = table->num_rows();

        // 读取字符串列
        auto symbols = extractStrings(table, "symbol");
        auto dates = extractStrings(table, "trade_date");

        // 建立 (symbol, date) → rowIndex 映射
        std::unordered_map<std::string, int> symbolToInst;
        std::unordered_map<std::string, int> dateToIdx;
        int nextInst = 0;
        for (int64_t i = 0; i < nRows; ++i) {
            std::string sym = i < static_cast<int64_t>(symbols.size()) ? symbols[i] : "";
            std::string dt = i < static_cast<int64_t>(dates.size()) ? dates[i] : "";
            if (!sym.empty() && symbolToInst.find(sym) == symbolToInst.end())
                symbolToInst[sym] = nextInst++;
            if (!dt.empty() && dateToIdx.find(dt) == dateToIdx.end()) {
                dateToIdx[dt] = static_cast<int>(dateToIdx.size());
                dateKeys_.push_back(DateKey{static_cast<int32_t>(std::stoi(dt))});
            }
        }

        int nDates = static_cast<int>(dateKeys_.size());
        int nInsts = static_cast<int>(symbolToInst.size());
        if (nDates == 0 || nInsts == 0) return;

        // 分配矩阵
        auto allocMat = [&](const std::string& colName) -> ColumnData {
            ColumnData cd;
            cd.values.resize(nDates * nInsts, std::numeric_limits<signal_value_t>::quiet_NaN());
            cd.length = nDates * nInsts;
            cd.ptr = cd.values.data();

            auto raw = extractFloatColumn(table, colName);
            if (raw.length == 0) return cd;

            // 重新排序：按 (dateIdx, instIdx) 写入
            for (int64_t i = 0; i < nRows && i < raw.length; ++i) {
                std::string sym = i < static_cast<int64_t>(symbols.size()) ? symbols[i] : "";
                std::string dt = i < static_cast<int64_t>(dates.size()) ? dates[i] : "";
                auto si = symbolToInst.find(sym);
                auto di = dateToIdx.find(dt);
                if (si != symbolToInst.end() && di != dateToIdx.end()) {
                    int idx = di->second * nInsts + si->second;
                    cd.values[idx] = raw.ptr[i];
                }
            }
            return cd;
        };

        openCol_ = allocMat("open");
        highCol_ = allocMat("high");
        lowCol_ = allocMat("low");
        closeCol_ = allocMat("close");
        volCol_ = allocMat("volume");

        instruments_.resize(nInsts);
        symbolStrings_.resize(nInsts);
        for (const auto& [sym, id] : symbolToInst) {
            instruments_[id] = InstrumentId{static_cast<uint32_t>(id)};
            symbolStrings_[id] = sym;
        }

        // 额外字段
        for (const auto& field : table->schema()->field_names()) {
            if (field == "symbol" || field == "trade_date" ||
                field == "open" || field == "high" || field == "low" ||
                field == "close" || field == "volume") continue;
            extraFields_[field] = allocMat(field);
        }

        fprintf(stderr, "[ArrowView] loaded %s: %d dates x %d insts\n",
                path.c_str(), nDates, nInsts);
        fflush(stderr);
    }

    ColumnData openCol_, highCol_, lowCol_, closeCol_, volCol_;
    std::vector<DateKey> dateKeys_;
    std::vector<InstrumentId> instruments_;
    std::vector<std::string> symbolStrings_;
    std::unordered_map<std::string, ColumnData> extraFields_;
};

ArrowMarketDataView::ArrowMarketDataView(const std::string& path)
    : impl_(std::make_unique<Impl>(path)) {}
ArrowMarketDataView::~ArrowMarketDataView() = default;

NumericConstMatrixView ArrowMarketDataView::open() const {
    return buildMatrixView(impl_->openCol_.ptr, static_cast<int32_t>(impl_->dateKeys_.size()),
                           static_cast<int32_t>(impl_->instruments_.size()));
}
NumericConstMatrixView ArrowMarketDataView::high() const {
    return buildMatrixView(impl_->highCol_.ptr, static_cast<int32_t>(impl_->dateKeys_.size()),
                           static_cast<int32_t>(impl_->instruments_.size()));
}
NumericConstMatrixView ArrowMarketDataView::low() const {
    return buildMatrixView(impl_->lowCol_.ptr, static_cast<int32_t>(impl_->dateKeys_.size()),
                           static_cast<int32_t>(impl_->instruments_.size()));
}
NumericConstMatrixView ArrowMarketDataView::close() const {
    return buildMatrixView(impl_->closeCol_.ptr, static_cast<int32_t>(impl_->dateKeys_.size()),
                           static_cast<int32_t>(impl_->instruments_.size()));
}
NumericConstMatrixView ArrowMarketDataView::volume() const {
    return buildMatrixView(impl_->volCol_.ptr, static_cast<int32_t>(impl_->dateKeys_.size()),
                           static_cast<int32_t>(impl_->instruments_.size()));
}

std::optional<NumericConstMatrixView>
ArrowMarketDataView::getField(const std::string& name) const {
    auto it = impl_->extraFields_.find(name);
    if (it == impl_->extraFields_.end()) return std::nullopt;
    return buildMatrixView(it->second.ptr, static_cast<int32_t>(impl_->dateKeys_.size()),
                           static_cast<int32_t>(impl_->instruments_.size()));
}

const std::vector<DateKey>& ArrowMarketDataView::dates() const { return impl_->dateKeys_; }
const std::vector<InstrumentId>& ArrowMarketDataView::instruments() const { return impl_->instruments_; }

std::unique_ptr<IMarketDataView> ArrowMarketDataView::slice(DateRange range) const {
    auto ds = std::vector<DateKey>();
    for (const auto& d : impl_->dateKeys_)
        if (d.value >= range.from.value && d.value <= range.to.value) ds.push_back(d);
    auto ins = impl_->instruments_;
    return std::make_unique<SubMarketDataView>(*this, std::move(ds), std::move(ins));
}

std::unique_ptr<IMarketDataView> ArrowMarketDataView::slice(
    const std::vector<InstrumentId>& ids) const {
    return std::make_unique<SubMarketDataView>(*this, impl_->dateKeys_, ids);
}

} // namespace factor::compute
