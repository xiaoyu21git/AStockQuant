#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <cmath>

namespace factor::bridge {

// 复权处理 —— 用 adj_factor 对价格做后复权
class AdjustedPriceRule final : public ICleaningRule {
public:
    QString id() const override { return "adjusted_price"; }
    QString displayName() const override { return QStringLiteral("复权"); }
    int executionOrder() const override { return 30; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::AdjFactor.has(record) && Accessors::Close.has(record);
    }

    bool clean(QVariantMap& record) override {
        auto adj = Accessors::AdjFactor.get(record);
        if (!adj || *adj <= 0.0 || !std::isfinite(*adj)) return true;

        auto apply = [&](const FieldAccessor<double>& f) {
            auto v = f.get(record);
            if (v) f.set(record, *v * *adj);
        };

        apply(Accessors::Open);
        apply(Accessors::High);
        apply(Accessors::Low);
        apply(Accessors::Close);
        apply(Accessors::PreClose);

        return true;
    }
};

} // namespace factor::bridge
