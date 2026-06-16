#include "factor_compute/CachedMarketDataView.h"
#include "factor_compute/SubMarketDataView.h"
#include "foundation/json/json_facade.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace factor::compute {

using RowMatrixXf = Eigen::Matrix<signal_value_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

class CachedMarketDataView::Impl {
public:
    ColumnData m_open[1]{};
    ColumnData m_high[1]{};
    ColumnData m_low[1]{};
    ColumnData m_close[1]{};
    ColumnData m_volume[1]{};

    RowMatrixXf m_openMat;
    RowMatrixXf m_highMat;
    RowMatrixXf m_lowMat;
    RowMatrixXf m_closeMat;
    RowMatrixXf m_volumeMat;

    std::unordered_map<std::string, RowMatrixXf> m_fieldMats;

    std::vector<DateKey> m_dates;
    std::vector<InstrumentId> m_instruments;
    std::vector<std::string> m_symbolStrings;  // InstrumentId → 真实股票代码

    void buildMatrix(const ColumnData& col, RowMatrixXf& mat)
    {
        mat.resize(col.dateCount, col.instrumentCount);
        for (int d = 0; d < col.dateCount; ++d) {
            for (int i = 0; i < col.instrumentCount; ++i) {
                int idx = d * col.instrumentCount + i;
                mat(d, i) = (idx < static_cast<int>(col.values.size())) ? col.values[idx] : signal_value_t{0};
            }
        }
    }
};

CachedMarketDataView::CachedMarketDataView()
    : impl_(std::make_unique<Impl>())
{
}

CachedMarketDataView::~CachedMarketDataView() = default;

void CachedMarketDataView::loadFromColumnData(
    ColumnData open, ColumnData high, ColumnData low, ColumnData close, ColumnData volume)
{
    impl_->m_open[0]   = std::move(open);
    impl_->m_high[0]   = std::move(high);
    impl_->m_low[0]    = std::move(low);
    impl_->m_close[0]  = std::move(close);
    impl_->m_volume[0] = std::move(volume);

    impl_->buildMatrix(impl_->m_open[0], impl_->m_openMat);
    impl_->buildMatrix(impl_->m_high[0], impl_->m_highMat);
    impl_->buildMatrix(impl_->m_low[0],  impl_->m_lowMat);
    impl_->buildMatrix(impl_->m_close[0], impl_->m_closeMat);
    impl_->buildMatrix(impl_->m_volume[0], impl_->m_volumeMat);

    impl_->m_dates       = impl_->m_open[0].dates;
    impl_->m_instruments = impl_->m_open[0].instruments;
}

void CachedMarketDataView::loadAdditionalField(const std::string& fieldName, ColumnData column)
{
    if (fieldName.empty()) return;
    RowMatrixXf mat;
    impl_->buildMatrix(column, mat);
    impl_->m_fieldMats[fieldName] = std::move(mat);
}

NumericConstMatrixView CachedMarketDataView::open() const
{
    return NumericConstMatrixView{impl_->m_openMat.data(), static_cast<int>(impl_->m_openMat.rows()), static_cast<int>(impl_->m_openMat.cols()), static_cast<int>(impl_->m_openMat.cols())};
}

NumericConstMatrixView CachedMarketDataView::high() const
{
    return NumericConstMatrixView{impl_->m_highMat.data(), static_cast<int>(impl_->m_highMat.rows()), static_cast<int>(impl_->m_highMat.cols()), static_cast<int>(impl_->m_highMat.cols())};
}

NumericConstMatrixView CachedMarketDataView::low() const
{
    return NumericConstMatrixView{impl_->m_lowMat.data(), static_cast<int>(impl_->m_lowMat.rows()), static_cast<int>(impl_->m_lowMat.cols()), static_cast<int>(impl_->m_lowMat.cols())};
}

NumericConstMatrixView CachedMarketDataView::close() const
{
    return NumericConstMatrixView{impl_->m_closeMat.data(), static_cast<int>(impl_->m_closeMat.rows()), static_cast<int>(impl_->m_closeMat.cols()), static_cast<int>(impl_->m_closeMat.cols())};
}

NumericConstMatrixView CachedMarketDataView::volume() const
{
    return NumericConstMatrixView{impl_->m_volumeMat.data(), static_cast<int>(impl_->m_volumeMat.rows()), static_cast<int>(impl_->m_volumeMat.cols()), static_cast<int>(impl_->m_volumeMat.cols())};
}

std::optional<NumericConstMatrixView>
CachedMarketDataView::getField(const std::string& fieldName) const
{
    // 先查 OHLCV 基类矩阵（这些不在 m_fieldMats 中）
    if (fieldName == "open")   return open();
    if (fieldName == "high")   return high();
    if (fieldName == "low")    return low();
    if (fieldName == "close")  return close();
    if (fieldName == "volume") return volume();

    auto it = impl_->m_fieldMats.find(fieldName);
    if (it == impl_->m_fieldMats.end()) {
        return std::nullopt;
    }
    const RowMatrixXf& mat = it->second;
    return NumericConstMatrixView{
        mat.data(),
        static_cast<int>(mat.rows()),
        static_cast<int>(mat.cols()),
        static_cast<int>(mat.cols())
    };
}

bool CachedMarketDataView::hasField(const std::string& fieldName) const
{
    return impl_->m_fieldMats.find(fieldName) != impl_->m_fieldMats.end();
}

const std::vector<DateKey>& CachedMarketDataView::dates() const { return impl_->m_dates; }
const std::vector<InstrumentId>& CachedMarketDataView::instruments() const { return impl_->m_instruments; }
const std::vector<std::string>& CachedMarketDataView::symbolStrings() const { return impl_->m_symbolStrings; }

std::unique_ptr<IMarketDataView>
CachedMarketDataView::slice(DateRange dateRange) const
{
    // Return a view filtered by date range using SubMarketDataView
    const auto& allDates = dates();
    std::vector<DateKey> dateSubset;
    for (const auto& d : allDates) {
        if (d.value >= dateRange.from.value && d.value <= dateRange.to.value) {
            dateSubset.push_back(d);
        }
    }
    if (dateSubset.empty()) {
        throw std::runtime_error("CachedMarketDataView::slice(dateRange): empty date subset");
    }
    const auto& allInstruments = instruments();
    return std::make_unique<SubMarketDataView>(
        *this, std::move(dateSubset), std::vector<InstrumentId>(allInstruments));
}

std::unique_ptr<IMarketDataView>
CachedMarketDataView::slice(const std::vector<InstrumentId>& instrumentIds) const
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
        throw std::runtime_error("CachedMarketDataView::slice(instrumentIds): empty instrument subset");
    }
    const auto& allDates = dates();
    return std::make_unique<SubMarketDataView>(
        *this, std::vector<DateKey>(allDates), std::move(instSubset));
}

std::unique_ptr<CachedMarketDataView>
CachedMarketDataView::fromJson(const foundation::json::JsonFacade& root,
                               const std::vector<std::string>& extraFields)
{
    if (!root.isArray() || root.size() == 0) {
        return nullptr;
    }

    auto view = std::make_unique<CachedMarketDataView>();
    std::unordered_map<std::string, InstrumentId> symToId;
    std::vector<InstrumentId> instruments;
    uint32_t nextId = 0;

    std::unordered_set<std::string> symbolSet;
    std::unordered_map<std::string, int> dateIndex;
    std::vector<std::string> sortedDates;
    const size_t rowCount = root.size();

    for (size_t i = 0; i < rowCount; ++i) {
        const auto row = root.at(i);
        if (!row.isObject()) continue;

        std::string sym, date;
        if (row.has("symbol"))  sym  = row.get("symbol").asString();
        if (row.has("trade_date")) date = row.get("trade_date").asString();
        if (sym.empty() || date.empty()) continue;

        if (symbolSet.insert(sym).second) {
            InstrumentId instId{nextId};
            instruments.push_back(instId);
            symToId[sym] = instId;
            ++nextId;
        }
        if (dateIndex.find(date) == dateIndex.end()) {
            dateIndex[date] = static_cast<int>(sortedDates.size());
            sortedDates.push_back(date);
        }
    }

    int numDates = static_cast<int>(sortedDates.size());
    int numInsts = static_cast<int>(instruments.size());
    if (numDates <= 0 || numInsts <= 0) {
        return nullptr;
    }

    auto buildColumn = [&](float nanVal = 0.0f) -> CachedMarketDataView::ColumnData {
        CachedMarketDataView::ColumnData col;
        col.dateCount = numDates;
        col.instrumentCount = numInsts;
        col.values.assign(static_cast<size_t>(numDates) * numInsts, nanVal);
        for (const auto& d : sortedDates) {
            int dateInt = 0;
            try {
                if (d.size() == 10 && d[4] == '-' && d[7] == '-') {
                    dateInt = std::stoi(d) * 10000 + std::stoi(d.substr(5, 2)) * 100 + std::stoi(d.substr(8, 2));
                } else {
                    dateInt = std::stoi(d);
                }
            } catch (...) {
                dateInt = 0;
            }
            col.dates.push_back(DateKey{dateInt});
        }
        col.instruments = instruments;
        return col;
    };

    std::unordered_map<std::string, int> stringToIntMap;
    int nextStringId = 1;

    auto fillColumn = [&](CachedMarketDataView::ColumnData& col,
                          const std::string& field) {
        for (size_t i = 0; i < rowCount; ++i) {
            const auto row = root.at(i);
            if (!row.isObject()) continue;
            if (!row.has(field)) continue;
            const auto fieldValue = row.get(field);

            double val = 0.0;
            if (fieldValue.isNumber()) {
                val = fieldValue.asDouble();
            } else if (fieldValue.isString()) {
                const std::string strVal = fieldValue.asString();
                auto it = stringToIntMap.find(strVal);
                if (it == stringToIntMap.end()) {
                    it = stringToIntMap.emplace(strVal, nextStringId++).first;
                }
                val = static_cast<double>(it->second);
            } else {
                continue;
            }

            std::string sym, date;
            if (row.has("symbol"))  sym  = row.get("symbol").asString();
            if (row.has("trade_date")) date = row.get("trade_date").asString();
            auto sit = symToId.find(sym);
            auto dit = dateIndex.find(date);
            if (sit == symToId.end() || dit == dateIndex.end()) continue;

            int sIdx = static_cast<int>(sit->second.value);
            int dIdx = dit->second;
            col.values[static_cast<size_t>(dIdx) * numInsts + sIdx]
                = static_cast<float>(val);
        }
    };

    auto colOpen  = buildColumn();
    auto colHigh  = buildColumn();
    auto colLow   = buildColumn();
    auto colClose = buildColumn();
    auto colVol   = buildColumn();

    fillColumn(colOpen,  "open");
    fillColumn(colHigh,  "high");
    fillColumn(colLow,   "low");
    fillColumn(colClose, "close");
    fillColumn(colVol,   "volume");

    view->loadFromColumnData(
        std::move(colOpen), std::move(colHigh), std::move(colLow),
        std::move(colClose), std::move(colVol));

    for (const auto& field : extraFields) {
        auto col = buildColumn(std::numeric_limits<float>::quiet_NaN());
        fillColumn(col, field);
        view->loadAdditionalField(field, std::move(col));
    }

    // 保存真实股票代码映射 (InstrumentId → 代码字符串)
    view->impl_->m_symbolStrings.resize(instruments.size());
    for (const auto& [sym, id] : symToId) {
        view->impl_->m_symbolStrings[id.value] = sym;
    }

    return view;
}

static void writeBinaryColumn(FILE* f, const CachedMarketDataView::ColumnData& col)
{
    int32_t dc = col.dateCount;
    int32_t ic = col.instrumentCount;
    std::fwrite(&dc, sizeof(int32_t), 1, f);
    std::fwrite(&ic, sizeof(int32_t), 1, f);
    for (int i = 0; i < dc; ++i) {
        int32_t dv = col.dates[static_cast<size_t>(i)].value;
        std::fwrite(&dv, sizeof(int32_t), 1, f);
    }
    for (int i = 0; i < ic; ++i) {
        uint32_t iv = col.instruments[static_cast<size_t>(i)].value;
        std::fwrite(&iv, sizeof(uint32_t), 1, f);
    }
    size_t total = static_cast<size_t>(dc) * static_cast<size_t>(ic);
    std::fwrite(col.values.data(), sizeof(float), total, f);
}

static bool readBinaryColumn(FILE* f, CachedMarketDataView::ColumnData& col)
{
    int32_t dc=0, ic=0;
    if (std::fread(&dc, sizeof(int32_t), 1, f) != 1) return false;
    if (std::fread(&ic, sizeof(int32_t), 1, f) != 1) return false;
    col.dateCount = dc;
    col.instrumentCount = ic;
    col.dates.resize(static_cast<size_t>(dc));
    for (int i = 0; i < dc; ++i) {
        int32_t dv=0;
        if (std::fread(&dv, sizeof(int32_t), 1, f) != 1) return false;
        col.dates[static_cast<size_t>(i)] = DateKey{dv};
    }
    col.instruments.resize(static_cast<size_t>(ic));
    for (int i = 0; i < ic; ++i) {
        uint32_t iv=0;
        if (std::fread(&iv, sizeof(uint32_t), 1, f) != 1) return false;
        col.instruments[static_cast<size_t>(i)] = InstrumentId{iv};
    }
    size_t total = static_cast<size_t>(dc) * static_cast<size_t>(ic);
    col.values.resize(total);
    if (total > 0 && std::fread(col.values.data(), sizeof(float), total, f) != total) return false;
    return true;
}

bool CachedMarketDataView::saveToBinary(const std::string& filePath) const
{
    const auto& datesVec = dates();
    const auto& instVec = instruments();
    if (datesVec.empty() || instVec.empty()) return false;

    FILE* f = nullptr;
#ifdef _MSC_VER
    fopen_s(&f, filePath.c_str(), "wb");
#else
    f = std::fopen(filePath.c_str(), "wb");
#endif
    if (!f) { fprintf(stderr, "[CMDV] saveToBinary: cannot open %s\n", filePath.c_str()); fflush(stderr); return false; }

    uint32_t magic = 0x42564453; // "BVDS"
    uint32_t version = 2;  // v2: +symbol strings
    int32_t dc = static_cast<int32_t>(datesVec.size());
    int32_t ic = static_cast<int32_t>(instVec.size());
    std::fwrite(&magic, sizeof(uint32_t), 1, f);
    std::fwrite(&version, sizeof(uint32_t), 1, f);
    std::fwrite(&dc, sizeof(int32_t), 1, f);
    std::fwrite(&ic, sizeof(int32_t), 1, f);

    // OHLCV columns (5)
    writeBinaryColumn(f, impl_->m_open[0]);
    writeBinaryColumn(f, impl_->m_high[0]);
    writeBinaryColumn(f, impl_->m_low[0]);
    writeBinaryColumn(f, impl_->m_close[0]);
    writeBinaryColumn(f, impl_->m_volume[0]);

    // Extra fields
    int32_t extraCount = static_cast<int32_t>(impl_->m_fieldMats.size());
    std::fwrite(&extraCount, sizeof(int32_t), 1, f);
    for (const auto& [name, mat] : impl_->m_fieldMats) {
        uint32_t nameLen = static_cast<uint32_t>(name.size());
        std::fwrite(&nameLen, sizeof(uint32_t), 1, f);
        std::fwrite(name.data(), 1, nameLen, f);
        int32_t edc = static_cast<int32_t>(mat.rows());
        int32_t eic = static_cast<int32_t>(mat.cols());
        std::fwrite(&edc, sizeof(int32_t), 1, f);
        std::fwrite(&eic, sizeof(int32_t), 1, f);
        // Write dates and instruments from the base columns (same for all fields)
        for (const auto& d : datesVec) {
            int32_t dv = d.value;
            std::fwrite(&dv, sizeof(int32_t), 1, f);
        }
        for (const auto& inst : instVec) {
            uint32_t iv = inst.value;
            std::fwrite(&iv, sizeof(uint32_t), 1, f);
        }
        for (int r = 0; r < edc; ++r)
            for (int c = 0; c < eic; ++c)
                std::fwrite(&mat(r,c), sizeof(float), 1, f);
    }

    // Symbol strings (v2)
    int32_t symCount = static_cast<int32_t>(impl_->m_symbolStrings.size());
    std::fwrite(&symCount, sizeof(int32_t), 1, f);
    for (const auto& s : impl_->m_symbolStrings) {
        uint32_t slen = static_cast<uint32_t>(s.size());
        std::fwrite(&slen, sizeof(uint32_t), 1, f);
        std::fwrite(s.data(), 1, slen, f);
    }

    std::fclose(f);
    fprintf(stderr, "[CMDV] saveToBinary: %s (%d dates x %d insts, %d extra fields, %d symbols)\n",
            filePath.c_str(), dc, ic, extraCount, symCount);
    fflush(stderr);
    return true;
}

std::unique_ptr<CachedMarketDataView> CachedMarketDataView::fromBinary(const std::string& filePath)
{
    FILE* f = nullptr;
#ifdef _MSC_VER
    fopen_s(&f, filePath.c_str(), "rb");
#else
    f = std::fopen(filePath.c_str(), "rb");
#endif
    if (!f) return nullptr;

    uint32_t magic=0, version=0;
    int32_t dc=0, ic=0;
    if (std::fread(&magic, sizeof(uint32_t), 1, f) != 1 || magic != 0x42564453) { std::fclose(f); return nullptr; }
    if (std::fread(&version, sizeof(uint32_t), 1, f) != 1 || (version != 1 && version != 2)) { std::fclose(f); return nullptr; }
    if (std::fread(&dc, sizeof(int32_t), 1, f) != 1) { std::fclose(f); return nullptr; }
    if (std::fread(&ic, sizeof(int32_t), 1, f) != 1) { std::fclose(f); return nullptr; }

    auto view = std::make_unique<CachedMarketDataView>();

    // Read OHLCV columns
    CachedMarketDataView::ColumnData cols[5];
    for (int i = 0; i < 5; ++i) {
        if (!readBinaryColumn(f, cols[i])) { std::fclose(f); return nullptr; }
    }
    view->loadFromColumnData(
        std::move(cols[0]), std::move(cols[1]), std::move(cols[2]),
        std::move(cols[3]), std::move(cols[4]));

    // Read extra fields
    int32_t extraCount = 0;
    if (std::fread(&extraCount, sizeof(int32_t), 1, f) != 1) { std::fclose(f); return nullptr; }
    for (int fi = 0; fi < extraCount; ++fi) {
        uint32_t nameLen = 0;
        if (std::fread(&nameLen, sizeof(uint32_t), 1, f) != 1) { std::fclose(f); return nullptr; }
        std::string name(nameLen, '\0');
        if (std::fread(&name[0], 1, nameLen, f) != nameLen) { std::fclose(f); return nullptr; }
        CachedMarketDataView::ColumnData col;
        if (!readBinaryColumn(f, col)) { std::fclose(f); return nullptr; }
        view->loadAdditionalField(name, std::move(col));
    }

    // Symbol strings (v2+)
    if (version >= 2) {
        int32_t symCount = 0;
        if (std::fread(&symCount, sizeof(int32_t), 1, f) == 1 && symCount > 0) {
            view->impl_->m_symbolStrings.resize(static_cast<size_t>(symCount));
            for (int32_t si = 0; si < symCount; ++si) {
                uint32_t slen = 0;
                if (std::fread(&slen, sizeof(uint32_t), 1, f) != 1) break;
                std::string sym(slen, '\0');
                if (std::fread(&sym[0], 1, slen, f) != slen) break;
                view->impl_->m_symbolStrings[static_cast<size_t>(si)] = sym;
            }
        }
    }

    std::fclose(f);
    fprintf(stderr, "[CMDV] fromBinary: %s (%d dates x %d insts, %d extra fields, %d symbols)\n",
            filePath.c_str(), dc, ic, extraCount, static_cast<int>(view->impl_->m_symbolStrings.size()));
    fflush(stderr);
    return view;
}

} // namespace factor::compute
