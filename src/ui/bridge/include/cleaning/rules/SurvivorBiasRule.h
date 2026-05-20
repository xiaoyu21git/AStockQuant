#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"

#include <QDate>

namespace factor::bridge {

// 生存者偏差处理 —— 交易日在退市日之后则剔除
class SurvivorBiasRule final : public ICleaningRule {
public:
    QString id() const override { return cleaningRuleIdName(CleaningRuleId::SurvivorBias); }
    QString displayName() const override { return QStringLiteral("生存者偏差"); }
    int executionOrder() const override { return 55; }

    bool appliesTo(const QVariantMap&) const override { return true; }

    bool clean(QVariantMap& record) override {
        const auto delistDateStr = Accessors::DelistDate.get(record);
        if (!delistDateStr.has_value()) return true;

        const QDate delistDate = QDate::fromString(delistDateStr->left(10), "yyyy-MM-dd");
        if (!delistDate.isValid()) return true;

        const auto effectiveDateStr = resolveEffectiveRecordDate(record);
        if (!effectiveDateStr.has_value()) return true;

        const QDate effectiveDate = QDate::fromString(effectiveDateStr->left(10), "yyyy-MM-dd");
        if (!effectiveDate.isValid()) return true;

        return effectiveDate <= delistDate;
    }

private:
    static std::optional<QString> resolveEffectiveRecordDate(const QVariantMap& record)
    {
        if (const auto tradeDate = Accessors::TradeDate.get(record); tradeDate.has_value()) {
            return tradeDate;
        }
        if (const auto disclosureDate = Accessors::DisclosureDate.get(record); disclosureDate.has_value()) {
            return disclosureDate;
        }
        if (const auto reportDate = Accessors::ReportDate.get(record); reportDate.has_value()) {
            return reportDate;
        }
        return std::nullopt;
    }
};

} // namespace factor::bridge
