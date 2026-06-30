// StockDataLoader.h — K线数据加载器
// v2: 实时同步改为增量更新 (appendCandle + updateLastCandle)
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include "CandleDataModel.h"

namespace domain::market { class LiveData; }

namespace bridge {

class StockDataLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(bridge::CandleDataModel* model READ model CONSTANT)
    Q_PROPERTY(QString symbol READ symbol WRITE setSymbol NOTIFY symbolChanged)
    Q_PROPERTY(int period READ period WRITE setPeriod NOTIFY periodChanged)

public:
    enum Period { TimeShare = 0, Min1 = 1, Min5 = 2, Min15 = 3, Min30 = 4, Min60 = 5, Min120 = 6,
                  Daily = 7, Weekly = 8, Monthly = 9 };
    Q_ENUM(Period)

    explicit StockDataLoader(QObject* parent = nullptr);
    ~StockDataLoader() override;

    bridge::CandleDataModel* model() const { return m_model; }
    void setModel(bridge::CandleDataModel* m) { m_model = m; }

    QString symbol() const { return m_symbol; }
    void setSymbol(const QString& s);

    int period() const { return m_period; }
    void setPeriod(int p);

    Q_INVOKABLE void loadHistory(const QString& code, int period);
    Q_INVOKABLE void loadFromDB(const QString& code, int period);

signals:
    void symbolChanged();
    void periodChanged();
    void dataReady();
    void tickReceived(const QString& symbol, double price, double volume);

private slots:
    void syncLiveData();

private:
    static qint64 periodMs(int period);

    /// @brief 重置增量同步状态 (周期切换/标的变化时调用)
    void resetSyncState();

    CandleDataModel* m_model = nullptr;
    QString m_symbol;
    int m_period = Daily;
    QTimer m_timer;

    // ── 增量同步状态 ──
    bool   m_isFirstSync = true;
    int    m_modelCount = 0;
    qint64 m_lastBucketKey = -1;  // 最近一个聚合桶的时间键
    double m_lastClose = 0.0;
    double m_lastVolume = 0.0;
    double m_lastHigh = 0.0;
    double m_lastLow = 0.0;
};

} // namespace bridge
