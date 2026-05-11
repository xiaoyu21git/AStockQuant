#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <cmath>

namespace factor::bridge {

// 价格有效性 —— 正数且 high >= low
class PriceValidityRule final : public ICleaningRule {
public:
    QString id() const override { return "price_validity"; }
    QString displayName() const override { return QStringLiteral("价格校验"); }
    int executionOrder() const override { return 20; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::Open.has(record) || Accessors::Close.has(record);
    }

    bool clean(QVariantMap& record) override {
        auto check = [](std::optional<double> v) {
            return v.has_value() && std::isfinite(*v) && *v > 0.0;
        };

        if (!check(Accessors::Open.get(record))) return false;
        if (!check(Accessors::High.get(record))) return false;
        if (!check(Accessors::Low.get(record))) return false;
        if (!check(Accessors::Close.get(record))) return false;

        auto h = Accessors::High.get(record);
        auto l = Accessors::Low.get(record);
        if (h && l && *h < *l) return false;

        return true;
    }
};

} // namespace factor::bridge
