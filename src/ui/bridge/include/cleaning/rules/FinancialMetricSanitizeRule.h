#pragma once

#include "../CleaningEngine.h"

#include <cmath>
#include <optional>

namespace factor::bridge {

// 财务指标净化 —— 清理非有限值，并收紧少量确定性的财务关系
class FinancialMetricSanitizeRule final : public ICleaningRule {
public:
    QString id() const override { return "financial_metric_sanitize"; }
    QString displayName() const override { return QStringLiteral("财务指标净化"); }
    int executionOrder() const override { return 16; }

    bool appliesTo(const QVariantMap& record) const override {
        return record.contains(QStringLiteral("report_date"))
            || record.contains(QStringLiteral("eps"))
            || record.contains(QStringLiteral("roe"))
            || record.contains(QStringLiteral("gross_margin"));
    }

    bool clean(QVariantMap& record) override {
        sanitizeFinite(record, QStringLiteral("eps"));
        sanitizeFinite(record, QStringLiteral("bps"));
        sanitizeFinite(record, QStringLiteral("roe"));
        sanitizeFinite(record, QStringLiteral("roa"));
        sanitizeFinite(record, QStringLiteral("profit_margin"));
        sanitizeFinite(record, QStringLiteral("gross_margin"));
        sanitizeFinite(record, QStringLiteral("operating_margin"));
        sanitizeFinite(record, QStringLiteral("net_margin"));
        sanitizeFinite(record, QStringLiteral("net_profit"));
        sanitizeFinite(record, QStringLiteral("total_revenue"));
        sanitizeFinite(record, QStringLiteral("equity"));
        sanitizeFinite(record, QStringLiteral("debt_to_equity"));
        sanitizeFinite(record, QStringLiteral("operating_cash_flow"));
        sanitizeFinite(record, QStringLiteral("investing_cash_flow"));
        sanitizeFinite(record, QStringLiteral("financing_cash_flow"));
        sanitizeFinite(record, QStringLiteral("payout_ratio"));
        sanitizeFinite(record, QStringLiteral("dividend_yield"));

        sanitizePositive(record, QStringLiteral("total_assets"));
        sanitizeNonNegative(record, QStringLiteral("total_liabilities"));
        sanitizePositive(record, QStringLiteral("current_ratio"));
        sanitizePositive(record, QStringLiteral("quick_ratio"));

        const auto currentRatio = parseFinite(record, QStringLiteral("current_ratio"));
        const auto quickRatio = parseFinite(record, QStringLiteral("quick_ratio"));
        if (currentRatio.has_value() && quickRatio.has_value() && *quickRatio > *currentRatio) {
            record.remove(QStringLiteral("quick_ratio"));
        }

        return true;
    }

private:
    static std::optional<double> parseFinite(const QVariantMap& record, const QString& field)
    {
        const QVariant value = record.value(field);
        if (!value.isValid() || value.isNull()) {
            return std::nullopt;
        }

        bool ok = false;
        const double numericValue = value.toDouble(&ok);
        if (!ok || !std::isfinite(numericValue)) {
            return std::nullopt;
        }
        return numericValue;
    }

    static void sanitizeFinite(QVariantMap& record, const QString& field)
    {
        const QVariant value = record.value(field);
        if (!value.isValid() || value.isNull()) {
            return;
        }

        bool ok = false;
        const double numericValue = value.toDouble(&ok);
        if (!ok || !std::isfinite(numericValue)) {
            record.remove(field);
        }
    }

    static void sanitizePositive(QVariantMap& record, const QString& field)
    {
        const auto value = parseFinite(record, field);
        if (!value.has_value()) {
            if (record.contains(field)) {
                record.remove(field);
            }
            return;
        }

        if (*value <= 0.0) {
            record.remove(field);
        }
    }

    static void sanitizeNonNegative(QVariantMap& record, const QString& field)
    {
        const auto value = parseFinite(record, field);
        if (!value.has_value()) {
            if (record.contains(field)) {
                record.remove(field);
            }
            return;
        }

        if (*value < 0.0) {
            record.remove(field);
        }
    }
};

} // namespace factor::bridge