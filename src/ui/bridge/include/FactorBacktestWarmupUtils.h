#pragma once

#include <QDate>
#include <QSet>
#include <QStringList>

#include <QString>

#include <algorithm>

namespace factor::warmup {

inline int requiredWarmupTradingDays(int minDataPoints, int skipRecent)
{
    const int baseWarmupDays = (std::max)(0, minDataPoints - 1);
    return baseWarmupDays + (std::max)(0, skipRecent);
}

inline int fallbackWarmupCalendarLookbackDays(int lookbackTradingDays)
{
    return (std::max)(365, (lookbackTradingDays + 10) * 2);
}

inline QDate resolveWarmupHistoryStartDate(const QDate& anchorStartDate,
                                          const QStringList& ascendingTradeDates,
                                          int lookbackTradingDays)
{
    if (!anchorStartDate.isValid() || lookbackTradingDays <= 0 || ascendingTradeDates.isEmpty()) {
        return {};
    }

    QStringList eligibleTradeDates;
    eligibleTradeDates.reserve(ascendingTradeDates.size());
    for (const QString& tradeDateText : ascendingTradeDates) {
        const QDate tradeDate = QDate::fromString(tradeDateText.trimmed(), "yyyy-MM-dd");
        if (!tradeDate.isValid() || tradeDate >= anchorStartDate) {
            continue;
        }
        eligibleTradeDates.append(tradeDate.toString("yyyy-MM-dd"));
    }

    if (eligibleTradeDates.isEmpty()) {
        return {};
    }

    const qsizetype eligibleCount = eligibleTradeDates.size();
    const qsizetype lookbackCount = static_cast<qsizetype>((std::max)(0, lookbackTradingDays));
    const qsizetype startIndex = eligibleCount > lookbackCount ? (eligibleCount - lookbackCount) : 0;
    return QDate::fromString(eligibleTradeDates.at(startIndex), "yyyy-MM-dd");
}

inline QString canonicalDailyBarSourceField(const QString& rawField)
{
    const QString field = rawField.trimmed().toLower();
    if (field == QStringLiteral("adj_factor")) {
        return QStringLiteral("post_adjust_factor");
    }
    return field;
}

inline QString buildDailyBarSelectExpression(const QString& rawField,
                                             const QSet<QString>& availableColumns)
{
    const QString field = rawField.trimmed().toLower();
    if (field.isEmpty()) {
        return {};
    }

    const QString sourceField = canonicalDailyBarSourceField(field);
    if (sourceField.isEmpty() || !availableColumns.contains(sourceField)) {
        return {};
    }

    if (sourceField == field) {
        return sourceField;
    }

    return QStringLiteral("%1 AS %2").arg(sourceField, field);
}

} // namespace factor::warmup