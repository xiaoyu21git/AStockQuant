#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>

class RuleTemplateDetailHelper : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString templateName READ templateName CONSTANT)
    Q_PROPERTY(QString templateDescription READ templateDescription CONSTANT)
    Q_PROPERTY(bool valid READ valid CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage CONSTANT)
public:
    explicit RuleTemplateDetailHelper(QObject* parent = nullptr);

    QString templateName() const;
    QString templateDescription() const;
    bool valid() const;
    QString errorMessage() const;

    Q_INVOKABLE QVariantMap describeBinding(const QVariantMap& bindingData) const;

private:
    QVariantMap makeEmptyPreview() const;

    QString m_templateName;
    QString m_templateDescription;
    bool m_valid = false;
    QString m_errorMessage;
};
