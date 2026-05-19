#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <QSet>
#include <cmath>

namespace factor::bridge {

// 缺失值向前填充 —— 用最后一次有效值填充当前缺失
class MissingValueFillRule final : public ICleaningRule {
public:
    explicit MissingValueFillRule(int maxLookbackDays = 5,
                                  QStringList fields = defaultFields())
        : m_maxLookbackDays(maxLookbackDays)
        , m_fields(normalizeFields(std::move(fields))) {}

    static QStringList defaultFields() {
        return {QStringLiteral("open"),
                QStringLiteral("high"),
                QStringLiteral("low"),
                QStringLiteral("close"),
                QStringLiteral("turnover_rate"),
                QStringLiteral("market_cap"),
                QStringLiteral("circulating_market_cap")};
    }

    static bool isSupportedFieldName(const QString& fieldName) {
        return supportedFields().contains(normalizedFieldName(fieldName));
    }

    QString id() const override { return "missingValueFill"; }
    QString displayName() const override { return QStringLiteral("缺失填充"); }
    int executionOrder() const override { return 40; }

    bool appliesTo(const QVariantMap& record) const override {
        return Accessors::Symbol.has(record);
    }

    bool clean(QVariantMap& record) override {
        auto sym = Accessors::Symbol.get(record);
        if (!sym) return true;

        auto& symbolState = m_state[sym.value()];
        bool updated = false;

        for (const QString& fieldName : m_fields) {
            auto& fieldState = symbolState[fieldName];
            double currentValue = 0.0;
            if (tryReadFiniteNumeric(record, fieldName, &currentValue)) {
                fieldState.lastValue = currentValue;
                fieldState.hasLastValue = true;
                fieldState.missingRows = 0;
                continue;
            }

            ++fieldState.missingRows;
            if (!fieldState.hasLastValue || fieldState.missingRows > m_maxLookbackDays) {
                continue;
            }

            record[fieldName] = fieldState.lastValue;
            updated = true;
        }

        Accessors::MissingValueFilled.set(record, updated);

        return true;
    }

    void cleanCrossSectional(QVariantList&) override {
        m_state.clear();
    }

private:
    struct FieldState {
        double lastValue{0.0};
        bool hasLastValue{false};
        int missingRows{0};
    };

    static QString normalizedFieldName(const QString& fieldName) {
        return fieldName.trimmed().toLower();
    }

    static QStringList normalizeFields(QStringList fields) {
        if (fields.isEmpty()) {
            fields = defaultFields();
        }

        QStringList normalized;
        QSet<QString> seen;
        normalized.reserve(fields.size());
        for (const QString& fieldName : fields) {
            const QString normalizedField = normalizedFieldName(fieldName);
            if (normalizedField.isEmpty() || seen.contains(normalizedField)) {
                continue;
            }
            seen.insert(normalizedField);
            normalized.push_back(normalizedField);
        }

        return normalized.isEmpty() ? defaultFields() : normalized;
    }

    static const QSet<QString>& supportedFields() {
        static const QSet<QString> fields = {
            QStringLiteral("open"),
            QStringLiteral("high"),
            QStringLiteral("low"),
            QStringLiteral("close"),
            QStringLiteral("turnover_rate"),
            QStringLiteral("market_cap"),
            QStringLiteral("circulating_market_cap")
        };
        return fields;
    }

    static bool tryReadFiniteNumeric(const QVariantMap& record,
                                     const QString& fieldName,
                                     double* outValue) {
        const auto it = record.constFind(fieldName);
        if (it == record.constEnd() || !it.value().isValid() || it.value().isNull()) {
            return false;
        }

        bool ok = false;
        const double value = it.value().toDouble(&ok);
        if (!ok || !std::isfinite(value)) {
            return false;
        }

        if (outValue) {
            *outValue = value;
        }
        return true;
    }

    int m_maxLookbackDays;
    QStringList m_fields;
    QHash<QString, QHash<QString, FieldState>> m_state;
};

} // namespace factor::bridge
