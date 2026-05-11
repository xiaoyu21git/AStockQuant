#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <cmath>

namespace factor::bridge {

// 停牌填充 —— 成交量=0 视为停牌，向前填充价格
class SuspensionFillRule final : public ICleaningRule {
public:
    explicit SuspensionFillRule(int maxDays = 10) : m_maxForwardFillDays(maxDays) {}

    QString id() const override { return "suspension_fill"; }
    QString displayName() const override { return QStringLiteral("停牌填充"); }
    int executionOrder() const override { return 35; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::Volume.has(record);
    }

    bool clean(QVariantMap& record) override {
        auto sym = Accessors::Symbol.get(record);
        if (!sym) return true;

        auto vol = Accessors::Volume.get(record);
        bool isSuspended = !vol || *vol <= 0.0;

        if (isSuspended) {
            m_consecutive[sym.value()]++;

            if (m_consecutive[sym.value()] > m_maxForwardFillDays)
                return false;

            // 有历史值则填充
            if (m_lastClose.contains(sym.value())) {
                Accessors::Open.set(record, m_lastOpen[sym.value()]);
                Accessors::High.set(record, m_lastHigh[sym.value()]);
                Accessors::Low.set(record, m_lastLow[sym.value()]);
                Accessors::Close.set(record, m_lastClose[sym.value()]);
            }
            return true;
        }

        // 正常交易，记录最后有效值
        m_consecutive[sym.value()] = 0;

        auto oc = Accessors::Close.get(record);
        if (oc && *oc > 0.0) {
            auto oo = Accessors::Open.get(record);
            auto oh = Accessors::High.get(record);
            auto ol = Accessors::Low.get(record);
            if (oo) m_lastOpen[sym.value()] = *oo;
            if (oh) m_lastHigh[sym.value()] = *oh;
            if (ol) m_lastLow[sym.value()] = *ol;
            m_lastClose[sym.value()] = *oc;
        }

        return true;
    }

    void cleanCrossSectional(QVariantList&) override {
        m_consecutive.clear();
        m_lastOpen.clear();
        m_lastHigh.clear();
        m_lastLow.clear();
        m_lastClose.clear();
    }

private:
    int m_maxForwardFillDays;
    QHash<QString, int> m_consecutive;
    QHash<QString, double> m_lastOpen, m_lastHigh, m_lastLow, m_lastClose;
};

} // namespace factor::bridge
