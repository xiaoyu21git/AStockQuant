#pragma once

#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>

namespace factor::bridge {

enum class CleaningRuleKey : std::uint8_t {
    Completeness,
    DuplicateRemoval,
    FinancialDateValidity,
    FinancialMetricSanitize,
    ReportDateAlignment,
    SurvivorBias,
    SuspensionFill,
    MissingValueFill,
    AdjustedPrice,
    NewStockFilter,
    STFilter,
    PriceValidity,
    VolumeFilter,
    LimitMoveTag,
    ValuationSanitize
};

enum class CleaningRuleId : std::uint8_t {
    Completeness,
    DuplicateRemoval,
    FinancialDateValidity,
    FinancialMetricSanitize,
    ReportAlignment,
    SurvivorBias,
    SuspensionFill,
    MissingValueFill,
    AdjustedPrice,
    NewStockFilter,
    STFilter,
    PriceValidity,
    LimitTag,
    ValuationSanitize,
    FieldStandardization,
    VolumeFilter
};

enum class CleaningRuleConfigField : std::uint8_t {
    Enabled,
    KeyFields,
    MaxForwardFillDays,
    DropAfterMaxDays,
    FillFields,
    MaxLookbackDays,
    Fields,
    PreferAdjustedFields,
    ApplyFactorFallback,
    MinTradeDays,
    MinPrice,
    MaxPrice,
    MinVolume,
    MaxVolume,
    EnforceChain,
    AllowZeroWhenSuspended,
    UpThreshold,
    DownThreshold
};

inline QString cleaningRuleKeyName(CleaningRuleKey key)
{
    switch (key) {
    case CleaningRuleKey::Completeness:
        return QStringLiteral("completeness");
    case CleaningRuleKey::DuplicateRemoval:
        return QStringLiteral("duplicateRemoval");
    case CleaningRuleKey::FinancialDateValidity:
        return QStringLiteral("financialDateValidity");
    case CleaningRuleKey::FinancialMetricSanitize:
        return QStringLiteral("financialMetricSanitize");
    case CleaningRuleKey::ReportDateAlignment:
        return QStringLiteral("reportDateAlignment");
    case CleaningRuleKey::SurvivorBias:
        return QStringLiteral("survivorBias");
    case CleaningRuleKey::SuspensionFill:
        return QStringLiteral("suspensionFill");
    case CleaningRuleKey::MissingValueFill:
        return QStringLiteral("missingValueFill");
    case CleaningRuleKey::AdjustedPrice:
        return QStringLiteral("adjustedPrice");
    case CleaningRuleKey::NewStockFilter:
        return QStringLiteral("newStockFilter");
    case CleaningRuleKey::STFilter:
        return QStringLiteral("stFilter");
    case CleaningRuleKey::PriceValidity:
        return QStringLiteral("priceValidity");
    case CleaningRuleKey::VolumeFilter:
        return QStringLiteral("volumeFilter");
    case CleaningRuleKey::LimitMoveTag:
        return QStringLiteral("limitMoveTag");
    case CleaningRuleKey::ValuationSanitize:
        return QStringLiteral("valuationSanitize");
    }

    return {};
}

inline QString cleaningRuleIdName(CleaningRuleId id)
{
    switch (id) {
    case CleaningRuleId::Completeness:
        return QStringLiteral("completeness");
    case CleaningRuleId::DuplicateRemoval:
        return QStringLiteral("duplicateRemoval");
    case CleaningRuleId::FinancialDateValidity:
        return QStringLiteral("financial_date_validity");
    case CleaningRuleId::FinancialMetricSanitize:
        return QStringLiteral("financial_metric_sanitize");
    case CleaningRuleId::ReportAlignment:
        return QStringLiteral("report_alignment");
    case CleaningRuleId::SurvivorBias:
        return QStringLiteral("survivorBias");
    case CleaningRuleId::SuspensionFill:
        return QStringLiteral("suspension_fill");
    case CleaningRuleId::MissingValueFill:
        return QStringLiteral("missingValueFill");
    case CleaningRuleId::AdjustedPrice:
        return QStringLiteral("adjustedPrice");
    case CleaningRuleId::NewStockFilter:
        return QStringLiteral("new_stock_filter");
    case CleaningRuleId::STFilter:
        return QStringLiteral("st_filter");
    case CleaningRuleId::PriceValidity:
        return QStringLiteral("priceValidity");
    case CleaningRuleId::LimitTag:
        return QStringLiteral("limit_tag");
    case CleaningRuleId::ValuationSanitize:
        return QStringLiteral("valuation_sanitize");
    case CleaningRuleId::FieldStandardization:
        return QStringLiteral("field_standardization");
    case CleaningRuleId::VolumeFilter:
        return QStringLiteral("volume_filter");
    }

    return {};
}

inline QString cleaningRuleFieldName(CleaningRuleConfigField field)
{
    switch (field) {
    case CleaningRuleConfigField::Enabled:
        return QStringLiteral("enabled");
    case CleaningRuleConfigField::KeyFields:
        return QStringLiteral("keyFields");
    case CleaningRuleConfigField::MaxForwardFillDays:
        return QStringLiteral("maxForwardFillDays");
    case CleaningRuleConfigField::DropAfterMaxDays:
        return QStringLiteral("dropAfterMaxDays");
    case CleaningRuleConfigField::FillFields:
        return QStringLiteral("fillFields");
    case CleaningRuleConfigField::MaxLookbackDays:
        return QStringLiteral("maxLookbackDays");
    case CleaningRuleConfigField::Fields:
        return QStringLiteral("fields");
    case CleaningRuleConfigField::PreferAdjustedFields:
        return QStringLiteral("preferAdjustedFields");
    case CleaningRuleConfigField::ApplyFactorFallback:
        return QStringLiteral("applyFactorFallback");
    case CleaningRuleConfigField::MinTradeDays:
        return QStringLiteral("minTradeDays");
    case CleaningRuleConfigField::MinPrice:
        return QStringLiteral("minPrice");
    case CleaningRuleConfigField::MaxPrice:
        return QStringLiteral("maxPrice");
    case CleaningRuleConfigField::MinVolume:
        return QStringLiteral("minVolume");
    case CleaningRuleConfigField::MaxVolume:
        return QStringLiteral("maxVolume");
    case CleaningRuleConfigField::EnforceChain:
        return QStringLiteral("enforceChain");
    case CleaningRuleConfigField::AllowZeroWhenSuspended:
        return QStringLiteral("allowZeroWhenSuspended");
    case CleaningRuleConfigField::UpThreshold:
        return QStringLiteral("upThreshold");
    case CleaningRuleConfigField::DownThreshold:
        return QStringLiteral("downThreshold");
    }

    return {};
}

inline QString cleaningRuleConfigPath(CleaningRuleKey key, CleaningRuleConfigField field)
{
    return QStringLiteral("%1.%2")
        .arg(cleaningRuleKeyName(key), cleaningRuleFieldName(field));
}

inline const std::array<CleaningRuleKey, 15>& supportedStrictCleaningRuleKeys()
{
    static const std::array<CleaningRuleKey, 15> keys{
        CleaningRuleKey::Completeness,
        CleaningRuleKey::DuplicateRemoval,
        CleaningRuleKey::FinancialDateValidity,
        CleaningRuleKey::FinancialMetricSanitize,
        CleaningRuleKey::ReportDateAlignment,
        CleaningRuleKey::SurvivorBias,
        CleaningRuleKey::SuspensionFill,
        CleaningRuleKey::MissingValueFill,
        CleaningRuleKey::AdjustedPrice,
        CleaningRuleKey::NewStockFilter,
        CleaningRuleKey::STFilter,
        CleaningRuleKey::PriceValidity,
        CleaningRuleKey::VolumeFilter,
        CleaningRuleKey::LimitMoveTag,
        CleaningRuleKey::ValuationSanitize
    };
    return keys;
}

inline QStringList supportedStrictCleaningRuleKeyNames()
{
    QStringList keys;
    keys.reserve(static_cast<qsizetype>(supportedStrictCleaningRuleKeys().size()));
    for (const CleaningRuleKey key : supportedStrictCleaningRuleKeys()) {
        keys.push_back(cleaningRuleKeyName(key));
    }
    return keys;
}

} // namespace factor::bridge