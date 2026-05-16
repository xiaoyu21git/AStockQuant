#include "StrategyService.h"
#include "RuleTemplateRuntimeEvaluator.h"
#include "StrategyRuntimeRuleEvaluator.h"
#include "StrategyViewModel.h"
#include "StrategyStructureResolvers.h"
#include "RiskConfigService.h"
#include "TradingConnectionConfigService.h"
#include "TradingMarketCalendarService.h"
#include "TradingRuntimeStatusService.h"
#include "PortfolioExecutionPlanUtils.h"
#include "RiskMonitorService.h"
#include "PositionAccountService.h"
#include "TradeExecutionService.h"
#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "../../ui/bridge/include/DatabaseConnectionManager.h"
#include "database/StrategyRepository.h"
#include <QDebug>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <QReadWriteLock>
#include <QCoreApplication>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QDir>
#include <QMetaType>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

using namespace astock::database;

namespace {

constexpr int MAX_BACKTEST_HISTORY_ITEMS = 20;
constexpr qint64 kStrategySignalCooldownMs = 1000;
constexpr qint64 kTradingConfigurationCacheTtlMs = 250;
constexpr qint64 kRiskConfigurationCacheTtlMs = 500;
constexpr qint64 kMarketSessionCacheTtlMs = 250;
constexpr double kDefaultTemplateInitialCapital = 1000000.0;
constexpr double kDefaultTemplateCommissionRate = 0.0015;
constexpr double kDefaultTemplateSlippageRate = 0.001;

QString normalizePersistedStatus(const QString& rawStatus)
{
    const QStringList validStatuses = {"ACTIVE", "INACTIVE", "TESTING", "ARCHIVED"};
    if (validStatuses.contains(rawStatus)) {
        return rawStatus;
    }

    if (!rawStatus.isEmpty()) {
        qWarning() << "StrategyService: 转换无效状态" << rawStatus << "为ACTIVE";
    }
    return "ACTIVE";
}

int positiveIntegerFromVariant(const QVariant& value, int fallback = 0)
{
    if (!value.isValid() || value.isNull()) {
        return fallback;
    }

    bool ok = false;
    int parsedValue = value.toInt(&ok);
    if (!ok) {
        const double parsedDouble = value.toDouble(&ok);
        if (ok) {
            parsedValue = qRound(parsedDouble);
        }
    }
    return ok && parsedValue > 0 ? parsedValue : fallback;
}

QVariantMap mergePerformanceMetrics(const QVariantMap& existingStrategy,
                                   const QVariantMap& incomingPerformance,
                                   const QString& updatedAt)
{
    QVariantMap mergedPerformance = existingStrategy.value("performance_metrics").toMap();
    for (auto it = incomingPerformance.constBegin(); it != incomingPerformance.constEnd(); ++it) {
        mergedPerformance.insert(it.key(), it.value());
    }

    QVariantMap historyEntry = incomingPerformance.value("backtestHistoryEntry").toMap();
    QVariantList backtestHistory = mergedPerformance.value("backtestHistory").toList();
    if (!historyEntry.isEmpty()) {
        const bool replaceLatestBacktest = incomingPerformance.value("replaceLatestBacktest", true).toBool();
        historyEntry["recordedAt"] = historyEntry.value("recordedAt", updatedAt).toString();
        backtestHistory.prepend(historyEntry);
        while (backtestHistory.size() > MAX_BACKTEST_HISTORY_ITEMS) {
            backtestHistory.removeLast();
        }
        if (replaceLatestBacktest || !mergedPerformance.value("latestBacktest").canConvert<QVariantMap>()) {
            mergedPerformance["latestBacktest"] = historyEntry;
        }
        mergedPerformance["backtestHistory"] = backtestHistory;
    }

    mergedPerformance["lastBacktestAt"] = incomingPerformance.value("lastBacktestAt", updatedAt).toString();
    return mergedPerformance;
}

QVariantMap mergeVariantMapsRecursive(const QVariantMap& base, const QVariantMap& overlay)
{
    QVariantMap merged = base;
    for (auto it = overlay.constBegin(); it != overlay.constEnd(); ++it) {
        const QVariant existingValue = merged.value(it.key());
        if (existingValue.canConvert<QVariantMap>() && it.value().canConvert<QVariantMap>()) {
            merged.insert(it.key(), mergeVariantMapsRecursive(existingValue.toMap(), it.value().toMap()));
            continue;
        }

        merged.insert(it.key(), it.value());
    }
    return merged;
}

void mergeStrategyParameterPayload(QVariantMap& targetStrategy, const QVariantMap& incomingStrategy)
{
    const QVariantMap existingParameters = targetStrategy.value(QStringLiteral("parameters")).toMap();
    const QVariantMap incomingParameters = incomingStrategy.value(QStringLiteral("parameters")).toMap();
    if (!incomingParameters.isEmpty() || incomingStrategy.contains(QStringLiteral("parameters"))) {
        targetStrategy.insert(QStringLiteral("parameters"), mergeVariantMapsRecursive(existingParameters, incomingParameters));
    }

    const QStringList structuredSnapshotKeys = {
        QStringLiteral("ruleProfileSnapshot"),
        QStringLiteral("executionPolicySnapshot"),
        QStringLiteral("backtestAssumptionsSnapshot"),
        QStringLiteral("strategyScopeContextSnapshot")
    };

    for (const QString& key : structuredSnapshotKeys) {
        const QVariantMap incomingSnapshot = incomingStrategy.value(key).toMap();
        if (incomingSnapshot.isEmpty() && !incomingStrategy.contains(key)) {
            continue;
        }

        const QVariantMap existingSnapshot = targetStrategy.value(key).toMap();
        targetStrategy.insert(key, mergeVariantMapsRecursive(existingSnapshot, incomingSnapshot));
    }
}

// 策略类型描述
const QMap<QString, QString> STRATEGY_TYPE_DESCRIPTIONS = {
    {"TREND", "趋势跟踪策略 - 跟随市场趋势进行交易"},
    {"MEAN_REVERSION", "均值回归策略 - 假设价格会回归均值水平"},
    {"ALPHA", "阿尔法策略 - 寻找超越市场的超额收益"},
    {"ARBITRAGE", "套利策略 - 利用市场定价差异获取无风险收益"},
    {"HFT", "高频交易策略 - 利用极短时间窗口的交易机会"},
    {"PORTFOLIO", "组合策略 - 多个策略的组合配置"},
    {"CUSTOM", "自定义策略 - 用户自定义的交易逻辑"}
};

// 策略类型映射
const QMap<QString, QStringList> STRATEGY_TYPE_MAPPING = {
    {"trend_following", {"TREND", "趋势跟踪"}},
    {"mean_reversion", {"MEAN_REVERSION", "均值回归"}},
    {"momentum", {"ALPHA", "动量"}},
    {"arbitrage", {"ARBITRAGE", "套利"}},
    {"machine_learning", {"ALPHA", "机器学习"}},
    {"multi_factor", {"ALPHA", "多因子"}},
    {"custom", {"CUSTOM", "自定义"}}
};

QVariantList buildStrategyListFromCache(const QMap<QString, QVariantMap>& memoryCache)
{
    QVariantList strategies;
    for (const QVariantMap& strategy : memoryCache) {
        strategies.append(strategy);
    }
    return strategies;
}

QString firstNonEmptyStrategyText(const QVariantMap& strategy, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QString text = strategy.value(QString::fromUtf8(key)).toString().trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}

QStringList strategyTags(const QVariantMap& strategy)
{
    const QVariant tagsValue = strategy.value(QStringLiteral("tags"));
    QStringList tags;
    if (tagsValue.canConvert<QStringList>()) {
        tags = tagsValue.toStringList();
    } else {
        const QVariantList tagList = tagsValue.toList();
        for (const QVariant& tagValue : tagList) {
            const QString tagText = tagValue.toString().trimmed();
            if (!tagText.isEmpty()) {
                tags.append(tagText);
            }
        }
    }
    tags.removeDuplicates();
    return tags;
}

QString normalizeStrategySymbol(const QString& rawSymbol)
{
    const QString symbol = rawSymbol.trimmed().toUpper();
    if (symbol.isEmpty()) {
        return {};
    }

    const QStringList parts = symbol.split('.');
    if (parts.size() == 2) {
        const QString left = parts.at(0);
        const QString right = parts.at(1);
        if (left == QStringLiteral("SHSE") || left == QStringLiteral("SZSE") || left == QStringLiteral("BSE")
            || left == QStringLiteral("CFFEX") || left == QStringLiteral("SHFE") || left == QStringLiteral("DCE")
            || left == QStringLiteral("CZCE") || left == QStringLiteral("INE") || left == QStringLiteral("GFEX")) {
            if (left == QStringLiteral("SHSE")) {
                return right + QStringLiteral(".SH");
            }
            if (left == QStringLiteral("SZSE")) {
                return right + QStringLiteral(".SZ");
            }
            if (left == QStringLiteral("BSE")) {
                return right + QStringLiteral(".BJ");
            }
            return right + QStringLiteral(".") + left;
        }
    }

    return symbol;
}

QString buildSignalPublicationKey(const QString& strategyId, const QString& symbol)
{
    return strategyId.trimmed() + QChar('|') + normalizeStrategySymbol(symbol);
}

QStringList parseStrategySymbolPoolText(const QString& text)
{
    const QStringList rawTokens = text.split(QRegularExpression(QStringLiteral("[,;\\s，；]+")), Qt::SkipEmptyParts);
    QStringList symbols;
    QSet<QString> seen;
    for (const QString& rawToken : rawTokens) {
        const QString normalizedToken = normalizeStrategySymbol(rawToken);
        if (normalizedToken.isEmpty() || seen.contains(normalizedToken)) {
            continue;
        }
        seen.insert(normalizedToken);
        symbols.append(normalizedToken);
    }
    return symbols;
}

QStringList symbolPoolFromVariant(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }

    QStringList symbols;
    if (value.canConvert<QStringList>()) {
        symbols = value.toStringList();
    } else {
        const QVariantList symbolList = value.toList();
        for (const QVariant& symbolValue : symbolList) {
            const QString symbolText = symbolValue.toString();
            if (!symbolText.trimmed().isEmpty()) {
                symbols.append(symbolText);
            }
        }
        if (symbols.isEmpty()) {
            return parseStrategySymbolPoolText(value.toString());
        }
    }

    QStringList normalizedSymbols;
    QSet<QString> seen;
    for (const QString& symbol : symbols) {
        const QString normalizedSymbol = normalizeStrategySymbol(symbol);
        if (normalizedSymbol.isEmpty() || seen.contains(normalizedSymbol)) {
            continue;
        }
        seen.insert(normalizedSymbol);
        normalizedSymbols.append(normalizedSymbol);
    }
    return normalizedSymbols;
}

QStringList strategySymbolPool(const QVariantMap& strategy)
{
    QStringList symbols = symbolPoolFromVariant(strategy.value(QStringLiteral("symbol_pool")));
    if (!symbols.isEmpty()) {
        return symbols;
    }

    symbols = symbolPoolFromVariant(strategy.value(QStringLiteral("symbolPool")));
    if (!symbols.isEmpty()) {
        return symbols;
    }

    const QVariantMap parameters = strategy.value(QStringLiteral("parameters")).toMap();
    symbols = symbolPoolFromVariant(parameters.value(QStringLiteral("symbol_pool")));
    if (!symbols.isEmpty()) {
        return symbols;
    }

    return symbolPoolFromVariant(parameters.value(QStringLiteral("symbolPool")));
}

QStringList strategyBacktestSymbolPool(const QVariantMap& strategy)
{
    QStringList symbols = symbolPoolFromVariant(strategy.value(QStringLiteral("backtest_symbol_pool")));
    if (!symbols.isEmpty()) {
        return symbols;
    }

    symbols = symbolPoolFromVariant(strategy.value(QStringLiteral("backtestSymbolPool")));
    if (!symbols.isEmpty()) {
        return symbols;
    }

    const QVariantMap parameters = strategy.value(QStringLiteral("parameters")).toMap();
    symbols = symbolPoolFromVariant(parameters.value(QStringLiteral("backtest_symbol_pool")));
    if (!symbols.isEmpty()) {
        return symbols;
    }

    return symbolPoolFromVariant(parameters.value(QStringLiteral("backtestSymbolPool")));
}

QVariantList normalizeRuleTemplateBindings(const QVariant& value)
{
    QVariantList bindings;
    const QVariantList list = value.toList();
    for (const QVariant& item : list) {
        const QVariantMap binding = item.toMap();
        if (!binding.isEmpty()) {
            bindings.append(binding);
        }
    }
    if (!bindings.isEmpty()) {
        return bindings;
    }

    const QVariantMap map = value.toMap();
    if (!map.isEmpty()) {
        const QStringList phaseOrder{QStringLiteral("market"), QStringLiteral("signal"), QStringLiteral("entry"), QStringLiteral("rebalance"), QStringLiteral("exit"), QStringLiteral("risk"), QStringLiteral("watch")};
        for (const QString& phase : phaseOrder) {
            const QVariantMap binding = map.value(phase).toMap();
            if (!binding.isEmpty()) {
                bindings.append(binding);
            }
        }
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            const QVariantMap binding = it.value().toMap();
            if (binding.isEmpty()) {
                continue;
            }
            bool exists = false;
            for (const QVariant& existing : bindings) {
                if (existing.toMap() == binding) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                bindings.append(binding);
            }
        }
        if (!bindings.isEmpty()) {
            return bindings;
        }
    }

    const QVariantMap singleBinding = value.toMap();
    if (!singleBinding.isEmpty()) {
        bindings.append(singleBinding);
    }
    return bindings;
}

QVariantList extractRuleTemplateBindingsFromComposerState(const QVariantMap& ruleProfile)
{
    QVariantMap composerState = ruleProfile.value(QStringLiteral("ruleComposerState")).toMap();
    if (composerState.isEmpty()) {
        composerState = ruleProfile.value(QStringLiteral("rule_composer_state")).toMap();
    }
    if (composerState.isEmpty()) {
        return {};
    }

    QVariantList bindings;
    const QVariantList stages = composerState.value(QStringLiteral("stages")).toList();
    for (const QVariant& stageValue : stages) {
        const QVariantMap stage = stageValue.toMap();
        const QString stageId = stage.value(QStringLiteral("stageId")).toString().trimmed().toLower();
        const QVariantList groups = stage.value(QStringLiteral("groups")).toList();
        for (const QVariant& groupValue : groups) {
            const QVariantMap group = groupValue.toMap();
            const QString groupId = group.value(QStringLiteral("groupId")).toString().trimmed();
            const QString groupTitle = group.value(QStringLiteral("title")).toString().trimmed();
            const QString groupRole = group.value(QStringLiteral("role")).toString().trimmed().toLower();
            const QString groupOperator = group.value(QStringLiteral("operator")).toString().trimmed().toLower();
            const int groupMinMatchCount = positiveIntegerFromVariant(
                group.value(QStringLiteral("groupMinMatchCount")),
                positiveIntegerFromVariant(
                    group.value(QStringLiteral("minMatchCount")),
                    positiveIntegerFromVariant(
                        group.value(QStringLiteral("minimumMatches")),
                        positiveIntegerFromVariant(group.value(QStringLiteral("atLeastCount"))))));
            const QVariantList rules = group.value(QStringLiteral("rules")).toList();
            for (const QVariant& ruleValue : rules) {
                const QVariantMap rule = ruleValue.toMap();
                const QString filePath = rule.value(QStringLiteral("filePath")).toString().trimmed();
                const QString fileName = rule.value(QStringLiteral("fileName")).toString().trimmed();
                const QString templateId = rule.value(QStringLiteral("templateId")).toString().trimmed();
                if (filePath.isEmpty() && fileName.isEmpty() && templateId.isEmpty()) {
                    continue;
                }

                QVariantMap binding;
                const QString phase = rule.value(QStringLiteral("phase")).toString().trimmed().toLower();
                binding.insert(QStringLiteral("phase"), phase.isEmpty() ? stageId : phase);
                if (!fileName.isEmpty()) {
                    binding.insert(QStringLiteral("file_name"), fileName);
                }
                if (!filePath.isEmpty()) {
                    binding.insert(QStringLiteral("file_path"), filePath);
                }
                if (!templateId.isEmpty()) {
                    binding.insert(QStringLiteral("template_id"), templateId);
                }

                const QString templateName = rule.value(QStringLiteral("templateName")).toString().trimmed();
                if (!templateName.isEmpty()) {
                    binding.insert(QStringLiteral("template_display_name"), templateName);
                }
                const QString summary = rule.value(QStringLiteral("summary")).toString().trimmed();
                if (!summary.isEmpty()) {
                    binding.insert(QStringLiteral("summary"), summary);
                }
                const QString category = rule.value(QStringLiteral("category")).toString().trimmed();
                if (!category.isEmpty()) {
                    binding.insert(QStringLiteral("category"), category);
                }
                const QString termId = rule.value(QStringLiteral("termId")).toString().trimmed();
                if (!termId.isEmpty()) {
                    binding.insert(QStringLiteral("term_id"), termId);
                }
                const QString termName = rule.value(QStringLiteral("termName")).toString().trimmed();
                if (!termName.isEmpty()) {
                    binding.insert(QStringLiteral("term_display_name"), termName);
                }
                if (!groupId.isEmpty()) {
                    binding.insert(QStringLiteral("group_id"), groupId);
                }
                if (!groupTitle.isEmpty()) {
                    binding.insert(QStringLiteral("group_title"), groupTitle);
                }
                if (!groupRole.isEmpty()) {
                    binding.insert(QStringLiteral("group_role"), groupRole);
                }
                if (!groupOperator.isEmpty()) {
                    binding.insert(QStringLiteral("group_operator"), groupOperator);
                }
                if (groupMinMatchCount > 0) {
                    binding.insert(QStringLiteral("group_min_match_count"), groupMinMatchCount);
                }

                bool exists = false;
                for (const QVariant& existing : bindings) {
                    if (existing.toMap() == binding) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    bindings.append(binding);
                }
            }
        }
    }

    return bindings;
}

QVariantList strategyRuleTemplateBindings(const QVariantMap& strategy)
{
    const QVariantMap parameters = strategy.value(QStringLiteral("parameters")).toMap();
    QVariantList bindings = extractRuleTemplateBindingsFromComposerState(parameters.value(QStringLiteral("rule_profile")).toMap());
    if (!bindings.isEmpty()) {
        return bindings;
    }

    bindings = extractRuleTemplateBindingsFromComposerState(strategy.value(QStringLiteral("ruleProfileSnapshot")).toMap());
    if (!bindings.isEmpty()) {
        return bindings;
    }

    bindings = normalizeRuleTemplateBindings(parameters.value(QStringLiteral("rule_template_bindings")));
    if (!bindings.isEmpty()) {
        return bindings;
    }
    bindings = normalizeRuleTemplateBindings(parameters.value(QStringLiteral("rule_template_binding")));
    if (!bindings.isEmpty()) {
        return bindings;
    }

    return {};
}

QVariantMap primaryRuleTemplateBinding(const QVariantList& bindings)
{
    QVariantMap fallback;
    for (const QVariant& bindingValue : bindings) {
        const QVariantMap binding = bindingValue.toMap();
        if (binding.isEmpty()) {
            continue;
        }
        const QString phase = binding.value(QStringLiteral("phase")).toString().trimmed().toLower();
        if (phase == QStringLiteral("signal") || phase == QStringLiteral("entry")) {
            return binding;
        }
        if (fallback.isEmpty()) {
            fallback = binding;
        }
    }
    return fallback;
}

void applyCanonicalStrategyStructures(QVariantMap& strategy);

bool hasCompleteEditableRulePayload(const QVariantMap& strategy)
{
    if (strategy.isEmpty()) {
        return false;
    }

    const QVariantMap parameters = strategy.value(QStringLiteral("parameters")).toMap();
    if (!parameters.isEmpty()) {
        const QVariantMap ruleProfile = parameters.value(QStringLiteral("rule_profile")).toMap();
        if (!ruleProfile.isEmpty()) {
            return true;
        }

        const QVariantMap composerState = parameters.value(QStringLiteral("rule_composer_state")).toMap();
        if (!composerState.isEmpty()) {
            return true;
        }

        if (!normalizeRuleTemplateBindings(parameters.value(QStringLiteral("rule_template_bindings"))).isEmpty()) {
            return true;
        }
        if (!normalizeRuleTemplateBindings(parameters.value(QStringLiteral("rule_template_binding"))).isEmpty()) {
            return true;
        }
    }

    if (!strategy.value(QStringLiteral("ruleProfileSnapshot")).toMap().isEmpty()) {
        return true;
    }

    return false;
}

QVariantMap recoverEditableRulePayloadFromBacktest(const QVariantMap& strategy)
{
    if (strategy.isEmpty() || hasCompleteEditableRulePayload(strategy)) {
        return strategy;
    }

    QVariantMap repairedStrategy = strategy;
    QVariantMap parameters = repairedStrategy.value(QStringLiteral("parameters")).toMap();

    auto mergeRuntimeParameters = [&](const QVariantMap& runtimeParameters) {
        if (runtimeParameters.isEmpty()) {
            return false;
        }

        bool changed = false;

        const QVariantMap runtimeRuleProfile = runtimeParameters.value(QStringLiteral("rule_profile")).toMap();
        if (parameters.value(QStringLiteral("rule_profile")).toMap().isEmpty() && !runtimeRuleProfile.isEmpty()) {
            parameters.insert(QStringLiteral("rule_profile"), runtimeRuleProfile);
            changed = true;
        }

        const QVariantMap runtimeComposerState = runtimeParameters.value(QStringLiteral("rule_composer_state")).toMap();
        if (parameters.value(QStringLiteral("rule_composer_state")).toMap().isEmpty() && !runtimeComposerState.isEmpty()) {
            parameters.insert(QStringLiteral("rule_composer_state"), runtimeComposerState);
            changed = true;
        }

        if (normalizeRuleTemplateBindings(parameters.value(QStringLiteral("rule_template_bindings"))).isEmpty()) {
            const QVariant runtimeBindings = runtimeParameters.value(QStringLiteral("rule_template_bindings"));
            if (!normalizeRuleTemplateBindings(runtimeBindings).isEmpty()) {
                parameters.insert(QStringLiteral("rule_template_bindings"), runtimeBindings);
                changed = true;
            }
        }

        if (normalizeRuleTemplateBindings(parameters.value(QStringLiteral("rule_template_binding"))).isEmpty()) {
            const QVariant runtimeBinding = runtimeParameters.value(QStringLiteral("rule_template_binding"));
            if (!normalizeRuleTemplateBindings(runtimeBinding).isEmpty()) {
                parameters.insert(QStringLiteral("rule_template_binding"), runtimeBinding);
                changed = true;
            }
        }

        const QVariantMap runtimeExecutionPolicy = runtimeParameters.value(QStringLiteral("execution_policy")).toMap();
        if (parameters.value(QStringLiteral("execution_policy")).toMap().isEmpty() && !runtimeExecutionPolicy.isEmpty()) {
            parameters.insert(QStringLiteral("execution_policy"), runtimeExecutionPolicy);
            changed = true;
        }

        const QVariantMap runtimeBacktestAssumptions = runtimeParameters.value(QStringLiteral("backtest_assumptions")).toMap();
        if (parameters.value(QStringLiteral("backtest_assumptions")).toMap().isEmpty() && !runtimeBacktestAssumptions.isEmpty()) {
            parameters.insert(QStringLiteral("backtest_assumptions"), runtimeBacktestAssumptions);
            changed = true;
        }

        const QVariantMap runtimeScopeContext = runtimeParameters.value(QStringLiteral("strategy_scope_context")).toMap();
        if (parameters.value(QStringLiteral("strategy_scope_context")).toMap().isEmpty() && !runtimeScopeContext.isEmpty()) {
            parameters.insert(QStringLiteral("strategy_scope_context"), runtimeScopeContext);
            changed = true;
        }

        if (changed) {
            repairedStrategy.insert(QStringLiteral("parameters"), parameters);
            applyCanonicalStrategyStructures(repairedStrategy);
        }

        return changed;
    };

    const QVariantMap performanceMetrics = repairedStrategy.value(QStringLiteral("performance_metrics")).toMap();
    const QVariantMap latestBacktest = performanceMetrics.value(QStringLiteral("latestBacktest")).toMap();
    if (mergeRuntimeParameters(latestBacktest.value(QStringLiteral("runtimeParameters")).toMap())) {
        qInfo() << "StrategyService: recovered editable rule payload from latestBacktest"
                << repairedStrategy.value(QStringLiteral("strategy_id")).toString();
        return repairedStrategy;
    }

    const QVariantList backtestHistory = performanceMetrics.value(QStringLiteral("backtestHistory")).toList();
    for (const QVariant& historyEntryValue : backtestHistory) {
        const QVariantMap historyEntry = historyEntryValue.toMap();
        if (mergeRuntimeParameters(historyEntry.value(QStringLiteral("runtimeParameters")).toMap())) {
            qInfo() << "StrategyService: recovered editable rule payload from backtestHistory"
                    << repairedStrategy.value(QStringLiteral("strategy_id")).toString();
            return repairedStrategy;
        }
    }

    return repairedStrategy;
}

bool validateRuleTemplateBindings(const QVariantList& bindings, QString* errorMessage = nullptr)
{
    if (bindings.isEmpty()) {
        if (errorMessage) {
            errorMessage->clear();
        }
        return true;
    }
    return !bridge::rules::loadCompiledRuleTemplates(bindings, errorMessage).isEmpty();
}

void clearStrategySymbolPoolBindings(QVariantMap* strategy)
{
    if (!strategy) {
        return;
    }

    strategy->remove(QStringLiteral("symbol_pool"));
    strategy->remove(QStringLiteral("symbolPool"));

    QVariantMap parameters = strategy->value(QStringLiteral("parameters")).toMap();
    parameters.remove(QStringLiteral("symbol_pool"));
    parameters.remove(QStringLiteral("symbolPool"));
    strategy->insert(QStringLiteral("parameters"), parameters);
}

QStringList linkedStrategyLiveSymbolPool(const QVariantMap& strategy)
{
    const QVariantMap parameters = strategy.value(QStringLiteral("parameters")).toMap();

    QStringList symbols = symbolPoolFromVariant(parameters.value(QStringLiteral("linked_stock_pool_symbols")));
    if (!symbols.isEmpty()) {
        return symbols;
    }

    symbols = symbolPoolFromVariant(parameters.value(QStringLiteral("linkedStockPoolSymbols")));
    if (!symbols.isEmpty()) {
        return symbols;
    }

    return {};
}

QStringList strategyLiveSymbolPool(const QVariantMap& strategy)
{
    const QStringList linkedSymbols = linkedStrategyLiveSymbolPool(strategy);
    if (!linkedSymbols.isEmpty()) {
        return linkedSymbols;
    }

    return strategySymbolPool(strategy);
}

bool strategyAllowsMarketSymbol(const QVariantMap& strategy, const QString& marketSymbol)
{
    const QStringList symbolPool = strategyLiveSymbolPool(strategy);
    if (symbolPool.isEmpty()) {
        return false;
    }

    return symbolPool.contains(normalizeStrategySymbol(marketSymbol));
}

QVariantMap loadTradingConfigurationSnapshot()
{
    TradingConnectionConfigService* configService = TradingConnectionConfigService::instance();
    if (!configService) {
        return {};
    }

    QVariantMap configuration = configService->currentConfiguration();
    if (configuration.isEmpty()) {
        configuration = configService->loadConfiguration();
    }
    return configuration;
}

QVariantMap loadRiskConfigurationSnapshot()
{
    RiskConfigService* riskConfigService = RiskConfigService::instance();
    if (!riskConfigService) {
        return {};
    }

    QVariantMap riskConfiguration = riskConfigService->appliedConfiguration();
    if (riskConfiguration.isEmpty()) {
        riskConfiguration = riskConfigService->currentConfiguration();
    }
    return riskConfiguration;
}

QVariant firstConfiguredValue(const QVariantMap& map, const QStringList& keys)
{
    for (const QString& key : keys) {
        if (!map.contains(key)) {
            continue;
        }

        const QVariant value = map.value(key);
        if (!value.isValid() || value.isNull()) {
            continue;
        }

        if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
            continue;
        }

        return value;
    }

    return {};
}

double numericParam(const QVariantMap& map, const QStringList& keys, double fallback)
{
    const QVariant rawValue = firstConfiguredValue(map, keys);
    if (!rawValue.isValid()) {
        return fallback;
    }

    bool ok = false;
    const double numericValue = rawValue.toDouble(&ok);
    return ok ? numericValue : fallback;
}

double normalizedRatio(double value, double fallback)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return fallback;
    }
    return value > 1.0 ? value / 100.0 : value;
}

double numericRatioParam(const QVariantMap& map, const QStringList& keys, double fallback)
{
    const QVariant rawValue = firstConfiguredValue(map, keys);
    if (!rawValue.isValid()) {
        return fallback;
    }

    bool ok = false;
    const double numericValue = rawValue.toDouble(&ok);
    if (!ok) {
        return fallback;
    }
    return normalizedRatio(numericValue, fallback);
}

double resolvedRatioLimit(const QVariantMap& primary,
                          const QVariantMap& secondary,
                          const QStringList& keys,
                          double fallback)
{
    const double primaryValue = numericRatioParam(primary, keys, -1.0);
    const double secondaryValue = numericRatioParam(secondary, keys, -1.0);
    if (primaryValue > 0.0 && secondaryValue > 0.0) {
        return std::min(primaryValue, secondaryValue);
    }
    if (primaryValue > 0.0) {
        return primaryValue;
    }
    if (secondaryValue > 0.0) {
        return secondaryValue;
    }
    return fallback;
}

double resolvedNumericLimit(const QVariantMap& primary,
                            const QVariantMap& secondary,
                            const QStringList& keys,
                            double fallback)
{
    const double primaryValue = numericParam(primary, keys, -1.0);
    const double secondaryValue = numericParam(secondary, keys, -1.0);
    if (primaryValue > 0.0 && secondaryValue > 0.0) {
        return std::min(primaryValue, secondaryValue);
    }
    if (primaryValue > 0.0) {
        return primaryValue;
    }
    if (secondaryValue > 0.0) {
        return secondaryValue;
    }
    return fallback;
}

QVariantMap positionSnapshotForSymbol(const QVariantList& positions, const QString& symbol)
{
    const QString normalizedSymbol = normalizeStrategySymbol(symbol);
    for (const QVariant& rawPosition : positions) {
        const QVariantMap position = rawPosition.toMap();
        if (normalizeStrategySymbol(position.value(QStringLiteral("symbol")).toString()) == normalizedSymbol) {
            return position;
        }
    }

    return {};
}

qint64 closeableQuantityForPosition(const QVariantMap& position)
{
    return static_cast<qint64>(numericParam(
        position,
        {QStringLiteral("closeableQuantity"), QStringLiteral("closeable_quantity"), QStringLiteral("availableQuantity"), QStringLiteral("available_quantity"), QStringLiteral("quantity")},
        0.0));
}

double symbolMarketValue(const QVariantList& positions, const QString& symbol)
{
    return numericParam(positionSnapshotForSymbol(positions, symbol),
                        {QStringLiteral("marketValue"), QStringLiteral("market_value")},
                        0.0);
}

double totalAssetValue(const QVariantMap& accountSnapshot)
{
    const double totalAsset = numericParam(
        accountSnapshot,
        {QStringLiteral("totalAsset"), QStringLiteral("total_asset"), QStringLiteral("nav")},
        0.0);
    if (totalAsset > 0.0) {
        return totalAsset;
    }

    const double availableCash = numericParam(
        accountSnapshot,
        {QStringLiteral("availableCash"), QStringLiteral("available_cash")},
        0.0);
    const double marketValue = numericParam(
        accountSnapshot,
        {QStringLiteral("marketValue"), QStringLiteral("market_value")},
        0.0);
    return availableCash + marketValue;
}

qint64 boardLotQuantity(double notionalBudget, double price)
{
    if (!std::isfinite(notionalBudget) || notionalBudget <= 0.0 || price <= 0.0) {
        return 0;
    }

    const double boardLots = std::floor(notionalBudget / price / 100.0);
    return boardLots > 0.0 ? static_cast<qint64>(boardLots) * 100 : 0;
}

qint64 shareQuantity(double notionalBudget, double price)
{
    if (!std::isfinite(notionalBudget) || notionalBudget <= 0.0 || price <= 0.0) {
        return 0;
    }

    const double rawQuantity = std::floor(notionalBudget / price);
    return rawQuantity > 0.0 ? static_cast<qint64>(rawQuantity) : 0;
}

struct RuntimeOrderSizingResult {
    qint64 quantity = 0;
    double requestedNotional = 0.0;
    QString side;
    QString positionEffect;
    QString failureReason;
    QString failureMessage;
};

bool normalizedRuntimeExecutionOption(const QVariant& value, bool fallback = false)
{
    if (!value.isValid() || value.isNull()) {
        return fallback;
    }

    if (value.typeId() == QMetaType::Bool) {
        return value.toBool();
    }

    const QString normalized = value.toString().trimmed().toLower();
    if (normalized.isEmpty()) {
        return fallback;
    }

    return normalized == QStringLiteral("true")
        || normalized == QStringLiteral("1")
        || normalized == QStringLiteral("yes")
        || normalized == QStringLiteral("y");
}

int normalizedRuntimeBatchCount(const QVariant& value)
{
    bool ok = false;
    const int numericValue = value.toInt(&ok);
    return ok && numericValue > 0 ? numericValue : 0;
}

double normalizedRuntimeBatchLimit(const QVariant& value)
{
    bool ok = false;
    const double numericValue = value.toDouble(&ok);
    return ok && numericValue > 0.0 ? numericValue : 0.0;
}

QVariantMap resolveRuntimeExecutionBatchOptions(const QVariantMap& executionPolicy)
{
    const QVariantMap batchPolicy = executionPolicy.value(QStringLiteral("batchExecution")).toMap();

    const int maxBatchOrders = normalizedRuntimeBatchCount(firstConfiguredValue(
        batchPolicy,
        {QStringLiteral("maxBatchOrders"), QStringLiteral("batchOrderLimit")}));
    double maxBatchNotionalWan = normalizedRuntimeBatchLimit(firstConfiguredValue(
        batchPolicy,
        {QStringLiteral("maxBatchNotionalWan"), QStringLiteral("batchNotionalLimitWan")}));
    const double maxBatchNotionalAbsolute = normalizedRuntimeBatchLimit(firstConfiguredValue(
        batchPolicy,
        {QStringLiteral("maxBatchNotional"), QStringLiteral("batchNotionalLimit")}));
    const double maxBatchNotional = maxBatchNotionalAbsolute > 0.0
        ? maxBatchNotionalAbsolute
        : maxBatchNotionalWan * 10000.0;
    if (maxBatchNotionalWan <= 0.0 && maxBatchNotional > 0.0) {
        maxBatchNotionalWan = maxBatchNotional / 10000.0;
    }

    const bool waitPreviousBatchFilled = normalizedRuntimeExecutionOption(
        firstConfiguredValue(batchPolicy, {QStringLiteral("waitPreviousBatchFilled")}),
        true);
    const bool pauseOnConflict = normalizedRuntimeExecutionOption(
        firstConfiguredValue(batchPolicy, {QStringLiteral("pauseOnConflict")}),
        false);
    const bool pauseOnAbnormalReject = normalizedRuntimeExecutionOption(
        firstConfiguredValue(batchPolicy, {QStringLiteral("pauseOnAbnormalReject")}),
        false);
    const int manualCheckpointBatchIndex = normalizedRuntimeBatchCount(firstConfiguredValue(
        batchPolicy,
        {QStringLiteral("manualCheckpointBatchIndex")}));
    const bool requireManualCheckpoint = manualCheckpointBatchIndex > 0 || normalizedRuntimeExecutionOption(
        firstConfiguredValue(batchPolicy, {QStringLiteral("requireManualCheckpoint")}),
        false);

    return QVariantMap{{QStringLiteral("maxBatchOrders"), maxBatchOrders},
                       {QStringLiteral("waitPreviousBatchFilled"), waitPreviousBatchFilled},
                       {QStringLiteral("pauseOnConflict"), pauseOnConflict},
                       {QStringLiteral("pauseOnAbnormalReject"), pauseOnAbnormalReject},
                       {QStringLiteral("requireManualCheckpoint"), requireManualCheckpoint},
                       {QStringLiteral("manualCheckpointBatchIndex"), manualCheckpointBatchIndex},
                       {QStringLiteral("maxBatchNotional"), maxBatchNotional},
                       {QStringLiteral("maxBatchNotionalWan"), maxBatchNotionalWan}};
}

QVariantMap resolvedRuntimeExecutionPolicy(const QVariantMap& strategy)
{
    const QVariantMap executionPolicySnapshot = strategy.value(QStringLiteral("executionPolicySnapshot")).toMap();
    if (!executionPolicySnapshot.isEmpty()) {
        return executionPolicySnapshot;
    }

    return strategy.value(QStringLiteral("parameters")).toMap().value(QStringLiteral("execution_policy")).toMap();
}

void applyRuntimeExecutionPlanMetadata(QVariantMap& request,
                                       const QVariantMap& executionPolicy)
{
    QVariantList plannedOrders{request};
    const QVariantMap batchOptions = resolveRuntimeExecutionBatchOptions(executionPolicy);
    const QVariantMap batchPlan = portfolio_execution_plan::buildSellFirstExecutionBatches(
        &plannedOrders,
        batchOptions);
    const QVariantList annotatedOrders = plannedOrders;
    if (!annotatedOrders.isEmpty()) {
        const QVariantMap annotatedOrder = annotatedOrders.first().toMap();
        for (auto it = annotatedOrder.constBegin(); it != annotatedOrder.constEnd(); ++it) {
            if (!request.contains(it.key()) || it.key().startsWith(QStringLiteral("batch"))
                || it.key().startsWith(QStringLiteral("execution"))
                || it.key().startsWith(QStringLiteral("previousBatch"))
                || it.key().startsWith(QStringLiteral("nextBatch"))
                || it.key().startsWith(QStringLiteral("requires"))
                || it.key().startsWith(QStringLiteral("pauseOn"))
                || it.key().startsWith(QStringLiteral("blocksFollowing"))
                || it.key() == QStringLiteral("manualCheckpointBatchIndex")) {
                request.insert(it.key(), it.value());
            }
        }
    }

    const QVariantMap batchSummary = batchPlan.value(QStringLiteral("summary")).toMap();
    if (!batchSummary.isEmpty()) {
        request.insert(QStringLiteral("executionBatchSummary"), batchSummary);
    }

    if (!request.value(QStringLiteral("batchId")).toString().trimmed().isEmpty()) {
        return;
    }

    const QString side = request.value(QStringLiteral("side")).toString().trimmed().toUpper();
    if (side.isEmpty()) {
        return;
    }

    const bool requireManualCheckpoint = batchOptions.value(QStringLiteral("requireManualCheckpoint")).toBool();
    const int manualCheckpointBatchIndex = batchOptions.value(QStringLiteral("manualCheckpointBatchIndex")).toInt();
    const int effectiveManualCheckpointBatchIndex = requireManualCheckpoint
        ? (manualCheckpointBatchIndex > 0 ? manualCheckpointBatchIndex : 1)
        : 0;
    const QString executionScopeId = portfolio_execution_plan::executionScopeIdForOrders(QVariantList{request});

    request.insert(QStringLiteral("batchId"), portfolio_execution_plan::batchIdForIndex(0));
    request.insert(QStringLiteral("batchIndex"), 0);
    request.insert(QStringLiteral("executionSequence"), 0);
    request.insert(QStringLiteral("batchRole"), portfolio_execution_plan::batchRoleForSide(side));
    request.insert(QStringLiteral("batchPhase"), portfolio_execution_plan::batchPhaseForSide(side));
    request.insert(QStringLiteral("batchOrderCount"), 1);
    request.insert(QStringLiteral("executionScopeId"), executionScopeId);
    request.insert(QStringLiteral("requiresPreviousBatchFilled"), false);
    request.insert(QStringLiteral("pauseOnConflict"), batchOptions.value(QStringLiteral("pauseOnConflict")).toBool());
    request.insert(QStringLiteral("pauseOnAbnormalReject"), batchOptions.value(QStringLiteral("pauseOnAbnormalReject")).toBool());
    request.insert(QStringLiteral("requiresManualCheckpoint"), false);
    request.insert(QStringLiteral("manualCheckpointBatchIndex"), effectiveManualCheckpointBatchIndex);
    request.insert(QStringLiteral("blocksFollowingBatches"), false);
}

RuntimeOrderSizingResult deriveRuntimeOrderSizing(const QVariantMap& strategy,
                                                  const QVariantMap& evaluation,
                                                  const QVariantMap& riskConfiguration)
{
    RuntimeOrderSizingResult result;

    const QString action = evaluation.value(QStringLiteral("candidateAction")).toString().trimmed().toUpper();
    const QString symbol = evaluation.value(QStringLiteral("symbol")).toString().trimmed().toUpper();
    const double price = evaluation.value(QStringLiteral("latestPrice")).toDouble();
    if (symbol.isEmpty() || price <= 0.0 || (action != QStringLiteral("BUY") && action != QStringLiteral("SELL"))) {
        result.failureReason = QStringLiteral("runtime_order_parameters_invalid");
        result.failureMessage = QStringLiteral("运行时候选信号缺少有效的价格、标的或方向");
        return result;
    }

    PositionAccountService* positionAccountService = PositionAccountService::instance();
    if (!positionAccountService || !positionAccountService->isInitialized() || !positionAccountService->initialSnapshotLoaded()) {
        result.failureReason = QStringLiteral("position_account_snapshot_unavailable");
        result.failureMessage = QStringLiteral("账户与持仓快照尚未就绪，禁止自动提交运行时委托");
        return result;
    }

    const QVariantMap accountSnapshot = positionAccountService->accountSnapshot();
    const QVariantList positions = positionAccountService->positions();
    const QVariantMap ruleProfile = strategy.value(QStringLiteral("ruleProfileSnapshot")).toMap();
    const QVariantMap executionPolicy = resolvedRuntimeExecutionPolicy(strategy);

    const double orderSizeLimitWan = resolvedNumericLimit(
        executionPolicy,
        riskConfiguration,
        risk::config::orderSizeLimitKeys(),
        100.0);
    const double orderSizeLimitNotional = orderSizeLimitWan > 0.0
        ? orderSizeLimitWan * 10000.0
        : std::numeric_limits<double>::max();

    result.side = action;
    result.positionEffect = action == QStringLiteral("SELL")
        ? QStringLiteral("CLOSE")
        : QStringLiteral("OPEN");

    if (action == QStringLiteral("SELL")) {
        const qint64 closeableQuantity = closeableQuantityForPosition(positionSnapshotForSymbol(positions, symbol));
        if (closeableQuantity <= 0) {
            result.failureReason = QStringLiteral("no_closeable_position");
            result.failureMessage = QStringLiteral("当前无可卖持仓，运行时候选卖出信号不会自动下单");
            return result;
        }

        qint64 quantity = closeableQuantity;
        const qint64 orderCapQuantity = shareQuantity(orderSizeLimitNotional, price);
        if (orderCapQuantity > 0) {
            quantity = std::min(quantity, orderCapQuantity);
        }

        if (quantity <= 0) {
            result.failureReason = QStringLiteral("order_budget_below_min_quantity");
            result.failureMessage = QStringLiteral("当前单笔委托上限不足以形成可执行卖出数量");
            return result;
        }

        result.quantity = quantity;
        result.requestedNotional = price * static_cast<double>(quantity);
        return result;
    }

    const double availableCash = numericParam(
        accountSnapshot,
        {QStringLiteral("availableCash"), QStringLiteral("available_cash")},
        0.0);
    if (availableCash <= 0.0) {
        result.failureReason = QStringLiteral("insufficient_available_cash");
        result.failureMessage = QStringLiteral("当前可用资金不足，运行时候选买入信号不会自动下单");
        return result;
    }

    const double totalAsset = totalAssetValue(accountSnapshot);
    const double marketValue = numericParam(
        accountSnapshot,
        {QStringLiteral("marketValue"), QStringLiteral("market_value")},
        0.0);
    const double currentSymbolExposure = symbolMarketValue(positions, symbol);
    const double maxPositionRatio = resolvedRatioLimit(
        ruleProfile,
        riskConfiguration,
        risk::config::maxPositionPercentKeys(),
        0.15);
    const double maxTotalExposureRatio = resolvedRatioLimit(
        ruleProfile,
        riskConfiguration,
        risk::config::maxTotalExposureKeys(),
        0.67);

    const double symbolBudget = totalAsset > 0.0
        ? std::max(0.0, (totalAsset * maxPositionRatio) - currentSymbolExposure)
        : 0.0;
    const double exposureBudget = totalAsset > 0.0
        ? std::max(0.0, (totalAsset * maxTotalExposureRatio) - marketValue)
        : 0.0;
    double budgetNotional = std::min(availableCash, orderSizeLimitNotional);
    if (symbolBudget > 0.0) {
        budgetNotional = std::min(budgetNotional, symbolBudget);
    }
    if (exposureBudget > 0.0) {
        budgetNotional = std::min(budgetNotional, exposureBudget);
    }

    if (budgetNotional <= 0.0) {
        result.failureReason = QStringLiteral("runtime_position_budget_exhausted");
        result.failureMessage = QStringLiteral("当前仓位或资金预算已耗尽，运行时候选买入信号不会自动下单");
        return result;
    }

    const qint64 quantity = boardLotQuantity(budgetNotional, price);
    if (quantity <= 0) {
        result.failureReason = QStringLiteral("order_budget_below_board_lot");
        result.failureMessage = QStringLiteral("当前预算不足以形成一手整股委托，运行时候选买入信号不会自动下单");
        return result;
    }

    result.quantity = quantity;
    result.requestedNotional = price * static_cast<double>(quantity);
    return result;
}

QVariantMap buildRuntimeAutoOrderRequest(const QVariantMap& strategy,
                                         const QVariantMap& evaluation,
                                         const QVariantMap& tradingConfiguration,
                                         const RuntimeOrderSizingResult& sizing)
{
    QVariantMap request;
    const QString trackingOrderId = QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string());
    request.insert(QStringLiteral("orderId"), trackingOrderId);
    request.insert(QStringLiteral("clientOrderId"), trackingOrderId);
    request.insert(QStringLiteral("strategyId"), evaluation.value(QStringLiteral("strategyId")).toString());
    request.insert(QStringLiteral("strategyName"), evaluation.value(QStringLiteral("strategyName")).toString());
    request.insert(QStringLiteral("runtimeStrategyId"),
                   tradingConfiguration.value(QStringLiteral("runtimeStrategyId"),
                                              tradingConfiguration.value(QStringLiteral("gmStrategyId"))).toString().trimmed());
    request.insert(QStringLiteral("symbol"), evaluation.value(QStringLiteral("symbol")).toString().trimmed().toUpper());
    request.insert(QStringLiteral("side"), sizing.side);
    request.insert(QStringLiteral("action"), evaluation.value(QStringLiteral("candidateAction")).toString().trimmed().toUpper());
    request.insert(QStringLiteral("positionEffect"), sizing.positionEffect);
    request.insert(QStringLiteral("price"), evaluation.value(QStringLiteral("latestPrice")).toDouble());
    request.insert(QStringLiteral("referencePrice"), evaluation.value(QStringLiteral("referencePrice")).toDouble());
    request.insert(QStringLiteral("quantity"), sizing.quantity);
    request.insert(QStringLiteral("requestedNotional"), sizing.requestedNotional);
    request.insert(QStringLiteral("strength"), evaluation.value(QStringLiteral("candidateStrength")).toDouble());
    request.insert(QStringLiteral("orderType"), QStringLiteral("LIMIT"));
    request.insert(QStringLiteral("mode"), QStringLiteral("stock"));
    request.insert(QStringLiteral("riskActionSource"), QStringLiteral("runtime_rule_candidate"));
    request.insert(QStringLiteral("marketEventType"), evaluation.value(QStringLiteral("marketEventType")).toString());
    request.insert(QStringLiteral("runtimeRuleDecision"), evaluation.value(QStringLiteral("decision")).toString());
    request.insert(QStringLiteral("runtimeRuleGate"), evaluation.value(QStringLiteral("gate")).toString());
    request.insert(QStringLiteral("runtimeRuleReason"), evaluation.value(QStringLiteral("reason")).toString());
    request.insert(QStringLiteral("strategyType"), strategy.value(QStringLiteral("strategy_type")).toString().trimmed().toUpper());
    applyRuntimeExecutionPlanMetadata(request, resolvedRuntimeExecutionPolicy(strategy));
    return request;
}

bool runtimeAutoExecutionConfigured(const QVariantMap& tradingConfiguration)
{
    return tradingConfiguration.value(QStringLiteral("autoExecuteRuntimeCandidates"), false).toBool();
}

QVariantMap currentMarketSessionSnapshot()
{
    TradingMarketCalendarService* calendarService = TradingMarketCalendarService::instance();
    if (!calendarService) {
        return {};
    }

    return calendarService->currentSessionSnapshot();
}

bridge::config::StrategyStructureResolverSet& strategyStructureResolverSet()
{
    static bridge::config::StrategyStructureResolverSet resolverSet;
    return resolverSet;
}

void applyCanonicalStrategyStructures(QVariantMap& strategy);

void applyCanonicalStrategyStructures(QVariantMap& strategy)
{
    if (strategy.isEmpty()) {
        return;
    }

    QVariantMap parameters = strategy.value(QStringLiteral("parameters")).toMap();
    const bridge::config::StrategyStructureResolution resolution = strategyStructureResolverSet().resolve(strategy);

    auto applyStructuredMap = [&](const QString& embeddedKey,
                                 const QString& snapshotKey,
                                 const QVariantMap& values) {
        if (values.isEmpty()) {
            return;
        }

        parameters.insert(embeddedKey, values);
        strategy.insert(snapshotKey, values);
    };

    applyStructuredMap(QStringLiteral("rule_profile"),
                       QStringLiteral("ruleProfileSnapshot"),
                       resolution.ruleProfile);
    applyStructuredMap(QStringLiteral("execution_policy"),
                       QStringLiteral("executionPolicySnapshot"),
                       resolution.executionPolicy);
    applyStructuredMap(QStringLiteral("backtest_assumptions"),
                       QStringLiteral("backtestAssumptionsSnapshot"),
                       resolution.backtestAssumptions);
    applyStructuredMap(QStringLiteral("strategy_scope_context"),
                       QStringLiteral("strategyScopeContextSnapshot"),
                       resolution.strategyScopeContext);

    strategy.insert(QStringLiteral("parameters"), parameters);
}

bool isConfiguredRuleDefaultValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return false;
    }

    if (value.typeId() == QMetaType::QString) {
        return !value.toString().trimmed().isEmpty();
    }

    return true;
}

void mergeMissingRuleDefaults(QVariantMap& target, const QVariantMap& defaults)
{
    for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it) {
        if (!isConfiguredRuleDefaultValue(it.value())) {
            continue;
        }
        if (!isConfiguredRuleDefaultValue(target.value(it.key()))) {
            target.insert(it.key(), it.value());
        }
    }
}

void applyRuntimeRuleDefaults(QVariantMap& strategy, const QVariantMap& tradingConfiguration)
{
    const QVariantMap runtimeRuleDefaults = tradingConfiguration.value(QStringLiteral("runtimeRuleDefaults")).toMap();
    if (runtimeRuleDefaults.isEmpty()) {
        return;
    }

    QVariantMap parameters = strategy.value(QStringLiteral("parameters")).toMap();

    QVariantMap ruleProfile = strategy.value(QStringLiteral("ruleProfileSnapshot")).toMap();
    mergeMissingRuleDefaults(ruleProfile, runtimeRuleDefaults.value(QStringLiteral("ruleProfile")).toMap());
    if (!ruleProfile.isEmpty()) {
        strategy.insert(QStringLiteral("ruleProfileSnapshot"), ruleProfile);
        parameters.insert(QStringLiteral("rule_profile"), ruleProfile);
    }

    QVariantMap executionPolicy = strategy.value(QStringLiteral("executionPolicySnapshot")).toMap();
    mergeMissingRuleDefaults(executionPolicy, runtimeRuleDefaults.value(QStringLiteral("executionPolicy")).toMap());
    if (!executionPolicy.isEmpty()) {
        strategy.insert(QStringLiteral("executionPolicySnapshot"), executionPolicy);
        parameters.insert(QStringLiteral("execution_policy"), executionPolicy);
    }

    QVariantMap backtestAssumptions = strategy.value(QStringLiteral("backtestAssumptionsSnapshot")).toMap();
    mergeMissingRuleDefaults(backtestAssumptions, runtimeRuleDefaults.value(QStringLiteral("backtestAssumptions")).toMap());
    if (!backtestAssumptions.isEmpty()) {
        strategy.insert(QStringLiteral("backtestAssumptionsSnapshot"), backtestAssumptions);
        parameters.insert(QStringLiteral("backtest_assumptions"), backtestAssumptions);
    }

    QVariantMap strategyScopeContext = strategy.value(QStringLiteral("strategyScopeContextSnapshot")).toMap();
    mergeMissingRuleDefaults(strategyScopeContext, runtimeRuleDefaults.value(QStringLiteral("strategyScopeContext")).toMap());
    if (!strategyScopeContext.isEmpty()) {
        strategy.insert(QStringLiteral("strategyScopeContextSnapshot"), strategyScopeContext);
        parameters.insert(QStringLiteral("strategy_scope_context"), strategyScopeContext);

        const QVariant scopeSymbolPool = strategyScopeContext.value(QStringLiteral("symbol_pool"),
            strategyScopeContext.value(QStringLiteral("symbolPool")));
        if (isConfiguredRuleDefaultValue(scopeSymbolPool)) {
            if (!isConfiguredRuleDefaultValue(strategy.value(QStringLiteral("symbol_pool")))) {
                strategy.insert(QStringLiteral("symbol_pool"), scopeSymbolPool);
            }
            if (!isConfiguredRuleDefaultValue(parameters.value(QStringLiteral("symbol_pool")))) {
                parameters.insert(QStringLiteral("symbol_pool"), scopeSymbolPool);
            }
        }

        const QVariant executionTimeframe = strategyScopeContext.value(QStringLiteral("executionTimeframe"),
            strategyScopeContext.value(QStringLiteral("execution_timeframe")));
        if (isConfiguredRuleDefaultValue(executionTimeframe)
                && !isConfiguredRuleDefaultValue(parameters.value(QStringLiteral("execution_timeframe")))) {
            parameters.insert(QStringLiteral("execution_timeframe"), executionTimeframe);
        }
    }

    strategy.insert(QStringLiteral("parameters"), parameters);
}

QSet<QString> configuredBoundStrategyIds(const QVariantMap& configuration)
{
    QSet<QString> strategyIds;

    const QVariantList boundStrategies = configuration.value(QStringLiteral("boundStrategies")).toList();
    for (const QVariant& rawEntry : boundStrategies) {
        const QVariantMap entry = rawEntry.toMap();
        const QString strategyId = entry.value(QStringLiteral("strategyId"),
            entry.value(QStringLiteral("strategy_id"), entry.value(QStringLiteral("id")))).toString().trimmed();
        if (!strategyId.isEmpty()) {
            strategyIds.insert(strategyId);
        }
    }

    const QString primaryStrategyId = configuration.value(QStringLiteral("boundStrategyId")).toString().trimmed();
    if (!primaryStrategyId.isEmpty()) {
        strategyIds.insert(primaryStrategyId);
    }

    return strategyIds;
}

QString resolvedStrategyIdentifier(const QVariantMap& strategy)
{
    return firstNonEmptyStrategyText(strategy, {"strategy_id", "strategyId"});
}

bool runtimeSessionIsReady(const QVariantMap& runtimeSessionSnapshot)
{
    return !runtimeSessionSnapshot.isEmpty()
        && runtimeSessionSnapshot.value(QStringLiteral("initialized")).toBool()
        && runtimeSessionSnapshot.value(QStringLiteral("connected")).toBool()
        && runtimeSessionSnapshot.value(QStringLiteral("isRunning")).toBool()
        && !runtimeSessionSnapshot.value(QStringLiteral("hasError")).toBool();
}

QVariantMap effectiveRuntimeSessionSnapshot(const QVariantMap& runtimeSessionSnapshot)
{
    if (runtimeSessionSnapshot.value(QStringLiteral("source")).toString().trimmed()
            == QStringLiteral("default")) {
        return {};
    }
    return runtimeSessionSnapshot;
}

QVariantMap runtimeSessionSnapshotForStrategy(TradingRuntimeStatusService* runtimeStatusService,
                                             const QVariantMap& tradingConfiguration,
                                             const QVariantMap& strategy)
{
    if (!runtimeStatusService) {
        return {};
    }

    const QString strategyId = resolvedStrategyIdentifier(strategy);
    const QString primaryStrategyId = tradingConfiguration.value(QStringLiteral("boundStrategyId")).toString().trimmed();
    const QString runtimeStrategyId = tradingConfiguration.value(QStringLiteral("runtimeStrategyId")).toString().trimmed();
    const QString accountId = tradingConfiguration.value(QStringLiteral("accountId")).toString().trimmed();

    if (!runtimeStrategyId.isEmpty() && strategyId == primaryStrategyId) {
        const QVariantMap runtimeSnapshot = runtimeStatusService->sessionSnapshotForStrategy(runtimeStrategyId);
        if (!runtimeSnapshot.isEmpty()) {
            return effectiveRuntimeSessionSnapshot(runtimeSnapshot);
        }
    }

    if (!strategyId.isEmpty()) {
        const QVariantMap runtimeSnapshot = runtimeStatusService->sessionSnapshotForStrategy(strategyId);
        if (!runtimeSnapshot.isEmpty()) {
            return effectiveRuntimeSessionSnapshot(runtimeSnapshot);
        }
    }

    if (!runtimeStrategyId.isEmpty()) {
        const QVariantMap runtimeSnapshot = runtimeStatusService->sessionSnapshotForStrategy(runtimeStrategyId);
        if (!runtimeSnapshot.isEmpty()) {
            return effectiveRuntimeSessionSnapshot(runtimeSnapshot);
        }
    }

    if (!accountId.isEmpty()) {
        return effectiveRuntimeSessionSnapshot(runtimeStatusService->sessionSnapshotForAccount(accountId));
    }

    return {};
}

bool matchesStrategyType(const QVariantMap& strategy, const QString& strategyType)
{
    const QString normalizedType = strategyType.trimmed();
    if (normalizedType.isEmpty() || normalizedType.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    const QString primaryType = firstNonEmptyStrategyText(strategy, {"strategy_type", "strategyType"});
    const QString subType = firstNonEmptyStrategyText(strategy, {"sub_type", "subType"});
    return primaryType == normalizedType || subType == normalizedType;
}

bool matchesStrategyStatus(const QVariantMap& strategy, const QString& status)
{
    const QString normalizedStatus = status.trimmed();
    if (normalizedStatus.isEmpty() || normalizedStatus.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    return firstNonEmptyStrategyText(strategy, {"status"}) == normalizedStatus;
}

bool matchesStrategyKeyword(const QVariantMap& strategy, const QString& keyword)
{
    const QString normalizedKeyword = keyword.trimmed();
    if (normalizedKeyword.isEmpty()) {
        return true;
    }

    const QStringList searchableTexts = {
        firstNonEmptyStrategyText(strategy, {"strategy_name", "strategyName"}),
        firstNonEmptyStrategyText(strategy, {"description"}),
        firstNonEmptyStrategyText(strategy, {"strategy_code", "strategyCode"}),
        firstNonEmptyStrategyText(strategy, {"strategy_type", "strategyType"}),
        firstNonEmptyStrategyText(strategy, {"sub_type", "subType"}),
        firstNonEmptyStrategyText(strategy, {"asset_type", "assetType"}),
        firstNonEmptyStrategyText(strategy, {"time_frame", "timeFrame"})
    };

    for (const QString& text : searchableTexts) {
        if (!text.isEmpty() && text.contains(normalizedKeyword, Qt::CaseInsensitive)) {
            return true;
        }
    }

    const QStringList tags = strategyTags(strategy);
    for (const QString& tag : tags) {
        if (tag.contains(normalizedKeyword, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

QString eventStringValue(const engine::EventFormat& event, const std::string& key)
{
    const auto metadataIt = event.metadata.find(key);
    if (metadataIt != event.metadata.end()) {
        return QString::fromStdString(metadataIt->second).trimmed();
    }

    auto dataValue = event.get<std::string>(key);
    if (dataValue.has_value()) {
        return QString::fromStdString(*dataValue).trimmed();
    }

    auto dataInt = event.get<int64_t>(key);
    if (dataInt.has_value()) {
        return QString::number(*dataInt);
    }

    auto dataDouble = event.get<double>(key);
    if (dataDouble.has_value()) {
        return QString::number(*dataDouble, 'f', 6);
    }

    return {};
}

double eventNumericValue(const engine::EventFormat& event, const std::string& key, double fallback = 0.0)
{
    auto dataDouble = event.get<double>(key);
    if (dataDouble.has_value()) {
        return *dataDouble;
    }

    auto dataInt = event.get<int64_t>(key);
    if (dataInt.has_value()) {
        return static_cast<double>(*dataInt);
    }

    const QString metadataValue = eventStringValue(event, key);
    if (metadataValue.isEmpty()) {
        return fallback;
    }

    bool ok = false;
    const double numericValue = metadataValue.toDouble(&ok);
    return ok ? numericValue : fallback;
}

QVariantMap buildEventFactSnapshot(const engine::EventFormat& event)
{
    QVariantMap facts;
    for (const auto& entry : event.metadata) {
        facts.insert(QString::fromStdString(entry.first), QString::fromStdString(entry.second));
    }
    return facts;
}

void applyRuntimeRuleTemplateResult(QVariantMap& evaluation,
                                    const bridge::rules::RuntimeRuleTemplateEvaluationResult& templateResult)
{
    if (!templateResult.hasTemplate) {
        return;
    }

    evaluation.insert(QStringLiteral("templateRuleTemplateNamespace"), templateResult.templateNamespace);
    evaluation.insert(QStringLiteral("templateRuleFilePath"), templateResult.templateFilePath);
    evaluation.insert(QStringLiteral("templateRuleMatched"), templateResult.matched);
    evaluation.insert(QStringLiteral("templateRuleActionPermitted"), templateResult.actionPermitted);
    if (!templateResult.groupDecisions.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleGroupDecisions"), templateResult.groupDecisions);
    }
    if (!templateResult.reasonCode.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleDecisionReasonCode"), templateResult.reasonCode);
    }
    if (!templateResult.message.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleDecisionMessage"), templateResult.message);
    }

    const QString groupId = firstConfiguredValue(
        templateResult.binding,
        {QStringLiteral("group_id"), QStringLiteral("groupId")}
    ).toString().trimmed();
    if (!groupId.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleGroupId"), groupId);
    }
    const QString groupTitle = firstConfiguredValue(
        templateResult.binding,
        {QStringLiteral("group_title"), QStringLiteral("groupTitle")}
    ).toString().trimmed();
    if (!groupTitle.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleGroupTitle"), groupTitle);
    }
    const QString groupRole = firstConfiguredValue(
        templateResult.binding,
        {QStringLiteral("group_role"), QStringLiteral("groupRole")}
    ).toString().trimmed();
    if (!groupRole.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleGroupRole"), groupRole);
    }
    const QString groupOperator = firstConfiguredValue(
        templateResult.binding,
        {QStringLiteral("group_operator"), QStringLiteral("groupOperator")}
    ).toString().trimmed();
    if (!groupOperator.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleGroupOperator"), groupOperator);
    }

    if (!templateResult.matched) {
        return;
    }

    evaluation.insert(QStringLiteral("templateRuleStage"), templateResult.stage);
    evaluation.insert(QStringLiteral("templateRuleId"), templateResult.ruleId);
    evaluation.insert(QStringLiteral("templateRuleReasonCode"), templateResult.reasonCode);
    evaluation.insert(QStringLiteral("templateRuleResult"), templateResult.resultType);
    if (!templateResult.message.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleMessage"), templateResult.message);
    }
    if (!templateResult.payload.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRulePayload"), templateResult.payload);
    }
    if (!templateResult.state.isEmpty()) {
        evaluation.insert(QStringLiteral("templateRuleState"), templateResult.state);
    }

}

QVariantList compiledTemplatesForStrategy(const QVariantMap& strategy, QString* errorMessage = nullptr)
{
    return bridge::rules::loadCompiledRuleTemplates(strategyRuleTemplateBindings(strategy), errorMessage);
}

QString runtimeAutoTrackingOrderId(const QVariantMap& orderRequest)
{
    const QString clientOrderId = orderRequest.value(QStringLiteral("clientOrderId")).toString().trimmed();
    if (!clientOrderId.isEmpty()) {
        return clientOrderId;
    }
    return orderRequest.value(QStringLiteral("orderId")).toString().trimmed();
}

QString runtimeAutoTrackingOrderId(const engine::EventFormat& event)
{
    const QString clientOrderId = eventStringValue(event, "client_order_id");
    if (!clientOrderId.isEmpty()) {
        return clientOrderId;
    }
    return eventStringValue(event, "order_id");
}

QString runtimeAutoExecutionStatus(const QString& orderStatus, const QString& statusOrigin)
{
    const QString normalizedStatus = orderStatus.trimmed().toUpper();
    const QString normalizedOrigin = statusOrigin.trimmed().toLower();
    if (normalizedStatus == QStringLiteral("PENDING_RISK") || normalizedOrigin == QStringLiteral("risk_pending")) {
        return QStringLiteral("risk_pending");
    }
    if (normalizedStatus == QStringLiteral("REJECTED")) {
        if (normalizedOrigin == QStringLiteral("execution_rule_reject")) {
            return QStringLiteral("execution_rule_reject");
        }
        if (normalizedOrigin == QStringLiteral("risk_reject")) {
            return QStringLiteral("risk_rejected");
        }
        if (normalizedOrigin == QStringLiteral("broker_reject")) {
            return QStringLiteral("broker_rejected");
        }
        return QStringLiteral("rejected");
    }
    if (normalizedStatus == QStringLiteral("FILLED")) {
        return QStringLiteral("filled");
    }
    if (normalizedStatus == QStringLiteral("PARTIAL_FILLED")) {
        return QStringLiteral("partial_filled");
    }
    if (normalizedStatus == QStringLiteral("CANCELLED")) {
        return QStringLiteral("cancelled");
    }
    if (normalizedStatus == QStringLiteral("SUBMITTED")) {
        return QStringLiteral("broker_submitted");
    }
    if (normalizedStatus == QStringLiteral("PENDING")) {
        if (normalizedOrigin == QStringLiteral("runtime")) {
            return QStringLiteral("runtime_pending");
        }
        if (normalizedOrigin == QStringLiteral("broker_submit")) {
            return QStringLiteral("broker_pending");
        }
        return QStringLiteral("pending");
    }
    return normalizedStatus.isEmpty() ? QStringLiteral("submitted") : normalizedStatus.toLower();
}

QString runtimeAutoExecutionStage(const QString& statusOrigin)
{
    const QString normalizedOrigin = statusOrigin.trimmed().toLower();
    if (normalizedOrigin == QStringLiteral("risk_pending") || normalizedOrigin == QStringLiteral("risk_reject")) {
        return QStringLiteral("risk");
    }
    if (normalizedOrigin == QStringLiteral("execution_rule_reject")) {
        return QStringLiteral("execution_rule");
    }
    if (normalizedOrigin == QStringLiteral("broker_submit") || normalizedOrigin == QStringLiteral("broker_reject")) {
        return QStringLiteral("broker");
    }
    if (normalizedOrigin == QStringLiteral("runtime")) {
        return QStringLiteral("runtime");
    }
    if (normalizedOrigin == QStringLiteral("fill")) {
        return QStringLiteral("fill");
    }
    if (normalizedOrigin == QStringLiteral("local_pending")) {
        return QStringLiteral("queue");
    }
    return QStringLiteral("submit");
}

bool isRuntimeAutoTerminalStatus(const QString& autoExecutionStatus)
{
    const QString normalizedStatus = autoExecutionStatus.trimmed().toLower();
    return normalizedStatus == QStringLiteral("filled")
        || normalizedStatus == QStringLiteral("cancelled")
        || normalizedStatus == QStringLiteral("rejected")
        || normalizedStatus == QStringLiteral("risk_rejected")
        || normalizedStatus == QStringLiteral("broker_rejected")
        || normalizedStatus == QStringLiteral("execution_rule_reject");
}

void applyRuntimeAutoOrderContext(QVariantMap& evaluation, const QVariantMap& orderRequest)
{
    for (const QString& key : {QStringLiteral("orderId"),
                               QStringLiteral("clientOrderId"),
                               QStringLiteral("executionScopeId"),
                               QStringLiteral("batchId"),
                               QStringLiteral("batchIndex"),
                               QStringLiteral("executionSequence"),
                               QStringLiteral("batchOrderCount"),
                               QStringLiteral("previousBatchId"),
                               QStringLiteral("nextBatchId"),
                               QStringLiteral("requiresPreviousBatchFilled"),
                               QStringLiteral("pauseOnConflict"),
                               QStringLiteral("pauseOnAbnormalReject"),
                               QStringLiteral("requiresManualCheckpoint"),
                               QStringLiteral("manualCheckpointBatchIndex")}) {
        if (!orderRequest.contains(key)) {
            continue;
        }
        evaluation.insert(key, orderRequest.value(key));
    }

    if (orderRequest.contains(QStringLiteral("orderId"))) {
        evaluation.insert(QStringLiteral("autoExecutionOrderId"), orderRequest.value(QStringLiteral("orderId")));
    }
    if (orderRequest.contains(QStringLiteral("clientOrderId"))) {
        evaluation.insert(QStringLiteral("autoExecutionClientOrderId"), orderRequest.value(QStringLiteral("clientOrderId")));
    }
}

QVariantMap buildRuntimeAutoExecutionFollowupEvaluation(const QVariantMap& baseEvaluation,
                                                        const engine::EventFormat& event,
                                                        const QString& eventType)
{
    QVariantMap evaluation = baseEvaluation;
    const QString orderStatus = eventStringValue(event, "status").trimmed().toUpper();
    const QString statusOrigin = eventStringValue(event, "status_origin").trimmed().toLower();
    const QString trackingOrderId = runtimeAutoTrackingOrderId(event);
    const QString brokerOrderId = eventStringValue(event, "broker_order_id");
    const QString message = eventStringValue(event, "message");
    const QString batchId = eventStringValue(event, "batch_id");
    const QString executionScopeId = eventStringValue(event, "execution_scope_id");
    const QString requiredBatchId = eventStringValue(event, "required_batch_id");
    const QString blockingBatchId = eventStringValue(event, "blocking_batch_id");
    const QString ruleId = eventStringValue(event, "rule_id");
    const QString reasonCode = eventStringValue(event, "reason_code");

    evaluation.insert(QStringLiteral("autoExecutionStatus"), runtimeAutoExecutionStatus(orderStatus, statusOrigin));
    evaluation.insert(QStringLiteral("autoExecutionStage"), runtimeAutoExecutionStage(statusOrigin));
    evaluation.insert(QStringLiteral("autoExecutionOrderStatus"), orderStatus);
    evaluation.insert(QStringLiteral("autoExecutionStatusOrigin"), statusOrigin);
    evaluation.insert(QStringLiteral("autoExecutionEventType"), eventType);
    if (!trackingOrderId.isEmpty()) {
        evaluation.insert(QStringLiteral("autoExecutionOrderId"), trackingOrderId);
        evaluation.insert(QStringLiteral("autoExecutionClientOrderId"), trackingOrderId);
    }
    if (!brokerOrderId.isEmpty()) {
        evaluation.insert(QStringLiteral("autoExecutionBrokerOrderId"), brokerOrderId);
    }
    if (!message.isEmpty()) {
        evaluation.insert(QStringLiteral("autoExecutionMessage"), message);
    }
    if (!batchId.isEmpty()) {
        evaluation.insert(QStringLiteral("batchId"), batchId);
    }
    if (!executionScopeId.isEmpty()) {
        evaluation.insert(QStringLiteral("executionScopeId"), executionScopeId);
    }
    if (!requiredBatchId.isEmpty()) {
        evaluation.insert(QStringLiteral("requiredBatchId"), requiredBatchId);
    }
    if (!blockingBatchId.isEmpty()) {
        evaluation.insert(QStringLiteral("blockingBatchId"), blockingBatchId);
    }
    if (!ruleId.isEmpty()) {
        evaluation.insert(QStringLiteral("ruleId"), ruleId);
    }
    if (!reasonCode.isEmpty()) {
        evaluation.insert(QStringLiteral("reasonCode"), reasonCode);
    }

    const double filledQuantity = eventNumericValue(event,
                                                    "filled_quantity",
                                                    eventNumericValue(event, "fill_quantity", 0.0));
    if (filledQuantity > 0.0) {
        evaluation.insert(QStringLiteral("autoExecutionFilledQuantity"), static_cast<qint64>(filledQuantity));
    }
    const double filledNotional = eventNumericValue(event, "filled_notional", 0.0);
    if (filledNotional > 0.0) {
        evaluation.insert(QStringLiteral("autoExecutionFilledNotional"), filledNotional);
    }

    return evaluation;
}

QString normalizedStrategyType(const QVariantMap& strategy)
{
    return strategy.value("strategy_type").toString().trimmed().toUpper();
}

bool strategyStatusAllowsSignals(const QVariantMap& strategy)
{
    const QString status = strategy.value("status").toString().trimmed().toUpper();
    return status == "ACTIVE" || status == "TESTING";
}

void refreshViewModelFromCache(StrategyViewModel* viewModel,
                               const QMap<QString, QVariantMap>& memoryCache)
{
    if (!viewModel) {
        return;
    }

    viewModel->updateData(buildStrategyListFromCache(memoryCache));
}

QString defaultTemplatePositionSizingMethod(const QString& backendType)
{
    if (backendType == QStringLiteral("ALPHA")) {
        return QStringLiteral("equal_weight");
    }
    if (backendType == QStringLiteral("ARBITRAGE")) {
        return QStringLiteral("spread_neutral");
    }
    if (backendType == QStringLiteral("CUSTOM")) {
        return QStringLiteral("discretionary");
    }
    return QStringLiteral("fixed_fraction");
}

void applyRuleNativeTemplateDefaults(QVariantMap& strategy,
                                     const QString& backendType,
                                     const QString& strategySubtype,
                                     const QString& strategyName)
{
    QVariantMap parameters = strategy.value(QStringLiteral("parameters")).toMap();

    QVariantMap ruleProfile = parameters.value(QStringLiteral("rule_profile")).toMap();
    if (parameters.contains(QStringLiteral("position_size"))) {
        risk::config::setMaxPositionPercent(ruleProfile, parameters.value(QStringLiteral("position_size")).toDouble());
    }
    if (parameters.contains(QStringLiteral("stop_loss"))) {
        risk::config::setStopLossPercent(ruleProfile, parameters.value(QStringLiteral("stop_loss")).toDouble());
    }
    if (parameters.contains(QStringLiteral("take_profit"))) {
        risk::config::setTakeProfitPercent(ruleProfile, parameters.value(QStringLiteral("take_profit")).toDouble());
    }

    QVariantMap executionPolicy = parameters.value(QStringLiteral("execution_policy")).toMap();
    risk::config::setPositionSizingMethod(
        executionPolicy,
        risk::config::positionSizingMethod(executionPolicy, defaultTemplatePositionSizingMethod(backendType)));
    if (parameters.contains(QStringLiteral("rebalance_days"))) {
        risk::config::setRebalanceDays(executionPolicy, parameters.value(QStringLiteral("rebalance_days")).toInt());
    }

    QVariantMap backtestAssumptions = parameters.value(QStringLiteral("backtest_assumptions")).toMap();
    backtestAssumptions.insert(QStringLiteral("initialCapital"),
                               backtestAssumptions.value(QStringLiteral("initialCapital"), kDefaultTemplateInitialCapital));
    risk::config::setCommissionRate(
        backtestAssumptions,
        risk::config::commissionRate(backtestAssumptions, kDefaultTemplateCommissionRate));
    risk::config::setSlippageRate(
        backtestAssumptions,
        risk::config::slippageRate(backtestAssumptions, kDefaultTemplateSlippageRate));

    QVariantMap strategyScopeContext = parameters.value(QStringLiteral("strategy_scope_context")).toMap();
    strategyScopeContext.insert(QStringLiteral("selectedStrategyType"), backendType);
    strategyScopeContext.insert(QStringLiteral("selectedStrategySubtype"), strategySubtype);
    strategyScopeContext.insert(QStringLiteral("selectedStrategyName"), strategyName);
    if (strategy.contains(QStringLiteral("symbol_pool"))) {
        strategyScopeContext.insert(QStringLiteral("symbol_pool"), strategy.value(QStringLiteral("symbol_pool")));
    }

    parameters.insert(QStringLiteral("rule_profile"), ruleProfile);
    parameters.insert(QStringLiteral("execution_policy"), executionPolicy);
    parameters.insert(QStringLiteral("backtest_assumptions"), backtestAssumptions);
    parameters.insert(QStringLiteral("strategy_scope_context"), strategyScopeContext);
    strategy.insert(QStringLiteral("parameters"), parameters);
}

} // namespace

StrategyService* StrategyService::m_instance = nullptr;
QMutex StrategyService::m_instanceMutex;

StrategyService* StrategyService::instance() {
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new StrategyService(qApp);
    }
    return m_instance;
}

StrategyService::StrategyService(QObject* parent) 
    : QObject(parent)
    , m_initialized(false)
    , m_isLoading(false)
    , m_cacheLoaded(false)
    , m_autoInitialize(true)
    , m_eventBusIntegrated(false)
    , m_viewModel(new StrategyViewModel(this)) {
    // 连接信号到视图模型
    connect(this, &StrategyService::strategiesLoaded, this, [this](const QVariantList& strategies) {
        if (m_viewModel) {
            m_viewModel->updateData(strategies);
        } else {
            qWarning() << "StrategyService: 视图模型为空，无法更新数据";
        }
    });
}

StrategyService::~StrategyService() {
    clearAllCache();
}

void StrategyService::initialize() {
    QMutexLocker locker(&m_initMutex);
    
    if (m_initialized.load()) {
        return;
    }
    
    if (m_isLoading.load()) {
        return;
    }
    
    m_isLoading = true;
    emit isLoadingChanged();
    
    try {
        // 初始化仓储
        initializeRepository();
        
        // 加载缓存
        loadStrategiesFromDatabase();

        initializeEventBusIntegration();
        
        m_initialized = true;
        m_cacheLoaded = true;
        m_isLoading = false;
        
        emit initializedChanged();
        emit isLoadingChanged();
        emit cacheLoadedChanged();
    } catch (const std::exception& e) {
        m_isLoading = false;
        emit isLoadingChanged();
        qWarning() << "StrategyService: 初始化失败 -" << e.what();
        emit errorOccurred(QString("初始化失败: %1").arg(e.what()));
    }
}

void StrategyService::initializeAsync() {
    {
        QMutexLocker locker(&m_initMutex);
        if (m_initialized.load() || m_isLoading.load()) {
            return;
        }
        m_isLoading = true;
    }

    emit isLoadingChanged();

    QPointer<StrategyService> safeService(this);
    std::thread([safeService]() {
        QVariantList strategies;
        QMap<QString, QVariantMap> loadedCache;
        std::shared_ptr<astock::database::IStrategyRepository> repository;
        QString errorMessage;

        try {
            auto& dbManager = astock::database::DatabaseConnectionManager::instance();
            if (!dbManager.initialize()) {
                throw std::runtime_error("数据库连接初始化失败");
            }

            repository = std::make_shared<StrategyRepository>();
            if (!repository->initialize()) {
                throw std::runtime_error("策略仓储初始化失败");
            }

            const auto strategyMaps = repository->findAll();
            for (const auto& strategy : strategyMaps) {
                QVariantMap normalizedStrategy = strategy;
                applyCanonicalStrategyStructures(normalizedStrategy);
                const QString strategyId = normalizedStrategy.value("strategy_id").toString();
                if (!strategyId.isEmpty()) {
                    loadedCache[strategyId] = normalizedStrategy;
                }
                strategies.append(normalizedStrategy);
            }
        } catch (const std::exception& e) {
            errorMessage = QString::fromUtf8(e.what());
        }

        if (!safeService) {
            return;
        }

        QMetaObject::invokeMethod(safeService.data(),
            [safeService, repository, loadedCache, strategies, errorMessage]() {
                if (!safeService) {
                    return;
                }

                if (!errorMessage.isEmpty()) {
                    {
                        QMutexLocker locker(&safeService->m_initMutex);
                        safeService->m_isLoading = false;
                    }
                    emit safeService->isLoadingChanged();
                    qWarning() << "StrategyService: 异步初始化失败 -" << errorMessage;
                    emit safeService->errorOccurred(QString("初始化失败: %1").arg(errorMessage));
                    return;
                }

                {
                    QMutexLocker locker(&safeService->m_initMutex);
                    if (safeService->m_initialized.load()) {
                        safeService->m_isLoading = false;
                        emit safeService->isLoadingChanged();
                        return;
                    }

                    safeService->m_repository = repository;
                    safeService->m_memoryCache = loadedCache;
                    safeService->initializeEventBusIntegration();
                    safeService->m_initialized = true;
                    safeService->m_cacheLoaded = true;
                    safeService->m_isLoading = false;
                }

                emit safeService->strategiesLoaded(strategies);
                emit safeService->initializedChanged();
                emit safeService->isLoadingChanged();
                emit safeService->cacheLoadedChanged();
            },
            Qt::QueuedConnection);
    }).detach();
}

bool StrategyService::publishSyntheticMarketEvent(const QVariantMap& marketEvent)
{
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        qWarning() << "StrategyService: EventBus unavailable, synthetic market event not published";
        return false;
    }

    const QString symbol = marketEvent.value("symbol").toString().trimmed();
    const double price = marketEvent.value("price").toDouble();
    if (symbol.isEmpty() || price <= 0.0) {
        qWarning() << "StrategyService: invalid synthetic market event" << marketEvent;
        return false;
    }

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        engine::EventTypes::MARKET_TICK,
        "APP_SYNTHETIC_MARKET",
        0);
    event.set("symbol", symbol.toStdString());
    event.set("price", price);
    event.metadata["symbol"] = symbol.toStdString();
    event.metadata["price"] = QString::number(price, 'f', 6).toStdString();
    event.metadata["source"] = "StrategyService.publishSyntheticMarketEvent";

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::NORMAL));
    if (!result) {
        qWarning() << "StrategyService: failed to publish synthetic market event" << QString::fromStdString(result.message);
        return false;
    }
    return true;
}

void StrategyService::initializeEventBusIntegration()
{
    QMutexLocker locker(&m_eventBusMutex);
    if (m_eventBusIntegrated.load()) {
        return;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        qWarning() << "StrategyService: EventBus not ready, skip event integration";
        return;
    }

    m_marketTickSubscription = bus->subscribe(engine::EventTypes::MARKET_TICK,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("market.tick"));
        });

    m_marketBarSubscription = bus->subscribe(engine::EventTypes::MARKET_BAR,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("market.bar"));
        });

    m_tradingMarketTickSubscription = bus->subscribe(engine::EventTypes::TRADING_MARKET_TICK,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("trading.market.tick"));
        });

    m_tradingMarketBarSubscription = bus->subscribe(engine::EventTypes::TRADING_MARKET_BAR,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("trading.market.bar"));
        });

    m_tradingOrderUpdatedSubscription = bus->subscribe(engine::EventTypes::TRADING_ORDER_UPDATED,
        [this](const engine::EventFormat& event) {
            handleRuntimeAutoExecutionEvent(event, QStringLiteral("trading.order.updated"));
        });

    m_orderFillSubscription = bus->subscribe(engine::EventTypes::ORDER_FILL,
        [this](const engine::EventFormat& event) {
            handleRuntimeAutoExecutionEvent(event, QStringLiteral("order.fill"));
        });

    m_eventBusIntegrated = true;
    qDebug() << "StrategyService: EventBus integration initialized";
}

void StrategyService::trackRuntimeAutoExecution(const QVariantMap& orderRequest,
                                               const QVariantMap& evaluation)
{
    const QString trackingOrderId = runtimeAutoTrackingOrderId(orderRequest);
    if (trackingOrderId.isEmpty()) {
        return;
    }

    QVariantMap trackedEvaluation = evaluation;
    applyRuntimeAutoOrderContext(trackedEvaluation, orderRequest);

    QMutexLocker locker(&m_runtimeAutoExecutionMutex);
    m_runtimeAutoExecutionTracking.insert(trackingOrderId, trackedEvaluation);
    m_pendingRuntimeAutoExecutionUpdates.remove(trackingOrderId);
    m_runtimeAutoExecutionCommitted.remove(trackingOrderId);
}

void StrategyService::finalizeRuntimeAutoExecutionSubmission(const QString& trackingOrderId)
{
    QVariantMap pendingEvaluation;
    bool shouldEmitPendingEvaluation = false;
    bool shouldDiscardTracking = false;
    {
        QMutexLocker locker(&m_runtimeAutoExecutionMutex);
        if (!m_runtimeAutoExecutionTracking.contains(trackingOrderId)) {
            return;
        }

        m_runtimeAutoExecutionCommitted.insert(trackingOrderId);
        if (m_pendingRuntimeAutoExecutionUpdates.contains(trackingOrderId)) {
            pendingEvaluation = m_pendingRuntimeAutoExecutionUpdates.take(trackingOrderId);
            shouldEmitPendingEvaluation = true;
            if (isRuntimeAutoTerminalStatus(pendingEvaluation.value(QStringLiteral("autoExecutionStatus")).toString())) {
                m_runtimeAutoExecutionTracking.remove(trackingOrderId);
                m_runtimeAutoExecutionCommitted.remove(trackingOrderId);
                shouldDiscardTracking = true;
            } else {
                m_runtimeAutoExecutionTracking.insert(trackingOrderId, pendingEvaluation);
            }
        }
    }

    if (shouldEmitPendingEvaluation) {
        emit strategyRuntimeRuleEvaluated(pendingEvaluation);
    }

    if (shouldDiscardTracking) {
        discardRuntimeAutoExecutionTracking(trackingOrderId);
    }
}

void StrategyService::discardRuntimeAutoExecutionTracking(const QString& trackingOrderId)
{
    if (trackingOrderId.isEmpty()) {
        return;
    }

    QMutexLocker locker(&m_runtimeAutoExecutionMutex);
    m_runtimeAutoExecutionTracking.remove(trackingOrderId);
    m_pendingRuntimeAutoExecutionUpdates.remove(trackingOrderId);
    m_runtimeAutoExecutionCommitted.remove(trackingOrderId);
}

void StrategyService::handleRuntimeAutoExecutionEvent(const engine::EventFormat& event,
                                                      const QString& eventType)
{
    const QString trackingOrderId = runtimeAutoTrackingOrderId(event);
    if (trackingOrderId.isEmpty()) {
        return;
    }

    QVariantMap followupEvaluation;
    bool shouldEmit = false;
    bool shouldDiscard = false;
    {
        QMutexLocker locker(&m_runtimeAutoExecutionMutex);
        const auto trackedIt = m_runtimeAutoExecutionTracking.constFind(trackingOrderId);
        if (trackedIt == m_runtimeAutoExecutionTracking.constEnd()) {
            return;
        }

        followupEvaluation = buildRuntimeAutoExecutionFollowupEvaluation(trackedIt.value(), event, eventType);
        if (!m_runtimeAutoExecutionCommitted.contains(trackingOrderId)) {
            m_pendingRuntimeAutoExecutionUpdates.insert(trackingOrderId, followupEvaluation);
            return;
        }

        shouldEmit = true;
        if (isRuntimeAutoTerminalStatus(followupEvaluation.value(QStringLiteral("autoExecutionStatus")).toString())) {
            m_runtimeAutoExecutionTracking.remove(trackingOrderId);
            m_pendingRuntimeAutoExecutionUpdates.remove(trackingOrderId);
            m_runtimeAutoExecutionCommitted.remove(trackingOrderId);
            shouldDiscard = true;
        } else {
            m_runtimeAutoExecutionTracking.insert(trackingOrderId, followupEvaluation);
        }
    }

    if (shouldEmit) {
        emit strategyRuntimeRuleEvaluated(followupEvaluation);
    }

    if (shouldDiscard) {
        discardRuntimeAutoExecutionTracking(trackingOrderId);
    }
}

QVariantMap StrategyService::cachedTradingConfigurationSnapshot(qint64 nowMs)
{
    const QVariantMap snapshot = loadTradingConfigurationSnapshot();
    QMutexLocker locker(&m_marketStateMutex);
    m_cachedTradingConfiguration = snapshot;
    m_cachedTradingConfigurationAtMs = nowMs;
    return m_cachedTradingConfiguration;
}

QVariantMap StrategyService::cachedRiskConfigurationSnapshot(qint64 nowMs)
{
    {
        QMutexLocker locker(&m_marketStateMutex);
        if (!m_cachedRiskConfiguration.isEmpty()
            && nowMs - m_cachedRiskConfigurationAtMs <= kRiskConfigurationCacheTtlMs) {
            return m_cachedRiskConfiguration;
        }
    }

    const QVariantMap snapshot = loadRiskConfigurationSnapshot();
    QMutexLocker locker(&m_marketStateMutex);
    m_cachedRiskConfiguration = snapshot;
    m_cachedRiskConfigurationAtMs = nowMs;
    return m_cachedRiskConfiguration;
}

QVariantMap StrategyService::cachedMarketSessionSnapshot(qint64 nowMs)
{
    {
        QMutexLocker locker(&m_marketStateMutex);
        if (!m_cachedMarketSessionSnapshot.isEmpty()
            && nowMs - m_cachedMarketSessionSnapshotAtMs <= kMarketSessionCacheTtlMs) {
            return m_cachedMarketSessionSnapshot;
        }
    }

    const QVariantMap snapshot = currentMarketSessionSnapshot();
    QMutexLocker locker(&m_marketStateMutex);
    m_cachedMarketSessionSnapshot = snapshot;
    m_cachedMarketSessionSnapshotAtMs = nowMs;
    return m_cachedMarketSessionSnapshot;
}

void StrategyService::handleMarketEvent(const engine::EventFormat& event, const QString& marketEventType)
{
    const QString symbol = eventStringValue(event, "symbol");
    if (symbol.isEmpty()) {
        return;
    }

    double referencePrice = eventNumericValue(event, "open", 0.0);
    const double closePrice = eventNumericValue(event, "close", 0.0);
    const double tickPrice = eventNumericValue(event, "price", 0.0);
    const double latestPrice = closePrice > 0.0 ? closePrice : tickPrice;

    if (latestPrice <= 0.0) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (referencePrice <= 0.0) {
        QMutexLocker locker(&m_marketStateMutex);
        referencePrice = m_latestMarketPriceBySymbol.value(symbol, 0.0);
    }

    {
        QMutexLocker locker(&m_marketStateMutex);
        m_latestMarketPriceBySymbol.insert(symbol, latestPrice);
    }

    if (referencePrice <= 0.0) {
        return;
    }

    const QVariantMap tradingConfiguration = cachedTradingConfigurationSnapshot(nowMs);
    const bool tradingConfigured = tradingConfiguration.value(QStringLiteral("enabled")).toBool();
    const bool liveTradingEnabled = tradingConfigured
        && !tradingConfiguration.value(QStringLiteral("readOnly"), true).toBool();
    const bool autoExecutionEnabled = liveTradingEnabled && runtimeAutoExecutionConfigured(tradingConfiguration);
    if (!tradingConfigured) {
        return;
    }

    const QSet<QString> allowedStrategyIds = configuredBoundStrategyIds(tradingConfiguration);
    QVariantList candidateStrategies;
    {
        QReadLocker locker(&m_rwLock);
        for (auto it = m_memoryCache.constBegin(); it != m_memoryCache.constEnd(); ++it) {
            const QVariantMap strategy = it.value();
            if (!strategyStatusAllowsSignals(strategy)) {
                continue;
            }

            const QString strategyId = resolvedStrategyIdentifier(strategy);
            if (strategyId.isEmpty()) {
                continue;
            }

            if (!allowedStrategyIds.isEmpty() && !allowedStrategyIds.contains(strategyId)) {
                continue;
            }

            candidateStrategies.append(strategy);
        }
    }

    if (candidateStrategies.isEmpty()) {
        return;
    }

    const QVariantMap riskConfiguration = autoExecutionEnabled
        ? cachedRiskConfigurationSnapshot(nowMs)
        : QVariantMap{};

    TradingRuntimeStatusService* runtimeStatusService = TradingRuntimeStatusService::instance();

    const QVariantMap marketSessionSnapshot = cachedMarketSessionSnapshot(nowMs);
    const bool marketSessionKnown = !marketSessionSnapshot.isEmpty();
    const bool marketSessionOpen = !marketSessionKnown
        || marketSessionSnapshot.value(QStringLiteral("sessionOpen")).toBool();
    const QVariantMap eventFacts = buildEventFactSnapshot(event);

    const StrategyRuntimeRuleEvaluator evaluator;
    StrategyRuntimeRuleEvaluator::MarketContext marketContext;
    marketContext.symbol = symbol;
    marketContext.latestPrice = latestPrice;
    marketContext.referencePrice = referencePrice;
    marketContext.marketEventType = marketEventType;
    marketContext.liveTradingEnabled = liveTradingEnabled;
    marketContext.marketSessionKnown = marketSessionKnown;
    marketContext.marketSessionOpen = marketSessionOpen;
    marketContext.marketSessionSnapshot = marketSessionSnapshot;
    marketContext.tradingConfiguration = tradingConfiguration;

    for (const QVariant& rawStrategy : candidateStrategies) {
        QVariantMap strategy = rawStrategy.toMap();
        applyRuntimeRuleDefaults(strategy, tradingConfiguration);
        StrategyRuntimeRuleEvaluator::MarketContext strategyContext = marketContext;
        strategyContext.runtimeSessionSnapshot = runtimeSessionSnapshotForStrategy(runtimeStatusService,
                                                                                  tradingConfiguration,
                                                                                  strategy);
        strategyContext.runtimeSessionKnown = !strategyContext.runtimeSessionSnapshot.isEmpty();
        strategyContext.runtimeSessionReady = runtimeSessionIsReady(strategyContext.runtimeSessionSnapshot);

        QVariantMap evaluation = evaluator.evaluateMarketCandidate(strategy, strategyContext);
        evaluation.insert(QStringLiteral("autoExecutionEnabled"), autoExecutionEnabled);

        if (evaluation.value(QStringLiteral("decision")).toString() != QStringLiteral("candidate_ready")) {
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        QString templateError;
        const QVariantList compiledTemplates = compiledTemplatesForStrategy(strategy, &templateError);
        if (!compiledTemplates.isEmpty()) {
            bridge::rules::RuntimeRuleTemplateEvaluationContext templateContext;
            templateContext.symbol = symbol;
            templateContext.latestPrice = latestPrice;
            templateContext.referencePrice = referencePrice;
            templateContext.marketEventType = marketEventType;
            templateContext.candidateAction = evaluation.value(QStringLiteral("candidateAction")).toString();
            templateContext.candidateStrength = evaluation.value(QStringLiteral("candidateStrength")).toDouble();
            templateContext.strategy = strategy;
            templateContext.flatEventFacts = eventFacts;
            templateContext.marketSessionSnapshot = marketSessionSnapshot;
            templateContext.runtimeSessionSnapshot = strategyContext.runtimeSessionSnapshot;

            const bridge::rules::RuntimeRuleTemplateEvaluationResult templateResult =
                bridge::rules::evaluateRuleTemplates(compiledTemplates, templateContext);
            applyRuntimeRuleTemplateResult(evaluation, templateResult);
            if (!templateResult.actionPermitted) {
                evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
                evaluation.insert(QStringLiteral("gate"), QStringLiteral("rule_template"));
                evaluation.insert(QStringLiteral("reason"),
                    templateResult.reasonCode.isEmpty()
                        ? QStringLiteral("runtime_rule_template_blocked")
                        : templateResult.reasonCode);
                evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("blocked"));
                evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("blocked"));
                if (!templateResult.message.isEmpty()) {
                    evaluation.insert(QStringLiteral("autoExecutionMessage"), templateResult.message);
                }
                emit strategyRuntimeRuleEvaluated(evaluation);
                continue;
            }
        } else if (!templateError.isEmpty()) {
            evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("gate"), QStringLiteral("rule_template"));
            evaluation.insert(QStringLiteral("reason"), QStringLiteral("runtime_rule_template_invalid"));
            evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("autoExecutionMessage"), templateError);
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        const QString strategyId = evaluation.value(QStringLiteral("strategyId")).toString();
        const QString action = evaluation.value(QStringLiteral("candidateAction")).toString();
        if (!shouldPublishStrategySignal(strategyId, symbol, action)) {
            evaluation.insert(QStringLiteral("decision"), QStringLiteral("suppressed"));
            evaluation.insert(QStringLiteral("gate"), QStringLiteral("execution"));
            evaluation.insert(QStringLiteral("reason"), QStringLiteral("duplicate_action_cooldown"));
            evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("suppressed"));
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        if (!autoExecutionEnabled) {
            evaluation.insert(QStringLiteral("reason"), QStringLiteral("auto_execution_disabled"));
            evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("disabled"));
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        const RuntimeOrderSizingResult sizing = deriveRuntimeOrderSizing(strategy, evaluation, riskConfiguration);
        if (sizing.quantity <= 0) {
            evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("gate"), QStringLiteral("execution"));
            evaluation.insert(QStringLiteral("reason"), sizing.failureReason.isEmpty()
                ? QStringLiteral("runtime_order_quantity_unavailable")
                : sizing.failureReason);
            evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("blocked"));
            if (!sizing.failureMessage.isEmpty()) {
                evaluation.insert(QStringLiteral("autoExecutionMessage"), sizing.failureMessage);
            }
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        TradeExecutionService* tradeExecutionService = TradeExecutionService::instance();
        if (!tradeExecutionService) {
            evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("gate"), QStringLiteral("execution"));
            evaluation.insert(QStringLiteral("reason"), QStringLiteral("trade_execution_service_unavailable"));
            evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("autoExecutionMessage"), QStringLiteral("执行服务未就绪，运行时候选信号未提交"));
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        const QVariantMap orderRequest = buildRuntimeAutoOrderRequest(strategy, evaluation, tradingConfiguration, sizing);
        const QString runtimeTrackingOrderId = runtimeAutoTrackingOrderId(orderRequest);
        trackRuntimeAutoExecution(orderRequest, evaluation);
        if (!tradeExecutionService->submitBridgeOrder(orderRequest)) {
            discardRuntimeAutoExecutionTracking(runtimeTrackingOrderId);
            evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("gate"), QStringLiteral("execution"));
            evaluation.insert(QStringLiteral("reason"), QStringLiteral("runtime_auto_submit_failed"));
            evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("blocked"));
            evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("rejected"));
            const QString errorMessage = tradeExecutionService->lastErrorMessage().trimmed();
            if (!errorMessage.isEmpty()) {
                evaluation.insert(QStringLiteral("autoExecutionMessage"), errorMessage);
            }
            emit strategyRuntimeRuleEvaluated(evaluation);
            continue;
        }

        evaluation.insert(QStringLiteral("reason"), QStringLiteral("submitted_for_risk_review"));
        evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("submitted"));
        evaluation.insert(QStringLiteral("autoExecutionStatus"), QStringLiteral("submitted"));
        evaluation.insert(QStringLiteral("autoExecutionQuantity"), sizing.quantity);
        evaluation.insert(QStringLiteral("autoExecutionRequestedNotional"), sizing.requestedNotional);
        applyRuntimeAutoOrderContext(evaluation, orderRequest);

        emit strategyRuntimeRuleEvaluated(evaluation);
        finalizeRuntimeAutoExecutionSubmission(runtimeTrackingOrderId);
    }

    return;
}

void StrategyService::publishStrategySignalForMarket(const QVariantMap& strategy,
                                                    const QString& symbol,
                                                    double latestPrice,
                                                    double referencePrice,
                                                    const QString& marketEventType,
                                                    const QString& eventId)
{
    const QString action = determineSignalAction(strategy, latestPrice, referencePrice);
    if (action.isEmpty()) {
        return;
    }

    const QString strategyId = strategy.value("strategy_id").toString();
    if (!shouldPublishStrategySignal(strategyId, symbol, action)) {
        return;
    }

    const double strength = determineSignalStrength(strategy, latestPrice, referencePrice);
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

    const QString strategyName = strategy.value("strategy_name").toString();
    engine::EventFormat signalEvent = engine::EventFormat::create_from_strings(
        engine::EventTypes::STRATEGY_SIGNAL,
        "STRATEGY_SERVICE",
        0);
    signalEvent.correlation_id = eventId.toStdString();
    signalEvent.set("strategy_id", strategyId.toStdString());
    signalEvent.set("strategy_name", strategyName.toStdString());
    signalEvent.set("symbol", symbol.toStdString());
    signalEvent.set("action", action.toStdString());
    signalEvent.set("price", latestPrice);
    signalEvent.set("reference_price", referencePrice);
    signalEvent.set("strength", strength);
    signalEvent.metadata["strategy_id"] = strategyId.toStdString();
    signalEvent.metadata["strategy_name"] = strategyName.toStdString();
    signalEvent.metadata["symbol"] = symbol.toStdString();
    signalEvent.metadata["action"] = action.toStdString();
    signalEvent.metadata["price"] = QString::number(latestPrice, 'f', 6).toStdString();
    signalEvent.metadata["reference_price"] = QString::number(referencePrice, 'f', 6).toStdString();
    signalEvent.metadata["strength"] = QString::number(strength, 'f', 6).toStdString();
    signalEvent.metadata["market_event_type"] = marketEventType.toStdString();
    signalEvent.metadata["strategy_type"] = normalizedStrategyType(strategy).toStdString();

        qInfo() << "StrategyService: publish strategy signal"
            << "strategy=" << strategyId
            << "symbol=" << symbol
            << "action=" << action
            << "marketEventType=" << marketEventType
            << "latestPrice=" << latestPrice
            << "referencePrice=" << referencePrice
            << "strength=" << strength;

    const auto result = bus->publish(signalEvent, static_cast<int>(engine::EventPriority::NORMAL));
    if (!result) {
        qWarning() << "StrategyService: failed to publish strategy signal" << QString::fromStdString(result.message);
        return;
    }

    QVariantMap signalData;
    signalData.insert("strategyId", strategyId);
    signalData.insert("strategyName", strategyName);
    signalData.insert("symbol", symbol);
    signalData.insert("action", action);
    signalData.insert("price", latestPrice);
    signalData.insert("referencePrice", referencePrice);
    signalData.insert("strength", strength);
    signalData.insert("marketEventType", marketEventType);
    emit strategySignalPublished(signalData);
}

bool StrategyService::shouldPublishStrategySignal(const QString& strategyId,
                                                 const QString& symbol,
                                                 const QString& action)
{
    const QString publicationKey = buildSignalPublicationKey(strategyId, symbol);
    if (publicationKey.isEmpty() || action.trimmed().isEmpty()) {
        return true;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker locker(&m_signalPublicationMutex);
    const auto existing = m_lastPublishedSignalByKey.constFind(publicationKey);
    if (existing != m_lastPublishedSignalByKey.constEnd()) {
        const bool sameAction = existing->action.compare(action, Qt::CaseInsensitive) == 0;
        const bool withinCooldown = (nowMs - existing->publishedAtMs) < kStrategySignalCooldownMs;
        if (sameAction || withinCooldown) {
            return false;
        }
    }

    m_lastPublishedSignalByKey.insert(publicationKey, StrategySignalPublicationState{action, nowMs});
    return true;
}

void StrategyService::clearSignalPublicationState(const QString& strategyId)
{
    const QString normalizedStrategyId = strategyId.trimmed();
    if (normalizedStrategyId.isEmpty()) {
        return;
    }

    QMutexLocker locker(&m_signalPublicationMutex);
    for (auto it = m_lastPublishedSignalByKey.begin(); it != m_lastPublishedSignalByKey.end();) {
        if (it.key().startsWith(normalizedStrategyId + QChar('|'))) {
            it = m_lastPublishedSignalByKey.erase(it);
            continue;
        }
        ++it;
    }
}

QString StrategyService::determineSignalAction(const QVariantMap& strategy,
                                              double latestPrice,
                                              double referencePrice) const
{
    return StrategyRuntimeRuleEvaluator::determineAction(strategy, latestPrice, referencePrice);
}

double StrategyService::determineSignalStrength(const QVariantMap& strategy,
                                                double latestPrice,
                                                double referencePrice) const
{
    return StrategyRuntimeRuleEvaluator::determineStrength(strategy, latestPrice, referencePrice);
}

QString StrategyService::createStrategy(const QVariantMap& strategyData) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QString();
    }

    QString strategyId;
    QVariantMap completeStrategy;
    {
        QWriteLocker locker(&m_rwLock);

        // 验证策略数据
        QString errorMessage;
        if (!validateStrategyData(strategyData, errorMessage)) {
            qWarning() << "StrategyService: 策略数据验证失败 -" << errorMessage;
            emit errorOccurred(errorMessage);
            return QString();
        }

        // 生成策略代码
        QString strategyCode = generateStrategyCode(
            strategyData.value("strategy_name").toString(),
            strategyData.value("strategy_type").toString()
        );

        // 生成策略ID（使用生成的策略代码作为ID）
        strategyId = generateStrategyId(strategyData.value("strategy_name").toString());

        // 构建完整的策略数据
        completeStrategy = strategyData;
        const bool clearSymbolPool = completeStrategy.value(QStringLiteral("clear_symbol_pool")).toBool();
        completeStrategy.remove(QStringLiteral("clear_symbol_pool"));
        if (clearSymbolPool) {
            clearStrategySymbolPoolBindings(&completeStrategy);
        }
        completeStrategy.remove("symbolPool");
        const QStringList normalizedSymbolPool = strategySymbolPool(completeStrategy);
        if (!normalizedSymbolPool.isEmpty()) {
            completeStrategy["symbol_pool"] = normalizedSymbolPool;
        } else if (clearSymbolPool) {
            completeStrategy.remove(QStringLiteral("symbol_pool"));
        }
        completeStrategy["strategy_id"] = strategyId;
        completeStrategy["strategy_code"] = strategyCode;

        // 设置默认状态，数据库只支持：'ACTIVE', 'INACTIVE', 'TESTING', 'ARCHIVED'
        completeStrategy["status"] = normalizePersistedStatus(strategyData.value("status").toString());

        completeStrategy["created_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        completeStrategy["updated_at"] = completeStrategy["created_at"];
        applyCanonicalStrategyStructures(completeStrategy);

        QString savedStrategyId = saveStrategyToDatabase(completeStrategy);
        if (savedStrategyId.isEmpty()) {
            qWarning() << "StrategyService: 保存策略到数据库失败";
            emit errorOccurred("保存策略到数据库失败");
            return QString();
        }

        strategyId = savedStrategyId;
        completeStrategy["strategy_id"] = strategyId;
        saveStrategyToCache(strategyId, completeStrategy);
    }

    if (m_viewModel) {
        m_viewModel->appendData(completeStrategy);
    }
    
    // 发送信号
    emit strategyCreated(strategyId, completeStrategy);
    emit dataChanged();
    
    qDebug() << "StrategyService: 创建策略成功 - ID:" << strategyId << "名称:" 
             << strategyData.value("strategy_name").toString();
    
    return strategyId;
}

bool StrategyService::updateStrategy(const QString& strategyId, const QVariantMap& strategyData) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    QVariantMap updatedStrategy;
    {
        QWriteLocker locker(&m_rwLock);

        QString errorMessage;
        if (!validateStrategyData(strategyData, errorMessage)) {
            qWarning() << "StrategyService: 策略数据验证失败 -" << errorMessage;
            emit errorOccurred(errorMessage);
            return false;
        }

        QVariantMap existingStrategy = loadStrategyFromCache(strategyId);
        if (existingStrategy.isEmpty()) {
            qWarning() << "StrategyService: 策略不存在 - ID:" << strategyId;
            return false;
        }

        updatedStrategy = existingStrategy;
        mergeStrategyParameterPayload(updatedStrategy, strategyData);
        for (auto it = strategyData.begin(); it != strategyData.end(); ++it) {
            if (it.key() == QStringLiteral("parameters")
                    || it.key() == QStringLiteral("ruleProfileSnapshot")
                    || it.key() == QStringLiteral("executionPolicySnapshot")
                    || it.key() == QStringLiteral("backtestAssumptionsSnapshot")
                    || it.key() == QStringLiteral("strategyScopeContextSnapshot")) {
                continue;
            }
            updatedStrategy[it.key()] = it.value();
        }
        const bool clearSymbolPool = updatedStrategy.value(QStringLiteral("clear_symbol_pool")).toBool();
        updatedStrategy.remove(QStringLiteral("clear_symbol_pool"));
        if (clearSymbolPool) {
            if (strategyBacktestSymbolPool(updatedStrategy).isEmpty()) {
                const QStringList legacyBacktestPool = strategySymbolPool(existingStrategy);
                if (!legacyBacktestPool.isEmpty()) {
                    updatedStrategy[QStringLiteral("backtest_symbol_pool")] = legacyBacktestPool;
                    QVariantMap updatedParameters = updatedStrategy.value(QStringLiteral("parameters")).toMap();
                    updatedParameters.insert(QStringLiteral("backtest_symbol_pool"), legacyBacktestPool);
                    updatedStrategy.insert(QStringLiteral("parameters"), updatedParameters);
                }
            }
            clearStrategySymbolPoolBindings(&updatedStrategy);
        }
        updatedStrategy.remove("symbolPool");
        const QStringList normalizedSymbolPool = strategySymbolPool(updatedStrategy);
        if (!normalizedSymbolPool.isEmpty()) {
            updatedStrategy["symbol_pool"] = normalizedSymbolPool;
        } else if (clearSymbolPool) {
            updatedStrategy.remove(QStringLiteral("symbol_pool"));
        }

        updatedStrategy["status"] = normalizePersistedStatus(updatedStrategy.value("status").toString());

        updatedStrategy["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        applyCanonicalStrategyStructures(updatedStrategy);

        if (!updateStrategyInDatabase(strategyId, updatedStrategy)) {
            qWarning() << "StrategyService: 更新策略到数据库失败";
            emit errorOccurred("更新策略到数据库失败");
            return false;
        }

        saveStrategyToCache(strategyId, updatedStrategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategy(strategyId, updatedStrategy);
    }
    
    // 发送信号
    emit strategyUpdated(strategyId, updatedStrategy);
    emit dataChanged();
    
    qDebug() << "StrategyService: 更新策略成功 - ID:" << strategyId;
    
    return true;
}

bool StrategyService::deleteStrategy(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    {
        QWriteLocker locker(&m_rwLock);

        if (!deleteStrategyFromDatabase(strategyId)) {
            qWarning() << "StrategyService: 从数据库删除策略失败 - ID:" << strategyId;
            emit errorOccurred(QString("从数据库删除策略失败: %1").arg(strategyId));
            return false;
        }

        removeStrategyFromCache(strategyId);
    }

    if (m_viewModel) {
        m_viewModel->removeStrategy(strategyId);
    }
    
    // 发送信号
    emit strategyDeleted(strategyId);
    emit dataChanged();
    
    qDebug() << "StrategyService: 删除策略成功 - ID:" << strategyId;
    
    return true;
}

QVariantMap StrategyService::getStrategyById(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantMap();
    }

    {
        QReadLocker locker(&m_rwLock);
        QVariantMap cachedStrategy = recoverEditableRulePayloadFromBacktest(loadStrategyFromCache(strategyId));
        if (!cachedStrategy.isEmpty() && hasCompleteEditableRulePayload(cachedStrategy)) {
            return cachedStrategy;
        }
    }

    QVariantMap repositoryStrategy;
    if (m_repository) {
        repositoryStrategy = m_repository->findById(strategyId);
    }

    repositoryStrategy = recoverEditableRulePayloadFromBacktest(repositoryStrategy);

    if (!repositoryStrategy.isEmpty()) {
        QWriteLocker locker(&m_rwLock);
        saveStrategyToCache(strategyId, repositoryStrategy);
        return loadStrategyFromCache(strategyId);
    }

    QReadLocker locker(&m_rwLock);
    return loadStrategyFromCache(strategyId);
}

QVariantMap StrategyService::getStrategyByCode(const QString& strategyCode) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantMap();
    }
    
    QReadLocker locker(&m_rwLock);
    
    // 从数据库获取
    QVariantMap strategy = m_repository->findByCode(strategyCode);
    if (!strategy.isEmpty()) {
        QString strategyId = strategy.value("strategy_id").toString();
        saveStrategyToCache(strategyId, strategy);
    }
    
    return strategy;
}

QVariantList StrategyService::getAllStrategies() {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantList();
    }
    
    QReadLocker locker(&m_rwLock);
    
    // 从缓存构建列表
    return buildStrategyListFromCache(m_memoryCache);
}

QVariantList StrategyService::getStrategiesByType(const QString& strategyType) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantList();
    }
    
    QReadLocker locker(&m_rwLock);
    
    QVariantList result;
    for (const QVariantMap& strategy : m_memoryCache.values()) {
        if (matchesStrategyType(strategy, strategyType)) {
            result.append(strategy);
        }
    }
    
    return result;
}

QVariantList StrategyService::getStrategiesByStatus(const QString& status) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantList();
    }
    
    QReadLocker locker(&m_rwLock);
    
    QVariantList result;
    for (const QVariantMap& strategy : m_memoryCache.values()) {
        if (matchesStrategyStatus(strategy, status)) {
            result.append(strategy);
        }
    }
    
    return result;
}

QVariantList StrategyService::searchStrategies(const QString& keyword) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantList();
    }
    
    QReadLocker locker(&m_rwLock);
    
    QVariantList result;
    for (const QVariantMap& strategy : m_memoryCache.values()) {
        if (matchesStrategyKeyword(strategy, keyword)) {
            result.append(strategy);
        }
    }
    
    return result;
}

bool StrategyService::importStrategies(const QVariantList& strategies) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QWriteLocker locker(&m_rwLock);
    
    QVariantList failedStrategies;
    std::vector<QVariantMap> successStrategies;
    
    for (const QVariant& strategyVar : strategies) {
        QVariantMap strategyData = strategyVar.toMap();
        
        QString errorMessage;
        if (!validateStrategyData(strategyData, errorMessage)) {
            strategyData["error"] = errorMessage;
            failedStrategies.append(strategyData);
            continue;
        }

        applyCanonicalStrategyStructures(strategyData);
        
        // 生成策略代码（策略ID由数据库自增生成）
        if (!strategyData.contains("strategy_code")) {
            QString strategyCode = generateStrategyCode(
                strategyData.value("strategy_name").toString(),
                strategyData.value("strategy_type").toString()
            );
            strategyData["strategy_code"] = strategyCode;
        }
        
        // 保存到数据库
        QString savedStrategyId = saveStrategyToDatabase(strategyData);
        if (savedStrategyId.isEmpty()) {
            strategyData["error"] = "保存到数据库失败";
            failedStrategies.append(strategyData);
            continue;
        }
        
        // 使用数据库返回的ID
        strategyData["strategy_id"] = savedStrategyId;
        
        successStrategies.push_back(strategyData);
    }
    
    // 更新缓存
    if (!successStrategies.empty()) {
        updateCacheBatch(successStrategies);
        refreshViewModelFromCache(m_viewModel, m_memoryCache);
        emit dataChanged();
    }
    
    // 发送失败信号
    if (!failedStrategies.isEmpty()) {
        emit importFailed(failedStrategies);
    }
    
    return failedStrategies.isEmpty();
}

bool StrategyService::exportStrategies(const QString& format, const QString& filePath) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QReadLocker locker(&m_rwLock);
    
    // 获取所有策略
    QVariantList strategies = getAllStrategies();
    
    // 构建导出数据
    QVariantMap exportData;
    exportData["version"] = "1.0";
    exportData["export_date"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    exportData["strategy_count"] = strategies.size();
    exportData["strategies"] = strategies;
    
    // 转换为JSON
    QJsonDocument doc = QJsonDocument::fromVariant(exportData);
    QByteArray jsonData = doc.toJson(QJsonDocument::Indented);
    
    // 保存到文件
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "StrategyService: 无法打开文件 -" << filePath << "错误:" << file.errorString();
        emit errorOccurred(QString("无法打开文件: %1").arg(file.errorString()));
        return false;
    }
    
    file.write(jsonData);
    file.close();
    
    qDebug() << "StrategyService: 导出策略成功 - 数量:" << strategies.size() << "路径:" << filePath;
    
    return true;
}

bool StrategyService::activateStrategy(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    QVariantMap updateData;
    updateData["status"] = "ACTIVE";
    updateData["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    {
        QWriteLocker locker(&m_rwLock);

        if (!m_repository->update(strategyId, updateData)) {
            qWarning() << "StrategyService: 激活策略失败 - ID:" << strategyId;
            emit errorOccurred(QString("激活策略失败: %1").arg(strategyId));
            return false;
        }

        QVariantMap strategy = loadStrategyFromCache(strategyId);
        if (!strategy.isEmpty()) {
            strategy["status"] = "ACTIVE";
            strategy["updated_at"] = updateData["updated_at"];
            saveStrategyToCache(strategyId, strategy);
        }

        clearSignalPublicationState(strategyId);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategyStatus(strategyId, "ACTIVE");
    }

    emit strategyActivated(strategyId);
    emit dataChanged();

    return true;
}

bool StrategyService::deactivateStrategy(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    QVariantMap updateData;
    updateData["status"] = "INACTIVE";
    updateData["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    {
        QWriteLocker locker(&m_rwLock);

        if (!m_repository->update(strategyId, updateData)) {
            qWarning() << "StrategyService: 停用策略失败 - ID:" << strategyId;
            emit errorOccurred(QString("停用策略失败: %1").arg(strategyId));
            return false;
        }

        QVariantMap strategy = loadStrategyFromCache(strategyId);
        if (!strategy.isEmpty()) {
            strategy["status"] = "INACTIVE";
            strategy["updated_at"] = updateData["updated_at"];
            saveStrategyToCache(strategyId, strategy);
        }

        clearSignalPublicationState(strategyId);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategyStatus(strategyId, "INACTIVE");
    }

    emit strategyDeactivated(strategyId);
    emit dataChanged();

    return true;
}

bool StrategyService::archiveStrategy(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QWriteLocker locker(&m_rwLock);
    
    // 更新状态 - 使用通用update方法
    QVariantMap updateData;
    updateData["status"] = "ARCHIVED";
    updateData["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    if (!m_repository->update(strategyId, updateData)) {
        qWarning() << "StrategyService: 归档策略失败 - ID:" << strategyId;
        emit errorOccurred(QString("归档策略失败: %1").arg(strategyId));
        return false;
    }
    
    // 更新缓存
    QVariantMap strategy = loadStrategyFromCache(strategyId);
    if (!strategy.isEmpty()) {
        strategy["status"] = "ARCHIVED";
        strategy["updated_at"] = updateData["updated_at"];
        saveStrategyToCache(strategyId, strategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategyStatus(strategyId, "ARCHIVED");
    }
    
    emit dataChanged();
    
    return true;
}

bool StrategyService::duplicateStrategy(const QString& sourceStrategyId, const QString& newName) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QReadLocker locker(&m_rwLock);
    
    // 获取源策略
    QVariantMap sourceStrategy = getStrategyById(sourceStrategyId);
    if (sourceStrategy.isEmpty()) {
        qWarning() << "StrategyService: 源策略不存在 - ID:" << sourceStrategyId;
        emit errorOccurred(QString("源策略不存在: %1").arg(sourceStrategyId));
        return false;
    }
    
    // 创建副本
    QVariantMap newStrategy = sourceStrategy;
    
    // 更新名称
    newStrategy["strategy_name"] = newName;
    
    // 生成新的代码（策略ID由数据库自动生成）
    QString newStrategyCode = generateStrategyCode(
        newName,
        newStrategy.value("strategy_type").toString()
    );
    
    // 移除ID，让数据库自动生成
    newStrategy.remove("strategy_id");
    newStrategy["strategy_code"] = newStrategyCode;
    
    // 重置状态和时间
    newStrategy["status"] = "DRAFT";
    newStrategy["created_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    newStrategy["updated_at"] = newStrategy["created_at"];
    
    // 移除性能指标
    newStrategy.remove("performance_metrics");
    
    // 创建新策略
    return createStrategy(newStrategy) != QString();
}

bool StrategyService::updateStrategyParameters(const QString& strategyId, const QVariantMap& parameters) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    QString templateError;
    QVariantMap strategyForValidation;
    strategyForValidation.insert(QStringLiteral("parameters"), parameters);
    const QVariantList templateBindings = strategyRuleTemplateBindings(strategyForValidation);
    if (!validateRuleTemplateBindings(templateBindings, &templateError)) {
        qWarning() << "StrategyService: 规则模板校验失败 - ID:" << strategyId << templateError;
        emit errorOccurred(templateError.isEmpty() ? QStringLiteral("规则模板校验失败") : templateError);
        return false;
    }
    
    QWriteLocker locker(&m_rwLock);
    
    // 获取现有策略
    QVariantMap existingStrategy = loadStrategyFromCache(strategyId);
    if (existingStrategy.isEmpty()) {
        existingStrategy = m_repository->findById(strategyId);
        if (existingStrategy.isEmpty()) {
            qWarning() << "StrategyService: 策略不存在 - ID:" << strategyId;
            return false;
        }
    }
    
    // 构建更新数据
    QVariantMap normalizedStrategy = existingStrategy;
        normalizedStrategy["parameters"] = mergeVariantMapsRecursive(existingStrategy.value("parameters").toMap(), parameters);
    applyCanonicalStrategyStructures(normalizedStrategy);

    QVariantMap updateData;
    updateData["parameters"] = normalizedStrategy.value("parameters").toMap();
    updateData["ruleProfileSnapshot"] = normalizedStrategy.value("ruleProfileSnapshot").toMap();
    updateData["executionPolicySnapshot"] = normalizedStrategy.value("executionPolicySnapshot").toMap();
    updateData["backtestAssumptionsSnapshot"] = normalizedStrategy.value("backtestAssumptionsSnapshot").toMap();
    updateData["strategyScopeContextSnapshot"] = normalizedStrategy.value("strategyScopeContextSnapshot").toMap();
    updateData["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    // 更新数据库 - 使用通用update方法
    if (!m_repository->update(strategyId, updateData)) {
        qWarning() << "StrategyService: 更新策略参数失败 - ID:" << strategyId;
        emit errorOccurred(QString("更新策略参数失败: %1").arg(strategyId));
        return false;
    }
    
    // 更新缓存
    QVariantMap strategy = loadStrategyFromCache(strategyId);
    if (!strategy.isEmpty()) {
        strategy = normalizedStrategy;
        strategy["updated_at"] = updateData["updated_at"];
        saveStrategyToCache(strategyId, strategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategy(strategyId, strategy);
    }
    
    emit dataChanged();
    
    return true;
}

QVariantMap StrategyService::getStrategyParameters(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantMap();
    }

    {
        QReadLocker locker(&m_rwLock);
        const QVariantMap cachedStrategy = recoverEditableRulePayloadFromBacktest(loadStrategyFromCache(strategyId));
        const QVariantMap cachedParameters = cachedStrategy.value("parameters").toMap();
        if (!cachedParameters.isEmpty() && hasCompleteEditableRulePayload(cachedStrategy)) {
            return cachedParameters;
        }
    }

    QVariantMap repositoryStrategy;
    if (m_repository) {
        repositoryStrategy = m_repository->findById(strategyId);
    }

    repositoryStrategy = recoverEditableRulePayloadFromBacktest(repositoryStrategy);

    if (!repositoryStrategy.isEmpty()) {
        QWriteLocker locker(&m_rwLock);
        saveStrategyToCache(strategyId, repositoryStrategy);
        return repositoryStrategy.value("parameters").toMap();
    }

    QReadLocker locker(&m_rwLock);
    return loadStrategyFromCache(strategyId).value("parameters").toMap();
}

bool StrategyService::updateStrategyPerformance(const QString& strategyId, const QVariantMap& performance) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    QVariantMap strategy;
    QString updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    QVariantMap mergedPerformance;
    {
        QWriteLocker locker(&m_rwLock);

        QVariantMap existingStrategy = loadStrategyFromCache(strategyId);
        if (existingStrategy.isEmpty() || !hasCompleteEditableRulePayload(existingStrategy)) {
            existingStrategy = m_repository->findById(strategyId);
            if (existingStrategy.isEmpty()) {
                qWarning() << "StrategyService: 策略不存在 - ID:" << strategyId;
                return false;
            }
        }

        const bool payloadWasIncomplete = !hasCompleteEditableRulePayload(existingStrategy);
        existingStrategy = recoverEditableRulePayloadFromBacktest(existingStrategy);
        const bool payloadRecovered = !payloadWasIncomplete ? false : hasCompleteEditableRulePayload(existingStrategy);

        mergedPerformance = mergePerformanceMetrics(existingStrategy, performance, updatedAt);

        QVariantMap updateData;
        updateData["performance_metrics"] = mergedPerformance;
        const bool replaceLatestBacktest = performance.value(QStringLiteral("replaceLatestBacktest"), true).toBool();
        if (replaceLatestBacktest) {
            QStringList persistedBacktestPool = symbolPoolFromVariant(performance.value(QStringLiteral("backtestSymbolPool")));
            if (persistedBacktestPool.isEmpty()) {
                persistedBacktestPool = strategyBacktestSymbolPool(performance.value(QStringLiteral("backtestHistoryEntry")).toMap());
            }
            if (!persistedBacktestPool.isEmpty()) {
                updateData["backtest_symbol_pool"] = persistedBacktestPool;

                QVariantMap updatedParameters = existingStrategy.value(QStringLiteral("parameters")).toMap();
                updatedParameters.insert(QStringLiteral("backtest_symbol_pool"), persistedBacktestPool);
                updateData["parameters"] = updatedParameters;
            }
        }
        if (payloadRecovered && !updateData.contains(QStringLiteral("parameters"))) {
            updateData["parameters"] = existingStrategy.value(QStringLiteral("parameters")).toMap();
        }
        updateData["updated_at"] = updatedAt;

        if (!m_repository->update(strategyId, updateData)) {
            qWarning() << "StrategyService: 更新策略性能失败 - ID:" << strategyId;
            emit errorOccurred(QString("更新策略性能失败: %1").arg(strategyId));
            return false;
        }

        strategy = existingStrategy;
        strategy["performance_metrics"] = mergedPerformance;
        if (updateData.contains(QStringLiteral("backtest_symbol_pool"))) {
            strategy["backtest_symbol_pool"] = updateData.value(QStringLiteral("backtest_symbol_pool"));
            QVariantMap updatedParameters = strategy.value(QStringLiteral("parameters")).toMap();
            updatedParameters.insert(QStringLiteral("backtest_symbol_pool"), updateData.value(QStringLiteral("backtest_symbol_pool")));
            strategy["parameters"] = updatedParameters;
        }
        strategy["updated_at"] = updatedAt;
        saveStrategyToCache(strategyId, strategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategyPerformance(strategyId, mergedPerformance);
    }

    emit dataChanged();

    return true;
}

QVariantMap StrategyService::getStrategyPerformance(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantMap();
    }
    
    QReadLocker locker(&m_rwLock);
    
    QVariantMap strategy = getStrategyById(strategyId);
    if (strategy.isEmpty()) {
        return QVariantMap();
    }
    
    return strategy.value("performance_metrics").toMap();
}

void StrategyService::syncWithDatabase() {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return;
    }

    {
        QWriteLocker locker(&m_rwLock);
        clearAllCache();
    }

    loadStrategiesFromDatabase();

    if (m_viewModel) {
        refreshViewModelFromCache(m_viewModel, m_memoryCache);
    }
    emit dataChanged();
    
    qDebug() << "StrategyService: 与数据库同步完成";
}

void StrategyService::clearCache() {
    QWriteLocker locker(&m_rwLock);
    clearAllCache();
    m_cacheLoaded = false;
    emit cacheLoadedChanged();
    qDebug() << "StrategyService: 缓存已清除";
}

QVariantMap StrategyService::createDefaultStrategy(const QString& strategyType, const QString& strategyName) {
    QString name = strategyName.isEmpty() ? QString("%1策略").arg(strategyType) : strategyName;
    
    // 根据前端类型映射后端类型
    QString backendType = "CUSTOM";
    QString displayName = "自定义";
    
    if (STRATEGY_TYPE_MAPPING.contains(strategyType)) {
        backendType = STRATEGY_TYPE_MAPPING[strategyType][0];
        displayName = STRATEGY_TYPE_MAPPING[strategyType][1];
    }
    
    QVariantMap strategy;
    
    if (backendType == "TREND") {
        strategy = createTrendFollowingStrategy(name);
    } else if (backendType == "MEAN_REVERSION") {
        strategy = createMeanReversionStrategy(name);
    } else if (backendType == "ALPHA") {
        strategy = createAlphaStrategy(name);
    } else if (backendType == "ARBITRAGE") {
        strategy = createArbitrageStrategy(name);
    } else {
        strategy = createCustomStrategy(name, backendType);
    }

    QVariantMap parameters = strategy.value("parameters").toMap();
    parameters["strategy_subtype"] = strategyType;
    if (strategyType == "machine_learning") {
        parameters["feature_window"] = parameters.value("feature_window", 60);
        parameters["prediction_days"] = parameters.value("prediction_days", 1);
        parameters["training_days"] = parameters.value("training_days", 1000);
        parameters["confidence_threshold"] = parameters.value("confidence_threshold", 0.6);
    } else if (strategyType == "multi_factor") {
        parameters["factor_types"] = parameters.value("factor_types", QStringList{"value", "quality", "growth", "momentum"});
    } else if (strategyType == "high_frequency") {
        parameters["execution_timeframe"] = parameters.value("execution_timeframe", "5min");
    } else if (strategyType == "event_driven") {
        parameters["event_types"] = parameters.value("event_types", QStringList{"earnings_release", "merger_announcement"});
    } else if (strategyType == "custom") {
        parameters["custom_code"] = parameters.value("custom_code", "# custom strategy\n");
    }
    strategy["parameters"] = parameters;
    strategy["sub_type"] = strategyType;
    
    // 添加描述
    strategy["description"] = QString("%1 - %2").arg(name).arg(STRATEGY_TYPE_DESCRIPTIONS.value(backendType, ""));
    applyRuleNativeTemplateDefaults(strategy, backendType, strategyType, name);
    applyCanonicalStrategyStructures(strategy);
    
    return strategy;
}

QVariantMap StrategyService::getStrategyTemplate(const QString& strategyType) {
    return createDefaultStrategy(strategyType, "");
}

QStringList StrategyService::getAvailableStrategyTypes() {
    return STRATEGY_TYPE_MAPPING.keys();
}

QVariantMap StrategyService::getStrategyTypeDescriptions() {
    QVariantMap descriptions;
    for (auto it = STRATEGY_TYPE_DESCRIPTIONS.begin(); it != STRATEGY_TYPE_DESCRIPTIONS.end(); ++it) {
        descriptions[it.key()] = it.value();
    }
    return descriptions;
}

// ============ 私有方法实现 ============

void StrategyService::initializeRepository() {
    // 首先确保数据库连接管理器已初始化
    // 这会配置ConnectionPool，确保StrategyRepository使用的连接池有正确的配置
    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    if (!dbManager.initialize()) {
        qWarning() << "StrategyService::initializeRepository: Database connection manager initialization failed";
        throw std::runtime_error("数据库连接初始化失败");
    }
    
    qDebug() << "✅ StrategyService::initializeRepository: Database connection manager initialized";
    
    // 创建策略仓储实例
    m_repository = std::make_shared<StrategyRepository>();
    if (!m_repository->initialize()) {
        throw std::runtime_error("策略仓储初始化失败");
    }
    
    qDebug() << "✅ StrategyService::initializeRepository: Repository initialized successfully";
}

QString StrategyService::saveStrategyToDatabase(const QVariantMap& strategyData) {
    if (!m_repository) {
        return QString();
    }
    return m_repository->save(strategyData);
}

bool StrategyService::updateStrategyInDatabase(const QString& strategyId, const QVariantMap& strategyData) {
    if (!m_repository) {
        return false;
    }
    return m_repository->update(strategyId, strategyData);
}

bool StrategyService::deleteStrategyFromDatabase(const QString& strategyId) {
    if (!m_repository) {
        return false;
    }
    return m_repository->remove(strategyId);
}

QVariantList StrategyService::loadStrategiesFromDatabase() {
    qDebug() << "=======================================";
    qDebug() << "StrategyService::loadStrategiesFromDatabase: 开始加载策略数据";
    qDebug() << "=======================================";
    QVariantList strategies;
    
    if (!m_repository) {
        qWarning() << "StrategyService::loadStrategiesFromDatabase: 仓储未初始化";
        qDebug() << "m_repository 为空指针";
        return strategies;
    }
    
    try {
        qDebug() << "调用 m_repository->findAll()...";
        auto strategyMaps = m_repository->findAll();
        qDebug() << "数据库查询返回" << strategyMaps.size() << "个策略";
        
        if (strategyMaps.empty()) {
            qWarning() << "数据库查询返回空结果集";
        } else {
            // 输出前几个策略的信息用于调试
            for (size_t i = 0; i < std::min(strategyMaps.size(), (size_t)3); ++i) {
                const auto& strategy = strategyMaps[i];
                qDebug() << "策略" << i << ":";
                qDebug() << "  ID:" << strategy.value("strategy_id").toString();
                qDebug() << "  名称:" << strategy.value("strategy_name").toString();
                qDebug() << "  类型:" << strategy.value("strategy_type").toString();
                qDebug() << "  状态:" << strategy.value("status").toString();
            }
        }
        
        // 清空缓存
        qDebug() << "清空内存缓存...";
        m_memoryCache.clear();
        
        // 加载到缓存
        qDebug() << "加载策略到缓存...";
        for (const auto& strategy : strategyMaps) {
            QVariantMap normalizedStrategy = strategy;
            applyCanonicalStrategyStructures(normalizedStrategy);
            const QStringList liveSymbolPool = strategyLiveSymbolPool(normalizedStrategy);
            if (liveSymbolPool.size() > 50) {
                qInfo() << "StrategyService: detected large live symbol pool"
                        << normalizedStrategy.value("strategy_id").toString()
                        << normalizedStrategy.value("strategy_name").toString()
                        << "symbolCount=" << liveSymbolPool.size();
            }
            QString strategyId = normalizedStrategy.value("strategy_id").toString();
            if (!strategyId.isEmpty()) {
                m_memoryCache[strategyId] = normalizedStrategy;
            }
            strategies.append(normalizedStrategy);
        }
        
        m_cacheLoaded = true;
        qDebug() << "缓存状态更新，发出cacheLoadedChanged信号";
        emit cacheLoadedChanged();

        // 发出加载完成信号
        qDebug() << "发出strategiesLoaded信号，策略数量:" << strategies.size();
        emit strategiesLoaded(strategies);
        
        qDebug() << "StrategyService::loadStrategiesFromDatabase: 加载完成";
        qDebug() << "缓存策略数量:" << m_memoryCache.size();
        qDebug() << "=======================================";
        return strategies;
        
    } catch (const std::exception& e) {
        qCritical() << "StrategyService::loadStrategiesFromDatabase: Error:" << e.what();
        qDebug() << "=======================================";
        return QVariantList();
    }
}

void StrategyService::saveStrategyToCache(const QString& strategyId, const QVariantMap& strategyData) {
    QVariantMap normalizedStrategy = strategyData;
    applyCanonicalStrategyStructures(normalizedStrategy);
    m_memoryCache[strategyId] = normalizedStrategy;
}

QVariantMap StrategyService::loadStrategyFromCache(const QString& strategyId) {
    return m_memoryCache.value(strategyId);
}

void StrategyService::removeStrategyFromCache(const QString& strategyId) {
    m_memoryCache.remove(strategyId);
}

void StrategyService::clearAllCache() {
    m_memoryCache.clear();
}

void StrategyService::updateCacheBatch(const std::vector<QVariantMap>& strategies) {
    for (const auto& strategy : strategies) {
        QString strategyId = strategy.value("strategy_id").toString();
        if (!strategyId.isEmpty()) {
            saveStrategyToCache(strategyId, strategy);
        }
    }
}

bool StrategyService::validateStrategyData(const QVariantMap& strategyData, QString& errorMessage) {
    // 检查必填字段
    if (!strategyData.contains("strategy_name") || strategyData.value("strategy_name").toString().isEmpty()) {
        errorMessage = "策略名称不能为空";
        return false;
    }
    
    if (!strategyData.contains("strategy_type") || strategyData.value("strategy_type").toString().isEmpty()) {
        errorMessage = "策略类型不能为空";
        return false;
    }
    
    // 验证策略类型
    QString strategyType = strategyData.value("strategy_type").toString();
    QStringList validTypes = {"TREND", "MEAN_REVERSION", "ALPHA", "ARBITRAGE", "HFT", "PORTFOLIO", "CUSTOM"};
    if (!validTypes.contains(strategyType)) {
        errorMessage = QString("无效的策略类型: %1").arg(strategyType);
        return false;
    }
    
    // 验证状态（如果提供）
    if (strategyData.contains("status")) {
        QString status = strategyData.value("status").toString();
        // 数据库只支持：'ACTIVE', 'INACTIVE', 'TESTING', 'ARCHIVED'
        // 将DRAFT转换为ACTIVE，其他无效状态也转换
        QStringList validStatuses = {"ACTIVE", "INACTIVE", "TESTING", "ARCHIVED"};
        if (!validStatuses.contains(status)) {
            // 如果状态无效，转换为ACTIVE并记录警告
            qWarning() << "StrategyService: 转换无效状态" << status << "为ACTIVE";
            // 不返回错误，而是转换状态
        }
    }

    const QVariantList templateBindings = strategyRuleTemplateBindings(strategyData);
    if (!templateBindings.isEmpty()) {
        QString templateError;
        if (!validateRuleTemplateBindings(templateBindings, &templateError)) {
            errorMessage = templateError.isEmpty() ? QStringLiteral("规则模板校验失败") : templateError;
            return false;
        }
    }
    
    return true;
}

QString StrategyService::generateStrategyId(const QString& strategyName) {
    // 使用名称哈希加随机数生成ID
    QString nameHash = QString::number(qHash(strategyName));
    QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString random = QString::number(QRandomGenerator::global()->generate());
    
    return QString("STR_%1_%2_%3").arg(nameHash.left(6)).arg(timestamp.right(6)).arg(random.right(4));
}

QString StrategyService::generateStrategyCode(const QString& strategyName, const QString& strategyType) {
    // 生成简写代码
    QString codePrefix;
    if (strategyType == "TREND") codePrefix = "TRD";
    else if (strategyType == "MEAN_REVERSION") codePrefix = "MR";
    else if (strategyType == "ALPHA") codePrefix = "ALPHA";
    else if (strategyType == "ARBITRAGE") codePrefix = "ARB";
    else if (strategyType == "PORTFOLIO") codePrefix = "PTF";
    else codePrefix = "CST";
    
    // 使用名称的前几个字符，转换为大写，移除空格
    QString namePart = strategyName.left(6).toUpper().replace(" ", "_").replace("-", "_");
    
    // 添加毫秒级时间戳和随机尾缀，避免同名策略在同一分钟内重复创建时撞唯一键
    QString timestamp = QDateTime::currentDateTimeUtc().toString("yyMMddHHmmsszzz");
    QString entropy = QString::number(QRandomGenerator::global()->bounded(1000, 10000));
    
    return QString("%1_%2_%3%4").arg(codePrefix).arg(namePart).arg(timestamp).arg(entropy);
}

QVariantMap StrategyService::createTrendFollowingStrategy(const QString& name) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategy_type"] = "TREND";
    strategy["description"] = QString("趋势跟踪策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["fast_period"] = 10;
    parameters["slow_period"] = 30;
    parameters["position_size"] = 0.1;
    parameters["stop_loss"] = 0.05;
    parameters["take_profit"] = 0.15;
    strategy["parameters"] = parameters;
    
    return strategy;
}

QVariantMap StrategyService::createMeanReversionStrategy(const QString& name) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategy_type"] = "MEAN_REVERSION";
    strategy["description"] = QString("均值回归策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["boll_period"] = 20;
    parameters["boll_std"] = 2.0;
    parameters["position_size"] = 0.1;
    parameters["reversion_threshold"] = 0.5;
    strategy["parameters"] = parameters;
    
    return strategy;
}

QVariantMap StrategyService::createAlphaStrategy(const QString& name) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategy_type"] = "ALPHA";
    strategy["description"] = QString("阿尔法策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["top_n"] = 10;
    parameters["rebalance_days"] = 20;
    parameters["momentum_period"] = 60;
    parameters["position_size"] = 0.1;
    strategy["parameters"] = parameters;
    
    return strategy;
}

QVariantMap StrategyService::createArbitrageStrategy(const QString& name) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategy_type"] = "ARBITRAGE";
    strategy["description"] = QString("套利策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["spread_threshold"] = 0.02;
    parameters["entry_z_score"] = 2.0;
    parameters["exit_z_score"] = 0.5;
    strategy["parameters"] = parameters;
    
    return strategy;
}

QVariantMap StrategyService::createCustomStrategy(const QString& name, const QString& type) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategy_type"] = type;
    strategy["description"] = QString("自定义策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["custom_param1"] = "value1";
    parameters["custom_param2"] = 0.5;
    strategy["parameters"] = parameters;
    
    return strategy;
}

StrategyViewModel* StrategyService::getViewModel() { 
    // 确保ViewModel总是有效
    if (!m_viewModel) {
        // 创建ViewModel作为Service的子对象
        m_viewModel = new StrategyViewModel(this);
        qDebug() << "StrategyService: 创建默认视图模型，地址:" << m_viewModel;
    }
    return m_viewModel; 
}

void StrategyService::setViewModel(StrategyViewModel* viewModel) { 
    if (m_viewModel && m_viewModel != viewModel) {
        // 删除旧的ViewModel，如果它是Service的子对象
        if (m_viewModel->parent() == this) {
            m_viewModel->deleteLater();
        }
    }
    m_viewModel = viewModel; 
    qDebug() << "StrategyService: 设置视图模型，地址:" << m_viewModel;
}
