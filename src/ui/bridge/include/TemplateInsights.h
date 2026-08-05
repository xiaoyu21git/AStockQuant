#pragma once
// Auto-generated from RuleTemplatePreviewUtils.js
#include <QString>
#include <QVariantList>
#include <unordered_map>

namespace astock::bridge {

struct TemplateInsight {
    QString summary;
    QString primaryTitle;
    QString secondaryTitle;
    QVariantList primaryItems;
    QVariantList secondaryItems;
};

static const std::unordered_map<QString, TemplateInsight> kTemplateInsights = {
#include "_TemplateInsights.inc"
};

} // namespace astock::bridge
