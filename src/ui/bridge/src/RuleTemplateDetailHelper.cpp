#include "RuleTemplateDetailHelper.h"

#include "RuleTemplateRuntimeEvaluator.h"

#include <QJsonDocument>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace {

QVariantMap buildLine(const QString& text, int level = 0)
{
    QVariantMap line;
    line.insert(QStringLiteral("text"), text);
    line.insert(QStringLiteral("level"), level);
    return line;
}

QString variantToDisplayText(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return QStringLiteral("-");
    }

    if (value.typeId() == QMetaType::Bool) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }

    if (value.canConvert<qlonglong>()
            && value.typeId() != QMetaType::QString
            && value.typeId() != QMetaType::Double) {
        return QString::number(value.toLongLong());
    }

    if (value.canConvert<double>() && value.typeId() == QMetaType::Double) {
        return QString::number(value.toDouble(), 'f', 4).remove(QRegularExpression(QStringLiteral("\\.?0+$")));
    }

    if (value.typeId() == QMetaType::QVariantList) {
        QStringList parts;
        const QVariantList items = value.toList();
        for (const QVariant& item : items) {
            parts.append(variantToDisplayText(item));
        }
        return parts.join(QStringLiteral(", "));
    }

    if (value.typeId() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        if (map.contains(QStringLiteral("var"))) {
            return map.value(QStringLiteral("var")).toString();
        }
        if (map.contains(QStringLiteral("literal"))) {
            return variantToDisplayText(map.value(QStringLiteral("literal")));
        }
        return QString::fromUtf8(QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact));
    }

    return value.toString();
}

QString conditionOperatorText(const QString& op)
{
    if (op == QStringLiteral("eq")) {
        return QStringLiteral("==");
    }
    if (op == QStringLiteral("ne")) {
        return QStringLiteral("!=");
    }
    if (op == QStringLiteral("gt")) {
        return QStringLiteral(">");
    }
    if (op == QStringLiteral("ge")) {
        return QStringLiteral(">=");
    }
    if (op == QStringLiteral("lt")) {
        return QStringLiteral("<");
    }
    if (op == QStringLiteral("le")) {
        return QStringLiteral("<=");
    }
    if (op == QStringLiteral("contains")) {
        return QStringLiteral("contains");
    }
    if (op == QStringLiteral("in")) {
        return QStringLiteral("in");
    }
    return op;
}

void appendConditionLines(const QVariantMap& condition, int level, QVariantList* target)
{
    if (!target) {
        return;
    }

    const QString op = condition.value(QStringLiteral("op")).toString().trimmed().toLower();
    if (op.isEmpty()) {
        target->append(buildLine(QStringLiteral("未配置条件表达式"), level));
        return;
    }

    if (op == QStringLiteral("all") || op == QStringLiteral("any")) {
        const QVariantList conditions = condition.value(QStringLiteral("conditions")).toList();
        target->append(buildLine(op == QStringLiteral("all") ? QStringLiteral("全部满足") : QStringLiteral("满足任一条件"), level));
        for (const QVariant& child : conditions) {
            appendConditionLines(child.toMap(), level + 1, target);
        }
        return;
    }

    if (op == QStringLiteral("truthy") || op == QStringLiteral("falsy")) {
        const QString valueText = variantToDisplayText(condition.value(QStringLiteral("value")));
        target->append(buildLine(QStringLiteral("%1 %2").arg(valueText, op == QStringLiteral("truthy") ? QStringLiteral("为真") : QStringLiteral("为假")), level));
        return;
    }

    const QVariant left = condition.value(QStringLiteral("left"));
    const QVariant right = condition.value(QStringLiteral("right"));
    if (left.isValid() || right.isValid()) {
        target->append(buildLine(QStringLiteral("%1 %2 %3")
                                     .arg(variantToDisplayText(left),
                                          conditionOperatorText(op),
                                          variantToDisplayText(right)),
                                 level));
        return;
    }

    if (condition.contains(QStringLiteral("value"))) {
        target->append(buildLine(QStringLiteral("%1: %2")
                                     .arg(op, variantToDisplayText(condition.value(QStringLiteral("value")))),
                                 level));
        return;
    }

    target->append(buildLine(QStringLiteral("%1 %2")
                                 .arg(op,
                                      QString::fromUtf8(QJsonDocument::fromVariant(condition).toJson(QJsonDocument::Compact))),
                             level));
}

QVariantList buildValueLines(const QVariantMap& values)
{
    QVariantList lines;
    QStringList keys = values.keys();
    std::sort(keys.begin(), keys.end(), [](const QString& left, const QString& right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });

    for (const QString& key : keys) {
        lines.append(buildLine(QStringLiteral("%1 = %2").arg(key, variantToDisplayText(values.value(key)))));
    }
    return lines;
}

QVariantMap buildRulePreview(const QVariantMap& rule)
{
    QVariantMap preview;
    const QVariantMap thenBlock = rule.value(QStringLiteral("then")).toMap();

    preview.insert(QStringLiteral("id"), rule.value(QStringLiteral("id")).toString());
    preview.insert(QStringLiteral("name"), rule.value(QStringLiteral("name")).toString());
    preview.insert(QStringLiteral("stage"), rule.value(QStringLiteral("stage")).toString());
    preview.insert(QStringLiteral("priority"), rule.value(QStringLiteral("priority")));
    preview.insert(QStringLiteral("description"), rule.value(QStringLiteral("description")).toString());
    preview.insert(QStringLiteral("tags"), rule.value(QStringLiteral("tags")).toList());

    QVariantList conditionLines;
    appendConditionLines(rule.value(QStringLiteral("when")).toMap(), 0, &conditionLines);
    preview.insert(QStringLiteral("conditionLines"), conditionLines);

    QVariantList actionLines;
    const QString result = thenBlock.value(QStringLiteral("result")).toString().trimmed();
    if (!result.isEmpty()) {
        actionLines.append(buildLine(QStringLiteral("结果 = %1").arg(result)));
    }
    const QString reasonCode = thenBlock.value(QStringLiteral("reason_code")).toString().trimmed();
    if (!reasonCode.isEmpty()) {
        actionLines.append(buildLine(QStringLiteral("原因码 = %1").arg(reasonCode)));
    }
    const QString message = thenBlock.value(QStringLiteral("message")).toString().trimmed();
    if (!message.isEmpty()) {
        actionLines.append(buildLine(QStringLiteral("提示 = %1").arg(message)));
    }
    if (thenBlock.contains(QStringLiteral("score"))) {
        actionLines.append(buildLine(QStringLiteral("评分 = %1").arg(variantToDisplayText(thenBlock.value(QStringLiteral("score"))))));
    }
    preview.insert(QStringLiteral("actionLines"), actionLines);
    preview.insert(QStringLiteral("payloadLines"), buildValueLines(thenBlock.value(QStringLiteral("payload")).toMap()));
    preview.insert(QStringLiteral("stateLines"), buildValueLines(rule.value(QStringLiteral("state")).toMap()));
    preview.insert(QStringLiteral("resultType"), result);

    return preview;
}

} // namespace

RuleTemplateDetailHelper* RuleTemplateDetailHelper::m_instance = nullptr;
QMutex RuleTemplateDetailHelper::m_instanceMutex;

RuleTemplateDetailHelper::RuleTemplateDetailHelper(QObject* parent)
    : QObject(parent)
{
}

RuleTemplateDetailHelper* RuleTemplateDetailHelper::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new RuleTemplateDetailHelper();
    }
    return m_instance;
}

QVariantMap RuleTemplateDetailHelper::describeBinding(const QVariantMap& binding) const
{
    QVariantMap preview;
    QString errorMessage;
    const QVariantMap compiledTemplate = bridge::rules::loadCompiledRuleTemplate(binding, &errorMessage);

    preview.insert(QStringLiteral("valid"), !compiledTemplate.isEmpty());
    preview.insert(QStringLiteral("errorMessage"), errorMessage);
    preview.insert(QStringLiteral("templateName"),
                   binding.value(QStringLiteral("template_display_name")).toString().trimmed().isEmpty()
                       ? binding.value(QStringLiteral("template_id")).toString()
                       : binding.value(QStringLiteral("template_display_name")).toString());
    preview.insert(QStringLiteral("namespace"), compiledTemplate.value(QStringLiteral("namespace")));
    preview.insert(QStringLiteral("templateDescription"), binding.value(QStringLiteral("summary")));

    if (compiledTemplate.isEmpty()) {
        return preview;
    }

    QVariantList rulePreviews;
    const QVariantList rules = compiledTemplate.value(QStringLiteral("rules")).toList();
    for (const QVariant& ruleValue : rules) {
        rulePreviews.append(buildRulePreview(ruleValue.toMap()));
    }

    preview.insert(QStringLiteral("ruleCount"), rulePreviews.size());
    preview.insert(QStringLiteral("rules"), rulePreviews);
    if (!rulePreviews.isEmpty()) {
        preview.insert(QStringLiteral("stage"), rulePreviews.first().toMap().value(QStringLiteral("stage")));
    }
    if (preview.value(QStringLiteral("templateName")).toString().trimmed().isEmpty()) {
        preview.insert(QStringLiteral("templateName"), compiledTemplate.value(QStringLiteral("namespace")));
    }
    return preview;
}