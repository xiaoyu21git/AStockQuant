#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace bridge::rules {

struct RuntimeRuleTemplateEvaluationContext {
    QString symbol;
    double latestPrice = 0.0;
    double referencePrice = 0.0;
    QString marketEventType;
    QString candidateAction;
    double candidateStrength = 0.0;
    QVariantMap strategy;
    QVariantMap flatEventFacts;
    QVariantMap marketSessionSnapshot;
    QVariantMap runtimeSessionSnapshot;
};

struct RuntimeRuleTemplateEvaluationResult {
    bool hasTemplate = false;
    bool matched = false;
    bool blocked = false;
    bool actionPermitted = true;
    QString stage;
    QString ruleId;
    QString reasonCode;
    QString message;
    QString resultType;
    QVariantMap payload;
    QVariantMap state;
    QVariantMap binding;
    QString templateNamespace;
    QString templateFilePath;
    QVariantList groupDecisions;
};

QVariantMap loadCompiledRuleTemplate(const QVariantMap& binding, QString* errorMessage = nullptr);

QVariantList loadCompiledRuleTemplates(const QVariantList& bindings, QString* errorMessage = nullptr);

RuntimeRuleTemplateEvaluationResult evaluateRuleTemplate(
    const QVariantMap& compiledTemplate,
    const RuntimeRuleTemplateEvaluationContext& context);

RuntimeRuleTemplateEvaluationResult evaluateRuleTemplates(
    const QVariantList& compiledTemplates,
    const RuntimeRuleTemplateEvaluationContext& context);

} // namespace bridge::rules