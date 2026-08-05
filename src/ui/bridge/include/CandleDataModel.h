// CandleDataModel.h — K线数据模型 (QAbstractTableModel)
// 供 QtCharts CandlestickSeries 直接绑定, 零拷贝
// v2: 扩展 Role 暴露 MA/MACD/KDJ/RSI 指标, 保证行同步原子性
#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <vector>

namespace domain::market { class BarSeries; }

namespace bridge {

struct CandleItem {
    qint64  timestamp;   // 毫秒时间戳
    double  open;
    double  high;
    double  low;
    double  close;
    double  volume;
};

class CandleDataModel : public QAbstractTableModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(double lastPrice READ lastPrice NOTIFY lastPriceChanged)
    Q_PROPERTY(double lastVolume READ lastVolume NOTIFY lastVolumeChanged)
    Q_PROPERTY(double preClose READ preClose WRITE setPreClose NOTIFY preCloseChanged)
    Q_PROPERTY(double avgLine READ avgLine NOTIFY avgLineChanged)

public:
    // ── 基础列 ──
    enum Column { TimestampCol = 0, OpenCol, HighCol, LowCol, CloseCol, VolumeCol, ColumnCount };

    // ── 基础 Role (Qt::UserRole + 1 ~ +6) ──
    enum Role {
        TimestampRole = Qt::UserRole + 1,
        OpenRole,
        HighRole,
        LowRole,
        CloseRole,
        VolumeRole,

        // ── 指标 Role (Qt::UserRole + 100 起始, 避开基础 Role) ──
        Ma5Role  = Qt::UserRole + 100,
        Ma10Role,
        Ma20Role,
        Ma60Role,
        Ema5Role,
        Ema10Role,
        Ema20Role,
        Ema60Role,
        MaVol5Role,
        MaVol10Role,
        MacdDifRole,
        MacdDeaRole,
        MacdHistRole,
        KdjKRole,
        KdjDRole,
        KdjJRole,
        Rsi6Role,
        Rsi14Role,
        VwapRole
    };

    explicit CandleDataModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void setCandles(const QVariantList& candles);        // 批量设置
    Q_INVOKABLE void appendCandle(qint64 ts, double o, double h, double l, double c, double v);
    Q_INVOKABLE void updateLastCandle(double close, double high, double low, double volume);
    Q_INVOKABLE void clear();

    double lastPrice()  const;
    double lastVolume() const;
    double preClose()   const { return m_preClose; }
    void   setPreClose(double v);
    double avgLine()    const { return m_avgLine; }
    void   setAvgLine(double v) { if (m_avgLine != v) { m_avgLine = v; emit avgLineChanged(); } }

    const CandleItem* candleAt(int row) const;  // 供 StockDataLoader 内部使用

    /// @brief 绑定数据源 BarSeries, 供 data() 查询指标时使用
    void setDataSource(const domain::market::BarSeries* series);
    const domain::market::BarSeries* dataSource() const { return m_series; }

signals:
    void countChanged();
    void lastPriceChanged();
    void lastVolumeChanged();
    void preCloseChanged();
    void avgLineChanged();

private:
    std::vector<CandleItem> m_data;
    double m_preClose = 0.0;
    double m_avgLine = 0.0;
    const domain::market::BarSeries* m_series = nullptr;
};

} // namespace bridge
