#pragma once

#include "FactorBacktestExecutor.h"
#include "../../../ui/bridge/include/DataFetchFieldContractUtils.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace factor::cached_bars {

inline bool rowOwnsMarketBarFields(const QVariantMap& row)
{
    const QString normalizedDataType = factor::bridge::normalizeSelectedDataType(
        row.value(factor::bridge::CleaningInternalFieldKeys::DATA_TYPE,
                  row.value(QStringLiteral("dataType"))).toString());
    return normalizedDataType.isEmpty()
        || normalizedDataType == factor::bridge::normalizedSelectedDataTypeName(
            factor::bridge::CleanedDataFieldGroup::MarketBar);
}

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
                                    int forwardDays,
                                    factor::MarketEnvironmentProfile marketEnvironmentProfile)
{
    if (forwardDays <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const int executionLagTradingDays = marketEnvironmentProfile == factor::MarketEnvironmentProfile::CN_A_SHARE ? 1 : 0;

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

    const size_t executionIndex = static_cast<size_t>(executionLagTradingDays);
    const size_t futureIndex = executionIndex + static_cast<size_t>(forwardDays);
    if (symbolBars.size() <= futureIndex) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double startClose = symbolBars[executionIndex].second;
    const double endClose = symbolBars[futureIndex].second;
    if (!std::isfinite(startClose) || !std::isfinite(endClose) || startClose <= 0.0 || endClose <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    return (endClose - startClose) / startClose;
}

inline std::vector<CachedMarketBar> buildCachedBarsFromRows(const QVariantList& rows)
{
    std::vector<CachedMarketBar> cachedBars;
    cachedBars.reserve(static_cast<size_t>(rows.size()));
    std::unordered_map<std::string, double> industryCodeBuckets;
    double nextIndustryCodeBucket = 1.0;
    std::unordered_map<std::string, size_t> rowIndexBySymbolDate;
    rowIndexBySymbolDate.reserve(static_cast<size_t>(rows.size()));

    for (const QVariant& rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        const bool marketBarOwner = rowOwnsMarketBarFields(row);
        const QString symbol = row.value(factor::bridge::CommonFieldKeys::SYMBOL).toString().trimmed();
        QString effectiveDate = row.value(
            factor::bridge::CommonFieldKeys::TRADE_DATE,
            row.value(factor::bridge::LegacyCleaningFieldKeys::DATE)).toString().trimmed();
        if (effectiveDate.isEmpty()) {
            effectiveDate = row.value(QStringLiteral("disclosure_date")).toString().trimmed();
        }
        bool closeOk = false;
        const double close = row.value(factor::bridge::MarketBarFieldKeys::CLOSE).toDouble(&closeOk);
        if (symbol.isEmpty() || effectiveDate.isEmpty()) {
            continue;
        }

        const std::string symbolKey = symbol.toStdString();
        const std::string dateKey = effectiveDate.toStdString();
        const std::string rowKey = symbolKey + "|" + dateKey;

        CachedMarketBar* bar = nullptr;
        auto rowIndexIt = rowIndexBySymbolDate.find(rowKey);
        if (rowIndexIt == rowIndexBySymbolDate.end()) {
            CachedMarketBar newBar;
            newBar.symbol = symbolKey;
            newBar.tradeDate = dateKey;
            newBar.close = std::numeric_limits<double>::quiet_NaN();
            cachedBars.push_back(std::move(newBar));
            rowIndexIt = rowIndexBySymbolDate.emplace(rowKey, cachedBars.size() - 1).first;
        }
        bar = &cachedBars[rowIndexIt->second];

        if (marketBarOwner && closeOk && std::isfinite(close)) {
            bar->close = close;
            bar->numericFields[factor::bridge::MarketBarFieldKeys::CLOSE.c_str()] = close;
        }

        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            const QString normalizedField = factor::bridge::runtimeContractFieldName(it.key());
            if (normalizedField.isEmpty()
                || normalizedField == factor::bridge::CommonFieldKeys::SYMBOL
                || normalizedField == factor::bridge::CommonFieldKeys::TRADE_DATE
                || normalizedField == factor::bridge::LegacyCleaningFieldKeys::DATE) {
                continue;
            }

            if (normalizedField == factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE) {
                bool ok = false;
                const double numericValue = it.value().toDouble(&ok);
                if (ok && std::isfinite(numericValue)) {
                    bar->numericFields[normalizedField.toStdString()] = numericValue;
                    continue;
                }

                const std::string industryText = it.value().toString().trimmed().toStdString();
                if (industryText.empty()) {
                    continue;
                }

                auto bucketIt = industryCodeBuckets.find(industryText);
                if (bucketIt == industryCodeBuckets.end()) {
                    bucketIt = industryCodeBuckets.emplace(industryText, nextIndustryCodeBucket).first;
                    nextIndustryCodeBucket += 1.0;
                }
                bar->numericFields[normalizedField.toStdString()] = bucketIt->second;
                continue;
            }

            if (!marketBarOwner && factor::bridge::marketBarFields().contains(normalizedField)) {
                continue;
            }

            bool ok = false;
            const double numericValue = it.value().toDouble(&ok);
            if (!ok || !std::isfinite(numericValue)) {
                continue;
            }

            bar->numericFields[normalizedField.toStdString()] = numericValue;
        }

        if (marketBarOwner) {
            const QVariant adjFactorValue = row.contains(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR)
                ? row.value(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR)
                : row.value(factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR, 1.0);
            const double adjFactor = adjFactorValue.toDouble();
            if (std::isfinite(adjFactor)) {
                bar->numericFields[factor::bridge::LegacyCleaningFieldKeys::ADJ_FACTOR.c_str()] = adjFactor;
            }
        }
    }

    std::sort(cachedBars.begin(), cachedBars.end(), [](const CachedMarketBar& left,
                                                       const CachedMarketBar& right) {
        if (left.symbol != right.symbol) {
            return left.symbol < right.symbol;
        }
        return left.tradeDate < right.tradeDate;
    });

    return cachedBars;
}

} // namespace factor::cached_bars