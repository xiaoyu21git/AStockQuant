#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <cmath>

namespace factor::bridge {

// 成交量过滤 —— 负数或超大量剔除
class VolumeFilterRule final : public ICleaningRule {
public:
    explicit VolumeFilterRule(double maxVol = 1e9) : m_maxVol(maxVol) {}

    QString id() const override { return "volume_filter"; }
    QString displayName() const override { return QStringLiteral("成交量过滤"); }
    int executionOrder() const override { return 25; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::Volume.has(record);
    }

    bool clean(QVariantMap& record) override {
        auto vol = Accessors::Volume.get(record);
        if (!vol) return true;
        return *vol >= 0.0 && *vol <= m_maxVol;
    }

private:
    double m_maxVol;
};

} // namespace factor::bridge
