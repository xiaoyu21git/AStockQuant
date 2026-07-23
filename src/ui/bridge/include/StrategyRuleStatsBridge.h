#pragma once
// StrategyRuleStatsBridge — 策略规则模板统计与参数桥接层
// 职责: 暴露 compiled.json 规则模板元数据 + RuleGate 运行时统计数据 + 可调参数提取
// 只转发/转换，不含业务逻辑

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <QMutex>

class StrategyRuleStatsBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasData READ hasData NOTIFY dataChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)

public:
    explicit StrategyRuleStatsBridge(QObject* parent = nullptr);

    // ── 模板元数据 ──
    /// @brief 加载全部规则模板(轻量: id+name+phase+tags+rulesCount+actions+summary截断100字符)
    Q_INVOKABLE QVariantList loadAllTemplates();

    /// @brief 按阶段过滤模板
    Q_INVOKABLE QVariantList getTemplatesByPhase(const QString& phase);

    /// @brief 获取单个模板详情(完整条件树+参数列表)
    Q_INVOKABLE QVariantMap getTemplateDetail(const QString& templateId);

    // ── 统计数据 ──
    /// @brief 获取某模板的运行时统计，strategyId为空则从首个活跃策略取值
    Q_INVOKABLE QVariantMap getTemplateStats(const QString& templateId,
                                              const QString& strategyId = QString());

    /// @brief 聚合统计(总模板/已绑定/平均拦截率/平均胜率)，排除evaluated===0
    Q_INVOKABLE QVariantMap getAggregateStats(const QString& strategyId = QString());

    // ── 参数管理 ──
    /// @brief 递归提取条件树中的可调数值参数(只读)
    Q_INVOKABLE QVariantList extractTunableParams(const QString& templateId);

    /// @brief 更新模板参数(Phase B实现，当前返回false)
    Q_INVOKABLE bool updateTemplateParams(const QString& templateId,
                                           const QVariantMap& params,
                                           const QString& strategyId);

    /// @brief 强制刷新（从 sharedRuleLibrary 重新读取）
    Q_INVOKABLE void refresh();

    // ── 规则归因 (回测后数据) ──
    /// @brief 获取指定策略下某模板的规则级归因数据
    Q_INVOKABLE QVariantList getRuleAttribution(const QString& strategyId,
                                                 const QString& templateId);

    // ── 状态查询 ──
    bool hasData() const { return m_hasData; }
    bool isLoading() const { return m_isLoading; }
    QString lastError() const { return m_lastError; }

signals:
    void dataChanged();
    void loadingChanged();
    void errorOccurred(const QString& message);
    void templateStatsUpdated(const QString& templateId);

private:
    void preload();  // 构造时预加载规则库元数据
    void setLoading(bool loading);
    void setError(const QString& msg);
    double safeDiv(double numerator, double denominator) const;
    QString formatWinRateSource(const QString& source) const;

    mutable QMutex m_mutex;
    bool m_hasData{false};
    bool m_isLoading{false};
    QString m_lastError;
    QVariantList m_cachedTemplates;  // 预加载的模板元数据
};
