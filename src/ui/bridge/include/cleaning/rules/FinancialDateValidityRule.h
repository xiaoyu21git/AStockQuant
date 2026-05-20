#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"

#include <QDate>

namespace factor::bridge {

// 财务日期有效性 —— report_date 必须有效，披露日不能早于报告日
class FinancialDateValidityRule final : public ICleaningRule {
public:
    QString id() const override { return cleaningRuleIdName(CleaningRuleId::FinancialDateValidity); }
    QString displayName() const override { return QStringLiteral("财务日期有效性"); }
    int executionOrder() const override { return 6; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::ReportDate.has(record)
            || record.contains(QStringLiteral("disclosure_date"))
            || record.contains(QStringLiteral("report_type"));
    }

    bool clean(QVariantMap& record) override {
        const QString reportDateText = record.value(QStringLiteral("report_date")).toString().trimmed();
        if (reportDateText.isEmpty()) {
            return false;
        }

        const QDate reportDate = QDate::fromString(reportDateText.left(10), QStringLiteral("yyyy-MM-dd"));
        if (!reportDate.isValid()) {
            return false;
        }
        record[QStringLiteral("report_date")] = reportDate.toString(QStringLiteral("yyyy-MM-dd"));

        std::optional<QDate> validDisclosureDate;

        const QString disclosureDateText = record.value(QStringLiteral("disclosure_date")).toString().trimmed();
        if (!disclosureDateText.isEmpty()) {
            const QDate disclosureDate = QDate::fromString(disclosureDateText.left(10), QStringLiteral("yyyy-MM-dd"));
            if (!disclosureDate.isValid() || disclosureDate < reportDate) {
                record.remove(QStringLiteral("disclosure_date"));
            } else {
                record[QStringLiteral("disclosure_date")] = disclosureDate.toString(QStringLiteral("yyyy-MM-dd"));
                validDisclosureDate = disclosureDate;
            }
        }

        const QString tradeDateText = record.value(QStringLiteral("trade_date")).toString().trimmed();
        if (!tradeDateText.isEmpty()) {
            const QDate tradeDate = QDate::fromString(tradeDateText.left(10), QStringLiteral("yyyy-MM-dd"));
            if (!tradeDate.isValid()
                || tradeDate < reportDate
                || (validDisclosureDate.has_value() && tradeDate < *validDisclosureDate)) {
                record.remove(QStringLiteral("trade_date"));
            } else {
                record[QStringLiteral("trade_date")] = tradeDate.toString(QStringLiteral("yyyy-MM-dd"));
            }
        }

        const QString reportType = record.value(QStringLiteral("report_type")).toString().trimmed();
        if (reportType.isEmpty()) {
            record.remove(QStringLiteral("report_type"));
        } else {
            record[QStringLiteral("report_type")] = reportType;
        }

        return true;
    }
};

} // namespace factor::bridge