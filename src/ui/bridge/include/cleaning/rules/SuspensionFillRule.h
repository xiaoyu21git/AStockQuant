#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <cmath>
#include <initializer_list>

namespace factor::bridge {

// 停牌填充 —— 成交量=0 视为停牌，向前填充价格
class SuspensionFillRule final : public ICleaningRule {
public:
    explicit SuspensionFillRule(int maxDays = 10,
                                QStringList fillFields = defaultFillFields(),
                                bool dropAfterMaxDays = true)
        : m_maxForwardFillDays(maxDays)
        , m_fillFields(std::move(fillFields))
        , m_dropAfterMaxDays(dropAfterMaxDays) {}

    QString id() const override { return cleaningRuleIdName(CleaningRuleId::SuspensionFill); }
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
        Accessors::IsSuspended.set(record, isSuspended);

        if (isSuspended) {
            const int consecutiveDays = ++m_consecutive[sym.value()];
            const bool hasPriorValues = hasAnyCachedValues(sym.value());
            const bool canForwardFill = hasPriorValues && consecutiveDays <= m_maxForwardFillDays;

            Accessors::ForwardFilled.set(record, canForwardFill);

            if (consecutiveDays > m_maxForwardFillDays && m_dropAfterMaxDays) {
                return false;
            }

            if (canForwardFill) {
                fillConfiguredFields(record, sym.value());
            }
            return true;
        }

        // 正常交易，记录最后有效值
        m_consecutive[sym.value()] = 0;
        Accessors::ForwardFilled.set(record, false);

        cacheConfiguredFields(record, sym.value());

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
    static QStringList defaultFillFields() {
        return {QStringLiteral("open"),
                QStringLiteral("high"),
                QStringLiteral("low"),
                QStringLiteral("close")};
    }

    bool shouldFillField(const QString& fieldName) const {
        return m_fillFields.contains(fieldName, Qt::CaseInsensitive);
    }

    bool hasAnyCachedValues(const QString& symbol) const {
        return (shouldFillField(QStringLiteral("open")) && m_lastOpen.contains(symbol))
            || (shouldFillField(QStringLiteral("high")) && m_lastHigh.contains(symbol))
            || (shouldFillField(QStringLiteral("low")) && m_lastLow.contains(symbol))
            || (shouldFillField(QStringLiteral("close")) && m_lastClose.contains(symbol));
    }

    void fillConfiguredFields(QVariantMap& record, const QString& symbol) const {
        if (shouldFillField(QStringLiteral("open")) && m_lastOpen.contains(symbol)) {
            Accessors::Open.set(record, m_lastOpen[symbol]);
        }
        if (shouldFillField(QStringLiteral("high")) && m_lastHigh.contains(symbol)) {
            Accessors::High.set(record, m_lastHigh[symbol]);
        }
        if (shouldFillField(QStringLiteral("low")) && m_lastLow.contains(symbol)) {
            Accessors::Low.set(record, m_lastLow[symbol]);
        }
        if (shouldFillField(QStringLiteral("close")) && m_lastClose.contains(symbol)) {
            Accessors::Close.set(record, m_lastClose[symbol]);
        }
    }

    void cacheConfiguredFields(const QVariantMap& record, const QString& symbol) {
        if (shouldFillField(QStringLiteral("open"))) {
            const auto value = Accessors::Open.get(record);
            if (value && std::isfinite(*value)) {
                m_lastOpen[symbol] = *value;
            }
        }
        if (shouldFillField(QStringLiteral("high"))) {
            const auto value = Accessors::High.get(record);
            if (value && std::isfinite(*value)) {
                m_lastHigh[symbol] = *value;
            }
        }
        if (shouldFillField(QStringLiteral("low"))) {
            const auto value = Accessors::Low.get(record);
            if (value && std::isfinite(*value)) {
                m_lastLow[symbol] = *value;
            }
        }
        if (shouldFillField(QStringLiteral("close"))) {
            const auto value = Accessors::Close.get(record);
            if (value && *value > 0.0 && std::isfinite(*value)) {
                m_lastClose[symbol] = *value;
            }
        }
    }

    int m_maxForwardFillDays;
    QStringList m_fillFields;
    bool m_dropAfterMaxDays = true;
    QHash<QString, int> m_consecutive;
    QHash<QString, double> m_lastOpen, m_lastHigh, m_lastLow, m_lastClose;
};

} // namespace factor::bridge
