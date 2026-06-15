#pragma once
// ═════════════════════════════════════════════════════════════════════════
// TradingMarketCalendarService — 交易日历服务桥接
// 提供当前交易时段快照（阶段标签、假期感知、开市状态等）
// ═════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace bridge {

class TradingMarketCalendarService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap currentSessionSnapshot READ currentSessionSnapshot
               NOTIFY currentSessionSnapshotChanged)
public:
    explicit TradingMarketCalendarService(QObject* parent = nullptr);

    QVariantMap currentSessionSnapshot() const;

    /// @brief 检查当前是否在交易时段内
    Q_INVOKABLE bool isTradingSessionOpen() const;

    /// @brief 检查是否支持假期感知
    Q_INVOKABLE bool isHolidayAware() const;

    /// @brief 刷新交易日历快照
    Q_INVOKABLE void refresh();

signals:
    void currentSessionSnapshotChanged();

private:
    void refreshCalendar();

    QVariantMap m_sessionSnapshot;
};

} // namespace bridge
