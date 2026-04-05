#pragma once

#include "FactorBacktestExecutor.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_set>
#include <vector>

namespace factor::cached_bars {

inline std::string normalizeTradeDate(const std::string& rawDate)
{
    const QString trimmed = QString::fromStdString(rawDate).trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QDateTime isoDateTime = QDateTime::fromString(trimmed, Qt::ISODate);
    if (isoDateTime.isValid()) {
        return isoDateTime.date().toString("yyyy-MM-dd").toStdString();
    }

    const QStringList dateTimeFormats = {
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy/MM/dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-ddTHH:mm:ss"),
        QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")
    };
    for (const QString& format : dateTimeFormats) {
        const QDateTime dateTime = QDateTime::fromString(trimmed, format);
        if (dateTime.isValid()) {
            return dateTime.date().toString("yyyy-MM-dd").toStdString();
        }
    }

    const QStringList dateFormats = {
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("yyyy/MM/dd")
    };
    for (const QString& format : dateFormats) {
        const QDate date = QDate::fromString(trimmed, format);
        if (date.isValid()) {
            return date.toString("yyyy-MM-dd").toStdString();
        }
    }

    const int firstSpace = trimmed.indexOf(' ');
    if (firstSpace > 0) {
        const QDate date = QDate::fromString(trimmed.left(firstSpace), "yyyy-MM-dd");
        if (date.isValid()) {
            return date.toString("yyyy-MM-dd").toStdString();
        }
    }

    return trimmed.toStdString();
}

inline bool isBarWithinRange(const CachedMarketBar& bar,
                             const std::string& startDate,
                             const std::string& endDate)
{
    const std::string normalizedTradeDate = normalizeTradeDate(bar.tradeDate);
    if (normalizedTradeDate.empty()) {
        return false;
    }

    return (startDate.empty() || normalizedTradeDate >= startDate) &&
           (endDate.empty() || normalizedTradeDate <= endDate);
}

inline std::vector<std::string> extractTradeDates(const std::vector<CachedMarketBar>& cachedBars,
                                                  const std::string& startDate,
                                                  const std::string& endDate)
{
    std::set<std::string> tradeDateSet;
    for (const auto& bar : cachedBars) {
        const std::string normalizedTradeDate = normalizeTradeDate(bar.tradeDate);
        if (!normalizedTradeDate.empty() &&
            (startDate.empty() || normalizedTradeDate >= startDate) &&
            (endDate.empty() || normalizedTradeDate <= endDate)) {
            tradeDateSet.insert(normalizedTradeDate);
        }
    }

    return {tradeDateSet.begin(), tradeDateSet.end()};
}

inline std::vector<std::string> extractSymbols(const std::vector<CachedMarketBar>& cachedBars,
                                               const std::string& date,
                                               const std::unordered_set<std::string>& allowedSymbols)
{
    std::set<std::string> symbolSet;
    for (const auto& bar : cachedBars) {
        if (normalizeTradeDate(bar.tradeDate) != date || bar.symbol.empty()) {
            continue;
        }
        if (!allowedSymbols.empty() && allowedSymbols.find(bar.symbol) == allowedSymbols.end()) {
            continue;
        }
        symbolSet.insert(bar.symbol);
    }

    return {symbolSet.begin(), symbolSet.end()};
}

inline double calculateFutureReturn(const std::vector<CachedMarketBar>& cachedBars,
                                    const std::string& symbol,
                                    const std::string& startDate,
                                    int forwardDays)
{
    if (forwardDays <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    std::vector<std::pair<std::string, double>> symbolBars;
    symbolBars.reserve(cachedBars.size());
    for (const auto& bar : cachedBars) {
        if (bar.symbol != symbol) {
            continue;
        }

        const std::string normalizedTradeDate = normalizeTradeDate(bar.tradeDate);
        if (normalizedTradeDate.empty() || normalizedTradeDate < startDate) {
            continue;
        }

        symbolBars.emplace_back(normalizedTradeDate, bar.close);
    }

    std::sort(symbolBars.begin(), symbolBars.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    if (symbolBars.size() <= static_cast<size_t>(forwardDays)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double startClose = symbolBars.front().second;
    const double endClose = symbolBars[static_cast<size_t>(forwardDays)].second;
    if (!std::isfinite(startClose) || !std::isfinite(endClose) || startClose <= 0.0 || endClose <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    return (endClose - startClose) / startClose;
}

} // namespace factor::cached_bars