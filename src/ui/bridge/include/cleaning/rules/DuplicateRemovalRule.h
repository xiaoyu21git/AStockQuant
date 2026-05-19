#pragma once

#include "../CleaningEngine.h"
#include "field_traits.h"
#include <QSet>

namespace factor::bridge {

// 重复数据删除 —— 仅对完整去重键的记录执行去重。
// 缺少去重键的记录保留，让后续规则继续补齐或处理。
class DuplicateRemovalRule final : public ICleaningRule {
public:
    explicit DuplicateRemovalRule(QStringList keyFields = defaultKeyFields())
        : m_keyFields(normalizeKeyFields(std::move(keyFields))) {}

    static QStringList defaultKeyFields() {
        return {QStringLiteral("symbol"), QStringLiteral("trade_date")};
    }

    static bool isSupportedKeyFieldName(const QString& fieldName) {
        return supportedKeyFields().contains(normalizedFieldName(fieldName));
    }

    QString id() const override { return "duplicateRemoval"; }
    QString displayName() const override { return QStringLiteral("去重"); }
    int executionOrder() const override { return 10; }

    bool appliesTo(const QVariantMap&) const override { return true; }

    bool clean(QVariantMap& record) override {
        QStringList keyParts;
        keyParts.reserve(m_keyFields.size());
        for (const QString& fieldName : m_keyFields) {
            const auto it = record.constFind(fieldName);
            if (it == record.constEnd() || !it.value().isValid() || it.value().isNull()) {
                return true;
            }

            const QString value = it.value().toString().trimmed();
            if (value.isEmpty()) {
                return true;
            }
            keyParts.push_back(value);
        }

        const QString key = keyParts.join(QStringLiteral("|"));

        if (m_seen.contains(key)) return false;

        m_seen.insert(key);
        return true;
    }

    void cleanCrossSectional(QVariantList&) override { m_seen.clear(); }

private:
    static QString normalizedFieldName(const QString& fieldName) {
        return fieldName.trimmed().toLower();
    }

    static QStringList normalizeKeyFields(QStringList keyFields) {
        if (keyFields.isEmpty()) {
            keyFields = defaultKeyFields();
        }

        QStringList normalized;
        QSet<QString> seen;
        normalized.reserve(keyFields.size());
        for (const QString& fieldName : keyFields) {
            const QString normalizedField = normalizedFieldName(fieldName);
            if (normalizedField.isEmpty() || seen.contains(normalizedField)) {
                continue;
            }
            seen.insert(normalizedField);
            normalized.push_back(normalizedField);
        }

        return normalized.isEmpty() ? defaultKeyFields() : normalized;
    }

    static const QSet<QString>& supportedKeyFields() {
        static const QSet<QString> fields = {
            QStringLiteral("symbol"),
            QStringLiteral("trade_date"),
            QStringLiteral("report_date"),
            QStringLiteral("disclosure_date"),
            QStringLiteral("report_type")
        };
        return fields;
    }

    QStringList m_keyFields;
    QSet<QString> m_seen;
};

} // namespace factor::bridge
