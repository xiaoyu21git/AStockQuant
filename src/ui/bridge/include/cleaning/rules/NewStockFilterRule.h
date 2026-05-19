#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"

namespace factor::bridge {

// 新股过滤 —— 上市未满 N 个交易日剔除
class NewStockFilterRule final : public ICleaningRule {
public:
    explicit NewStockFilterRule(int minDays = 60) : m_minTradeDays(minDays) {}

    QString id() const override { return "new_stock_filter"; }
    QString displayName() const override { return QStringLiteral("新股过滤"); }
    int executionOrder() const override { return 45; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::Close.has(record) || Accessors::Volume.has(record);
    }

    bool clean(QVariantMap& record) override {
        auto sym = Accessors::Symbol.get(record);
        if (!sym) return true;

        // 检查上市日期
        auto listDateStr = record.value(QStringLiteral("list_date")).toString();
        if (!listDateStr.isEmpty()) {
            QDate listDate = QDate::fromString(listDateStr.left(10), "yyyy-MM-dd");
            auto tradeDateStr = Accessors::TradeDate.get(record);
            if (listDate.isValid() && tradeDateStr) {
                QDate tradeDate = QDate::fromString(tradeDateStr->left(10), "yyyy-MM-dd");
                if (tradeDate.isValid() && tradeDate < listDate)
                    return false; // 交易日在上市前
            }
        }

        // 统计该symbol出现的次数
        m_daysSeen[sym.value()]++;
        return m_daysSeen[sym.value()] > m_minTradeDays;
    }

    void cleanCrossSectional(QVariantList&) override {
        m_daysSeen.clear();
    }

private:
    int m_minTradeDays;
    QHash<QString, int> m_daysSeen;
};

} // namespace factor::bridge
