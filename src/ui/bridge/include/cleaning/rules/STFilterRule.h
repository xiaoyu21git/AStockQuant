#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"

namespace factor::bridge {

// ST 股票剔除
class STFilterRule final : public ICleaningRule {
public:
    QString id() const override { return cleaningRuleIdName(CleaningRuleId::STFilter); }
    QString displayName() const override { return QStringLiteral("ST剔除"); }
    int executionOrder() const override { return 50; }

    bool appliesTo(const QVariantMap&) const override { return true; }

    bool clean(QVariantMap& record) override {
        // 检查 is_st 标记
        auto isStVal = record.value(QStringLiteral("is_st"));
        if (isStVal.isValid()) {
            if (isStVal.toBool()) return false;
            QString stText = isStVal.toString().trimmed().toLower();
            if (stText == "1" || stText == "true" || stText == "y") return false;
        }

        const QString statusVal = Accessors::StatusVal.get(record).value_or(QString()).trimmed().toUpper();
        if (statusVal == QStringLiteral("ST") || statusVal == QStringLiteral("*ST")) {
            return false;
        }

        // 检查股票名称
        auto nameVal = record.value(QStringLiteral("name")).toString().trimmed().toUpper();
        if (nameVal.startsWith("ST") || nameVal.startsWith("*ST"))
            return false;

        return true;
    }
};

} // namespace factor::bridge
