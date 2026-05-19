#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"

namespace factor::bridge {

// 财报对齐 —— 使用公布日或报告期末作为交易日。
// 这条规则必须早于依赖 trade_date 的去重/过滤规则执行。
class ReportDateAlignmentRule final : public ICleaningRule {
public:
    QString id() const override { return "report_alignment"; }
    QString displayName() const override { return QStringLiteral("财报对齐"); }
    int executionOrder() const override { return 7; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::ReportDate.has(record);
    }

    bool clean(QVariantMap& record) override {
        auto rDate = Accessors::ReportDate.get(record);
        if (!rDate) return true;

        QDate rd = QDate::fromString(rDate->left(10), "yyyy-MM-dd");
        if (!rd.isValid()) return true;

        auto dDate = Accessors::DisclosureDate.get(record);
        if (dDate) {
            QDate dd = QDate::fromString(dDate->left(10), "yyyy-MM-dd");
            if (dd.isValid()) {
                Accessors::TradeDate.set(record, dd.toString("yyyy-MM-dd"));
                return true;
            }
        }

        Accessors::TradeDate.set(record, rd.toString("yyyy-MM-dd"));
        return true;
    }
};

} // namespace factor::bridge
