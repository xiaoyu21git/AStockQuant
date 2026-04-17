#include "RuleTemplateRuntimeEvaluator.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QReadWriteLock>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace bridge::rules {
namespace {

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
        if (dir.exists(QStringLiteral("astock_engine/rules/examples"))) {
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
        {"file_path", "filePath", "template_file_path", "templateFilePath"});
    QFileInfo fileInfo(explicitFilePath);
    if (!explicitFilePath.isEmpty() && fileInfo.exists()) {
        return fileInfo.canonicalFilePath();
    }

    const QString fileName = firstNonEmptyBindingValue(
        rawBinding,
        {"file_name", "fileName", "template_file_name", "templateFileName"});
    if (fileName.isEmpty()) {
        return {};
    }

    const QString repositoryRoot = resolveRepositoryRoot();
    if (repositoryRoot.isEmpty()) {
        return {};
    }

    const QFileInfo fallbackInfo(
        QDir(repositoryRoot).filePath(QStringLiteral("astock_engine/rules/examples/%1").arg(fileName)));
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
        binding.insert(QStringLiteral("filePath"), resolvedFilePath);
        binding.insert(QStringLiteral("file_path"), resolvedFilePath);
    }

    const QString fileName = QFileInfo(resolvedFilePath).fileName();
    if (!fileName.isEmpty()) {
        binding.insert(QStringLiteral("fileName"), fileName);
        binding.insert(QStringLiteral("file_name"), fileName);
    }
    return binding;
}

QVariantMap attachResolvedBinding(const QVariantMap& compiledTemplate,
                                 const QVariantMap& rawBinding,
                                 const QString& resolvedFilePath,
                                 qint64 lastModifiedMs)
{
    QVariantMap hydratedTemplate = compiledTemplate;
    hydratedTemplate.insert(QStringLiteral("_binding"), normalizeBinding(rawBinding, resolvedFilePath));
    hydratedTemplate.insert(QStringLiteral("_filePath"), resolvedFilePath);
    hydratedTemplate.insert(QStringLiteral("_lastModifiedMs"), lastModifiedMs);
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

QString normalizedResultType(const QVariantMap& thenBlock)
{
    return thenBlock.value(QStringLiteral("result")).toString().trimmed().toLower();
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
        if (text.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0
                || text == QStringLiteral("0")) {
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
    if (expressionMap.size() == 1 && expressionMap.contains(QStringLiteral("var"))) {
        return resolveScopedValue(scopes, expressionMap.value(QStringLiteral("var")).toString().trimmed());
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
        if (op == QStringLiteral("eq")) {
            return std::fabs(leftNumber - rightNumber) <= 1e-12;
        }
        if (op == QStringLiteral("ne")) {
            return std::fabs(leftNumber - rightNumber) > 1e-12;
        }
        if (op == QStringLiteral("lt")) {
            return leftNumber < rightNumber;
        }
        if (op == QStringLiteral("le")) {
            return leftNumber <= rightNumber;
        }
        if (op == QStringLiteral("gt")) {
            return leftNumber > rightNumber;
        }
        if (op == QStringLiteral("ge")) {
            return leftNumber >= rightNumber;
        }
    }

    const QString leftText = left.toString().trimmed();
    const QString rightText = right.toString().trimmed();
    if (op == QStringLiteral("eq")) {
        return leftText.compare(rightText, Qt::CaseInsensitive) == 0;
    }
    if (op == QStringLiteral("ne")) {
        return leftText.compare(rightText, Qt::CaseInsensitive) != 0;
    }
    if (op == QStringLiteral("lt")) {
        return leftText < rightText;
    }
    if (op == QStringLiteral("le")) {
        return leftText <= rightText;
    }
    if (op == QStringLiteral("gt")) {
        return leftText > rightText;
    }
    if (op == QStringLiteral("ge")) {
        return leftText >= rightText;
    }
    return false;
}

bool evaluateConditionNode(const QVariantMap& node, const QVariantMap& scopes)
{
    const QString op = node.value(QStringLiteral("op")).toString().trimmed().toLower();
    if (op.isEmpty()) {
        return false;
    }

    if (op == QStringLiteral("all") || op == QStringLiteral("any")) {
        const QVariantList conditions = listFromVariant(node.value(QStringLiteral("conditions")));
        if (conditions.isEmpty()) {
            return false;
        }

        if (op == QStringLiteral("all")) {
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

    if (op == QStringLiteral("truthy")) {
        return isTruthy(resolveExpressionValue(node.value(QStringLiteral("value")), scopes));
    }

    if (op == QStringLiteral("not")) {
        return !evaluateConditionNode(node.value(QStringLiteral("condition")).toMap(), scopes);
    }

    if (op == QStringLiteral("eq") || op == QStringLiteral("ne") || op == QStringLiteral("lt")
            || op == QStringLiteral("le") || op == QStringLiteral("gt") || op == QStringLiteral("ge")) {
        const QVariant left = resolveExpressionValue(node.value(QStringLiteral("left")), scopes);
        const QVariant right = resolveExpressionValue(node.value(QStringLiteral("right")), scopes);
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

    if (text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (text.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
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

QStringList normalizeActionAliases(const QString& candidateAction)
{
    const QString normalized = candidateAction.trimmed().toLower();
    if (normalized == QStringLiteral("buy")) {
        return {QStringLiteral("buy"), QStringLiteral("entry"), QStringLiteral("candidate_entry"), QStringLiteral("open")};
    }
    if (normalized == QStringLiteral("sell")) {
        return {QStringLiteral("sell"), QStringLiteral("reduce"), QStringLiteral("exit"), QStringLiteral("close")};
    }
    return normalized.isEmpty() ? QStringList{} : QStringList{normalized};
}

bool allowActionsPermitCandidate(const QVariantMap& payload, const QString& candidateAction)
{
    const QVariantList allowActions = payload.value(QStringLiteral("allow_actions")).toList();
    if (allowActions.isEmpty()) {
        return false;
    }

    const QStringList aliases = normalizeActionAliases(candidateAction);
    for (const QVariant& action : allowActions) {
        const QString normalizedAction = action.toString().trimmed().toLower();
        if (aliases.contains(normalizedAction)) {
            return true;
        }
    }
    return false;
}

bool isEntryLikeCandidateAction(const QString& candidateAction)
{
    const QStringList aliases = normalizeActionAliases(candidateAction);
    return aliases.contains(QStringLiteral("buy"))
        || aliases.contains(QStringLiteral("entry"))
        || aliases.contains(QStringLiteral("candidate_entry"))
        || aliases.contains(QStringLiteral("open"));
}

bool isExitLikeCandidateAction(const QString& candidateAction)
{
    const QStringList aliases = normalizeActionAliases(candidateAction);
    return aliases.contains(QStringLiteral("sell"))
        || aliases.contains(QStringLiteral("exit"))
        || aliases.contains(QStringLiteral("reduce"))
        || aliases.contains(QStringLiteral("close"));
}

bool isBlockingTemplateResult(const QVariantMap& thenBlock, const QString& candidateAction)
{
    const QString resultType = normalizedResultType(thenBlock);
    if (resultType.isEmpty() || resultType == QStringLiteral("pass")) {
        return false;
    }

    if (resultType == QStringLiteral("candidate_entry")
            || resultType == QStringLiteral("open")
            || resultType == QStringLiteral("exit")
            || resultType == QStringLiteral("reduce")) {
        return false;
    }

    if (resultType == QStringLiteral("state_switch")) {
        return !allowActionsPermitCandidate(thenBlock.value(QStringLiteral("payload")).toMap(), candidateAction);
    }

    return true;
}

QVariantMap buildCandidateScope(const RuntimeRuleTemplateEvaluationContext& context)
{
    QVariantMap candidate;
    candidate.insert(QStringLiteral("symbol"), context.symbol.trimmed().toUpper());
    candidate.insert(QStringLiteral("latest_price"), context.latestPrice);
    candidate.insert(QStringLiteral("latestPrice"), context.latestPrice);
    candidate.insert(QStringLiteral("reference_price"), context.referencePrice);
    candidate.insert(QStringLiteral("referencePrice"), context.referencePrice);
    candidate.insert(QStringLiteral("market_event_type"), context.marketEventType);
    candidate.insert(QStringLiteral("marketEventType"), context.marketEventType);
    candidate.insert(QStringLiteral("candidate_action"), context.candidateAction);
    candidate.insert(QStringLiteral("candidateAction"), context.candidateAction);
    candidate.insert(QStringLiteral("candidate_strength"), context.candidateStrength);
    candidate.insert(QStringLiteral("candidateStrength"), context.candidateStrength);
    return candidate;
}

QVariantMap buildMarketScope(const RuntimeRuleTemplateEvaluationContext& context)
{
    QVariantMap market = context.marketSessionSnapshot;
    if (!context.marketEventType.trimmed().isEmpty()) {
        market.insert(QStringLiteral("market_event_type"), context.marketEventType);
        market.insert(QStringLiteral("marketEventType"), context.marketEventType);
    }
    return market;
}

QVariantMap buildStrategyScope(const RuntimeRuleTemplateEvaluationContext& context)
{
    QVariantMap strategyScope;
    strategyScope.insert(QStringLiteral("strategy_id"), context.strategy.value(QStringLiteral("strategy_id")));
    strategyScope.insert(QStringLiteral("strategy_type"), context.strategy.value(QStringLiteral("strategy_type")));
    strategyScope.insert(QStringLiteral("parameters"), context.strategy.value(QStringLiteral("parameters")).toMap());
    strategyScope.insert(QStringLiteral("rule_profile"), context.strategy.value(QStringLiteral("ruleProfileSnapshot")).toMap());
    strategyScope.insert(QStringLiteral("execution_policy"), context.strategy.value(QStringLiteral("executionPolicySnapshot")).toMap());
    strategyScope.insert(QStringLiteral("strategy_scope_context"), context.strategy.value(QStringLiteral("strategyScopeContextSnapshot")).toMap());
    strategyScope.insert(QStringLiteral("runtime_session"), context.runtimeSessionSnapshot);
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
        } else if (key.startsWith(snakePrefix, Qt::CaseInsensitive)) {
            target->insert(key.mid(snakePrefix.size()), it.value());
        }
    }
}

QVariantMap buildScopes(const RuntimeRuleTemplateEvaluationContext& context)
{
    QVariantMap candidate = buildCandidateScope(context);
    QVariantMap market = buildMarketScope(context);
    QVariantMap strategyScope = buildStrategyScope(context);

    mergePrefixedFacts(context.flatEventFacts, QStringLiteral("candidate."), QStringLiteral("candidate_"), &candidate);
    mergePrefixedFacts(context.flatEventFacts, QStringLiteral("market."), QStringLiteral("market_"), &market);
    mergePrefixedFacts(context.flatEventFacts, QStringLiteral("strategy."), QStringLiteral("strategy_"), &strategyScope);

    QVariantMap scopes;
    scopes.insert(QStringLiteral("candidate"), candidate);
    scopes.insert(QStringLiteral("market"), market);
    scopes.insert(QStringLiteral("strategy"), strategyScope);
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

    const QString resultType = result.resultType.trimmed().toLower();
    const QString stage = result.stage.trimmed().toLower();
    if (result.blocked) {
        if (stage == QStringLiteral("market") || stage == QStringLiteral("signal") || stage == QStringLiteral("entry")) {
            return 500;
        }
        return 450;
    }
    if (resultType == QStringLiteral("exit")) {
        return 400;
    }
    if (resultType == QStringLiteral("reduce")) {
        return 300;
    }
    if (resultType == QStringLiteral("state_switch") || resultType == QStringLiteral("halt")) {
        return 250;
    }
    if (resultType == QStringLiteral("candidate_entry") || resultType == QStringLiteral("open")) {
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

QString normalizedBindingGroupId(const QVariantMap& binding)
{
    return firstNonEmptyBindingValue(binding, {"group_id", "groupId"}).trimmed();
}

QString normalizedBindingGroupOperator(const QVariantMap& binding)
{
    return firstNonEmptyBindingValue(binding, {"group_operator", "groupOperator"}).trimmed().toLower();
}

QString normalizedBindingGroupRole(const QVariantMap& binding)
{
    return firstNonEmptyBindingValue(binding, {"group_role", "groupRole"}).trimmed().toLower();
}

QString normalizedBindingGroupTitle(const QVariantMap& binding)
{
    return firstNonEmptyBindingValue(binding, {"group_title", "groupTitle"}).trimmed();
}

QString normalizedBindingPhase(const QVariantMap& binding)
{
    return firstNonEmptyBindingValue(binding, {"phase", "stage"}).trimmed().toLower();
}

int normalizedBindingGroupMatchThreshold(const QVariantMap& binding)
{
    static const std::initializer_list<const char*> thresholdKeys{
        "group_min_match_count",
        "groupMinMatchCount",
        "min_match_count",
        "minMatchCount",
        "required_match_count",
        "requiredMatchCount",
        "minimum_matches",
        "minimumMatches",
        "at_least_count",
        "atLeastCount",
        "match_threshold",
        "matchThreshold",
        "threshold"
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
        {"group_score_sum",
         "groupScoreSum",
         "selection_bonus",
         "selectionBonus",
         "score_bonus",
         "scoreBonus",
         "priority_bonus",
         "priorityBonus",
         "boost_score",
         "boostScore",
         "priority_score",
         "priorityScore",
         "score",
         "bonus"},
        0.0);
}

bool isMandatoryGroupRole(const QString& groupRole)
{
    const QString normalizedRole = groupRole.trimmed().toLower();
    return normalizedRole == QStringLiteral("must_pass")
        || normalizedRole == QStringLiteral("entry_guard");
}

bool isTriggerGroupRole(const QString& groupRole)
{
    const QString normalizedRole = groupRole.trimmed().toLower();
    return normalizedRole == QStringLiteral("any_pass")
        || normalizedRole == QStringLiteral("trigger")
        || normalizedRole == QStringLiteral("exit_guard");
}

bool isNeutralGroupRole(const QString& groupRole)
{
    const QString normalizedRole = groupRole.trimmed().toLower();
    return normalizedRole == QStringLiteral("score_boost")
        || normalizedRole == QStringLiteral("position_management");
}

bool stageAppliesToCandidateAction(const QString& stage, const QString& candidateAction)
{
    const QString normalizedStage = stage.trimmed().toLower();
    if (normalizedStage.isEmpty() || candidateAction.trimmed().isEmpty()) {
        return true;
    }

    if (isEntryLikeCandidateAction(candidateAction)) {
        return normalizedStage != QStringLiteral("rebalance")
            && normalizedStage != QStringLiteral("exit");
    }

    if (isExitLikeCandidateAction(candidateAction)) {
        return normalizedStage != QStringLiteral("market")
            && normalizedStage != QStringLiteral("eligibility")
            && normalizedStage != QStringLiteral("entry");
    }

    return true;
}

bool roleAppliesToCandidateAction(const QString& groupRole,
                                  const QString& candidateAction,
                                  const QString& stage)
{
    if (candidateAction.trimmed().isEmpty()) {
        return true;
    }

    const QString normalizedRole = groupRole.trimmed().toLower();
    if (normalizedRole.isEmpty()) {
        return stageAppliesToCandidateAction(stage, candidateAction);
    }

    if (normalizedRole == QStringLiteral("must_pass")
            || normalizedRole == QStringLiteral("any_pass")) {
        return stageAppliesToCandidateAction(stage, candidateAction);
    }

    if (normalizedRole == QStringLiteral("trigger")
            || normalizedRole == QStringLiteral("score_boost")
            || normalizedRole == QStringLiteral("entry_guard")) {
        return isEntryLikeCandidateAction(candidateAction);
    }

    if (normalizedRole == QStringLiteral("exit_guard")) {
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
    const QString stage = normalizedBindingPhase(binding);
    if (!stageAppliesToCandidateAction(stage, context.candidateAction)) {
        return {false, QStringLiteral("stage_filtered")};
    }
    if (!roleAppliesToCandidateAction(
            normalizedBindingGroupRole(binding),
            context.candidateAction,
            stage)) {
        return {false, QStringLiteral("role_filtered")};
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
    fallbackResult.stage.clear();
    fallbackResult.ruleId.clear();
    fallbackResult.reasonCode.clear();
    fallbackResult.message.clear();
    fallbackResult.resultType.clear();
    fallbackResult.payload.clear();
    fallbackResult.state.clear();
    return fallbackResult;
}

struct GroupedTemplateEvaluation {
    QString groupKey;
    QString stage;
    QString groupId;
    QString groupTitle;
    QString groupRole;
    QString groupOperator;
    int matchThreshold = 0;
    int totalMembers = 0;
    int applicableMembers = 0;
    QString skipReason;
    std::vector<RuntimeRuleTemplateEvaluationResult> members;
};

QString effectiveGroupOperator(const GroupedTemplateEvaluation& groupedEvaluation)
{
    const QString groupOperator = groupedEvaluation.groupOperator.trimmed().toLower();
    return groupOperator.isEmpty() ? QStringLiteral("any") : groupOperator;
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
    const QString groupOperator = effectiveGroupOperator(groupedEvaluation);
    if (groupOperator == QStringLiteral("first_match")) {
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

    selectedResult.binding.insert(QStringLiteral("group_id"), groupedEvaluation.groupId);
    selectedResult.binding.insert(QStringLiteral("group_title"), groupedEvaluation.groupTitle);
    selectedResult.binding.insert(QStringLiteral("group_role"), groupedEvaluation.groupRole);
    selectedResult.binding.insert(QStringLiteral("group_operator"), groupedEvaluation.groupOperator);
    selectedResult.binding.insert(QStringLiteral("phase"), groupedEvaluation.stage);

    const int threshold = resolvedGroupMatchThreshold(groupedEvaluation);
    const int matchedCount = matchedMemberCount(groupedEvaluation);
    selectedResult.payload.insert(QStringLiteral("groupMatchedCount"), matchedCount);
    if (threshold > 0) {
        selectedResult.payload.insert(QStringLiteral("groupMatchThreshold"), threshold);
    }

    if (groupOperator == QStringLiteral("score_sum")) {
        selectedResult.payload.insert(QStringLiteral("groupScoreSum"), matchedScoreSum(groupedEvaluation));
        selectedResult.payload.insert(QStringLiteral("groupSelectionMode"), QStringLiteral("score_sum"));
    } else if (groupOperator == QStringLiteral("first_match")) {
        selectedResult.payload.insert(QStringLiteral("groupSelectionMode"), QStringLiteral("first_match"));
    } else {
        selectedResult.payload.insert(QStringLiteral("groupSelectionMode"), QStringLiteral("priority"));
    }

    return selectedResult;
}

double candidateSelectionScore(const RuntimeRuleTemplateEvaluationResult& result,
                               const QHash<QString, double>& stageScoreBoosts)
{
    if (!result.matched) {
        return -1.0;
    }

    const QString stage = result.stage.trimmed().toLower();
    return static_cast<double>(evaluationPriorityScore(result))
        + selectionBonusFromPayload(result.payload)
        + stageScoreBoosts.value(stage, 0.0);
}

QString groupDisplayName(const GroupedTemplateEvaluation& groupedEvaluation)
{
    if (!groupedEvaluation.groupTitle.isEmpty() && !groupedEvaluation.groupRole.isEmpty()) {
        return groupedEvaluation.groupTitle + QStringLiteral(" / ") + groupedEvaluation.groupRole;
    }
    if (!groupedEvaluation.groupTitle.isEmpty()) {
        return groupedEvaluation.groupTitle;
    }
    if (!groupedEvaluation.groupRole.isEmpty()) {
        return groupedEvaluation.groupRole;
    }
    return groupedEvaluation.groupId;
}

RuntimeRuleTemplateEvaluationResult adjudicationBlockedResult(
    const RuntimeRuleTemplateEvaluationResult& fallbackResult,
    const GroupedTemplateEvaluation& groupedEvaluation,
    const QString& reasonCode,
    const QString& message)
{
    RuntimeRuleTemplateEvaluationResult result = fallbackResult;
    if (!groupedEvaluation.members.empty()) {
        result = templatePresenceOnlyResult(groupedEvaluation.members.front());
    }
    result.hasTemplate = result.hasTemplate || !groupedEvaluation.groupId.isEmpty();
    result.matched = false;
    result.blocked = false;
    result.actionPermitted = false;
    result.stage = groupedEvaluation.stage;
    result.reasonCode = reasonCode;
    result.message = message;
    result.resultType.clear();
    result.payload.clear();
    result.state.clear();
    result.binding.insert(QStringLiteral("group_id"), groupedEvaluation.groupId);
    result.binding.insert(QStringLiteral("group_title"), groupedEvaluation.groupTitle);
    result.binding.insert(QStringLiteral("group_role"), groupedEvaluation.groupRole);
    result.binding.insert(QStringLiteral("group_operator"), groupedEvaluation.groupOperator);
    result.binding.insert(QStringLiteral("phase"), groupedEvaluation.stage);
    return result;
}

QVariantMap buildGroupDecision(const GroupedTemplateEvaluation& groupedEvaluation)
{
    QVariantMap decision;
    const QString groupOperator = effectiveGroupOperator(groupedEvaluation);
    const int matchedCount = matchedMemberCount(groupedEvaluation);
    const int threshold = resolvedGroupMatchThreshold(groupedEvaluation);
    const double aggregatedScore = matchedScoreSum(groupedEvaluation);
    decision.insert(QStringLiteral("stage"), groupedEvaluation.stage);
    decision.insert(QStringLiteral("groupId"), groupedEvaluation.groupId);
    if (!groupedEvaluation.groupTitle.isEmpty()) {
        decision.insert(QStringLiteral("groupTitle"), groupedEvaluation.groupTitle);
    }
    if (!groupedEvaluation.groupRole.isEmpty()) {
        decision.insert(QStringLiteral("groupRole"), groupedEvaluation.groupRole);
    }
    if (!groupedEvaluation.groupOperator.isEmpty()) {
        decision.insert(QStringLiteral("groupOperator"), groupedEvaluation.groupOperator);
    }

    const int filteredCount = (std::max)(0, groupedEvaluation.totalMembers - groupedEvaluation.applicableMembers);

    decision.insert(QStringLiteral("memberCount"), groupedEvaluation.totalMembers);
    decision.insert(QStringLiteral("applicableCount"), groupedEvaluation.applicableMembers);
    decision.insert(QStringLiteral("matchedCount"), matchedCount);
    decision.insert(QStringLiteral("filteredCount"), filteredCount);
    if (groupOperator == QStringLiteral("at_least")) {
        decision.insert(QStringLiteral("matchThreshold"), threshold);
    }
    if (groupOperator == QStringLiteral("score_sum")) {
        decision.insert(QStringLiteral("aggregatedScore"), aggregatedScore);
    }

    if (groupedEvaluation.applicableMembers <= 0) {
        decision.insert(QStringLiteral("disposition"), QStringLiteral("skipped"));
        decision.insert(QStringLiteral("outcome"), QStringLiteral("not_applicable"));
        decision.insert(
            QStringLiteral("skipReason"),
            groupedEvaluation.skipReason.isEmpty() ? QStringLiteral("role_filtered") : groupedEvaluation.skipReason);
        return decision;
    }

    decision.insert(QStringLiteral("disposition"), QStringLiteral("considered"));
    if (matchedCount <= 0) {
        decision.insert(QStringLiteral("outcome"), QStringLiteral("not_matched"));
        return decision;
    }

    if (groupOperator == QStringLiteral("all") && matchedCount < groupedEvaluation.applicableMembers) {
        decision.insert(QStringLiteral("outcome"), QStringLiteral("incomplete"));
        decision.insert(QStringLiteral("skipReason"), QStringLiteral("group_incomplete"));
    } else if (groupOperator == QStringLiteral("at_least") && matchedCount < threshold) {
        decision.insert(QStringLiteral("outcome"), QStringLiteral("incomplete"));
        decision.insert(QStringLiteral("skipReason"), QStringLiteral("group_threshold_unmet"));
    } else {
        decision.insert(QStringLiteral("outcome"), QStringLiteral("matched"));
    }

    const RuntimeRuleTemplateEvaluationResult selectedResult =
        selectGroupMatchedResult(groupedEvaluation);
    if (selectedResult.matched) {
        if (!selectedResult.ruleId.isEmpty()) {
            decision.insert(QStringLiteral("matchedRuleId"), selectedResult.ruleId);
        }
        if (!selectedResult.resultType.isEmpty()) {
            decision.insert(QStringLiteral("matchedResultType"), selectedResult.resultType);
        }
        if (!selectedResult.reasonCode.isEmpty()) {
            decision.insert(QStringLiteral("matchedReasonCode"), selectedResult.reasonCode);
        }
        decision.insert(
            QStringLiteral("selectedBy"),
            groupOperator == QStringLiteral("first_match")
                ? QStringLiteral("first_match")
                : (groupOperator == QStringLiteral("score_sum")
                    ? QStringLiteral("score_sum")
                    : QStringLiteral("priority")));
    }
    return decision;
}

bool groupSatisfied(const GroupedTemplateEvaluation& groupedEvaluation)
{
    if (groupedEvaluation.applicableMembers <= 0) {
        return false;
    }

    const QString groupOperator = effectiveGroupOperator(groupedEvaluation);
    const int matchedCount = matchedMemberCount(groupedEvaluation);
    if (groupOperator == QStringLiteral("all")) {
        return matchedCount >= groupedEvaluation.applicableMembers;
    }
    if (groupOperator == QStringLiteral("at_least")) {
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
    result.binding = compiledTemplate.value(QStringLiteral("_binding")).toMap();
    result.templateNamespace = compiledTemplate.value(QStringLiteral("namespace")).toString().trimmed();
    result.templateFilePath = compiledTemplate.value(QStringLiteral("_filePath")).toString().trimmed();

    const QVariantMap scopes = buildScopes(context);
    QVariantList rules = compiledTemplate.value(QStringLiteral("rules")).toList();
    std::stable_sort(rules.begin(), rules.end(), [](const QVariant& left, const QVariant& right) {
        return left.toMap().value(QStringLiteral("priority")).toInt() > right.toMap().value(QStringLiteral("priority")).toInt();
    });

    for (const QVariant& ruleValue : rules) {
        const QVariantMap rule = ruleValue.toMap();
        if (rule.isEmpty()) {
            continue;
        }

        const QVariantMap whenBlock = rule.value(QStringLiteral("when")).toMap();
        if (whenBlock.isEmpty() || !evaluateConditionNode(whenBlock, scopes)) {
            continue;
        }

        const QVariantMap thenBlock = rule.value(QStringLiteral("then")).toMap();
        result.matched = true;
        result.stage = rule.value(QStringLiteral("stage")).toString().trimmed().toLower();
        result.ruleId = rule.value(QStringLiteral("id")).toString().trimmed();
        result.reasonCode = thenBlock.value(QStringLiteral("reason_code")).toString().trimmed();
        result.message = thenBlock.value(QStringLiteral("message")).toString().trimmed();
        result.resultType = normalizedResultType(thenBlock);
        result.payload = thenBlock.value(QStringLiteral("payload")).toMap();
        if (thenBlock.contains(QStringLiteral("score"))
                && !result.payload.contains(QStringLiteral("score"))) {
            const QVariant resolvedScore = resolveExpressionValue(
                thenBlock.value(QStringLiteral("score")),
                scopes);
            if (resolvedScore.isValid() && !resolvedScore.isNull()) {
                result.payload.insert(QStringLiteral("score"), resolvedScore);
            }
        }
        result.state = thenBlock.value(QStringLiteral("state")).toMap();
        result.blocked = isBlockingTemplateResult(thenBlock, context.candidateAction);
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
    QHash<QString, int> groupedIndexes;
    QHash<QString, double> stageScoreBoosts;
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

        const QString groupId = normalizedBindingGroupId(currentResult.binding);
        const BindingApplicability applicability = bindingApplicability(currentResult.binding, context);
        if (groupId.isEmpty()) {
            if (applicability.applies && currentResult.matched) {
                candidateResults.push_back(currentResult);
            }
            continue;
        }

        const QString groupKey = normalizedBindingPhase(currentResult.binding) + QStringLiteral("|") + groupId;
        int groupIndex = groupedIndexes.value(groupKey, -1);
        if (groupIndex < 0) {
            GroupedTemplateEvaluation groupedEvaluation;
            groupedEvaluation.groupKey = groupKey;
            groupedEvaluation.stage = normalizedBindingPhase(currentResult.binding);
            groupedEvaluation.groupId = groupId;
            groupedEvaluation.groupTitle = normalizedBindingGroupTitle(currentResult.binding);
            groupedEvaluation.groupRole = normalizedBindingGroupRole(currentResult.binding);
            groupedEvaluation.groupOperator = normalizedBindingGroupOperator(currentResult.binding);
            groupedEvaluation.matchThreshold = normalizedBindingGroupMatchThreshold(currentResult.binding);
            groupIndex = static_cast<int>(groupedResults.size());
            groupedIndexes.insert(groupKey, groupIndex);
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
        if (groupMatched && groupedEvaluation.groupRole.trimmed().toLower() == QStringLiteral("score_boost")) {
            stageScoreBoosts.insert(
                groupedEvaluation.stage,
                stageScoreBoosts.value(groupedEvaluation.stage, 0.0) + matchedScoreSum(groupedEvaluation));
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
        const QString groupRole = groupedEvaluation.groupRole.trimmed().toLower();
        const bool groupMatched = groupSatisfied(groupedEvaluation);
        if (isMandatoryGroupRole(groupRole)
                && groupedEvaluation.applicableMembers > 0
                && !groupMatched
                && !firstUnmetMandatoryGroup) {
            firstUnmetMandatoryGroup = &groupedEvaluation;
        }
        if (isTriggerGroupRole(groupRole) && groupedEvaluation.applicableMembers > 0) {
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
        const QString groupRole = normalizedBindingGroupRole(candidateResult.binding);
        if (!candidateResult.matched || isNeutralGroupRole(groupRole) || candidateResult.blocked) {
            continue;
        }
        nonNeutralResults.push_back(candidateResult);
    }

    if (firstUnmetMandatoryGroup) {
        RuntimeRuleTemplateEvaluationResult gatedResult = adjudicationBlockedResult(
            fallbackResult,
            *firstUnmetMandatoryGroup,
            QStringLiteral("runtime_rule_template_must_pass_unmet"),
            QStringLiteral("必须满足组未全部通过: %1").arg(groupDisplayName(*firstUnmetMandatoryGroup)));
        gatedResult.groupDecisions = groupDecisions;
        return gatedResult;
    }

    if (hasApplicableTriggerGroup && !hasMatchedTriggerGroup && firstTriggerGroup) {
        RuntimeRuleTemplateEvaluationResult gatedResult = adjudicationBlockedResult(
            fallbackResult,
            *firstTriggerGroup,
            QStringLiteral("runtime_rule_template_any_pass_unmet"),
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