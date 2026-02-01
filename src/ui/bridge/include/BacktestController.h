// BacktestController.h
#pragma once

#include <QObject>
#include <QString>

// 简单的回测控制器，作为 QML 与引擎之间的桥接
class BacktestController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString dataSource READ dataSource WRITE setDataSource NOTIFY dataSourceChanged)
    Q_PROPERTY(QString symbol     READ symbol     WRITE setSymbol     NOTIFY symbolChanged)
    Q_PROPERTY(QString startDate  READ startDate  WRITE setStartDate  NOTIFY startDateChanged)
    Q_PROPERTY(QString endDate    READ endDate    WRITE setEndDate    NOTIFY endDateChanged)
    Q_PROPERTY(double  capital    READ capital    WRITE setCapital    NOTIFY capitalChanged)
    Q_PROPERTY(QString strategy   READ strategy   WRITE setStrategy   NOTIFY strategyChanged)
    Q_PROPERTY(double  maxPositionRatio READ maxPositionRatio WRITE setMaxPositionRatio NOTIFY maxPositionRatioChanged)
    Q_PROPERTY(bool    running    READ running    NOTIFY runningChanged)
    Q_PROPERTY(double  commissionRate READ commissionRate WRITE setCommissionRate NOTIFY commissionRateChanged)
    Q_PROPERTY(double  slippageRate   READ slippageRate   WRITE setSlippageRate   NOTIFY slippageRateChanged)
    Q_PROPERTY(double  minVolume      READ minVolume      WRITE setMinVolume      NOTIFY minVolumeChanged)

public:
    explicit BacktestController(QObject* parent = nullptr) : QObject(parent) {}

    // 由 QML 调用，启动一次回测（后续可接入真实 Engine）
    Q_INVOKABLE void run();

    QString dataSource() const { return m_dataSource; }
    void    setDataSource(const QString& source);

    QString symbol() const { return m_symbol; }
    void    setSymbol(const QString& s);

    QString startDate() const { return m_startDate; }
    void    setStartDate(const QString& date);

    QString endDate() const { return m_endDate; }
    void    setEndDate(const QString& date);

    double  capital() const { return m_capital; }
    void    setCapital(double c);

    QString strategy() const { return m_strategy; }
    void    setStrategy(const QString& s);

    bool   running() const { return m_running; }

    double commissionRate() const { return m_commissionRate; }
    void   setCommissionRate(double r) { if (!qFuzzyCompare(m_commissionRate, r)) { m_commissionRate = r; emit commissionRateChanged(); } }

    double slippageRate() const { return m_slippageRate; }
    void   setSlippageRate(double r) { if (!qFuzzyCompare(m_slippageRate, r)) { m_slippageRate = r; emit slippageRateChanged(); } }

    double minVolume() const { return m_minVolume; }
    void   setMinVolume(double v) { if (!qFuzzyCompare(m_minVolume, v)) { m_minVolume = v; emit minVolumeChanged(); } }

    double  maxPositionRatio() const { return m_maxPositionRatio; }
    void    setMaxPositionRatio(double r);

signals:
    void dataSourceChanged();
    void startDateChanged();
    void endDateChanged();
    void capitalChanged();
    void strategyChanged();
    void maxPositionRatioChanged();
    void symbolChanged();
    void runningChanged();
    void commissionRateChanged();
    void slippageRateChanged();
    void minVolumeChanged();

private:
    QString m_dataSource{"模拟数据"};
    QString m_symbol{"000001.SZ"};
    QString m_startDate;  // 形如 "2023-01-01"
    QString m_endDate;    // 形如 "2023-12-31"
    double  m_capital{100000.0};
    QString m_strategy{"移动平均线策略"};
    double  m_maxPositionRatio{1.0};
    bool    m_running{false};
    double  m_commissionRate{0.001};   // 默认万分之一
    double  m_slippageRate{0.0005};    // 默认每侧 5bp
    double  m_minVolume{0.0};          // 默认不开启流动性过滤
};
