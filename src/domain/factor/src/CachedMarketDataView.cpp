#include "factor_compute/CachedMarketDataView.h"
#include "factor_compute/SubMarketDataView.h"
#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"
#include "database/MarketDataRepository.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ankerl/unordered_dense.h>

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

    ankerl::unordered_dense::map<std::string, RowMatrixXf> m_fieldMats;

    std::vector<DateKey> m_dates;
    std::vector<InstrumentId> m_instruments;
    std::vector<std::string> m_symbolStrings;  // InstrumentId → 真实股票代码

    void buildMatrix(const ColumnData& col, RowMatrixXf& mat)
    {
        mat.resize(col.dateCount, col.instrumentCount);
        for (int d = 0; d < col.dateCount; ++d) {
            for (int i = 0; i < col.instrumentCount; ++i) {
                int idx = d * col.instrumentCount + i;
                // 缺失值用 NaN 填充，避免零值被当作合法价格参与因子计算
                mat(d, i) = (idx < static_cast<int>(col.values.size())) ? col.values[idx] : std::numeric_limits<signal_value_t>::quiet_NaN();
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
    ankerl::unordered_dense::set<uint32_t> idSet;
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
    ankerl::unordered_dense::map<std::string, InstrumentId> symToId;
    std::vector<InstrumentId> instruments;
    uint32_t nextId = 0;

    ankerl::unordered_dense::set<std::string> symbolSet;
    ankerl::unordered_dense::map<std::string, int> dateIndex;
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

    // ── 构建日期列数据 ──
    auto makeColumnData = [&]() -> CachedMarketDataView::ColumnData {
        CachedMarketDataView::ColumnData col;
        col.dateCount = numDates;
        col.instrumentCount = numInsts;
        col.values.assign(static_cast<size_t>(numDates) * numInsts,
                          std::numeric_limits<float>::quiet_NaN());
        for (const auto& d : sortedDates) {
            int dateInt = 0;
            try {
                if (d.size() == 10 && d[4] == '-' && d[7] == '-') {
                    dateInt = std::stoi(d.substr(0,4)) * 10000
                            + std::stoi(d.substr(5,2)) * 100
                            + std::stoi(d.substr(8,2));
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

    // ── 构建所有列的 ColumnData ──
    const size_t totalFields = 5 + extraFields.size();
    std::vector<CachedMarketDataView::ColumnData> allCols(totalFields);
    std::vector<std::string> allFieldNames(totalFields);
    allFieldNames[0] = "open";   allFieldNames[1] = "high";
    allFieldNames[2] = "low";    allFieldNames[3] = "close";
    allFieldNames[4] = "volume";
    for (size_t ei = 0; ei < extraFields.size(); ++ei)
        allFieldNames[5 + ei] = extraFields[ei];
    for (auto& col : allCols) col = makeColumnData();

    // ── 单遍遍历：一次填充所有字段 ──
    ankerl::unordered_dense::map<std::string, int> stringToIntMap;
    int nextStringId = 1;

    for (size_t i = 0; i < rowCount; ++i) {
        const auto row = root.at(i);
        if (!row.isObject()) continue;

        // 提取 symbol + date
        std::string sym, date;
        if (row.has("symbol"))     sym  = row.get("symbol").asString();
        if (row.has("trade_date")) date = row.get("trade_date").asString();
        if (sym.empty() || date.empty()) continue;

        auto sit = symToId.find(sym);
        auto dit = dateIndex.find(date);
        if (sit == symToId.end() || dit == dateIndex.end()) continue;
        int sIdx = static_cast<int>(sit->second.value);
        int dIdx = dit->second;
        size_t flatIdx = static_cast<size_t>(dIdx) * numInsts + sIdx;

        // 对每个字段尝试提取值
        for (size_t fi = 0; fi < totalFields; ++fi) {
            const std::string& field = allFieldNames[fi];
            if (!row.has(field)) continue;

            const auto fieldValue = row.get(field);
            float val;
            if (fieldValue.isNumber()) {
                val = static_cast<float>(fieldValue.asDouble());
            } else if (fieldValue.isString()) {
                const std::string strVal = fieldValue.asString();
                auto it = stringToIntMap.find(strVal);
                if (it == stringToIntMap.end()) {
                    it = stringToIntMap.emplace(strVal, nextStringId++).first;
                }
                val = static_cast<float>(it->second);
            } else {
                continue;
            }
            allCols[fi].values[flatIdx] = val;
        }
    }

    // ── 加载到 view ──
    view->loadFromColumnData(
        std::move(allCols[0]), std::move(allCols[1]), std::move(allCols[2]),
        std::move(allCols[3]), std::move(allCols[4]));

    for (size_t ei = 0; ei < extraFields.size(); ++ei)
        view->loadAdditionalField(extraFields[ei], std::move(allCols[5 + ei]));

    // 保存真实股票代码映射 (InstrumentId → 代码字符串)
    view->impl_->m_symbolStrings.resize(instruments.size());
    for (const auto& [sym, id] : symToId) {
        view->impl_->m_symbolStrings[id.value] = sym;
    }

    return view;
}

// ── fromDailyBarRows：数据库查询结果直接构建 CachedMarketDataView（零 JSON） ──
std::unique_ptr<CachedMarketDataView>
CachedMarketDataView::fromDailyBarRows(
    const std::vector<astock::infrastructure::database::DailyBarRow>& rows)
{
    if (rows.empty()) return nullptr;

    // 单遍收集唯一 symbol/date（数据已按 ORDER BY symbol, trade_date 排序）
    ankerl::unordered_dense::map<std::string, InstrumentId> symToId;
    std::vector<InstrumentId> instruments;
    uint32_t nextId = 0;
    ankerl::unordered_dense::map<std::string, int> dateIndex;
    std::vector<DateKey> sortedDateKeys;
    const size_t N = rows.size();

    for (const auto& row : rows) {
        if (symToId.find(row.symbol) == symToId.end()) {
            InstrumentId iid{nextId};
            instruments.push_back(iid);
            symToId[row.symbol] = iid;
            ++nextId;
        }
        if (dateIndex.find(row.tradeDate) == dateIndex.end()) {
            int y=0,m=0,d=0;
            sscanf(row.tradeDate.c_str(), "%d-%d-%d", &y, &m, &d);
            dateIndex[row.tradeDate] = static_cast<int>(sortedDateKeys.size());
            sortedDateKeys.push_back(DateKey{y*10000 + m*100 + d});
        }
    }

    int numInsts = static_cast<int>(instruments.size());
    int numDates = static_cast<int>(sortedDateKeys.size());
    if (numDates <= 0 || numInsts <= 0) return nullptr;

    // 分配列数据
    auto makeCol = [&]() -> ColumnData {
        ColumnData c;
        c.dateCount = numDates;
        c.instrumentCount = numInsts;
        c.values.assign(static_cast<size_t>(numDates) * numInsts, std::numeric_limits<float>::quiet_NaN());
        c.dates = sortedDateKeys;
        c.instruments = instruments;
        return c;
    };
    ColumnData colOpen = makeCol(), colHigh = makeCol(), colLow = makeCol();
    ColumnData colClose = makeCol(), colVol = makeCol();

    // 第二遍填充值
    for (const auto& row : rows) {
        auto si = symToId.find(row.symbol);
        auto di = dateIndex.find(row.tradeDate);
        if (si == symToId.end() || di == dateIndex.end()) continue;
        size_t idx = static_cast<size_t>(di->second) * numInsts + si->second.value;
        colOpen.values[idx]  = static_cast<float>(row.open);
        colHigh.values[idx]  = static_cast<float>(row.high);
        colLow.values[idx]   = static_cast<float>(row.low);
        colClose.values[idx] = static_cast<float>(row.close);
        colVol.values[idx]   = static_cast<float>(row.volume);
    }

    auto view = std::make_unique<CachedMarketDataView>();
    view->loadFromColumnData(
        std::move(colOpen), std::move(colHigh), std::move(colLow),
        std::move(colClose), std::move(colVol));
    view->impl_->m_symbolStrings.resize(instruments.size());
    for (const auto& [sym, id] : symToId)
        view->impl_->m_symbolStrings[id.value] = sym;
    return view;
}

// ── fromSqlRows：带额外字段的原始行直接构建 ──
std::unique_ptr<CachedMarketDataView>
CachedMarketDataView::fromSqlRows(
    const std::vector<astock::database::SqlQueryResultRow>& rows,
    const std::vector<std::string>& extraFields)
{
    if (rows.empty()) return nullptr;

    ankerl::unordered_dense::map<std::string, InstrumentId> symToId;
    std::vector<InstrumentId> instruments;
    uint32_t nextId = 0;
    ankerl::unordered_dense::map<std::string, int> dateIndex;
    std::vector<DateKey> sortedDateKeys;

    for (const auto& row : rows) {
        std::string sym = row.getString("symbol");
        std::string date = row.getString("trade_date");
        if (sym.empty() || date.empty()) continue;
        if (symToId.find(sym) == symToId.end()) {
            symToId[sym] = InstrumentId{nextId};
            instruments.push_back(InstrumentId{nextId});
            ++nextId;
        }
        if (dateIndex.find(date) == dateIndex.end()) {
            int y=0,m=0,d=0;
            sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d);
            dateIndex[date] = static_cast<int>(sortedDateKeys.size());
            sortedDateKeys.push_back(DateKey{y*10000 + m*100 + d});
        }
    }

    int numInsts = static_cast<int>(instruments.size());
    int numDates = static_cast<int>(sortedDateKeys.size());
    if (numDates <= 0 || numInsts <= 0) return nullptr;

    auto makeCol = [&]() -> ColumnData {
        ColumnData c;
        c.dateCount = numDates;
        c.instrumentCount = numInsts;
        c.values.assign(static_cast<size_t>(numDates) * numInsts, std::numeric_limits<float>::quiet_NaN());
        c.dates = sortedDateKeys;
        c.instruments = instruments;
        return c;
    };

    ColumnData colOpen = makeCol(), colHigh = makeCol(), colLow = makeCol();
    ColumnData colClose = makeCol(), colVol = makeCol();

    // 额外字段列
    std::vector<ColumnData> extraCols(extraFields.size());
    for (auto& c : extraCols) c = makeCol();

    for (const auto& row : rows) {
        std::string sym = row.getString("symbol");
        std::string date = row.getString("trade_date");
        auto si = symToId.find(sym);
        auto di = dateIndex.find(date);
        if (si == symToId.end() || di == dateIndex.end()) continue;
        size_t idx = static_cast<size_t>(di->second) * numInsts + si->second.value;
        colOpen.values[idx]  = static_cast<float>(row.getDouble("open"));
        colHigh.values[idx]  = static_cast<float>(row.getDouble("high"));
        colLow.values[idx]   = static_cast<float>(row.getDouble("low"));
        colClose.values[idx] = static_cast<float>(row.getDouble("close"));
        colVol.values[idx]   = static_cast<float>(row.getDouble("volume"));
        for (size_t ei = 0; ei < extraFields.size(); ++ei)
            extraCols[ei].values[idx] = static_cast<float>(row.getDouble(extraFields[ei]));
    }

    auto view = std::make_unique<CachedMarketDataView>();
    view->loadFromColumnData(
        std::move(colOpen), std::move(colHigh), std::move(colLow),
        std::move(colClose), std::move(colVol));
    for (size_t ei = 0; ei < extraFields.size(); ++ei)
        view->loadAdditionalField(extraFields[ei], std::move(extraCols[ei]));
    view->impl_->m_symbolStrings.resize(instruments.size());
    for (const auto& [sym, id] : symToId)
        view->impl_->m_symbolStrings[id.value] = sym;
    return view;
}

static bool writeBinaryColumn(FILE* f, const CachedMarketDataView::ColumnData& col)
{
    int32_t dc = col.dateCount;
    int32_t ic = col.instrumentCount;
    if (std::fwrite(&dc, sizeof(int32_t), 1, f) != 1) return false;
    if (std::fwrite(&ic, sizeof(int32_t), 1, f) != 1) return false;
    for (int i = 0; i < dc; ++i) {
        int32_t dv = col.dates[static_cast<size_t>(i)].value;
        if (std::fwrite(&dv, sizeof(int32_t), 1, f) != 1) return false;
    }
    for (int i = 0; i < ic; ++i) {
        uint32_t iv = col.instruments[static_cast<size_t>(i)].value;
        if (std::fwrite(&iv, sizeof(uint32_t), 1, f) != 1) return false;
    }
    size_t total = static_cast<size_t>(dc) * static_cast<size_t>(ic);
    if (std::fwrite(col.values.data(), sizeof(float), total, f) != total) return false;
    return true;
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
    if (!f) { INTERNAL_ERROR_STREAM << "[CMDV] saveToBinary: cannot open " << filePath; return false; }

    uint32_t magic = 0x42564453; // "BVDS"
    uint32_t version = 2;  // v2: +symbol strings
    int32_t dc = static_cast<int32_t>(datesVec.size());
    int32_t ic = static_cast<int32_t>(instVec.size());

    // Lambda: 写失败时关闭文件并报错
    auto fail = [&](const char* msg) -> bool {
        INTERNAL_ERROR_STREAM << "[CMDV] saveToBinary: " << msg << " (" << filePath << ")";
        std::fclose(f);
        std::remove(filePath.c_str());
        return false;
    };

    if (std::fwrite(&magic, sizeof(uint32_t), 1, f) != 1) return fail("write magic failed");
    if (std::fwrite(&version, sizeof(uint32_t), 1, f) != 1) return fail("write version failed");
    if (std::fwrite(&dc, sizeof(int32_t), 1, f) != 1) return fail("write dateCount failed");
    if (std::fwrite(&ic, sizeof(int32_t), 1, f) != 1) return fail("write instCount failed");

    // OHLCV columns (5)
    if (!writeBinaryColumn(f, impl_->m_open[0])) return fail("write open column failed");
    if (!writeBinaryColumn(f, impl_->m_high[0])) return fail("write high column failed");
    if (!writeBinaryColumn(f, impl_->m_low[0])) return fail("write low column failed");
    if (!writeBinaryColumn(f, impl_->m_close[0])) return fail("write close column failed");
    if (!writeBinaryColumn(f, impl_->m_volume[0])) return fail("write volume column failed");

    // Extra fields
    int32_t extraCount = static_cast<int32_t>(impl_->m_fieldMats.size());
    if (std::fwrite(&extraCount, sizeof(int32_t), 1, f) != 1) return fail("write extraCount failed");
    for (const auto& [name, mat] : impl_->m_fieldMats) {
        uint32_t nameLen = static_cast<uint32_t>(name.size());
        if (std::fwrite(&nameLen, sizeof(uint32_t), 1, f) != 1) return fail("write extra nameLen failed");
        if (std::fwrite(name.data(), 1, nameLen, f) != nameLen) return fail("write extra name failed");
        int32_t edc = static_cast<int32_t>(mat.rows());
        int32_t eic = static_cast<int32_t>(mat.cols());
        if (std::fwrite(&edc, sizeof(int32_t), 1, f) != 1) return fail("write extra dc failed");
        if (std::fwrite(&eic, sizeof(int32_t), 1, f) != 1) return fail("write extra ic failed");
        for (const auto& d : datesVec) {
            int32_t dv = d.value;
            if (std::fwrite(&dv, sizeof(int32_t), 1, f) != 1) return fail("write extra dates failed");
        }
        for (const auto& inst : instVec) {
            uint32_t iv = inst.value;
            if (std::fwrite(&iv, sizeof(uint32_t), 1, f) != 1) return fail("write extra insts failed");
        }
        for (int r = 0; r < edc; ++r)
            for (int c = 0; c < eic; ++c)
                if (std::fwrite(&mat(r,c), sizeof(float), 1, f) != 1) return fail("write extra values failed");
    }

    // Symbol strings (v2)
    int32_t symCount = static_cast<int32_t>(impl_->m_symbolStrings.size());
    if (std::fwrite(&symCount, sizeof(int32_t), 1, f) != 1) return fail("write symCount failed");
    for (const auto& s : impl_->m_symbolStrings) {
        uint32_t slen = static_cast<uint32_t>(s.size());
        if (std::fwrite(&slen, sizeof(uint32_t), 1, f) != 1) return fail("write slen failed");
        if (std::fwrite(s.data(), 1, slen, f) != slen) return fail("write symbol string failed");
    }

    std::fclose(f);
    INTERNAL_INFO_STREAM << "[CMDV] saveToBinary: " << filePath << " (" << dc << " dates x " << ic << " insts, " << extraCount << " extra fields, " << symCount << " symbols)";
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
    INTERNAL_INFO_STREAM << "[CMDV] fromBinary: " << filePath << " (" << dc << " dates x " << ic << " insts, " << extraCount << " extra fields, " << static_cast<int>(view->impl_->m_symbolStrings.size()) << " symbols)";
    return view;
}

} // namespace factor::compute
