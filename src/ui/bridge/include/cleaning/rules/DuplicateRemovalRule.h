#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"

namespace factor::bridge {

// 重复数据删除 —— 日线按 symbol + trade_date；财务记录额外纳入 report_type
class DuplicateRemovalRule final : public ICleaningRule {
public:
    QString id() const override { return "dedup"; }
    QString displayName() const override { return QStringLiteral("去重"); }
    int executionOrder() const override { return 10; }

    bool appliesTo(const QVariantMap&) const override { return true; }

    bool clean(QVariantMap& record) override {
        auto sym = Accessors::Symbol.get(record);
        if (!sym) return false;

        QString dateValue;
        if (auto tradeDate = Accessors::TradeDate.get(record)) {
            dateValue = *tradeDate;
        } else if (auto reportDate = Accessors::ReportDate.get(record)) {
            dateValue = *reportDate;
        }

        if (dateValue.isEmpty()) return false;

        QString key = *sym + "|" + dateValue;
        const QString reportType = record.value(QStringLiteral("report_type")).toString().trimmed();
        if (!reportType.isEmpty()) {
            key += QStringLiteral("|") + reportType;
        }

        if (m_seen.contains(key)) return false;

        m_seen.insert(key);
        return true;
    }

    void cleanCrossSectional(QVariantList&) override { m_seen.clear(); }

private:
    QSet<QString> m_seen;
};

} // namespace factor::bridge
