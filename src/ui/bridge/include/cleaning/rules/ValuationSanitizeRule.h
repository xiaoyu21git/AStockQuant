#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <cmath>

namespace factor::bridge {

// 估值字段净化 —— 清理 PE/PB/市值异常值
class ValuationSanitizeRule final : public ICleaningRule {
public:
    QString id() const override { return cleaningRuleIdName(CleaningRuleId::ValuationSanitize); }
    QString displayName() const override { return QStringLiteral("估值净化"); }
    int executionOrder() const override { return 15; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::PETTM.has(record) || Accessors::PBLF.has(record)
            || Accessors::TotalMarketCap.has(record)
            || record.contains(QStringLiteral("market_cap"))
            || Accessors::CirculatingMarketCap.has(record);
    }

    bool clean(QVariantMap& record) override {
        auto pe = Accessors::PETTM.get(record);
        if (pe && (!std::isfinite(*pe) || std::abs(*pe) <= 1e-8))
            Accessors::PETTM.clear(record);

        auto pb = Accessors::PBLF.get(record);
        if (pb && (!std::isfinite(*pb) || std::abs(*pb) <= 1e-8))
            Accessors::PBLF.clear(record);

        auto mcap = Accessors::TotalMarketCap.get(record);
        if (mcap && (!std::isfinite(*mcap) || *mcap <= 0.0))
            Accessors::TotalMarketCap.clear(record);

        const QVariant marketCapValue = record.value(QStringLiteral("market_cap"));
        if (marketCapValue.isValid() && !marketCapValue.isNull()) {
            bool ok = false;
            const double marketCap = marketCapValue.toDouble(&ok);
            if (!ok || !std::isfinite(marketCap) || marketCap <= 0.0) {
                record.remove(QStringLiteral("market_cap"));
            }
        }

        auto cmcap = Accessors::CirculatingMarketCap.get(record);
        if (cmcap && (!std::isfinite(*cmcap) || *cmcap <= 0.0))
            Accessors::CirculatingMarketCap.clear(record);

        // 总市值 < 流通市值 矛盾
        mcap = Accessors::TotalMarketCap.get(record);
        cmcap = Accessors::CirculatingMarketCap.get(record);
        if (mcap && cmcap && *mcap < *cmcap)
            Accessors::TotalMarketCap.clear(record);

        const QVariant normalizedMarketCapValue = record.value(QStringLiteral("market_cap"));
        if (normalizedMarketCapValue.isValid() && !normalizedMarketCapValue.isNull()) {
            bool marketCapOk = false;
            const double normalizedMarketCap = normalizedMarketCapValue.toDouble(&marketCapOk);
            cmcap = Accessors::CirculatingMarketCap.get(record);
            if (marketCapOk && cmcap && normalizedMarketCap < *cmcap) {
                record.remove(QStringLiteral("market_cap"));
            }
        }

        record.insert(QString::fromUtf8(QualityFields::VALUATION_SANITIZED), true);

        return true;
    }
};

} // namespace factor::bridge
