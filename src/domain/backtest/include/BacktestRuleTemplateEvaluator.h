#pragma once

#include <QVariantList>
#include <QVariantMap>
#include <QString>

#include <map>
#include <string>

namespace domain::backtest::rules {

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

QVariantMap bindingFromStrategyOptions(const std::map<std::string, std::string>& strategyOptions);

QVariantList bindingListFromStrategyOptions(const std::map<std::string, std::string>& strategyOptions);

QVariantMap flatFactsFromStrategyConfig(
    const std::map<std::string, std::string>& strategyOptions,
    const std::map<std::string, double>& strategyParams);

QVariantMap strategyScopeFromBacktestConfig(
    const std::string& strategyId,
    const std::map<std::string, std::string>& strategyOptions,
    const std::map<std::string, double>& strategyParams);

bool shouldBlockEntry(const RuntimeRuleTemplateEvaluationResult& result);

bool shouldForceExit(const RuntimeRuleTemplateEvaluationResult& result);

} // namespace domain::backtest::rules
