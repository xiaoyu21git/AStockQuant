#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"

namespace factor::bridge {

// 生存者偏差处理 —— 交易日在退市日之后则剔除
class SurvivorBiasRule final : public ICleaningRule {
public:
    QString id() const override { return "survivor_bias"; }
    QString displayName() const override { return QStringLiteral("生存者偏差"); }
    int executionOrder() const override { return 55; }

    bool appliesTo(const QVariantMap&) const override { return true; }

    bool clean(QVariantMap& record) override {
        auto delistDateStr = record.value(QStringLiteral("delist_date")).toString();
        if (delistDateStr.isEmpty()) return true;

        QDate delistDate = QDate::fromString(delistDateStr.left(10), "yyyy-MM-dd");
        if (!delistDate.isValid()) return true;

        auto tradeDateStr = Accessors::TradeDate.get(record);
        if (!tradeDateStr) return true;

        QDate tradeDate = QDate::fromString(tradeDateStr->left(10), "yyyy-MM-dd");
        if (!tradeDate.isValid()) return true;

        return tradeDate <= delistDate;
    }
};

} // namespace factor::bridge
