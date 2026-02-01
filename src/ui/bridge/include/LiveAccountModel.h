#pragma once

#include <QObject>

// 简单实盘账户模型：从 JSON 快照文件读取资金和持仓概览
class LiveAccountModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(double totalAsset READ totalAsset NOTIFY accountChanged)
    Q_PROPERTY(double availableCash READ availableCash NOTIFY accountChanged)
    Q_PROPERTY(double positionMarketValue READ positionMarketValue NOTIFY accountChanged)
    Q_PROPERTY(double todayPnl READ todayPnl NOTIFY accountChanged)

public:
    explicit LiveAccountModel(QObject* parent = nullptr);

    Q_INVOKABLE void refresh(); // 从快照文件刷新

    double totalAsset() const { return m_totalAsset; }
    double availableCash() const { return m_availableCash; }
    double positionMarketValue() const { return m_positionMarketValue; }
    double todayPnl() const { return m_todayPnl; }

signals:
    void accountChanged();

private:
    double m_totalAsset{0.0};
    double m_availableCash{0.0};
    double m_positionMarketValue{0.0};
    double m_todayPnl{0.0};
};
