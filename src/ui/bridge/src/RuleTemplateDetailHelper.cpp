#include "RuleTemplateDetailHelper.h"

RuleTemplateDetailHelper::RuleTemplateDetailHelper(QObject* parent)
    : QObject(parent) {}

QString RuleTemplateDetailHelper::templateName() const { return m_templateName; }
QString RuleTemplateDetailHelper::templateDescription() const { return m_templateDescription; }
bool RuleTemplateDetailHelper::valid() const { return m_valid; }
QString RuleTemplateDetailHelper::errorMessage() const { return m_errorMessage; }

QVariantMap RuleTemplateDetailHelper::describeBinding(const QVariantMap& bindingData) const
{
    if (bindingData.isEmpty())
        return makeEmptyPreview();

    const auto meta = bindingData.value("meta").toMap();
    const auto rules = bindingData.value("rules").toList();

    QVariantMap preview;
    preview["templateName"] = meta.value("name");
    if (preview["templateName"].toString().isEmpty())
        preview["templateName"] = QStringLiteral("规则模板");

    preview["templateDescription"] = meta.value("description");
    preview["valid"] = !rules.isEmpty();
    preview["errorMessage"] = rules.isEmpty()
        ? QStringLiteral("模板未包含任何规则条件")
        : QString();
    preview["rules"] = rules;

    const_cast<RuleTemplateDetailHelper*>(this)->m_templateName =
        preview["templateName"].toString();
    const_cast<RuleTemplateDetailHelper*>(this)->m_templateDescription =
        preview["templateDescription"].toString();
    const_cast<RuleTemplateDetailHelper*>(this)->m_valid = preview["valid"].toBool();
    const_cast<RuleTemplateDetailHelper*>(this)->m_errorMessage =
        preview["errorMessage"].toString();

    return preview;
}

QVariantMap RuleTemplateDetailHelper::makeEmptyPreview() const
{
    QVariantMap preview;
    preview["templateName"] = QStringLiteral("规则模板");
    preview["templateDescription"] = QString();
    preview["valid"] = false;
    preview["errorMessage"] = QStringLiteral("未能展开模板条件");
    preview["rules"] = QVariantList();
    return preview;
}

#include "moc_RuleTemplateDetailHelper.cpp"
