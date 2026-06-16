#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

// ═════════════════════════════════════════════════════════════════════════
// RuleTemplateDetailHelper — 规则模板详情辅助 (最小存根)
// 原完整实现已移除，当前提供空对象避免 QML 引用错误
// ═════════════════════════════════════════════════════════════════════════

class RuleTemplateDetailHelper : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString templateName READ templateName CONSTANT)
    Q_PROPERTY(QString templateDescription READ templateDescription CONSTANT)
    Q_PROPERTY(bool valid READ valid CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage CONSTANT)
public:
    explicit RuleTemplateDetailHelper(QObject* parent = nullptr) : QObject(parent) {}
    QString templateName() const { return {}; }
    QString templateDescription() const { return {}; }
    bool valid() const { return false; }
    QString errorMessage() const { return {}; }
};
