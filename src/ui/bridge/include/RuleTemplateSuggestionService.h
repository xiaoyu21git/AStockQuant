#pragma once

#include <QObject>
#include <QVariantMap>

class RuleTemplateSuggestionService : public QObject {
    Q_OBJECT
public:
    explicit RuleTemplateSuggestionService(QObject* parent = nullptr);

signals:
    void suggestionReady(const QVariantMap& result);
    void suggestionFailed(const QVariantMap& result);
};
