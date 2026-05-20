#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"

#include <QDate>
#include <QSqlDatabase>
#include <QSqlQuery>

namespace factor::bridge {

// 新股过滤 —— 上市未满 N 个交易日剔除
class NewStockFilterRule final : public ICleaningRule {
public:
    explicit NewStockFilterRule(int minDays = 60) : m_minTradeDays(minDays) {}

    void setDatabaseConnectionName(const QString& connName) {
        m_dbConnectionName = connName.trimmed();
        m_tradeDayCountCache.clear();
    }

    QString id() const override { return cleaningRuleIdName(CleaningRuleId::NewStockFilter); }
    QString displayName() const override { return QStringLiteral("新股过滤"); }
    int executionOrder() const override { return 45; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::Close.has(record) || Accessors::Volume.has(record);
    }

    bool clean(QVariantMap& record) override {
        auto sym = Accessors::Symbol.get(record);
        if (!sym) return true;

        if (m_minTradeDays <= 0) {
            return true;
        }

        // 检查上市日期
        const auto listDateStr = Accessors::ListDate.get(record);
        const auto tradeDateStr = Accessors::TradeDate.get(record);
        if (!listDateStr.has_value() || !tradeDateStr.has_value()) {
            return true;
        }

        const QDate listDate = QDate::fromString(listDateStr->left(10), "yyyy-MM-dd");
        const QDate tradeDate = QDate::fromString(tradeDateStr->left(10), "yyyy-MM-dd");
        if (!listDate.isValid() || !tradeDate.isValid()) {
            return true;
        }
        if (tradeDate < listDate) {
            return false;
        }

        const std::optional<int> tradingDaysSinceListing = countTradingDaysSinceListing(sym.value(),
                                                                                        listDate,
                                                                                        tradeDate);
        if (!tradingDaysSinceListing.has_value()) {
            return false;
        }

        return *tradingDaysSinceListing > m_minTradeDays;
    }

    void cleanCrossSectional(QVariantList&) override {
        m_tradeDayCountCache.clear();
    }

private:
    std::optional<int> countTradingDaysSinceListing(const QString& symbol,
                                                    const QDate& listDate,
                                                    const QDate& tradeDate)
    {
        const QString cacheKey = QStringLiteral("%1|%2|%3")
            .arg(symbol,
                 listDate.toString(QStringLiteral("yyyy-MM-dd")),
                 tradeDate.toString(QStringLiteral("yyyy-MM-dd")));
        const auto cacheIt = m_tradeDayCountCache.constFind(cacheKey);
        if (cacheIt != m_tradeDayCountCache.constEnd()) {
            return cacheIt.value();
        }

        const QString connName = m_dbConnectionName.isEmpty()
            ? QStringLiteral("qt_sql_default_connection")
            : m_dbConnectionName;
        if (!QSqlDatabase::contains(connName)) {
            qWarning() << "NewStockFilterRule: 数据库连接" << connName << "不存在";
            return std::nullopt;
        }

        QSqlDatabase db = QSqlDatabase::database(connName);
        if (!db.isOpen()) {
            qWarning() << "NewStockFilterRule: 数据库未打开";
            return std::nullopt;
        }

        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT COUNT(DISTINCT trade_date) "
            "FROM daily_bar "
            "WHERE symbol = :symbol "
            "  AND trade_date >= :list_date "
            "  AND trade_date <= :trade_date"));
        query.bindValue(QStringLiteral(":symbol"), symbol);
        query.bindValue(QStringLiteral(":list_date"), listDate.toString(QStringLiteral("yyyy-MM-dd")));
        query.bindValue(QStringLiteral(":trade_date"), tradeDate.toString(QStringLiteral("yyyy-MM-dd")));
        if (!query.exec()) {
            qWarning() << "NewStockFilterRule: daily_bar 查询失败:" << query.lastError().text();
            return std::nullopt;
        }
        if (!query.next()) {
            return std::nullopt;
        }

        const int tradingDays = query.value(0).toInt();
        m_tradeDayCountCache.insert(cacheKey, tradingDays);
        return tradingDays;
    }

    int m_minTradeDays;
    QString m_dbConnectionName;
    QHash<QString, int> m_tradeDayCountCache;
};

} // namespace factor::bridge
