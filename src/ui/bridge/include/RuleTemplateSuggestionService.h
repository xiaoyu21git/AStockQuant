#pragma once

#include <QObject>

class RuleTemplateSuggestionService : public QObject {
    Q_OBJECT
public:
    explicit RuleTemplateSuggestionService(QObject* parent = nullptr);
};
