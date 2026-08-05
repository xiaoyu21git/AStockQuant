// CandleDataModel.cpp
// v2: 指标 Role 从自身 m_data 计算, 适用于任意周期
#include "CandleDataModel.h"
#include "../../../domain/market/include/LiveData.h"

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

// ─── 辅助: 从 m_data 的第 row 根向前取 n 个 close 的 SMA ───
static double smaAt(const std::vector<CandleItem>& data, int row, int n) {
    if (row < n - 1 || n <= 0) return std::numeric_limits<double>::quiet_NaN();
    double sum = 0.0;
    for (int i = row - n + 1; i <= row; ++i)
        sum += data[static_cast<size_t>(i)].close;
    return sum / n;
}

// ─── 辅助: 从 m_data 的第 row 根向前取 n 个 volume 的 SMA ───
static double smaVolAt(const std::vector<CandleItem>& data, int row, int n) {
    if (row < n - 1 || n <= 0) return std::numeric_limits<double>::quiet_NaN();
    double sum = 0.0;
    for (int i = row - n + 1; i <= row; ++i)
        sum += data[static_cast<size_t>(i)].volume;
    return sum / n;
}

// ─── 辅助: 从 m_data 的第 row 根处的 EMA(n) ───
static double emaAt(const std::vector<CandleItem>& data, int row, int n) {
    if (row < n - 1 || n <= 0 || data.empty()) return std::numeric_limits<double>::quiet_NaN();
    double alpha = 2.0 / (n + 1.0);
    double ema = data[0].close;
    for (int i = 1; i <= row; ++i)
        ema = ema + alpha * (data[static_cast<size_t>(i)].close - ema);
    return ema;
}

// ─── 辅助: 从 m_data 的第 row 根处的 VWAP (累计 amount/volume) ───
static double vwapAt(const std::vector<CandleItem>& data, int row) {
    if (row < 0 || data.empty()) return std::numeric_limits<double>::quiet_NaN();
    double sumAmt = 0.0, sumVol = 0.0;
    for (int i = 0; i <= row; ++i) {
        // CandleItem 没有 amount 字段! 用 close * volume 近似
        sumAmt += data[static_cast<size_t>(i)].close * data[static_cast<size_t>(i)].volume;
        sumVol += data[static_cast<size_t>(i)].volume;
    }
    return sumVol > 0.0 ? sumAmt / sumVol : std::numeric_limits<double>::quiet_NaN();
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

    // ── 指标 Role — 从自身 m_data 计算 ──
    double val = std::numeric_limits<double>::quiet_NaN();

    switch (role) {
        case Ma5Role:   val = smaAt(m_data, row, 5);   break;
        case Ma10Role:  val = smaAt(m_data, row, 10);  break;
        case Ma20Role:  val = smaAt(m_data, row, 20);  break;
        case Ma60Role:  val = smaAt(m_data, row, 60);  break;
        case Ema5Role:  val = emaAt(m_data, row, 5);   break;
        case Ema10Role: val = emaAt(m_data, row, 10);  break;
        case Ema20Role: val = emaAt(m_data, row, 20);  break;
        case Ema60Role: val = emaAt(m_data, row, 60);  break;
        case MaVol5Role:  val = smaVolAt(m_data, row, 5);  break;
        case MaVol10Role: val = smaVolAt(m_data, row, 10); break;
        case VwapRole:  val = vwapAt(m_data, row); break;

        // MACD/KDJ/RSI: 只在最后一行返回有效值 (递推计算, 历史值成本高)
        case MacdDifRole:
        case MacdDeaRole:
        case MacdHistRole: {
            if (row != static_cast<int>(m_data.size()) - 1) return QVariant();
            if (m_data.size() < 26) return QVariant();
            // MACD(12,26,9)
            double a12 = 2.0/13.0, a26 = 2.0/27.0, a9 = 2.0/10.0;
            double ema12 = m_data[0].close, ema26 = m_data[0].close, dea = 0.0;
            bool deaInit = false;
            for (size_t i = 1; i < m_data.size(); ++i) {
                double c = m_data[i].close;
                ema12 += a12 * (c - ema12);
                ema26 += a26 * (c - ema26);
                double dif = ema12 - ema26;
                if (!deaInit) { dea = dif; deaInit = true; }
                else dea += a9 * (dif - dea);
                if (i == m_data.size() - 1) {
                    double difFinal = ema12 - ema26;
                    if (role == MacdDifRole)  return difFinal;
                    if (role == MacdDeaRole)  return dea;
                    if (role == MacdHistRole) return 2.0 * (difFinal - dea);
                }
            }
            return QVariant();
        }
        case KdjKRole: case KdjDRole: case KdjJRole: {
            if (row != static_cast<int>(m_data.size()) - 1) return QVariant();
            if (m_data.size() < 9 + 3) return QVariant();
            // KDJ(9,3,3)
            double aK = 2.0/4.0, aD = 2.0/4.0;
            double kval = 50.0, dval = 50.0;
            for (int i = 8; i < static_cast<int>(m_data.size()); ++i) {
                double hh = m_data[static_cast<size_t>(i)].high;
                double ll = m_data[static_cast<size_t>(i)].low;
                for (int j = i - 8; j < i; ++j) {
                    if (m_data[static_cast<size_t>(j)].high > hh) hh = m_data[static_cast<size_t>(j)].high;
                    if (m_data[static_cast<size_t>(j)].low  < ll) ll = m_data[static_cast<size_t>(j)].low;
                }
                double rsv = (hh - ll > 0) ? (m_data[static_cast<size_t>(i)].close - ll) / (hh - ll) * 100.0 : 50.0;
                kval += aK * (rsv - kval);
                dval += aD * (kval - dval);
            }
            if (role == KdjKRole) return kval;
            if (role == KdjDRole) return dval;
            if (role == KdjJRole) return 3.0 * kval - 2.0 * dval;
            return QVariant();
        }
        case Rsi6Role: case Rsi14Role: {
            if (row != static_cast<int>(m_data.size()) - 1) return QVariant();
            int n = (role == Rsi6Role) ? 6 : 14;
            if (static_cast<int>(m_data.size()) < n + 1) return QVariant();
            double avgGain = 0.0, avgLoss = 0.0;
            for (int i = 1; i <= n; ++i) {
                double diff = m_data[static_cast<size_t>(i)].close - m_data[static_cast<size_t>(i-1)].close;
                if (diff > 0) avgGain += diff; else avgLoss += -diff;
            }
            avgGain /= n; avgLoss /= n;
            for (size_t i = static_cast<size_t>(n) + 1; i < m_data.size(); ++i) {
                double diff = m_data[i].close - m_data[i-1].close;
                double gain = (diff > 0) ? diff : 0.0;
                double loss = (diff > 0) ? 0.0 : -diff;
                avgGain = (avgGain * (n - 1) + gain) / n;
                avgLoss = (avgLoss * (n - 1) + loss) / n;
            }
            if (avgLoss == 0.0) return 100.0;
            return 100.0 - 100.0 / (1.0 + avgGain / avgLoss);
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
