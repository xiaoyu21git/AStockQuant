#pragma once
// ═════════════════════════════════════════════════════════════════════════
// StrategyPerformanceModel — 策略绩效历史列表 (QAbstractListModel)
// 查询 BacktestResultRepository 获取历史回测记录, 以标准 Qt MVC 模式暴露
// ═════════════════════════════════════════════════════════════════════════

#include <QAbstractListModel>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <memory>

namespace domain::backtest {
class BacktestResultRepository;
}

class StrategyPerformanceModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString strategyId READ strategyId WRITE setStrategyId NOTIFY strategyIdChanged)
public:
    enum Roles {
        RunAtRole = Qt::UserRole + 1,
        TotalReturnRole,
        AnnualizedReturnRole,
        SharpeRatioRole,
        MaxDrawdownRole,
        VolatilityRole,
        WinRateRole,
        SortinoRatioRole,
        CalmarRatioRole,
        ProfitFactorRole,
        BehaviorKindRole,
        ResultIdRole
    };

    explicit StrategyPerformanceModel(QObject* parent = nullptr);
    ~StrategyPerformanceModel() override;

    QString strategyId() const { return m_strategyId; }
    void setStrategyId(const QString& id);

    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap loadResultDetail(int row);

signals:
    void strategyIdChanged();
    void errorOccurred(const QString& message);

private:
    bool ensureRepo();

    QString m_strategyId;
    QVector<QVariantMap> m_rows;
    std::unique_ptr<domain::backtest::BacktestResultRepository> m_repo;
};
