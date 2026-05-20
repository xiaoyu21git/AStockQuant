#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <cmath>
#include <optional>

namespace factor::bridge {

// 价格有效性 —— 正数且 high >= low
class PriceValidityRule final : public ICleaningRule {
public:
    explicit PriceValidityRule(double minPrice = 0.01,
                               double maxPrice = 10000.0,
                               bool enforceChain = true,
                               bool allowZeroWhenSuspended = true)
        : m_minPrice(minPrice)
        , m_maxPrice(maxPrice)
        , m_enforceChain(enforceChain)
        , m_allowZeroWhenSuspended(allowZeroWhenSuspended) {}

    QString id() const override { return cleaningRuleIdName(CleaningRuleId::PriceValidity); }
    QString displayName() const override { return QStringLiteral("价格校验"); }
    int executionOrder() const override { return 20; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::Open.has(record) || Accessors::Close.has(record);
    }

    bool clean(QVariantMap& record) override {
        const bool suspended = isSuspended(record);

        const auto open = readPrice(record, Accessors::Open);
        const auto high = readPrice(record, Accessors::High);
        const auto low = readPrice(record, Accessors::Low);
        const auto close = readPrice(record, Accessors::Close);

        if (!open.valid || !high.valid || !low.valid || !close.valid) {
            return false;
        }

        if (!isAcceptableValue(open.value, suspended)) return false;
        if (!isAcceptableValue(high.value, suspended)) return false;
        if (!isAcceptableValue(low.value, suspended)) return false;
        if (!isAcceptableValue(close.value, suspended)) return false;

        if (!m_enforceChain) {
            return true;
        }

        if (containsSuspensionPlaceholder(open.value, suspended)
            || containsSuspensionPlaceholder(high.value, suspended)
            || containsSuspensionPlaceholder(low.value, suspended)
            || containsSuspensionPlaceholder(close.value, suspended)) {
            return true;
        }

        if (high.value && low.value && *high.value < *low.value) {
            return false;
        }
        if (open.value && high.value && *open.value > *high.value) {
            return false;
        }
        if (open.value && low.value && *open.value < *low.value) {
            return false;
        }
        if (close.value && high.value && *close.value > *high.value) {
            return false;
        }
        if (close.value && low.value && *close.value < *low.value) {
            return false;
        }

        return true;
    }

private:
    struct PriceReadResult {
        bool valid{true};
        std::optional<double> value;
    };

    template <typename Accessor>
    static PriceReadResult readPrice(const QVariantMap& record, const Accessor& accessor) {
        if (!record.contains(accessor.name)) {
            return {};
        }

        const QVariant rawValue = record.value(accessor.name);
        if (!rawValue.isValid() || rawValue.isNull()) {
            return {};
        }

        bool ok = false;
        const double value = rawValue.toDouble(&ok);
        if (!ok || !std::isfinite(value)) {
            return {false, std::nullopt};
        }

        return {true, value};
    }

    bool isAcceptableValue(const std::optional<double>& value, bool suspended) const {
        if (!value) {
            return true;
        }
        if (containsSuspensionPlaceholder(value, suspended)) {
            return true;
        }
        return *value >= m_minPrice && *value <= m_maxPrice;
    }

    bool containsSuspensionPlaceholder(const std::optional<double>& value, bool suspended) const {
        return m_allowZeroWhenSuspended && suspended && value && *value == 0.0;
    }

    static bool isSuspended(const QVariantMap& record) {
        const auto suspended = Accessors::IsSuspended.get(record);
        if (suspended && *suspended) {
            return true;
        }

        const auto volume = Accessors::Volume.get(record);
        return volume && *volume <= 0.0;
    }

    double m_minPrice;
    double m_maxPrice;
    bool m_enforceChain;
    bool m_allowZeroWhenSuspended;
};

} // namespace factor::bridge
