#pragma once

#include "../../domain/strategy/include/StrategySnapshotTypes.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace bridge::rules {

struct RuntimeRuleTemplateEvaluationContext {
    domain::strategy::SymbolCode symbol;
    double latestPrice = 0.0;
    double referencePrice = 0.0;
    domain::strategy::MarketEventTypeId marketEventType;
    domain::strategy::CandidateAction candidateAction{domain::strategy::CandidateAction::None};
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
    domain::strategy::RuleTemplateStage stage{domain::strategy::RuleTemplateStage::Unspecified};
    domain::strategy::RuleTemplateId ruleId;
    domain::strategy::ReasonCode reasonCode;
    QString message;
    domain::strategy::RuleTemplateResultType resultType{domain::strategy::RuleTemplateResultType::Unspecified};
    QVariantMap payload;
    QVariantMap state;
    QVariantMap binding;
    domain::strategy::NamespaceId templateNamespace;
    domain::strategy::FilePathToken templateFilePath;
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