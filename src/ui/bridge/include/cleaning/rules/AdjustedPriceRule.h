#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <cmath>
#include <optional>

namespace factor::bridge {

// 复权处理 —— 用 post_adjust_factor 对价格做后复权
class AdjustedPriceRule final : public ICleaningRule {
public:
    explicit AdjustedPriceRule(bool preferAdjustedFields = true,
                               bool applyFactorFallback = true)
        : m_preferAdjustedFields(preferAdjustedFields)
        , m_applyFactorFallback(applyFactorFallback) {}

    QString id() const override { return "adjustedPrice"; }
    QString displayName() const override { return QStringLiteral("复权"); }
    int executionOrder() const override { return 30; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::Close.has(record);
    }

    bool clean(QVariantMap& record) override {
        if (!m_preferAdjustedFields) {
            return true;
        }

        const auto adj = resolveAdjustmentFactor(record);
        if (!adj) {
            return true;
        }

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

private:
    std::optional<double> resolveAdjustmentFactor(const QVariantMap& record) const {
        const auto postAdj = Accessors::PostAdjFactor.get(record);
        if (isValidFactor(postAdj)) {
            return postAdj;
        }

        if (!m_applyFactorFallback) {
            return std::nullopt;
        }

        const auto preAdj = Accessors::PreAdjFactor.get(record);
        if (isValidFactor(preAdj)) {
            return preAdj;
        }

        return std::nullopt;
    }

    static bool isValidFactor(const std::optional<double>& factor) {
        return factor && *factor > 0.0 && std::isfinite(*factor);
    }

    bool m_preferAdjustedFields;
    bool m_applyFactorFallback;
};

} // namespace factor::bridge
