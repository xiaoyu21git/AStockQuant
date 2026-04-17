#pragma once

#include <QObject>
#include <QMutex>
#include <QVariantMap>

class RuleTemplateDetailHelper : public QObject {
    Q_OBJECT

public:
    static RuleTemplateDetailHelper* instance();

    Q_INVOKABLE QVariantMap describeBinding(const QVariantMap& binding) const;

private:
    explicit RuleTemplateDetailHelper(QObject* parent = nullptr);

    static RuleTemplateDetailHelper* m_instance;
    static QMutex m_instanceMutex;
};