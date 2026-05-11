#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <cmath>

namespace factor::bridge {

// 涨跌停标记 —— 标记涨跌停状态，不剔除
class LimitMoveTagRule final : public ICleaningRule {
public:
    QString id() const override { return "limit_tag"; }
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

        constexpr double upThresh = 9.5;
        constexpr double downThresh = -9.5;

        record[QStringLiteral("limit_up")]   = (pct >= upThresh);
        record[QStringLiteral("limit_down")] = (pct <= downThresh);
        record[QStringLiteral("can_buy")]    = (pct < upThresh);
        record[QStringLiteral("can_sell")]   = (pct > downThresh);

        return true;
    }
};

} // namespace factor::bridge
