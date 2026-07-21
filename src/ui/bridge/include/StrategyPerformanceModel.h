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
        ResultIdRole,
        ParametersRole       ///< 策略参数快照 JSON
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
    /// @brief 加载指定行的完整回测详情（含时间序列、交易统计、参数快照）
    Q_INVOKABLE QVariantMap loadResultDetail(int row);
    /// @brief 加载指定回测 run_id 的逐笔成交明细
    Q_INVOKABLE QVariantList loadTrades(const QString& runId, int offset = 0, int limit = 200);
    /// @brief 加载指定回测 run_id 的每日持仓快照
    Q_INVOKABLE QVariantList loadDailyPositions(const QString& runId);

signals:
    void strategyIdChanged();
    void errorOccurred(const QString& message);

private:
    bool ensureRepo();

    QString m_strategyId;
    QVector<QVariantMap> m_rows;
    std::unique_ptr<domain::backtest::BacktestResultRepository> m_repo;
};
