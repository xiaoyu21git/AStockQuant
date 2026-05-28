#include "RuleTemplateRuntimeEvaluator.h"

#include "../../domain/backtest/include/ResolvedStrategyBehaviorVariant.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QReadWriteLock>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "../../domain/strategy/include/RuleTemplateStringConstants.h"
#include "../../domain/strategy/include/StrategySnapshotTypes.h"

namespace bridge::rules {
namespace {

namespace rule_template_strings = domain::strategy::rule_template_strings;

QString ruleTemplateString(const char* literal)
{
    return QString::fromLatin1(literal);
}

bool matchesRuleTemplateString(const QString& value, const char* literal)
{
    return value == QString::fromLatin1(literal);
}

struct CachedTemplateEntry {
    qint64 lastModifiedMs = 0;
    QVariantMap compiledTemplate;
};

QReadWriteLock& templateCacheLock()
{
    static QReadWriteLock lock;
    return lock;
}

QHash<QString, CachedTemplateEntry>& templateCache()
{
    static QHash<QString, CachedTemplateEntry> cache;
    return cache;
}

QString firstNonEmptyBindingValue(const QVariantMap& binding, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QString value = binding.value(QString::fromUtf8(key)).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString resolveRepositoryRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    while (dir.exists()) {
        if (dir.exists(ruleTemplateString(rule_template_strings::kPathRulesExamples))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

QString resolveTemplateFilePath(const QVariantMap& rawBinding)
{
    const QString explicitFilePath = firstNonEmptyBindingValue(
        rawBinding,
        {rule_template_strings::kBindingFilePath});
    QFileInfo fileInfo(explicitFilePath);
    if (!explicitFilePath.isEmpty() && fileInfo.exists()) {
        return fileInfo.canonicalFilePath();
    }

    const QString fileName = firstNonEmptyBindingValue(
        rawBinding,
        {rule_template_strings::kBindingFileName});
    if (fileName.isEmpty()) {
        return {};
    }

    const QString repositoryRoot = resolveRepositoryRoot();
    if (repositoryRoot.isEmpty()) {
        return {};
    }

    const QFileInfo fallbackInfo(
        QDir(repositoryRoot).filePath(
            ruleTemplateString(rule_template_strings::kPathRulesExamples) + QStringLiteral("/%1").arg(fileName)));
    if (!fallbackInfo.exists()) {
        return {};
    }
    return fallbackInfo.canonicalFilePath();
}

QVariant parseScalarNode(const YAML::Node& node)
{
    if (!node || !node.IsScalar()) {
        return {};
    }

    const std::string rawScalar = node.Scalar();
    try {
        return node.as<bool>();
    } catch (const YAML::Exception&) {
    }

    try {
        return static_cast<qlonglong>(node.as<long long>());
    } catch (const YAML::Exception&) {
    }

    try {
        return node.as<double>();
    } catch (const YAML::Exception&) {
    }

    return QString::fromStdString(rawScalar);
}

QVariant yamlNodeToVariant(const YAML::Node& node)
{
    if (!node || node.IsNull()) {
        return {};
    }

    if (node.IsScalar()) {
        return parseScalarNode(node);
    }

    if (node.IsSequence()) {
        QVariantList items;
        for (const YAML::Node& child : node) {
            items.append(yamlNodeToVariant(child));
        }
        return items;
    }

    QVariantMap mapping;
    for (const auto& item : node) {
        mapping.insert(QString::fromStdString(item.first.as<std::string>()), yamlNodeToVariant(item.second));
    }
    return mapping;
}

QVariantMap normalizeBinding(const QVariantMap& rawBinding, const QString& resolvedFilePath)
{
    QVariantMap binding = rawBinding;
    if (!resolvedFilePath.isEmpty()) {
        binding.insert(ruleTemplateString(rule_template_strings::kBindingFilePath), resolvedFilePath);
    }

    const QString fileName = QFileInfo(resolvedFilePath).fileName();
    if (!fileName.isEmpty()) {
        binding.insert(ruleTemplateString(rule_template_strings::kBindingFileName), fileName);
    }
    return binding;
}

QVariantMap attachResolvedBinding(const QVariantMap& compiledTemplate,
                                 const QVariantMap& rawBinding,
                                 const QString& resolvedFilePath,
                                 qint64 lastModifiedMs)
{
    QVariantMap hydratedTemplate = compiledTemplate;
    hydratedTemplate.insert(
        ruleTemplateString(rule_template_strings::kCompiledTemplateBinding),
        normalizeBinding(rawBinding, resolvedFilePath));
    hydratedTemplate.insert(ruleTemplateString(rule_template_strings::kCompiledTemplateFilePath), resolvedFilePath);
    hydratedTemplate.insert(
        ruleTemplateString(rule_template_strings::kCompiledTemplateLastModifiedMs),
        lastModifiedMs);
    return hydratedTemplate;
}

QVariantMap mapFromVariant(const QVariant& value)
{
    return value.toMap();
}

QVariantList listFromVariant(const QVariant& value)
{
    return value.toList();
}

domain::strategy::RuleTemplateResultType parseRuleTemplateResultType(const QString& resultType)
{
    const QString normalizedResultType = resultType.trimmed().toLower();
    if (matchesRuleTemplateString(normalizedResultType, rule_template_strings::kResultPass)) {
        return domain::strategy::RuleTemplateResultType::Pass;
    }
    if (matchesRuleTemplateString(normalizedResultType, rule_template_strings::kResultStateSwitch)) {
        return domain::strategy::RuleTemplateResultType::StateSwitch;
    }
    if (matchesRuleTemplateString(normalizedResultType, rule_template_strings::kResultHalt)) {
        return domain::strategy::RuleTemplateResultType::Halt;
    }
    if (matchesRuleTemplateString(normalizedResultType, rule_template_strings::kResultBlock)) {
        return domain::strategy::RuleTemplateResultType::Block;
    }
    if (matchesRuleTemplateString(normalizedResultType, rule_template_strings::kActionCandidateEntry)) {
        return domain::strategy::RuleTemplateResultType::CandidateEntry;
    }
    if (matchesRuleTemplateString(normalizedResultType, rule_template_strings::kActionOpen)) {
        return domain::strategy::RuleTemplateResultType::Open;
    }
    if (matchesRuleTemplateString(normalizedResultType, rule_template_strings::kActionReduce)) {
        return domain::strategy::RuleTemplateResultType::Reduce;
    }
    if (matchesRuleTemplateString(normalizedResultType, rule_template_strings::kActionExit)) {
        return domain::strategy::RuleTemplateResultType::Exit;
    }
    return domain::strategy::RuleTemplateResultType::Unspecified;
}

QString ruleTemplateResultTypeName(domain::strategy::RuleTemplateResultType resultType)
{
    switch (resultType) {
    case domain::strategy::RuleTemplateResultType::Pass:
        return ruleTemplateString(rule_template_strings::kResultPass);
    case domain::strategy::RuleTemplateResultType::StateSwitch:
        return ruleTemplateString(rule_template_strings::kResultStateSwitch);
    case domain::strategy::RuleTemplateResultType::Halt:
        return ruleTemplateString(rule_template_strings::kResultHalt);
    case domain::strategy::RuleTemplateResultType::Block:
        return ruleTemplateString(rule_template_strings::kResultBlock);
    case domain::strategy::RuleTemplateResultType::CandidateEntry:
        return ruleTemplateString(rule_template_strings::kActionCandidateEntry);
    case domain::strategy::RuleTemplateResultType::Open:
        return ruleTemplateString(rule_template_strings::kActionOpen);
    case domain::strategy::RuleTemplateResultType::Reduce:
        return ruleTemplateString(rule_template_strings::kActionReduce);
    case domain::strategy::RuleTemplateResultType::Exit:
        return ruleTemplateString(rule_template_strings::kActionExit);
    case domain::strategy::RuleTemplateResultType::Unspecified:
        return {};
    }

    return {};
}

domain::strategy::RuleTemplateResultType normalizedResultType(const QVariantMap& thenBlock)
{
    return parseRuleTemplateResultType(
        thenBlock.value(ruleTemplateString(rule_template_strings::kFieldResult)).toString());
}

domain::strategy::RuleTemplateStage parseRuleTemplateStage(const QString& stage)
{
    const QString normalizedStage = stage.trimmed().toLower();
    if (matchesRuleTemplateString(normalizedStage, rule_template_strings::kScopeMarket)) {
        return domain::strategy::RuleTemplateStage::Market;
    }
    if (matchesRuleTemplateString(normalizedStage, rule_template_strings::kStageSignal)) {
        return domain::strategy::RuleTemplateStage::Signal;
    }
    if (matchesRuleTemplateString(normalizedStage, rule_template_strings::kActionEntry)) {
        return domain::strategy::RuleTemplateStage::Entry;
    }
    if (matchesRuleTemplateString(normalizedStage, rule_template_strings::kStageRebalance)) {
        return domain::strategy::RuleTemplateStage::Rebalance;
    }
    if (matchesRuleTemplateString(normalizedStage, rule_template_strings::kActionExit)) {
        return domain::strategy::RuleTemplateStage::Exit;
    }
    if (matchesRuleTemplateString(normalizedStage, rule_template_strings::kStageRisk)) {
        return domain::strategy::RuleTemplateStage::Risk;
    }
    if (matchesRuleTemplateString(normalizedStage, rule_template_strings::kStageWatch)) {
        return domain::strategy::RuleTemplateStage::Watch;
    }
    if (matchesRuleTemplateString(normalizedStage, rule_template_strings::kStageEligibility)) {
        return domain::strategy::RuleTemplateStage::Eligibility;
    }
    if (matchesRuleTemplateString(normalizedStage, rule_template_strings::kStagePortfolio)) {
        return domain::strategy::RuleTemplateStage::Portfolio;
    }
    if (matchesRuleTemplateString(normalizedStage, rule_template_strings::kStageExecution)) {
        return domain::strategy::RuleTemplateStage::Execution;
    }
    if (matchesRuleTemplateString(normalizedStage, rule_template_strings::kStageAccountRisk)) {
        return domain::strategy::RuleTemplateStage::AccountRisk;
    }
    return domain::strategy::RuleTemplateStage::Unspecified;
}

QString ruleTemplateStageName(domain::strategy::RuleTemplateStage stage)
{
    switch (stage) {
    case domain::strategy::RuleTemplateStage::Market:
        return ruleTemplateString(rule_template_strings::kScopeMarket);
    case domain::strategy::RuleTemplateStage::Signal:
        return ruleTemplateString(rule_template_strings::kStageSignal);
    case domain::strategy::RuleTemplateStage::Entry:
        return ruleTemplateString(rule_template_strings::kActionEntry);
    case domain::strategy::RuleTemplateStage::Rebalance:
        return ruleTemplateString(rule_template_strings::kStageRebalance);
    case domain::strategy::RuleTemplateStage::Exit:
        return ruleTemplateString(rule_template_strings::kActionExit);
    case domain::strategy::RuleTemplateStage::Risk:
        return ruleTemplateString(rule_template_strings::kStageRisk);
    case domain::strategy::RuleTemplateStage::Watch:
        return ruleTemplateString(rule_template_strings::kStageWatch);
    case domain::strategy::RuleTemplateStage::Eligibility:
        return ruleTemplateString(rule_template_strings::kStageEligibility);
    case domain::strategy::RuleTemplateStage::Portfolio:
        return ruleTemplateString(rule_template_strings::kStagePortfolio);
    case domain::strategy::RuleTemplateStage::Execution:
        return ruleTemplateString(rule_template_strings::kStageExecution);
    case domain::strategy::RuleTemplateStage::AccountRisk:
        return ruleTemplateString(rule_template_strings::kStageAccountRisk);
    case domain::strategy::RuleTemplateStage::Unspecified:
        return {};
    }

    return {};
}

domain::strategy::RuleTemplateStage ruleTemplateStageFromBindingPhase(domain::strategy::RuleBindingPhase phase)
{
    switch (phase) {
    case domain::strategy::RuleBindingPhase::Market:
        return domain::strategy::RuleTemplateStage::Market;
    case domain::strategy::RuleBindingPhase::Signal:
        return domain::strategy::RuleTemplateStage::Signal;
    case domain::strategy::RuleBindingPhase::Entry:
        return domain::strategy::RuleTemplateStage::Entry;
    case domain::strategy::RuleBindingPhase::Rebalance:
        return domain::strategy::RuleTemplateStage::Rebalance;
    case domain::strategy::RuleBindingPhase::Exit:
        return domain::strategy::RuleTemplateStage::Exit;
    case domain::strategy::RuleBindingPhase::Risk:
        return domain::strategy::RuleTemplateStage::Risk;
    case domain::strategy::RuleBindingPhase::Watch:
        return domain::strategy::RuleTemplateStage::Watch;
    }

    return domain::strategy::RuleTemplateStage::Unspecified;
}

QVariant mapValueByPath(const QVariantMap& scope, const QStringList& pathParts, int startIndex = 0)
{
    QVariant current = scope;
    for (int index = startIndex; index < pathParts.size(); ++index) {
        const QVariantMap currentMap = current.toMap();
        if (currentMap.isEmpty()) {
            return {};
        }
        current = currentMap.value(pathParts.at(index));
        if (!current.isValid()) {
            return {};
        }
    }
    return current;
}

QVariant resolveScopedValue(const QVariantMap& scopes, const QString& variablePath)
{
    const QStringList pathParts = variablePath.split('.', Qt::SkipEmptyParts);
    if (pathParts.isEmpty()) {
        return {};
    }

    const QString rootScope = pathParts.first();
    if (scopes.contains(rootScope)) {
        return mapValueByPath(scopes.value(rootScope).toMap(), pathParts, 1);
    }

    return mapValueByPath(scopes, pathParts, 0);
}

bool isTruthy(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return false;
    }

    if (value.typeId() == QMetaType::Bool) {
        return value.toBool();
    }

    if (value.canConvert<double>()) {
        bool ok = false;
        const double numeric = value.toDouble(&ok);
        if (ok) {
            return std::fabs(numeric) > 1e-12;
        }
    }

    if (value.typeId() == QMetaType::QString) {
        const QString text = value.toString().trimmed();
        if (text.isEmpty()) {
            return false;
        }
        if (text.compare(ruleTemplateString(rule_template_strings::kBooleanFalse), Qt::CaseInsensitive) == 0
                || text == ruleTemplateString(rule_template_strings::kNumericZero)) {
            return false;
        }
        return true;
    }

    if (value.typeId() == QMetaType::QVariantList) {
        return !value.toList().isEmpty();
    }

    if (value.typeId() == QMetaType::QVariantMap) {
        return !value.toMap().isEmpty();
    }

    return true;
}

QVariant resolveExpressionValue(const QVariant& expression, const QVariantMap& scopes)
{
    const QVariantMap expressionMap = expression.toMap();
    if (expressionMap.size() == 1
            && expressionMap.contains(ruleTemplateString(rule_template_strings::kConditionVar))) {
        return resolveScopedValue(
            scopes,
            expressionMap.value(ruleTemplateString(rule_template_strings::kConditionVar)).toString().trimmed());
    }
    return expression;
}

bool compareVariantValues(const QVariant& left, const QVariant& right, const QString& op)
{
    bool leftOk = false;
    bool rightOk = false;
    const double leftNumber = left.toDouble(&leftOk);
    const double rightNumber = right.toDouble(&rightOk);
    if (leftOk && rightOk) {
        if (matchesRuleTemplateString(op, rule_template_strings::kConditionEq)) {
            return std::fabs(leftNumber - rightNumber) <= 1e-12;
        }
        if (matchesRuleTemplateString(op, rule_template_strings::kConditionNe)) {
            return std::fabs(leftNumber - rightNumber) > 1e-12;
        }
        if (matchesRuleTemplateString(op, rule_template_strings::kConditionLt)) {
            return leftNumber < rightNumber;
        }
        if (matchesRuleTemplateString(op, rule_template_strings::kConditionLe)) {
            return leftNumber <= rightNumber;
        }
        if (matchesRuleTemplateString(op, rule_template_strings::kConditionGt)) {
            return leftNumber > rightNumber;
        }
        if (matchesRuleTemplateString(op, rule_template_strings::kConditionGe)) {
            return leftNumber >= rightNumber;
        }
    }

    const QString leftText = left.toString().trimmed();
    const QString rightText = right.toString().trimmed();
    if (matchesRuleTemplateString(op, rule_template_strings::kConditionEq)) {
        return leftText.compare(rightText, Qt::CaseInsensitive) == 0;
    }
    if (matchesRuleTemplateString(op, rule_template_strings::kConditionNe)) {
        return leftText.compare(rightText, Qt::CaseInsensitive) != 0;
    }
    if (matchesRuleTemplateString(op, rule_template_strings::kConditionLt)) {
        return leftText < rightText;
    }
    if (matchesRuleTemplateString(op, rule_template_strings::kConditionLe)) {
        return leftText <= rightText;
    }
    if (matchesRuleTemplateString(op, rule_template_strings::kConditionGt)) {
        return leftText > rightText;
    }
    if (matchesRuleTemplateString(op, rule_template_strings::kConditionGe)) {
        return leftText >= rightText;
    }
    return false;
}

bool evaluateConditionNode(const QVariantMap& node, const QVariantMap& scopes)
{
    const QString op = node.value(ruleTemplateString(rule_template_strings::kConditionOp)).toString().trimmed().toLower();
    if (op.isEmpty()) {
        return false;
    }

        if (matchesRuleTemplateString(op, rule_template_strings::kGroupOperatorAll)
            || matchesRuleTemplateString(op, rule_template_strings::kGroupOperatorAny)) {
        const QVariantList conditions = listFromVariant(
            node.value(ruleTemplateString(rule_template_strings::kConditionConditions)));
        if (conditions.isEmpty()) {
            return false;
        }

        if (matchesRuleTemplateString(op, rule_template_strings::kGroupOperatorAll)) {
            for (const QVariant& condition : conditions) {
                if (!evaluateConditionNode(condition.toMap(), scopes)) {
                    return false;
                }
            }
            return true;
        }

        for (const QVariant& condition : conditions) {
            if (evaluateConditionNode(condition.toMap(), scopes)) {
                return true;
            }
        }
        return false;
    }

    if (matchesRuleTemplateString(op, rule_template_strings::kConditionTruthy)) {
        return isTruthy(
            resolveExpressionValue(node.value(ruleTemplateString(rule_template_strings::kConditionValue)), scopes));
    }

    if (matchesRuleTemplateString(op, rule_template_strings::kConditionNot)) {
        return !evaluateConditionNode(
            node.value(ruleTemplateString(rule_template_strings::kConditionNotCondition)).toMap(),
            scopes);
    }

    if (matchesRuleTemplateString(op, rule_template_strings::kConditionEq)
            || matchesRuleTemplateString(op, rule_template_strings::kConditionNe)
            || matchesRuleTemplateString(op, rule_template_strings::kConditionLt)
            || matchesRuleTemplateString(op, rule_template_strings::kConditionLe)
            || matchesRuleTemplateString(op, rule_template_strings::kConditionGt)
            || matchesRuleTemplateString(op, rule_template_strings::kConditionGe)) {
        const QVariant left =
            resolveExpressionValue(node.value(ruleTemplateString(rule_template_strings::kConditionLeft)), scopes);
        const QVariant right =
            resolveExpressionValue(node.value(ruleTemplateString(rule_template_strings::kConditionRight)), scopes);
        return compareVariantValues(left, right, op);
    }

    return false;
}

QVariant parseLooseValue(const QString& rawValue)
{
    const QString text = rawValue.trimmed();
    if (text.isEmpty()) {
        return {};
    }

    if (text.compare(ruleTemplateString(rule_template_strings::kBooleanTrue), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (text.compare(ruleTemplateString(rule_template_strings::kBooleanFalse), Qt::CaseInsensitive) == 0) {
        return false;
    }

    bool integerOk = false;
    const qlonglong integerValue = text.toLongLong(&integerOk);
    if (integerOk) {
        return integerValue;
    }

    bool doubleOk = false;
    const double doubleValue = text.toDouble(&doubleOk);
    if (doubleOk) {
        return doubleValue;
    }

    if ((text.startsWith('{') && text.endsWith('}')) || (text.startsWith('[') && text.endsWith(']'))) {
        const QJsonDocument jsonDocument = QJsonDocument::fromJson(text.toUtf8());
        if (!jsonDocument.isNull()) {
            if (jsonDocument.isObject()) {
                return jsonDocument.object().toVariantMap();
            }
            if (jsonDocument.isArray()) {
                return jsonDocument.array().toVariantList();
            }
        }
    }

    return text;
}

domain::strategy::CandidateAction parseCandidateAction(const QString& candidateAction)
{
    const QString normalized = candidateAction.trimmed().toLower();
    if (normalized.isEmpty()) {
        return domain::strategy::CandidateAction::None;
    }

    if (matchesRuleTemplateString(normalized, rule_template_strings::kActionBuy)
            || matchesRuleTemplateString(normalized, rule_template_strings::kActionEntry)
            || matchesRuleTemplateString(normalized, rule_template_strings::kActionCandidateEntry)
            || matchesRuleTemplateString(normalized, rule_template_strings::kActionOpen)) {
        return domain::strategy::CandidateAction::Buy;
    }

    if (matchesRuleTemplateString(normalized, rule_template_strings::kActionSell)
            || matchesRuleTemplateString(normalized, rule_template_strings::kActionReduce)
            || matchesRuleTemplateString(normalized, rule_template_strings::kActionExit)
            || matchesRuleTemplateString(normalized, rule_template_strings::kActionClose)) {
        return domain::strategy::CandidateAction::Sell;
    }

    return domain::strategy::CandidateAction::None;
}

QString candidateActionName(const domain::strategy::CandidateAction candidateAction)
{
    switch (candidateAction) {
    case domain::strategy::CandidateAction::Buy:
        return ruleTemplateString(rule_template_strings::kActionBuy);
    case domain::strategy::CandidateAction::Sell:
        return ruleTemplateString(rule_template_strings::kActionSell);
    case domain::strategy::CandidateAction::None:
        return {};
    }

    return {};
}

bool allowActionsPermitCandidate(const QVariantMap& payload,
                                 domain::strategy::CandidateAction candidateAction)
{
    const QVariantList allowActions = payload.value(ruleTemplateString(rule_template_strings::kFieldAllowActions)).toList();
    if (allowActions.isEmpty() || candidateAction == domain::strategy::CandidateAction::None) {
        return false;
    }

    for (const QVariant& action : allowActions) {
        if (parseCandidateAction(action.toString()) == candidateAction) {
            return true;
        }
    }
    return false;
}

bool isEntryLikeCandidateAction(domain::strategy::CandidateAction candidateAction)
{
    return candidateAction == domain::strategy::CandidateAction::Buy;
}

bool isExitLikeCandidateAction(domain::strategy::CandidateAction candidateAction)
{
    return candidateAction == domain::strategy::CandidateAction::Sell;
}

bool isBlockingTemplateResult(const QVariantMap& thenBlock,
                              domain::strategy::CandidateAction candidateAction)
{
    const domain::strategy::RuleTemplateResultType resultType = normalizedResultType(thenBlock);
    if (resultType == domain::strategy::RuleTemplateResultType::Unspecified
            || resultType == domain::strategy::RuleTemplateResultType::Pass) {
        return false;
    }

    if (resultType == domain::strategy::RuleTemplateResultType::CandidateEntry
            || resultType == domain::strategy::RuleTemplateResultType::Open
            || resultType == domain::strategy::RuleTemplateResultType::Exit
            || resultType == domain::strategy::RuleTemplateResultType::Reduce) {
        return false;
    }

    if (resultType == domain::strategy::RuleTemplateResultType::StateSwitch) {
        return !allowActionsPermitCandidate(
            thenBlock.value(ruleTemplateString(rule_template_strings::kFieldPayload)).toMap(),
            candidateAction);
    }

    return true;
}

QVariantMap buildCandidateScope(const RuntimeRuleTemplateEvaluationContext& context)
{
    QVariantMap candidate;
    candidate.insert(ruleTemplateString(rule_template_strings::kScopeSymbol), context.symbol.text().toUpper());
    candidate.insert(ruleTemplateString(rule_template_strings::kScopeLatestPrice), context.latestPrice);
    candidate.insert(ruleTemplateString(rule_template_strings::kScopeReferencePrice), context.referencePrice);
    candidate.insert(ruleTemplateString(rule_template_strings::kScopeMarketEventType), context.marketEventType.text());
    candidate.insert(ruleTemplateString(rule_template_strings::kScopeCandidateAction), candidateActionName(context.candidateAction));
    candidate.insert(ruleTemplateString(rule_template_strings::kScopeCandidateStrength), context.candidateStrength);
    return candidate;
}

QVariantMap buildMarketScope(const RuntimeRuleTemplateEvaluationContext& context)
{
    QVariantMap market = context.marketSessionSnapshot;
    if (!context.marketEventType.isEmpty()) {
        market.insert(ruleTemplateString(rule_template_strings::kScopeMarketEventType), context.marketEventType.text());
    }
    return market;
}

QVariantMap buildStrategyScope(const RuntimeRuleTemplateEvaluationContext& context)
{
    QVariantMap strategyScope;
    strategyScope.insert(
        ruleTemplateString(rule_template_strings::kScopeStrategyId),
        context.strategy.value(ruleTemplateString(rule_template_strings::kScopeStrategyId)));
    const domain::backtest::ResolvedStrategyBehavior behavior =
        domain::backtest::resolveStrategyBehavior(context.strategy);
    if (behavior.valid) {
        strategyScope.insert(ruleTemplateString(rule_template_strings::kScopeStrategyBehaviorKind), behavior.index());
    }
    strategyScope.insert(ruleTemplateString(rule_template_strings::kScopeParameters), context.strategy.value(ruleTemplateString(rule_template_strings::kScopeParameters)).toMap());
    strategyScope.insert(
        ruleTemplateString(rule_template_strings::kScopeRuleProfile),
        context.strategy.value(ruleTemplateString(rule_template_strings::kSnapshotRuleProfile)).toMap());
    strategyScope.insert(
        ruleTemplateString(rule_template_strings::kScopeExecutionPolicy),
        context.strategy.value(ruleTemplateString(rule_template_strings::kSnapshotExecutionPolicy)).toMap());
    strategyScope.insert(
        ruleTemplateString(rule_template_strings::kScopeStrategyScopeContext),
        context.strategy.value(ruleTemplateString(rule_template_strings::kSnapshotStrategyScopeContext)).toMap());
    strategyScope.insert(ruleTemplateString(rule_template_strings::kScopeRuntimeSession), context.runtimeSessionSnapshot);
    return strategyScope;
}

void mergePrefixedFacts(const QVariantMap& flatFacts,
                        const QString& dottedPrefix,
                        const QString& snakePrefix,
                        QVariantMap* target)
{
    if (!target) {
        return;
    }

    for (auto it = flatFacts.constBegin(); it != flatFacts.constEnd(); ++it) {
        const QString key = it.key();
        if (key.startsWith(dottedPrefix, Qt::CaseInsensitive)) {
            target->insert(key.mid(dottedPrefix.size()), it.value());
        } else if (!snakePrefix.isEmpty() && key.startsWith(snakePrefix, Qt::CaseInsensitive)) {
            target->insert(key.mid(snakePrefix.size()), it.value());
        }
    }
}

QVariantMap buildScopes(const RuntimeRuleTemplateEvaluationContext& context)
{
    QVariantMap candidate = buildCandidateScope(context);
    QVariantMap market = buildMarketScope(context);
    QVariantMap strategyScope = buildStrategyScope(context);

    mergePrefixedFacts(
        context.flatEventFacts,
        ruleTemplateString(rule_template_strings::kFactPrefixCandidateDot),
        QString(),
        &candidate);
    mergePrefixedFacts(
        context.flatEventFacts,
        ruleTemplateString(rule_template_strings::kFactPrefixMarketDot),
        QString(),
        &market);
    mergePrefixedFacts(
        context.flatEventFacts,
        ruleTemplateString(rule_template_strings::kFactPrefixStrategyDot),
        QString(),
        &strategyScope);

    QVariantMap scopes;
    scopes.insert(ruleTemplateString(rule_template_strings::kScopeCandidate), candidate);
    scopes.insert(ruleTemplateString(rule_template_strings::kScopeMarket), market);
    scopes.insert(ruleTemplateString(rule_template_strings::kScopeStrategy), strategyScope);
    return scopes;
}

} // namespace

QVariantMap loadCompiledRuleTemplate(const QVariantMap& binding, QString* errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    if (binding.isEmpty()) {
        return {};
    }

    const QString resolvedFilePath = resolveTemplateFilePath(binding);
    if (resolvedFilePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未找到规则模板文件");
        }
        return {};
    }

    const QFileInfo fileInfo(resolvedFilePath);
    const qint64 lastModifiedMs = fileInfo.lastModified().toMSecsSinceEpoch();
    {
        QReadLocker locker(&templateCacheLock());
        const auto it = templateCache().constFind(resolvedFilePath);
        if (it != templateCache().constEnd() && it->lastModifiedMs == lastModifiedMs) {
            return attachResolvedBinding(it->compiledTemplate, binding, resolvedFilePath, lastModifiedMs);
        }
    }

    QVariantMap compiledTemplate;
    try {
        const YAML::Node document = YAML::LoadFile(resolvedFilePath.toStdString());
        compiledTemplate = yamlNodeToVariant(document).toMap();
    } catch (const YAML::Exception& exception) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("规则模板解析失败: %1").arg(QString::fromUtf8(exception.what()));
        }
        return {};
    }

    if (compiledTemplate.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("规则模板内容为空");
        }
        return {};
    }

    {
        QWriteLocker locker(&templateCacheLock());
        templateCache().insert(resolvedFilePath, CachedTemplateEntry{lastModifiedMs, compiledTemplate});
    }
    return attachResolvedBinding(compiledTemplate, binding, resolvedFilePath, lastModifiedMs);
}

QVariantList loadCompiledRuleTemplates(const QVariantList& bindings, QString* errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    QVariantList compiledTemplates;
    for (const QVariant& bindingValue : bindings) {
        const QVariantMap binding = bindingValue.toMap();
        if (binding.isEmpty()) {
            continue;
        }

        QString templateError;
        const QVariantMap compiledTemplate = loadCompiledRuleTemplate(binding, &templateError);
        if (compiledTemplate.isEmpty()) {
            if (errorMessage) {
                *errorMessage = templateError;
            }
            return {};
        }
        compiledTemplates.append(compiledTemplate);
    }
    return compiledTemplates;
}

namespace {

int evaluationPriorityScore(const RuntimeRuleTemplateEvaluationResult& result)
{
    if (!result.matched) {
        return -1;
    }

    if (result.blocked) {
        if (result.stage == domain::strategy::RuleTemplateStage::Market
            || result.stage == domain::strategy::RuleTemplateStage::Signal
            || result.stage == domain::strategy::RuleTemplateStage::Entry) {
            return 500;
        }
        return 450;
    }
    if (result.resultType == domain::strategy::RuleTemplateResultType::Exit) {
        return 400;
    }
    if (result.resultType == domain::strategy::RuleTemplateResultType::Reduce) {
        return 300;
    }
        if (result.resultType == domain::strategy::RuleTemplateResultType::StateSwitch
            || result.resultType == domain::strategy::RuleTemplateResultType::Halt) {
        return 250;
    }
        if (result.resultType == domain::strategy::RuleTemplateResultType::CandidateEntry
            || result.resultType == domain::strategy::RuleTemplateResultType::Open) {
        return 200;
    }
    return 100;
}

int positiveIntegerFromVariant(const QVariant& value, int defaultValue = 0)
{
    if (!value.isValid() || value.isNull()) {
        return defaultValue;
    }

    bool ok = false;
    int parsedValue = value.toInt(&ok);
    if (!ok) {
        const double parsedDouble = value.toDouble(&ok);
        if (ok) {
            parsedValue = qRound(parsedDouble);
        }
    }
    return ok && parsedValue > 0 ? parsedValue : defaultValue;
}

double firstNumericMapValue(const QVariantMap& map,
                            std::initializer_list<const char*> keys,
                            double defaultValue = 0.0)
{
    for (const char* key : keys) {
        const QVariant value = map.value(QString::fromUtf8(key));
        if (!value.isValid() || value.isNull()) {
            continue;
        }
        bool ok = false;
        const double parsedValue = value.toDouble(&ok);
        if (ok) {
            return parsedValue;
        }
    }
    return defaultValue;
}

domain::strategy::GroupId normalizedBindingGroupIdValue(const QVariantMap& binding)
{
    return domain::strategy::GroupId(
        firstNonEmptyBindingValue(binding, {rule_template_strings::kBindingGroupId}).trimmed());
}

domain::strategy::RuleGroupOperator normalizedBindingGroupOperatorValue(const QVariantMap& binding)
{
    bool ok = false;
    const int groupOperator = binding.value(ruleTemplateString(rule_template_strings::kBindingGroupOperator)).toInt(&ok);
    if (!ok) {
        return domain::strategy::RuleGroupOperator::Any;
    }

    switch (static_cast<domain::strategy::RuleGroupOperator>(groupOperator)) {
    case domain::strategy::RuleGroupOperator::All:
    case domain::strategy::RuleGroupOperator::Any:
    case domain::strategy::RuleGroupOperator::MinimumMatch:
    case domain::strategy::RuleGroupOperator::FirstMatch:
    case domain::strategy::RuleGroupOperator::ScoreSum:
        return static_cast<domain::strategy::RuleGroupOperator>(groupOperator);
    }

    return domain::strategy::RuleGroupOperator::Any;
}

domain::strategy::RuleGroupRole normalizedBindingGroupRoleValue(const QVariantMap& binding)
{
    bool ok = false;
    const int groupRole = binding.value(ruleTemplateString(rule_template_strings::kBindingGroupRole)).toInt(&ok);
    if (!ok) {
        return domain::strategy::RuleGroupRole::Unspecified;
    }

    switch (static_cast<domain::strategy::RuleGroupRole>(groupRole)) {
    case domain::strategy::RuleGroupRole::Unspecified:
    case domain::strategy::RuleGroupRole::MustPass:
    case domain::strategy::RuleGroupRole::AnyPass:
    case domain::strategy::RuleGroupRole::Trigger:
    case domain::strategy::RuleGroupRole::ScoreBoost:
    case domain::strategy::RuleGroupRole::EntryGuard:
    case domain::strategy::RuleGroupRole::ExitGuard:
    case domain::strategy::RuleGroupRole::PositionManagement:
        return static_cast<domain::strategy::RuleGroupRole>(groupRole);
    }

    return domain::strategy::RuleGroupRole::Unspecified;
}

domain::strategy::GroupTitle normalizedBindingGroupTitleValue(const QVariantMap& binding)
{
    return domain::strategy::GroupTitle(
        firstNonEmptyBindingValue(binding, {rule_template_strings::kBindingGroupTitle}).trimmed());
}

int ruleBindingPhaseIndexFromStageName(const QString& stage)
{
    switch (parseRuleTemplateStage(stage)) {
    case domain::strategy::RuleTemplateStage::Market:
        return static_cast<int>(domain::strategy::RuleBindingPhase::Market);
    case domain::strategy::RuleTemplateStage::Signal:
        return static_cast<int>(domain::strategy::RuleBindingPhase::Signal);
    case domain::strategy::RuleTemplateStage::Entry:
        return static_cast<int>(domain::strategy::RuleBindingPhase::Entry);
    case domain::strategy::RuleTemplateStage::Rebalance:
        return static_cast<int>(domain::strategy::RuleBindingPhase::Rebalance);
    case domain::strategy::RuleTemplateStage::Exit:
        return static_cast<int>(domain::strategy::RuleBindingPhase::Exit);
    case domain::strategy::RuleTemplateStage::Risk:
        return static_cast<int>(domain::strategy::RuleBindingPhase::Risk);
    case domain::strategy::RuleTemplateStage::Watch:
        return static_cast<int>(domain::strategy::RuleBindingPhase::Watch);
    case domain::strategy::RuleTemplateStage::Unspecified:
    case domain::strategy::RuleTemplateStage::Eligibility:
    case domain::strategy::RuleTemplateStage::Portfolio:
    case domain::strategy::RuleTemplateStage::Execution:
    case domain::strategy::RuleTemplateStage::AccountRisk:
        return -1;
    }

    return -1;
}

bool parseSupportedRuleBindingPhaseIndex(const QVariant& value, int* phaseIndex)
{
    if (!phaseIndex || !value.isValid() || value.isNull() || value.typeId() == QMetaType::QString) {
        return false;
    }

    bool ok = false;
    const int parsedValue = value.toInt(&ok);
    if (!ok) {
        return false;
    }

    switch (static_cast<domain::strategy::RuleBindingPhase>(parsedValue)) {
    case domain::strategy::RuleBindingPhase::Market:
        *phaseIndex = parsedValue;
        return true;
    case domain::strategy::RuleBindingPhase::Signal:
        *phaseIndex = parsedValue;
        return true;
    case domain::strategy::RuleBindingPhase::Entry:
        *phaseIndex = parsedValue;
        return true;
    case domain::strategy::RuleBindingPhase::Rebalance:
        *phaseIndex = parsedValue;
        return true;
    case domain::strategy::RuleBindingPhase::Exit:
        *phaseIndex = parsedValue;
        return true;
    case domain::strategy::RuleBindingPhase::Risk:
        *phaseIndex = parsedValue;
        return true;
    case domain::strategy::RuleBindingPhase::Watch:
        *phaseIndex = parsedValue;
        return true;
    }

    return false;
}

QString ruleBindingPhaseName(domain::strategy::RuleBindingPhase phase)
{
    switch (phase) {
    case domain::strategy::RuleBindingPhase::Market:
        return ruleTemplateString(rule_template_strings::kScopeMarket);
    case domain::strategy::RuleBindingPhase::Signal:
        return ruleTemplateString(rule_template_strings::kStageSignal);
    case domain::strategy::RuleBindingPhase::Entry:
        return ruleTemplateString(rule_template_strings::kActionEntry);
    case domain::strategy::RuleBindingPhase::Rebalance:
        return ruleTemplateString(rule_template_strings::kStageRebalance);
    case domain::strategy::RuleBindingPhase::Exit:
        return ruleTemplateString(rule_template_strings::kActionExit);
    case domain::strategy::RuleBindingPhase::Risk:
        return ruleTemplateString(rule_template_strings::kStageRisk);
    case domain::strategy::RuleBindingPhase::Watch:
        return ruleTemplateString(rule_template_strings::kStageWatch);
    }

    return {};
}

domain::strategy::RuleBindingPhase normalizedBindingPhaseValue(const QVariantMap& binding)
{
    int phaseIndex = -1;
    if (parseSupportedRuleBindingPhaseIndex(
            binding.value(ruleTemplateString(rule_template_strings::kBindingPhase)),
            &phaseIndex)) {
        return static_cast<domain::strategy::RuleBindingPhase>(phaseIndex);
    }
    return domain::strategy::RuleBindingPhase::Signal;
}

QString ruleGroupOperatorName(domain::strategy::RuleGroupOperator groupOperator)
{
    switch (groupOperator) {
    case domain::strategy::RuleGroupOperator::All:
        return ruleTemplateString(rule_template_strings::kGroupOperatorAll);
    case domain::strategy::RuleGroupOperator::MinimumMatch:
        return ruleTemplateString(rule_template_strings::kGroupOperatorAtLeast);
    case domain::strategy::RuleGroupOperator::FirstMatch:
        return ruleTemplateString(rule_template_strings::kGroupOperatorFirstMatch);
    case domain::strategy::RuleGroupOperator::ScoreSum:
        return ruleTemplateString(rule_template_strings::kGroupOperatorScoreSum);
    case domain::strategy::RuleGroupOperator::Any:
        return ruleTemplateString(rule_template_strings::kGroupOperatorAny);
    }

    return ruleTemplateString(rule_template_strings::kGroupOperatorAny);
}

QString ruleGroupRoleName(domain::strategy::RuleGroupRole groupRole)
{
    switch (groupRole) {
    case domain::strategy::RuleGroupRole::MustPass:
        return ruleTemplateString(rule_template_strings::kGroupRoleMustPass);
    case domain::strategy::RuleGroupRole::AnyPass:
        return ruleTemplateString(rule_template_strings::kGroupRoleAnyPass);
    case domain::strategy::RuleGroupRole::Trigger:
        return ruleTemplateString(rule_template_strings::kGroupRoleTrigger);
    case domain::strategy::RuleGroupRole::ScoreBoost:
        return ruleTemplateString(rule_template_strings::kGroupRoleScoreBoost);
    case domain::strategy::RuleGroupRole::EntryGuard:
        return ruleTemplateString(rule_template_strings::kGroupRoleEntryGuard);
    case domain::strategy::RuleGroupRole::ExitGuard:
        return ruleTemplateString(rule_template_strings::kGroupRoleExitGuard);
    case domain::strategy::RuleGroupRole::PositionManagement:
        return ruleTemplateString(rule_template_strings::kGroupRolePositionManagement);
    case domain::strategy::RuleGroupRole::Unspecified:
        return {};
    }

    return {};
}

int normalizedBindingGroupMatchThreshold(const QVariantMap& binding)
{
    static const std::initializer_list<const char*> thresholdKeys{
        rule_template_strings::kBindingGroupMinMatchCount
    };

    for (const char* key : thresholdKeys) {
        const int threshold = positiveIntegerFromVariant(binding.value(QString::fromUtf8(key)));
        if (threshold > 0) {
            return threshold;
        }
    }
    return 0;
}

double selectionBonusFromPayload(const QVariantMap& payload)
{
    return firstNumericMapValue(
        payload,
        {rule_template_strings::kFieldScore},
        0.0);
}

bool isMandatoryGroupRole(domain::strategy::RuleGroupRole groupRole)
{
    return groupRole == domain::strategy::RuleGroupRole::MustPass
        || groupRole == domain::strategy::RuleGroupRole::EntryGuard;
}

bool isTriggerGroupRole(domain::strategy::RuleGroupRole groupRole)
{
    return groupRole == domain::strategy::RuleGroupRole::AnyPass
        || groupRole == domain::strategy::RuleGroupRole::Trigger
        || groupRole == domain::strategy::RuleGroupRole::ExitGuard;
}

bool isNeutralGroupRole(domain::strategy::RuleGroupRole groupRole)
{
    return groupRole == domain::strategy::RuleGroupRole::ScoreBoost
        || groupRole == domain::strategy::RuleGroupRole::PositionManagement;
}

bool stageAppliesToCandidateAction(domain::strategy::RuleTemplateStage stage,
                                   domain::strategy::CandidateAction candidateAction)
{
    if (stage == domain::strategy::RuleTemplateStage::Unspecified
            || candidateAction == domain::strategy::CandidateAction::None) {
        return true;
    }

    if (isEntryLikeCandidateAction(candidateAction)) {
        return stage != domain::strategy::RuleTemplateStage::Rebalance
            && stage != domain::strategy::RuleTemplateStage::Exit;
    }

    if (isExitLikeCandidateAction(candidateAction)) {
        return stage != domain::strategy::RuleTemplateStage::Market
            && stage != domain::strategy::RuleTemplateStage::Eligibility
            && stage != domain::strategy::RuleTemplateStage::Entry;
    }

    return true;
}

bool roleAppliesToCandidateAction(domain::strategy::RuleGroupRole groupRole,
                                  domain::strategy::CandidateAction candidateAction,
                                  domain::strategy::RuleTemplateStage stage)
{
    if (candidateAction == domain::strategy::CandidateAction::None) {
        return true;
    }

    if (groupRole == domain::strategy::RuleGroupRole::Unspecified) {
        return stageAppliesToCandidateAction(stage, candidateAction);
    }

    if (groupRole == domain::strategy::RuleGroupRole::MustPass
            || groupRole == domain::strategy::RuleGroupRole::AnyPass) {
        return stageAppliesToCandidateAction(stage, candidateAction);
    }

    if (groupRole == domain::strategy::RuleGroupRole::Trigger
            || groupRole == domain::strategy::RuleGroupRole::ScoreBoost
            || groupRole == domain::strategy::RuleGroupRole::EntryGuard) {
        return isEntryLikeCandidateAction(candidateAction);
    }

    if (groupRole == domain::strategy::RuleGroupRole::ExitGuard) {
        return isExitLikeCandidateAction(candidateAction);
    }

    return stageAppliesToCandidateAction(stage, candidateAction);
}

struct BindingApplicability {
    bool applies = true;
    QString reason;
};

BindingApplicability bindingApplicability(const QVariantMap& binding,
                                          const RuntimeRuleTemplateEvaluationContext& context)
{
    const domain::strategy::RuleTemplateStage stage =
        ruleTemplateStageFromBindingPhase(normalizedBindingPhaseValue(binding));
    const domain::strategy::CandidateAction candidateAction = context.candidateAction;
    if (!stageAppliesToCandidateAction(stage, candidateAction)) {
        return {false, ruleTemplateString(rule_template_strings::kApplicabilityStageFiltered)};
    }
    if (!roleAppliesToCandidateAction(
            normalizedBindingGroupRoleValue(binding),
            candidateAction,
            stage)) {
        return {false, ruleTemplateString(rule_template_strings::kApplicabilityRoleFiltered)};
    }
    return {};
}

RuntimeRuleTemplateEvaluationResult selectBestMatchedResult(
    const std::vector<RuntimeRuleTemplateEvaluationResult>& results)
{
    RuntimeRuleTemplateEvaluationResult bestResult;
    int bestScore = -1;
    for (const RuntimeRuleTemplateEvaluationResult& currentResult : results) {
        if (!currentResult.matched) {
            continue;
        }

        const int currentScore = evaluationPriorityScore(currentResult);
        if (!bestResult.matched || currentScore > bestScore) {
            bestScore = currentScore;
            bestResult = currentResult;
        }
    }
    return bestResult;
}

RuntimeRuleTemplateEvaluationResult templatePresenceOnlyResult(
    const RuntimeRuleTemplateEvaluationResult& source)
{
    RuntimeRuleTemplateEvaluationResult fallbackResult = source;
    fallbackResult.matched = false;
    fallbackResult.blocked = false;
    fallbackResult.actionPermitted = true;
    fallbackResult.stage = domain::strategy::RuleTemplateStage::Unspecified;
    fallbackResult.ruleId = {};
    fallbackResult.reasonCode = {};
    fallbackResult.message.clear();
    fallbackResult.resultType = domain::strategy::RuleTemplateResultType::Unspecified;
    fallbackResult.payload.clear();
    fallbackResult.state.clear();
    return fallbackResult;
}

struct GroupedTemplateEvaluation {
    domain::strategy::RuleBindingPhase stage{domain::strategy::RuleBindingPhase::Signal};
    domain::strategy::GroupId groupId;
    domain::strategy::GroupTitle groupTitle;
    domain::strategy::RuleGroupRole groupRole{domain::strategy::RuleGroupRole::Unspecified};
    domain::strategy::RuleGroupOperator groupOperator{domain::strategy::RuleGroupOperator::Any};
    int matchThreshold = 0;
    int totalMembers = 0;
    int applicableMembers = 0;
    QString skipReason;
    std::vector<RuntimeRuleTemplateEvaluationResult> members;
};

constexpr int kRuleTemplateStageScoreSlotCount =
    static_cast<int>(domain::strategy::RuleTemplateStage::AccountRisk) + 1;

int ruleTemplateStageScoreSlot(domain::strategy::RuleTemplateStage stage)
{
    const int slot = static_cast<int>(stage);
    return slot >= 0 && slot < kRuleTemplateStageScoreSlotCount ? slot : -1;
}

double stageScoreBoostAt(
    const std::array<double, kRuleTemplateStageScoreSlotCount>& stageScoreBoosts,
    domain::strategy::RuleTemplateStage stage)
{
    const int slot = ruleTemplateStageScoreSlot(stage);
    return slot >= 0 ? stageScoreBoosts[static_cast<std::size_t>(slot)] : 0.0;
}

void addStageScoreBoost(
    std::array<double, kRuleTemplateStageScoreSlotCount>* stageScoreBoosts,
    domain::strategy::RuleTemplateStage stage,
    double scoreBoost)
{
    if (!stageScoreBoosts) {
        return;
    }

    const int slot = ruleTemplateStageScoreSlot(stage);
    if (slot >= 0) {
        (*stageScoreBoosts)[static_cast<std::size_t>(slot)] += scoreBoost;
    }
}

int findGroupedEvaluationIndex(const std::vector<GroupedTemplateEvaluation>& groupedResults,
                               domain::strategy::RuleBindingPhase stage,
                               const domain::strategy::GroupId& groupId)
{
    for (std::size_t index = 0; index < groupedResults.size(); ++index) {
        const GroupedTemplateEvaluation& groupedEvaluation = groupedResults[index];
        if (groupedEvaluation.stage == stage && groupedEvaluation.groupId == groupId) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int matchedMemberCount(const GroupedTemplateEvaluation& groupedEvaluation)
{
    int matchedCount = 0;
    for (const RuntimeRuleTemplateEvaluationResult& memberResult : groupedEvaluation.members) {
        if (memberResult.matched) {
            ++matchedCount;
        }
    }
    return matchedCount;
}

int resolvedGroupMatchThreshold(const GroupedTemplateEvaluation& groupedEvaluation)
{
    if (groupedEvaluation.applicableMembers <= 0) {
        return 0;
    }
    const int configuredThreshold = groupedEvaluation.matchThreshold > 0 ? groupedEvaluation.matchThreshold : 1;
    return (std::min)(groupedEvaluation.applicableMembers, configuredThreshold);
}

double matchedScoreSum(const GroupedTemplateEvaluation& groupedEvaluation)
{
    double scoreSum = 0.0;
    for (const RuntimeRuleTemplateEvaluationResult& memberResult : groupedEvaluation.members) {
        if (!memberResult.matched) {
            continue;
        }
        scoreSum += selectionBonusFromPayload(memberResult.payload);
    }
    return scoreSum;
}

RuntimeRuleTemplateEvaluationResult selectGroupMatchedResult(
    const GroupedTemplateEvaluation& groupedEvaluation)
{
    RuntimeRuleTemplateEvaluationResult selectedResult;
    if (groupedEvaluation.groupOperator == domain::strategy::RuleGroupOperator::FirstMatch) {
        for (const RuntimeRuleTemplateEvaluationResult& memberResult : groupedEvaluation.members) {
            if (memberResult.matched) {
                selectedResult = memberResult;
                break;
            }
        }
    } else {
        selectedResult = selectBestMatchedResult(groupedEvaluation.members);
    }

    if (!selectedResult.matched) {
        return selectedResult;
    }

    selectedResult.binding.insert(ruleTemplateString(rule_template_strings::kBindingGroupId), groupedEvaluation.groupId.text());
    selectedResult.binding.insert(ruleTemplateString(rule_template_strings::kBindingGroupTitle), groupedEvaluation.groupTitle.text());
    selectedResult.binding.insert(ruleTemplateString(rule_template_strings::kBindingGroupRole), static_cast<int>(groupedEvaluation.groupRole));
    selectedResult.binding.insert(ruleTemplateString(rule_template_strings::kBindingGroupOperator), static_cast<int>(groupedEvaluation.groupOperator));
    selectedResult.binding.insert(
        ruleTemplateString(rule_template_strings::kBindingPhase),
        static_cast<int>(groupedEvaluation.stage));

    if (groupedEvaluation.groupOperator == domain::strategy::RuleGroupOperator::ScoreSum) {
        selectedResult.payload.insert(
            ruleTemplateString(rule_template_strings::kFieldScore),
            matchedScoreSum(groupedEvaluation));
    }

    return selectedResult;
}

double candidateSelectionScore(const RuntimeRuleTemplateEvaluationResult& result,
                               const std::array<double, kRuleTemplateStageScoreSlotCount>& stageScoreBoosts)
{
    if (!result.matched) {
        return -1.0;
    }

    return static_cast<double>(evaluationPriorityScore(result))
        + selectionBonusFromPayload(result.payload)
        + stageScoreBoostAt(stageScoreBoosts, result.stage);
}

QString groupDisplayName(const GroupedTemplateEvaluation& groupedEvaluation)
{
    const QString groupTitle = groupedEvaluation.groupTitle.text();
    const QString groupRole = ruleGroupRoleName(groupedEvaluation.groupRole);
    if (!groupTitle.isEmpty() && !groupRole.isEmpty()) {
        return groupTitle + ruleTemplateString(rule_template_strings::kSeparatorScopeDisplay) + groupRole;
    }
    if (!groupTitle.isEmpty()) {
        return groupTitle;
    }
    if (!groupRole.isEmpty()) {
        return groupRole;
    }
    return groupedEvaluation.groupId.text();
}

RuntimeRuleTemplateEvaluationResult adjudicationBlockedResult(
    const RuntimeRuleTemplateEvaluationResult& fallbackResult,
    const GroupedTemplateEvaluation& groupedEvaluation,
    const domain::strategy::ReasonCode& reasonCode,
    const QString& message)
{
    RuntimeRuleTemplateEvaluationResult result = fallbackResult;
    if (!groupedEvaluation.members.empty()) {
        result = templatePresenceOnlyResult(groupedEvaluation.members.front());
    }
    result.hasTemplate = result.hasTemplate || groupedEvaluation.groupId.isValid();
    result.matched = false;
    result.blocked = false;
    result.actionPermitted = false;
    result.stage = ruleTemplateStageFromBindingPhase(groupedEvaluation.stage);
    result.reasonCode = reasonCode;
    result.message = message;
    result.resultType = domain::strategy::RuleTemplateResultType::Unspecified;
    result.payload.clear();
    result.state.clear();
    result.binding.insert(ruleTemplateString(rule_template_strings::kBindingGroupId), groupedEvaluation.groupId.text());
    result.binding.insert(ruleTemplateString(rule_template_strings::kBindingGroupTitle), groupedEvaluation.groupTitle.text());
    result.binding.insert(ruleTemplateString(rule_template_strings::kBindingGroupRole), static_cast<int>(groupedEvaluation.groupRole));
    result.binding.insert(ruleTemplateString(rule_template_strings::kBindingGroupOperator), static_cast<int>(groupedEvaluation.groupOperator));
    result.binding.insert(
        ruleTemplateString(rule_template_strings::kBindingPhase),
        static_cast<int>(groupedEvaluation.stage));
    return result;
}

QVariantMap buildGroupDecision(const GroupedTemplateEvaluation& groupedEvaluation)
{
    QVariantMap decision;
    const int matchedCount = matchedMemberCount(groupedEvaluation);
    const int threshold = resolvedGroupMatchThreshold(groupedEvaluation);
    const double aggregatedScore = matchedScoreSum(groupedEvaluation);
    decision.insert(ruleTemplateString(rule_template_strings::kFieldStage), ruleBindingPhaseName(groupedEvaluation.stage));
    decision.insert(ruleTemplateString(rule_template_strings::kComposerGroupId), groupedEvaluation.groupId.text());
    if (groupedEvaluation.groupTitle.isValid()) {
        decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldGroupTitle), groupedEvaluation.groupTitle.text());
    }
    const QString groupRole = ruleGroupRoleName(groupedEvaluation.groupRole);
    if (!groupRole.isEmpty()) {
        decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldGroupRole), groupRole);
    }
    const QString serializedGroupOperator = ruleGroupOperatorName(groupedEvaluation.groupOperator);
    if (!serializedGroupOperator.isEmpty()) {
        decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldGroupOperator), serializedGroupOperator);
    }

    const int filteredCount = (std::max)(0, groupedEvaluation.totalMembers - groupedEvaluation.applicableMembers);

    decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldMemberCount), groupedEvaluation.totalMembers);
    decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldApplicableCount), groupedEvaluation.applicableMembers);
    decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldMatchedCount), matchedCount);
    decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldFilteredCount), filteredCount);
    if (groupedEvaluation.groupOperator == domain::strategy::RuleGroupOperator::MinimumMatch) {
        decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldMatchThreshold), threshold);
    }
    if (groupedEvaluation.groupOperator == domain::strategy::RuleGroupOperator::ScoreSum) {
        decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldAggregatedScore), aggregatedScore);
    }

    if (groupedEvaluation.applicableMembers <= 0) {
        decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldDisposition), ruleTemplateString(rule_template_strings::kDecisionDispositionSkipped));
        decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldOutcome), ruleTemplateString(rule_template_strings::kDecisionOutcomeNotApplicable));
        decision.insert(
            ruleTemplateString(rule_template_strings::kDecisionFieldSkipReason),
            groupedEvaluation.skipReason.isEmpty()
                ? ruleTemplateString(rule_template_strings::kApplicabilityRoleFiltered)
                : groupedEvaluation.skipReason);
        return decision;
    }

    decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldDisposition), ruleTemplateString(rule_template_strings::kDecisionDispositionConsidered));
    if (matchedCount <= 0) {
        decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldOutcome), ruleTemplateString(rule_template_strings::kDecisionOutcomeNotMatched));
        return decision;
    }

    if (groupedEvaluation.groupOperator == domain::strategy::RuleGroupOperator::All
            && matchedCount < groupedEvaluation.applicableMembers) {
        decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldOutcome), ruleTemplateString(rule_template_strings::kDecisionOutcomeIncomplete));
        decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldSkipReason), ruleTemplateString(rule_template_strings::kDecisionReasonGroupIncomplete));
    } else if (groupedEvaluation.groupOperator == domain::strategy::RuleGroupOperator::MinimumMatch
               && matchedCount < threshold) {
        decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldOutcome), ruleTemplateString(rule_template_strings::kDecisionOutcomeIncomplete));
        decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldSkipReason), ruleTemplateString(rule_template_strings::kDecisionReasonGroupThresholdUnmet));
    } else {
        decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldOutcome), ruleTemplateString(rule_template_strings::kDecisionOutcomeMatched));
    }

    const RuntimeRuleTemplateEvaluationResult selectedResult =
        selectGroupMatchedResult(groupedEvaluation);
    if (selectedResult.matched) {
        if (!selectedResult.ruleId.isEmpty()) {
            decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldMatchedRuleId), selectedResult.ruleId.text());
        }
        if (selectedResult.resultType != domain::strategy::RuleTemplateResultType::Unspecified) {
            decision.insert(
                ruleTemplateString(rule_template_strings::kDecisionFieldMatchedResultType),
                ruleTemplateResultTypeName(selectedResult.resultType));
        }
        if (!selectedResult.reasonCode.isEmpty()) {
            decision.insert(ruleTemplateString(rule_template_strings::kDecisionFieldMatchedReasonCode), selectedResult.reasonCode.text());
        }
        decision.insert(
            ruleTemplateString(rule_template_strings::kDecisionFieldSelectedBy),
            groupedEvaluation.groupOperator == domain::strategy::RuleGroupOperator::FirstMatch
                ? ruleTemplateString(rule_template_strings::kGroupOperatorFirstMatch)
                : (groupedEvaluation.groupOperator == domain::strategy::RuleGroupOperator::ScoreSum
                    ? ruleTemplateString(rule_template_strings::kGroupOperatorScoreSum)
                    : ruleTemplateString(rule_template_strings::kFieldPriority)));
    }
    return decision;
}

bool groupSatisfied(const GroupedTemplateEvaluation& groupedEvaluation)
{
    if (groupedEvaluation.applicableMembers <= 0) {
        return false;
    }

    const int matchedCount = matchedMemberCount(groupedEvaluation);
    if (groupedEvaluation.groupOperator == domain::strategy::RuleGroupOperator::All) {
        return matchedCount >= groupedEvaluation.applicableMembers;
    }
    if (groupedEvaluation.groupOperator == domain::strategy::RuleGroupOperator::MinimumMatch) {
        return matchedCount >= resolvedGroupMatchThreshold(groupedEvaluation);
    }
    return matchedCount > 0;
}

} // namespace

RuntimeRuleTemplateEvaluationResult evaluateRuleTemplate(
    const QVariantMap& compiledTemplate,
    const RuntimeRuleTemplateEvaluationContext& context)
{
    RuntimeRuleTemplateEvaluationResult result;
    if (compiledTemplate.isEmpty()) {
        return result;
    }

    result.hasTemplate = true;
    result.binding = compiledTemplate.value(ruleTemplateString(rule_template_strings::kCompiledTemplateBinding)).toMap();
    result.templateNamespace = domain::strategy::NamespaceId(
        compiledTemplate.value(ruleTemplateString(rule_template_strings::kFieldNamespace)).toString().trimmed());
    result.templateFilePath = domain::strategy::FilePathToken(
        compiledTemplate.value(ruleTemplateString(rule_template_strings::kCompiledTemplateFilePath)).toString().trimmed());

    const QVariantMap scopes = buildScopes(context);
    QVariantList rules = compiledTemplate.value(ruleTemplateString(rule_template_strings::kFieldRules)).toList();
    std::stable_sort(rules.begin(), rules.end(), [](const QVariant& left, const QVariant& right) {
        return left.toMap().value(ruleTemplateString(rule_template_strings::kFieldPriority)).toInt()
            > right.toMap().value(ruleTemplateString(rule_template_strings::kFieldPriority)).toInt();
    });

    for (const QVariant& ruleValue : rules) {
        const QVariantMap rule = ruleValue.toMap();
        if (rule.isEmpty()) {
            continue;
        }

        const QVariantMap whenBlock = rule.value(ruleTemplateString(rule_template_strings::kFieldWhen)).toMap();
        if (whenBlock.isEmpty() || !evaluateConditionNode(whenBlock, scopes)) {
            continue;
        }

        const QVariantMap thenBlock = rule.value(ruleTemplateString(rule_template_strings::kFieldThen)).toMap();
        result.matched = true;
        result.stage = parseRuleTemplateStage(
            rule.value(ruleTemplateString(rule_template_strings::kFieldStage)).toString());
        result.ruleId = domain::strategy::RuleTemplateId(
            rule.value(ruleTemplateString(rule_template_strings::kFieldId)).toString().trimmed());
        result.reasonCode = domain::strategy::ReasonCode(
            thenBlock.value(ruleTemplateString(rule_template_strings::kFieldReasonCode)).toString().trimmed());
        result.message = thenBlock.value(ruleTemplateString(rule_template_strings::kFieldMessage)).toString().trimmed();
        result.resultType = normalizedResultType(thenBlock);
        result.payload = thenBlock.value(ruleTemplateString(rule_template_strings::kFieldPayload)).toMap();
        if (thenBlock.contains(ruleTemplateString(rule_template_strings::kFieldScore))
                && !result.payload.contains(ruleTemplateString(rule_template_strings::kFieldScore))) {
            const QVariant resolvedScore = resolveExpressionValue(
                thenBlock.value(ruleTemplateString(rule_template_strings::kFieldScore)),
                scopes);
            if (resolvedScore.isValid() && !resolvedScore.isNull()) {
                result.payload.insert(ruleTemplateString(rule_template_strings::kFieldScore), resolvedScore);
            }
        }
        result.state = thenBlock.value(ruleTemplateString(rule_template_strings::kFieldState)).toMap();
        result.blocked = isBlockingTemplateResult(
            thenBlock,
            context.candidateAction);
        result.actionPermitted = !result.blocked;
        return result;
    }

    return result;
}

RuntimeRuleTemplateEvaluationResult evaluateRuleTemplates(
    const QVariantList& compiledTemplates,
    const RuntimeRuleTemplateEvaluationContext& context)
{
    RuntimeRuleTemplateEvaluationResult fallbackResult;
    std::vector<RuntimeRuleTemplateEvaluationResult> candidateResults;
    std::vector<GroupedTemplateEvaluation> groupedResults;
    std::array<double, kRuleTemplateStageScoreSlotCount> stageScoreBoosts{};
    QVariantList groupDecisions;

    for (const QVariant& compiledTemplateValue : compiledTemplates) {
        const QVariantMap compiledTemplate = compiledTemplateValue.toMap();
        if (compiledTemplate.isEmpty()) {
            continue;
        }

        const RuntimeRuleTemplateEvaluationResult currentResult = evaluateRuleTemplate(compiledTemplate, context);
        if (!currentResult.hasTemplate && !currentResult.matched) {
            continue;
        }

        if (!fallbackResult.hasTemplate && currentResult.hasTemplate) {
            fallbackResult = templatePresenceOnlyResult(currentResult);
        }

        const domain::strategy::GroupId groupId = normalizedBindingGroupIdValue(currentResult.binding);
        const domain::strategy::RuleBindingPhase bindingPhase = normalizedBindingPhaseValue(currentResult.binding);
        const BindingApplicability applicability = bindingApplicability(currentResult.binding, context);
        if (groupId.isEmpty()) {
            if (applicability.applies && currentResult.matched) {
                candidateResults.push_back(currentResult);
            }
            continue;
        }

        int groupIndex = findGroupedEvaluationIndex(groupedResults, bindingPhase, groupId);
        if (groupIndex < 0) {
            GroupedTemplateEvaluation groupedEvaluation;
            groupedEvaluation.stage = bindingPhase;
            groupedEvaluation.groupId = groupId;
            groupedEvaluation.groupTitle = normalizedBindingGroupTitleValue(currentResult.binding);
            groupedEvaluation.groupRole = normalizedBindingGroupRoleValue(currentResult.binding);
            groupedEvaluation.groupOperator = normalizedBindingGroupOperatorValue(currentResult.binding);
            groupedEvaluation.matchThreshold = normalizedBindingGroupMatchThreshold(currentResult.binding);
            groupIndex = static_cast<int>(groupedResults.size());
            groupedResults.push_back(std::move(groupedEvaluation));
        }

        GroupedTemplateEvaluation& groupedEvaluation = groupedResults[static_cast<std::size_t>(groupIndex)];
        ++groupedEvaluation.totalMembers;
        if (!applicability.applies) {
            if (groupedEvaluation.skipReason.isEmpty()) {
                groupedEvaluation.skipReason = applicability.reason;
            }
            continue;
        }

        ++groupedEvaluation.applicableMembers;
        groupedEvaluation.members.push_back(currentResult);
    }

    RuntimeRuleTemplateEvaluationResult bestBlockingResult;
    int bestBlockingScore = -1;
    for (const GroupedTemplateEvaluation& groupedEvaluation : groupedResults) {
        groupDecisions.append(buildGroupDecision(groupedEvaluation));

        const bool groupMatched = groupSatisfied(groupedEvaluation);
        if (groupMatched && groupedEvaluation.groupRole == domain::strategy::RuleGroupRole::ScoreBoost) {
            addStageScoreBoost(
                &stageScoreBoosts,
                ruleTemplateStageFromBindingPhase(groupedEvaluation.stage),
                matchedScoreSum(groupedEvaluation));
        }

        if (!groupMatched) {
            continue;
        }

        const RuntimeRuleTemplateEvaluationResult groupedResult =
            selectGroupMatchedResult(groupedEvaluation);
        if (groupedResult.matched) {
            candidateResults.push_back(groupedResult);
            if (groupedResult.blocked) {
                const int blockingScore = evaluationPriorityScore(groupedResult);
                if (!bestBlockingResult.matched || blockingScore > bestBlockingScore) {
                    bestBlockingScore = blockingScore;
                    bestBlockingResult = groupedResult;
                }
            }
        }
    }

    if (!bestBlockingResult.matched) {
        for (const RuntimeRuleTemplateEvaluationResult& candidateResult : candidateResults) {
            if (!candidateResult.matched || !candidateResult.blocked) {
                continue;
            }
            const int blockingScore = evaluationPriorityScore(candidateResult);
            if (!bestBlockingResult.matched || blockingScore > bestBlockingScore) {
                bestBlockingScore = blockingScore;
                bestBlockingResult = candidateResult;
            }
        }
    }

    if (bestBlockingResult.matched) {
        bestBlockingResult.groupDecisions = groupDecisions;
        bestBlockingResult.actionPermitted = false;
        return bestBlockingResult;
    }

    const GroupedTemplateEvaluation* firstUnmetMandatoryGroup = nullptr;
    const GroupedTemplateEvaluation* firstTriggerGroup = nullptr;
    bool hasApplicableTriggerGroup = false;
    bool hasMatchedTriggerGroup = false;
    std::vector<RuntimeRuleTemplateEvaluationResult> triggerResults;
    std::vector<RuntimeRuleTemplateEvaluationResult> nonNeutralResults;
    for (const GroupedTemplateEvaluation& groupedEvaluation : groupedResults) {
        const bool groupMatched = groupSatisfied(groupedEvaluation);
        if (isMandatoryGroupRole(groupedEvaluation.groupRole)
                && groupedEvaluation.applicableMembers > 0
                && !groupMatched
                && !firstUnmetMandatoryGroup) {
            firstUnmetMandatoryGroup = &groupedEvaluation;
        }
        if (isTriggerGroupRole(groupedEvaluation.groupRole) && groupedEvaluation.applicableMembers > 0) {
            hasApplicableTriggerGroup = true;
            if (!firstTriggerGroup) {
                firstTriggerGroup = &groupedEvaluation;
            }
            if (groupMatched) {
                hasMatchedTriggerGroup = true;
                const RuntimeRuleTemplateEvaluationResult groupedResult = selectGroupMatchedResult(groupedEvaluation);
                if (groupedResult.matched) {
                    triggerResults.push_back(groupedResult);
                }
            }
        }
    }

    for (const RuntimeRuleTemplateEvaluationResult& candidateResult : candidateResults) {
        const domain::strategy::RuleGroupRole groupRole = normalizedBindingGroupRoleValue(candidateResult.binding);
        if (!candidateResult.matched || isNeutralGroupRole(groupRole) || candidateResult.blocked) {
            continue;
        }
        nonNeutralResults.push_back(candidateResult);
    }

    if (firstUnmetMandatoryGroup) {
        RuntimeRuleTemplateEvaluationResult gatedResult = adjudicationBlockedResult(
            fallbackResult,
            *firstUnmetMandatoryGroup,
            domain::strategy::ReasonCode(ruleTemplateString(rule_template_strings::kRuntimeReasonMustPassUnmet)),
            QStringLiteral("必须满足组未全部通过: %1").arg(groupDisplayName(*firstUnmetMandatoryGroup)));
        gatedResult.groupDecisions = groupDecisions;
        return gatedResult;
    }

    if (hasApplicableTriggerGroup && !hasMatchedTriggerGroup && firstTriggerGroup) {
        RuntimeRuleTemplateEvaluationResult gatedResult = adjudicationBlockedResult(
            fallbackResult,
            *firstTriggerGroup,
            domain::strategy::ReasonCode(ruleTemplateString(rule_template_strings::kRuntimeReasonAnyPassUnmet)),
            QStringLiteral("任一满足组尚未命中: %1").arg(groupDisplayName(*firstTriggerGroup)));
        gatedResult.groupDecisions = groupDecisions;
        return gatedResult;
    }

    RuntimeRuleTemplateEvaluationResult bestResult = fallbackResult;
    double bestScore = -1.0;
    const std::vector<RuntimeRuleTemplateEvaluationResult>& preferredResults =
        hasMatchedTriggerGroup ? triggerResults : nonNeutralResults;
    const std::vector<RuntimeRuleTemplateEvaluationResult>& resultsToSelect =
        preferredResults.empty() ? candidateResults : preferredResults;
    for (const RuntimeRuleTemplateEvaluationResult& candidateResult : resultsToSelect) {
        const double currentScore = candidateSelectionScore(candidateResult, stageScoreBoosts);
        if (!bestResult.matched || currentScore > bestScore) {
            bestScore = currentScore;
            bestResult = candidateResult;
        }
    }

    if (bestResult.hasTemplate || !groupDecisions.isEmpty()) {
        bestResult.groupDecisions = groupDecisions;
    }

    return bestResult;
}

} // namespace bridge::rules