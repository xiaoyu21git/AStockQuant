#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>

// ═════════════════════════════════════════════════════════════════════════
// RuleTemplateDetailHelper — 规则模板详情辅助
// QML 侧 RuleTemplateStructureView 调用 describeBinding() 解析 bindingData
// ═════════════════════════════════════════════════════════════════════════

class RuleTemplateDetailHelper : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString templateName READ templateName CONSTANT)
    Q_PROPERTY(QString templateDescription READ templateDescription CONSTANT)
    Q_PROPERTY(bool valid READ valid CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage CONSTANT)
public:
    explicit RuleTemplateDetailHelper(QObject* parent = nullptr) : QObject(parent) {}

    QString templateName() const { return m_templateName; }
    QString templateDescription() const { return m_templateDescription; }
    bool valid() const { return m_valid; }
    QString errorMessage() const { return m_errorMessage; }

    /// @brief 解析 bindingData，返回预览所需的字段 (QML 侧: describeBinding(bindingData))
    Q_INVOKABLE QVariantMap describeBinding(const QVariantMap& bindingData) const
    {
        if (bindingData.isEmpty())
            return makeEmptyPreview();

        // 从 bindingData 中提取规则模板元信息
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

        // 同步到自身属性 (供 Q_PROPERTY 读取)
        const_cast<RuleTemplateDetailHelper*>(this)->m_templateName =
            preview["templateName"].toString();
        const_cast<RuleTemplateDetailHelper*>(this)->m_templateDescription =
            preview["templateDescription"].toString();
        const_cast<RuleTemplateDetailHelper*>(this)->m_valid = preview["valid"].toBool();
        const_cast<RuleTemplateDetailHelper*>(this)->m_errorMessage =
            preview["errorMessage"].toString();

        return preview;
    }

private:
    QVariantMap makeEmptyPreview() const {
        QVariantMap preview;
        preview["templateName"] = QStringLiteral("规则模板");
        preview["templateDescription"] = QString();
        preview["valid"] = false;
        preview["errorMessage"] = QStringLiteral("未能展开模板条件");
        preview["rules"] = QVariantList();
        return preview;
    }

    QString m_templateName;
    QString m_templateDescription;
    bool m_valid = false;
    QString m_errorMessage;
};
