#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"

#include <QDate>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace factor::bridge {

// 财报对齐 —— 使用公告后的首个交易日作为生效日；缺少公告日时保留 report_date 回退。
// 这条规则必须早于依赖 trade_date 的去重/过滤规则执行。
class ReportDateAlignmentRule final : public ICleaningRule {
public:
    void setDatabaseConnectionName(const QString& connName) {
        m_dbConnectionName = connName;
    }

    QString id() const override { return cleaningRuleIdName(CleaningRuleId::ReportAlignment); }
    QString displayName() const override { return QStringLiteral("财报对齐"); }
    int executionOrder() const override { return 7; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::ReportDate.has(record);
    }

    bool clean(QVariantMap& record) override {
        auto rDate = Accessors::ReportDate.get(record);
        if (!rDate) return true;

        const QDate rd = QDate::fromString(rDate->left(10), "yyyy-MM-dd");
        if (!rd.isValid()) return true;

        const auto dDate = Accessors::DisclosureDate.get(record);
        if (dDate) {
            const QDate dd = QDate::fromString(dDate->left(10), "yyyy-MM-dd");
            if (dd.isValid()) {
                const QDate effectiveDate = nextTradingDay(dd);
                if (effectiveDate.isValid()) {
                    Accessors::TradeDate.set(record, effectiveDate.toString("yyyy-MM-dd"));
                } else {
                    Accessors::TradeDate.set(record, dd.addDays(1).toString("yyyy-MM-dd"));
                }
                return true;
            }
        }

        Accessors::TradeDate.set(record, rd.toString("yyyy-MM-dd"));
        return true;
    }

private:
    QDate nextTradingDay(const QDate& disclosureDate) const
    {
        const QString connName = m_dbConnectionName.trimmed().isEmpty()
            ? QStringLiteral("qt_sql_default_connection")
            : m_dbConnectionName.trimmed();
        if (!QSqlDatabase::contains(connName)) {
            return {};
        }

        QSqlDatabase db = QSqlDatabase::database(connName);
        if (!db.isOpen()) {
            return {};
        }

        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT MIN(trade_date) "
            "FROM daily_bar "
            "WHERE trade_date > :disclosure_date"));
        query.bindValue(QStringLiteral(":disclosure_date"), disclosureDate.toString(QStringLiteral("yyyy-MM-dd")));
        if (!query.exec() || !query.next()) {
            return {};
        }

        const QString nextDateText = query.value(0).toString().trimmed();
        if (nextDateText.isEmpty()) {
            return {};
        }
        return QDate::fromString(nextDateText.left(10), QStringLiteral("yyyy-MM-dd"));
    }

    QString m_dbConnectionName;
};

} // namespace factor::bridge
