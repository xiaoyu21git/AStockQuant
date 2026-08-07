// CandleDataModel.cpp
// v3: 技术指标委托 domain::market::indicators, 桥接层不再持有算法
#include "CandleDataModel.h"
#include "../../../domain/market/include/LiveData.h"
#include "../../../domain/market/include/TechnicalIndicators.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bridge {

CandleDataModel::CandleDataModel(QObject* parent) : QAbstractTableModel(parent) {}

int CandleDataModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_data.size());
}

int CandleDataModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant CandleDataModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() >= static_cast<int>(m_data.size()))
        return QVariant();

    const auto& c = m_data[idx.row()];
    int row = idx.row();

    // ── 基础 Role ──
    switch (role) {
        case TimestampRole: return c.timestamp;
        case OpenRole:      return c.open;
        case HighRole:      return c.high;
        case LowRole:       return c.low;
        case CloseRole:     return c.close;
        case VolumeRole:    return c.volume;
        case Qt::DisplayRole: {
            switch (idx.column()) {
                case TimestampCol: return c.timestamp;
                case OpenCol:      return c.open;
                case HighCol:      return c.high;
                case LowCol:       return c.low;
                case CloseCol:     return c.close;
                case VolumeCol:    return c.volume;
                default: return QVariant();
            }
        }
        default: break;
    }

    // ── 指标 Role — 委托 domain::market::indicators ──
    using namespace domain::market::indicators;
    auto closeAt = [this](int i) -> double { return m_data[static_cast<size_t>(i)].close; };
    auto highAt  = [this](int i) -> double { return m_data[static_cast<size_t>(i)].high; };
    auto lowAt   = [this](int i) -> double { return m_data[static_cast<size_t>(i)].low; };
    auto volAt   = [this](int i) -> double { return m_data[static_cast<size_t>(i)].volume; };
    int count = static_cast<int>(m_data.size());

    double val = std::numeric_limits<double>::quiet_NaN();

    switch (role) {
        // ── 任意行查询: SMA / EMA / VWAP ──
        case Ma5Role:   val = smaAt(closeAt, row, 5);   break;
        case Ma10Role:  val = smaAt(closeAt, row, 10);  break;
        case Ma20Role:  val = smaAt(closeAt, row, 20);  break;
        case Ma60Role:  val = smaAt(closeAt, row, 60);  break;
        case Ema5Role:  val = emaAt(closeAt, row, 5);   break;
        case Ema10Role: val = emaAt(closeAt, row, 10);  break;
        case Ema20Role: val = emaAt(closeAt, row, 20);  break;
        case Ema60Role: val = emaAt(closeAt, row, 60);  break;
        case MaVol5Role:  val = smaAt(volAt, row, 5);  break;
        case MaVol10Role: val = smaAt(volAt, row, 10); break;
        case VwapRole:  val = vwapAt(closeAt, volAt, row); break;

        // ── 递推指标: MACD / KDJ / RSI (仅末尾行) ──
        case MacdDifRole:
        case MacdDeaRole:
        case MacdHistRole: {
            if (row != count - 1 || count < 26) return QVariant();
            auto macd = macdLatest(closeAt, count);
            if (role == MacdDifRole)  return macd.dif;
            if (role == MacdDeaRole)  return macd.dea;
            if (role == MacdHistRole) return macd.histogram;
            return QVariant();
        }
        case KdjKRole: case KdjDRole: case KdjJRole: {
            if (row != count - 1 || count < 12) return QVariant();
            auto kdj = kdjLatest(closeAt, highAt, lowAt, count);
            if (role == KdjKRole) return kdj.k;
            if (role == KdjDRole) return kdj.d;
            if (role == KdjJRole) return kdj.j;
            return QVariant();
        }
        case Rsi6Role: case Rsi14Role: {
            if (row != count - 1) return QVariant();
            int n = (role == Rsi6Role) ? 6 : 14;
            double rsi = rsiLatest(closeAt, count, n);
            if (std::isnan(rsi)) return QVariant();
            return rsi;
        }
        default: return QVariant();
    }

    return std::isnan(val) ? QVariant() : QVariant(val);
}

QHash<int, QByteArray> CandleDataModel::roleNames() const {
    static const QHash<int, QByteArray> roles = {
        {TimestampRole, "timestamp"}, {OpenRole, "open"}, {HighRole, "high"},
        {LowRole, "low"}, {CloseRole, "close"}, {VolumeRole, "volume"},
        {Ma5Role,  "ma5"},  {Ma10Role, "ma10"},  {Ma20Role, "ma20"},  {Ma60Role, "ma60"},
        {Ema5Role, "ema5"}, {Ema10Role,"ema10"}, {Ema20Role,"ema20"}, {Ema60Role,"ema60"},
        {MaVol5Role,  "maVol5"},  {MaVol10Role, "maVol10"},
        {MacdDifRole, "macdDif"}, {MacdDeaRole, "macdDea"}, {MacdHistRole, "macdHist"},
        {KdjKRole, "kdjK"}, {KdjDRole, "kdjD"}, {KdjJRole, "kdjJ"},
        {Rsi6Role, "rsi6"}, {Rsi14Role, "rsi14"},
        {VwapRole, "vwap"}
    };
    return roles;
}

void CandleDataModel::appendCandle(qint64 ts, double o, double h, double l, double c, double v) {
    int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    m_data.push_back({ts, o, h, l, c, v});
    endInsertRows();
    emit countChanged();
    emit lastPriceChanged();
    emit lastVolumeChanged();
}

void CandleDataModel::updateLastCandle(double close, double high, double low, double volume) {
    if (m_data.empty()) return;
    auto& last = m_data.back();
    last.close = close;
    last.high  = std::max(last.high, high);
    last.low   = std::min(last.low, low);
    last.volume += volume;
    int r = static_cast<int>(m_data.size()) - 1;
    emit dataChanged(index(r, 0), index(r, ColumnCount - 1));
    emit lastPriceChanged();
    emit lastVolumeChanged();
}

void CandleDataModel::setCandles(const QVariantList& candles) {
    beginResetModel();
    m_data.clear();
    m_data.reserve(candles.size());
    for (const auto& item : candles) {
        auto m = item.toMap();
        m_data.push_back({
            static_cast<qint64>(m["timestamp"].toLongLong()),
            m["open"].toDouble(), m["high"].toDouble(), m["low"].toDouble(),
            m["close"].toDouble(), m["volume"].toDouble()
        });
    }
    endResetModel();
    emit countChanged();
    emit lastPriceChanged();
    emit lastVolumeChanged();
}

void CandleDataModel::clear() {
    beginResetModel();
    m_data.clear();
    endResetModel();
    emit countChanged();
}

double CandleDataModel::lastPrice()  const { return m_data.empty() ? 0.0 : m_data.back().close; }
double CandleDataModel::lastVolume() const { return m_data.empty() ? 0.0 : m_data.back().volume; }
void   CandleDataModel::setPreClose(double v) { m_preClose = v; emit preCloseChanged(); }

const CandleItem* CandleDataModel::candleAt(int row) const {
    return (row >= 0 && row < static_cast<int>(m_data.size())) ? &m_data[row] : nullptr;
}

void CandleDataModel::setDataSource(const domain::market::BarSeries* series) {
    m_series = series;
}

} // namespace bridge
