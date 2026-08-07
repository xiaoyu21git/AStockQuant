#pragma once
// ─────────────────────────────────────────────────────────────────────
// TradingBridges.h — QML桥接层 (Qt QObject, 纯类型转换+信号转发)
// 委托给纯C++的 app::system::TradingSystem 单例
// 禁止: 字符串比较业务判断, QMap key 业务匹配, 直接数据库访问
// ─────────────────────────────────────────────────────────────────────

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <QTimer>
#include <QDateTime>
#include <QMutex>
#include "foundation/Utils/Uuid.h"

namespace bridge {

// ═══════════════════════════════════════════════════════════════════
// TradeExecutionBridge — 订单提交/撤销/状态查询/执行调度
// ═══════════════════════════════════════════════════════════════════
class TradeExecutionBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool liveBridgeReady READ liveBridgeReady NOTIFY liveBridgeReadyChanged)
    Q_PROPERTY(QVariantList recentRuleHits READ recentRuleHits NOTIFY recentRuleHitsChanged)
    Q_PROPERTY(QVariantList recentOrders READ recentOrders NOTIFY recentOrdersChanged)
    Q_PROPERTY(QString lastErrorMessage READ lastErrorMessage NOTIFY lastErrorMessageChanged)
public:
    explicit TradeExecutionBridge(QObject* parent = nullptr);

    bool initialized() const;
    bool liveBridgeReady() const;
    QVariantList recentRuleHits() const;
    QVariantList recentOrders() const;
    QString lastErrorMessage() const;

    // ── 订单操作 ──
    Q_INVOKABLE QVariantMap submitOrder(const QVariantMap& orderMap);
    Q_INVOKABLE bool cancelOrder(const QString& brokerOrderId);
    Q_INVOKABLE QString liveBridgeStatusMessage() const;
    Q_INVOKABLE bool isLiveBridgeReady();

    /// @brief 设置交易配置（由系统设置页调用，在 ensureInitialized 前配置）
    void setTradingConfig(const QVariantMap& config) { m_tradingConfig = config; }

    /// @brief 提交订单（QML 兼容入口），返回 true=已受理 false=已拒绝
    /// 错误详情通过 lastErrorMessage 属性获取
    Q_INVOKABLE bool submitBridgeOrder(const QVariantMap& request);

    // ── 执行调度 ──
    Q_INVOKABLE bool resumeExecutionPause(const QString& executionScopeId,
                                           const QString& pausedBatchId);
    Q_INVOKABLE bool approveExecutionCheckpoint(const QString& executionScopeId,
                                                 const QString& batchId);
    Q_INVOKABLE bool cancelManualTestOrder(const QString& orderId);

    /// @brief 一键平仓: 查持仓 → 构建平仓单 → 提交, 一步到位
    /// 返回 QVariantMap {accepted: bool, message: string}
    Q_INVOKABLE QVariantMap quickClosePosition(const QString& symbol, const QString& mode);

    // ── 管理 ──
    Q_INVOKABLE void clearRecentOrders();

    // ── Domain 工具方法 (薄转发到 OrderUtils.h / PositionUtils.h 自由函数) ──
    Q_INVOKABLE QString resolveLiveOrderType(const QString& rawType, const QString& optionType,
                                              const QString& underlying, const QString& exchange) const;
    Q_INVOKABLE QString resolveLiveOrderAction(const QString& rawType, const QString& side,
                                                const QString& positionEffect, const QString& action) const;
    Q_INVOKABLE QString translateOrderSide(const QString& side) const;
    Q_INVOKABLE bool isFuturesExchange(const QString& exchange) const;

signals:
    void initializedChanged();
    void liveBridgeReadyChanged();
    void recentRuleHitsChanged();
    void recentOrdersChanged();
    void lastErrorMessageChanged();

    void orderRequested(const QVariantMap& orderRequest);
    void orderRequestPublished(const QVariantMap& orderRequest);   // QML 别名
    void orderStatusChanged(const QVariantMap& orderStatus);
    void orderStatusPublished(const QVariantMap& orderStatus);    // QML 别名
    void tradeFilled(const QVariantMap& tradeFill);
    void tradeFillPublished(const QVariantMap& tradeFill);        // QML 别名

    /// @brief 策略引擎产生订单时立即通知（无论 TradingSystem 是否初始化）
    void orderGenerated(const QVariantMap& orderInfo);
    /// @brief 订单提交结果（成功/风控拒绝/引擎未初始化/无价格）
    void orderSubmitResult(const QVariantMap& result);

private:
    void ensureInitialized();
    void appendRecentOrder(const QVariantMap& order);
    void setLastError(const QString& message);
    void publishOrderStatusUpdate(const QString& brokerOrderId,
                                  const QString& status,
                                  const QString& message);

    bool m_initialized{false};
    QVariantMap m_tradingConfig;
    QVariantList m_recentOrders;
    mutable QMutex m_recentOrdersMutex;        // 保护 m_recentOrders 多线程访问
    foundation::utils::Uuid m_orderUpdateSub;  // EventBus subscription
    QString m_lastErrorMessage;
};

// ═══════════════════════════════════════════════════════════════════
// PositionAccountBridge — 持仓账户快照/持仓列表/订单状态
// ═══════════════════════════════════════════════════════════════════
class PositionAccountBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(QVariantMap accountSnapshot READ accountSnapshot NOTIFY accountSnapshotChanged)
    Q_PROPERTY(QVariantList positions READ positions NOTIFY positionsChanged)
    Q_PROPERTY(QVariantList recentOrderStatuses READ recentOrderStatuses
               NOTIFY recentOrderStatusesChanged)
public:
    explicit PositionAccountBridge(QObject* parent = nullptr);

    bool initialized() const;
    QVariantMap accountSnapshot() const;
    QVariantList positions() const;
    QVariantList recentOrderStatuses() const;

    Q_INVOKABLE void requestInitialSnapshot();
    Q_INVOKABLE bool initialSnapshotLoaded() const;
    Q_INVOKABLE void initialize();

    // ── Domain 工具方法 (薄转发到 PositionAccountEngine 静态方法) ──
    Q_INVOKABLE double normalizePositionQuantity(double rawQty, const QString& type) const;
    Q_INVOKABLE double calculatePositionMarketValue(const QVariantList& rawPositions) const;
    Q_INVOKABLE QString positionTypeTitle(const QString& type) const;
    Q_INVOKABLE QString positionUnit(const QString& type) const;
    Q_INVOKABLE QString positionSideLabel(const QString& side) const;
    Q_INVOKABLE QString closeableLabel(const QString& type, const QString& side) const;

signals:
    void initializedChanged();
    void accountSnapshotChanged();
    void positionsChanged();
    void dataChanged();
    void recentOrderStatusesChanged();
    void errorOccurred(const QString& message);

private:
    void refresh();
    void appendOrderStatus(const QVariantMap& status);

    bool m_initialized{false};
    QVariantList m_recentOrderStatuses;
    mutable QMutex m_orderStatusesMutex;       // 保护 m_recentOrderStatuses 多线程访问
};

// ═══════════════════════════════════════════════════════════════════
// RiskControlBridge — 实时风控指标/预警/组合快照
// ═══════════════════════════════════════════════════════════════════
class RiskControlBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(double varUsagePercent READ varUsagePercent NOTIFY varUsagePercentChanged)
    Q_PROPERTY(double currentDrawdownPercent READ currentDrawdownPercent NOTIFY currentDrawdownPercentChanged)
    Q_PROPERTY(double currentTotalExposurePercent READ currentTotalExposurePercent NOTIFY currentTotalExposurePercentChanged)
    Q_PROPERTY(double varBudgetAmount READ varBudgetAmount NOTIFY varBudgetAmountChanged)
    Q_PROPERTY(double estimatedVarAmount READ estimatedVarAmount NOTIFY estimatedVarAmountChanged)
public:
    explicit RiskControlBridge(QObject* parent = nullptr);

    double varUsagePercent() const;
    double currentDrawdownPercent() const;
    double currentTotalExposurePercent() const;
    double varBudgetAmount() const;
    double estimatedVarAmount() const;

    Q_INVOKABLE void initializeAsync();
    Q_INVOKABLE void initialize();
    Q_INVOKABLE void refresh();

    /// @brief 构建组合快照（供回测/策略分析用）
    Q_INVOKABLE QVariantMap buildPortfolioSnapshot(const QVariantMap& strategy,
                                                     const QVariantMap& backtestRecord);

signals:
    void varUsagePercentChanged();
    void currentDrawdownPercentChanged();
    void currentTotalExposurePercentChanged();
    void varBudgetAmountChanged();
    void estimatedVarAmountChanged();

private:
    QTimer m_timer;
    double m_varUsagePct{68.0};
    double m_drawdownPct{0.0};
    double m_exposurePct{0.0};
    double m_varBudget{0.0};
    double m_estimatedVar{0.0};
};

} // namespace bridge
