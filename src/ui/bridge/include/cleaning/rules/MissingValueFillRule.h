#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <cmath>

namespace factor::bridge {

// 缺失值向前填充 —— 用最后一次有效值填充当前缺失
class MissingValueFillRule final : public ICleaningRule {
public:
    QString id() const override { return "missing_fill"; }
    QString displayName() const override { return QStringLiteral("缺失填充"); }
    int executionOrder() const override { return 40; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::Symbol.has(record);
    }

    bool clean(QVariantMap& record) override {
        auto sym = Accessors::Symbol.get(record);
        if (!sym) return true;

        auto& last = m_state[sym.value()];
        bool updated = false;

        auto fill = [&](const FieldAccessor<double>& f, double& cache) {
            auto v = f.get(record);
            if (v && std::isfinite(*v)) {
                cache = *v;
            } else if (cache > 0.0) {
                f.set(record, cache);
                updated = true;
            }
        };

        fill(Accessors::Open,        last.open);
        fill(Accessors::High,        last.high);
        fill(Accessors::Low,         last.low);
        fill(Accessors::Close,       last.close);
        fill(Accessors::Volume,      last.volume);
        fill(Accessors::Turnover,    last.turnover);

        // 标记被填充
        if (updated)
            record[QStringLiteral("missing_filled")] = true;

        return true;
    }

    void cleanCrossSectional(QVariantList&) override {
        m_state.clear();
    }

private:
    struct SymbolState {
        double open{0.0}, high{0.0}, low{0.0}, close{0.0};
        double volume{0.0}, turnover{0.0};
    };
    QHash<QString, SymbolState> m_state;
};

} // namespace factor::bridge
