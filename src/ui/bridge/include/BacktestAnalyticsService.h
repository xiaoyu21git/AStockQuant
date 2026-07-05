#pragma once
// BacktestAnalyticsService — 因子回测绩效分析数据服务
// 查询 alpha.factor_backtest_* 表，暴露给 QML 性能页

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QString>

namespace ui::bridge {

class BacktestAnalyticsService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList runList READ runList NOTIFY runListChanged)
    Q_PROPERTY(QVariantMap activeRunDetail READ activeRunDetail NOTIFY activeRunDetailChanged)

public:
    explicit BacktestAnalyticsService(QObject* parent = nullptr);

    QVariantList runList() const { return m_runList; }
    QVariantMap activeRunDetail() const { return m_activeDetail; }

    // ── 查询接口 ──
    Q_INVOKABLE void refreshRunList();
    Q_INVOKABLE void loadRunDetail(const QString& runId);
    Q_INVOKABLE QVariantList loadDailyReturns(const QString& runId);
    Q_INVOKABLE QVariantList loadIcSeries(const QString& runId);
    Q_INVOKABLE QVariantList loadTrades(const QString& runId, int offset = 0, int limit = 100);
    Q_INVOKABLE QVariantList loadPeriods(const QString& runId);
    Q_INVOKABLE QVariantMap loadRunComparison(const QStringList& runIds);

    // ── 统计辅助 ──
    Q_INVOKABLE QVariantMap aggregateStats(const QVariantList& data, const QString& field);
    Q_INVOKABLE QVariantList computeDistribution(const QVariantList& data, const QString& field, int bins = 20);

signals:
    void runListChanged();
    void activeRunDetailChanged();
    void runDetailLoaded(const QString& runId);

private:
    QVariantList m_runList;
    QVariantMap  m_activeDetail;
};

} // namespace ui::bridge
