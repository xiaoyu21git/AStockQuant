#include "TradingMarketCalendarService.h"

#include <QDate>
#include <QTime>

namespace bridge {

TradingMarketCalendarService::TradingMarketCalendarService(QObject* parent)
    : QObject(parent) {
    refreshCalendar();
}

QVariantMap TradingMarketCalendarService::currentSessionSnapshot() const {
    return m_sessionSnapshot;
}

bool TradingMarketCalendarService::isTradingSessionOpen() const {
    return m_sessionSnapshot.value("sessionOpen").toBool();
}

bool TradingMarketCalendarService::isHolidayAware() const {
    return m_sessionSnapshot.value("holidayAware").toBool();
}

void TradingMarketCalendarService::refresh() {
    refreshCalendar();
}

void TradingMarketCalendarService::refreshCalendar() {
    QVariantMap snapshot;
    const QDate today = QDate::currentDate();
    const QTime now = QTime::currentTime();

    // 交易日历感知（MVP：工作日检测，未对接真实假期数据）
    snapshot["holidayAware"] = false;
    snapshot["error"] = QString();
    snapshot["sourceLabel"] = QStringLiteral("内置日历（工作日检测）");

    // 最新的已确认交易日（简化：最近一个工作日）
    QDate lastTrade = today;
    if (today.dayOfWeek() == 7) lastTrade = today.addDays(-1);       // 周日→周五
    else if (today.dayOfWeek() == 6) lastTrade = today.addDays(-1);  // 周六→周五
    snapshot["latestClosedTradeDate"] = lastTrade.toString("yyyy-MM-dd");

    // 交易时段判断（中国A股市场时段）
    bool weekend = (today.dayOfWeek() == 6 || today.dayOfWeek() == 7);
    const QTime morningOpen(9, 15);
    const QTime morningClose(11, 30);
    const QTime afternoonOpen(13, 0);
    const QTime afternoonClose(15, 0);
    const QTime postClose(15, 30);

    QString phaseLabel;
    bool sessionOpen = false;

    if (weekend) {
        phaseLabel = QStringLiteral("周末休市");
    } else if (now < morningOpen) {
        phaseLabel = QStringLiteral("等待开盘");
    } else if (now >= morningOpen && now <= morningClose) {
        phaseLabel = QStringLiteral("早盘交易中");
        sessionOpen = true;
    } else if (now > morningClose && now < afternoonOpen) {
        phaseLabel = QStringLiteral("午间休市");
    } else if (now >= afternoonOpen && now <= afternoonClose) {
        phaseLabel = QStringLiteral("午盘交易中");
        sessionOpen = true;
    } else if (now > afternoonClose && now <= postClose) {
        phaseLabel = QStringLiteral("盘后处理");
    } else {
        phaseLabel = QStringLiteral("已收盘");
    }

    snapshot["sessionPhaseLabel"] = phaseLabel;
    snapshot["sessionOpen"] = sessionOpen;

    m_sessionSnapshot = snapshot;
    emit currentSessionSnapshotChanged();
}

} // namespace bridge
