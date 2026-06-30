// CandleDataModel.h — K线数据模型 (QAbstractTableModel)
// 供 QtCharts CandlestickSeries 直接绑定, 零拷贝
#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <vector>

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

public:
    enum Column { TimestampCol = 0, OpenCol, HighCol, LowCol, CloseCol, VolumeCol, ColumnCount };
    enum Role { TimestampRole = Qt::UserRole + 1, OpenRole, HighRole, LowRole, CloseRole, VolumeRole };

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

    const CandleItem* candleAt(int row) const;  // 供 StockDataLoader 内部使用

signals:
    void countChanged();
    void lastPriceChanged();
    void lastVolumeChanged();
    void preCloseChanged();

private:
    std::vector<CandleItem> m_data;
    double m_preClose = 0.0;
};

} // namespace bridge
