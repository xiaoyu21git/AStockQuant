#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <cmath>

namespace factor::bridge {

// 涨跌停标记 —— 标记涨跌停状态，不剔除
class LimitMoveTagRule final : public ICleaningRule {
public:
    explicit LimitMoveTagRule(double upThreshold = 9.5, double downThreshold = -9.5)
        : m_upThreshold(upThreshold), m_downThreshold(downThreshold) {}

    QString id() const override { return cleaningRuleIdName(CleaningRuleId::LimitTag); }
    QString displayName() const override { return QStringLiteral("涨跌停标记"); }
    int executionOrder() const override { return 100; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::Close.has(record);
    }

    bool clean(QVariantMap& record) override {
        double pct = 0.0;

        auto cp = Accessors::ChangePct.get(record);
        if (cp) {
            pct = *cp;
        } else {
            auto close = Accessors::Close.get(record);
            auto pre = Accessors::PreClose.get(record);
            if (close && pre && *pre > 0.0)
                pct = (*close - *pre) / *pre * 100.0;
        }

        record[QStringLiteral("limit_up")]   = (pct >= m_upThreshold);
        record[QStringLiteral("limit_down")] = (pct <= m_downThreshold);
        record[QStringLiteral("can_buy")]    = (pct < m_upThreshold);
        record[QStringLiteral("can_sell")]   = (pct > m_downThreshold);

        return true;
    }

private:
    double m_upThreshold;
    double m_downThreshold;
};

} // namespace factor::bridge
