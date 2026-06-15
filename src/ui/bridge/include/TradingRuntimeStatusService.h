#pragma once
// ═════════════════════════════════════════════════════════════════════════
// TradingRuntimeStatusService — 运行时状态服务桥接
// 提供策略/账户运行时会话快照查询
// 委托给 app::system::TradingSystem
// ═════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>

namespace bridge {

class TradingRuntimeStatusService : public QObject {
    Q_OBJECT
public:
    explicit TradingRuntimeStatusService(QObject* parent = nullptr);

    /// @brief 按策略 ID 查询运行时会话快照
    Q_INVOKABLE QVariantMap sessionSnapshotForStrategy(const QString& strategyId);

    /// @brief 按账户 ID 查询运行时会话快照
    Q_INVOKABLE QVariantMap sessionSnapshotForAccount(const QString& accountId);

    /// @brief 同步刷新
    Q_INVOKABLE void refresh();

    /// @brief 异步刷新
    Q_INVOKABLE void refreshAsync();

    /// @brief 获取所有会话快照
    Q_INVOKABLE QVariantList sessionSnapshots() const;

signals:
    void sessionSnapshotsChanged();

private:
    QVariantMap buildDefaultSnapshot(const QString& sessionId) const;
    QVariantMap buildTradingSystemSnapshot() const;

    QVariantList m_sessionSnapshots;
};

} // namespace bridge
