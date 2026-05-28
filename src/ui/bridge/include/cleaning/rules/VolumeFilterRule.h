#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <cmath>

namespace factor::bridge {

// 成交量过滤 —— 负数或超大量剔除
class VolumeFilterRule final : public ICleaningRule {
public:
    explicit VolumeFilterRule(double minVolume = 0.0,
                              double maxVolume = 1e9,
                              bool allowZeroWhenSuspended = true)
        : m_minVolume(minVolume)
        , m_maxVolume(maxVolume)
        , m_allowZeroWhenSuspended(allowZeroWhenSuspended) {}

    QString id() const override { return cleaningRuleIdName(CleaningRuleId::VolumeFilter); }
    QString displayName() const override { return QStringLiteral("成交量过滤"); }
    int executionOrder() const override { return 36; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::Volume.has(record);
    }

    bool clean(QVariantMap& record) override {
        const auto volume = Accessors::Volume.get(record);
        if (!volume) return true;
        if (!std::isfinite(*volume)) return false;
        if (m_allowZeroWhenSuspended && isSuspended(record) && *volume == 0.0) {
            return true;
        }
        return *volume >= m_minVolume && *volume <= m_maxVolume;
    }

private:
    static bool isSuspended(const QVariantMap& record) {
        const auto suspended = Accessors::IsSuspended.get(record);
        if (suspended && *suspended) {
            return true;
        }

        const auto volume = Accessors::Volume.get(record);
        return volume && *volume <= 0.0;
    }

    double m_minVolume;
    double m_maxVolume;
    bool m_allowZeroWhenSuspended;
};

} // namespace factor::bridge
