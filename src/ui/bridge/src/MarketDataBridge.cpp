#include "MarketDataBridge.h"

#include <QDebug>

MarketDataBridge::MarketDataBridge(QObject* parent)
    : QObject(parent) {}

void MarketDataBridge::loadBars(const QStringList& symbols,
                                 const QString& startDate,
                                 const QString& endDate)
{
    Q_UNUSED(symbols); Q_UNUSED(startDate); Q_UNUSED(endDate);
    emit barsChanged();
}

QVariantMap MarketDataBridge::getCrossSection(const QString& field,
                                                const QString& date,
                                                const QStringList& symbols)
{
    Q_UNUSED(field); Q_UNUSED(date); Q_UNUSED(symbols);
    return {};
}

QVariantList MarketDataBridge::getIndexConstituents(const QString& indexSymbol,
                                                      const QString& snapshotDate)
{
    Q_UNUSED(indexSymbol); Q_UNUSED(snapshotDate);
    return {};
}

QString MarketDataBridge::getNextTradingDay(const QString& anchorDate)
{
    Q_UNUSED(anchorDate);
    return {};
}

void MarketDataBridge::subscribeRealtime(const QStringList& symbols)
{
    for (const auto& sym : symbols) {
        auto& ref = m_subRefCount[sym];
        ++ref;
    }
}

void MarketDataBridge::unsubscribeRealtime()
{
    m_subRefCount.clear();
}