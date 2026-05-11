#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"

namespace factor::bridge {

// 完整性校验 —— symbol 和 trade_date 必须存在
class CompletenessRule final : public ICleaningRule {
public:
    QString id() const override { return "completeness"; }
    QString displayName() const override { return QStringLiteral("完整性校验"); }
    int executionOrder() const override { return 5; }

    bool appliesTo(const QVariantMap&) const override { return true; }

    bool clean(QVariantMap& record) override {
        if (!Accessors::Symbol.has(record)) return false;
        if (!Accessors::TradeDate.has(record)) return false;
        return true;
    }
};

} // namespace factor::bridge
