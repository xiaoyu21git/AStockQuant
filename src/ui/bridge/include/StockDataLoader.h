// StockDataLoader.h — K线数据加载器
// 桥接 GmSessionEngine tick 数据 → CandleDataModel
#pragma once

#include <QObject>
#include <QString>
#include "CandleDataModel.h"
#include "foundation/Utils/Uuid.h"

namespace engine { class EventBus; }

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

    // 实时行情回调 (由 GmSessionEngine::on_tick 或 EventBus 触发)
    void onTick(const QString& symbol, double price, double volume, qint64 timestamp);

signals:
    void symbolChanged();
    void periodChanged();
    void dataReady();
    void tickReceived(const QString& symbol, double price, double volume);

private:
    void subscribeToEventBus();

    CandleDataModel* m_model = nullptr;
    QString m_symbol;
    int m_period = Daily;
    CandleItem m_currentCandle;
    bool m_hasCurrentCandle = false;
    foundation::utils::Uuid m_tickSub;

    static qint64 periodStartMs(qint64 ts, int period);
    static qint64 periodMs(int period);
};

} // namespace bridge
