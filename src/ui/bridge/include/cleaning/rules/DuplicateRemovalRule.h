#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"

namespace factor::bridge {

// 重复数据删除 —— symbol + trade_date 唯一
class DuplicateRemovalRule final : public ICleaningRule {
public:
    QString id() const override { return "dedup"; }
    QString displayName() const override { return QStringLiteral("去重"); }
    int executionOrder() const override { return 10; }

    bool appliesTo(const QVariantMap&) const override { return true; }

    bool clean(QVariantMap& record) override {
        auto sym = Accessors::Symbol.get(record);
        if (!sym) return false;

        auto date = Accessors::TradeDate.get(record);
        if (!date) return false;

        QString key = *sym + "|" + *date;
        if (m_seen.contains(key)) return false;

        m_seen.insert(key);
        return true;
    }

    void cleanCrossSectional(QVariantList&) override { m_seen.clear(); }

private:
    QSet<QString> m_seen;
};

} // namespace factor::bridge
