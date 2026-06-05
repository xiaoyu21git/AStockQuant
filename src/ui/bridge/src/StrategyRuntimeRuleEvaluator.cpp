#include "StrategyRuntimeRuleEvaluator.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <cmath>

namespace {

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

QStringList parseStrategySymbolPoolText(const QString& text)
{
    const QStringList rawTokens = text.split(QRegularExpression(QStringLiteral("[,;\\s，；]+")), Qt::SkipEmptyParts);
    QStringList symbols;
    QSet<QString> seen;
    for (const QString& rawToken : rawTokens) {
        const QString normalizedToken = StrategyRuntimeRuleEvaluator::normalizeStrategySymbol(rawToken);
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
        const QString normalizedSymbol = StrategyRuntimeRuleEvaluator::normalizeStrategySymbol(symbol);
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

QString runtimeSessionBlockedReason(const QVariantMap& runtimeSessionSnapshot)
{
    if (runtimeSessionSnapshot.value(QStringLiteral("hasError")).toBool()) {
        return QStringLiteral("runtime_session_error");
    }

    if (!runtimeSessionSnapshot.value(QStringLiteral("initialized")).toBool()) {
        return QStringLiteral("runtime_not_initialized");
    }

    if (!runtimeSessionSnapshot.value(QStringLiteral("connected")).toBool()) {
        return QStringLiteral("runtime_not_connected");
    }

    if (!runtimeSessionSnapshot.value(QStringLiteral("isRunning")).toBool()) {
        return QStringLiteral("runtime_not_running");
    }

    return QStringLiteral("runtime_not_ready");
}

} // namespace

QVariantMap StrategyRuntimeRuleEvaluator::evaluateMarketCandidate(const QVariantMap& strategy,
                                                                 const MarketContext& context) const
{
    QVariantMap evaluation = buildBaseEvaluation(strategy, context);
    if (!applyMarketEnvironmentGate(evaluation, context)) {
        return evaluation;
    }
    if (!applyScopeGate(evaluation, strategy, context)) {
        return evaluation;
    }
    if (!applySignalGate(evaluation, strategy, context)) {
        return evaluation;
    }
    applyExecutionGate(evaluation, context);
    return evaluation;
}

QVariantMap StrategyRuntimeRuleEvaluator::buildBaseEvaluation(const QVariantMap& strategy,
                                                              const MarketContext& context)
{
    QVariantMap evaluation;
    evaluation.insert(QStringLiteral("strategyId"), resolveStrategyIdentifier(strategy));
    evaluation.insert(QStringLiteral("strategyName"), resolveStrategyName(strategy));
    evaluation.insert(QStringLiteral("strategyType"), normalizedStrategyType(strategy));
    evaluation.insert(QStringLiteral("symbol"), normalizeStrategySymbol(context.symbol));
    evaluation.insert(QStringLiteral("latestPrice"), context.latestPrice);
    evaluation.insert(QStringLiteral("referencePrice"), context.referencePrice);
    evaluation.insert(QStringLiteral("marketEventType"), context.marketEventType);
    evaluation.insert(QStringLiteral("evaluationMode"), QStringLiteral("shadow"));
    evaluation.insert(QStringLiteral("autoExecutionEnabled"), false);
    evaluation.insert(QStringLiteral("tradingEnabled"), context.tradingConfiguration.value(QStringLiteral("enabled")).toBool());
    evaluation.insert(QStringLiteral("readOnly"), context.tradingConfiguration.value(QStringLiteral("readOnly"), true).toBool());
    evaluation.insert(QStringLiteral("boundStrategyId"), context.tradingConfiguration.value(QStringLiteral("boundStrategyId")).toString().trimmed());
    evaluation.insert(QStringLiteral("marketSessionKnown"), context.marketSessionKnown);
    evaluation.insert(QStringLiteral("marketSessionOpen"), context.marketSessionOpen);
    evaluation.insert(QStringLiteral("marketSession"), context.marketSessionSnapshot);
    evaluation.insert(QStringLiteral("runtimeSessionKnown"), context.runtimeSessionKnown);
    evaluation.insert(QStringLiteral("runtimeSessionReady"), context.runtimeSessionReady);
    evaluation.insert(QStringLiteral("runtimeSession"), context.runtimeSessionSnapshot);
    evaluation.insert(QStringLiteral("ruleProfile"), strategy.value(QStringLiteral("ruleProfileSnapshot")).toMap());
    evaluation.insert(QStringLiteral("executionPolicy"), strategy.value(QStringLiteral("executionPolicySnapshot")).toMap());
    evaluation.insert(QStringLiteral("backtestAssumptions"), strategy.value(QStringLiteral("backtestAssumptionsSnapshot")).toMap());
    evaluation.insert(QStringLiteral("strategyScopeContext"), strategy.value(QStringLiteral("strategyScopeContextSnapshot")).toMap());
    evaluation.insert(QStringLiteral("marketEnvironmentGate"), QStringLiteral("pending"));
    evaluation.insert(QStringLiteral("scopeGate"), QStringLiteral("pending"));
    evaluation.insert(QStringLiteral("signalGate"), QStringLiteral("pending"));
    evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("pending"));
    return evaluation;
}

bool StrategyRuntimeRuleEvaluator::applyMarketEnvironmentGate(QVariantMap& evaluation,
                                                              const MarketContext& context)
{
    if (context.latestPrice <= 0.0 || context.referencePrice <= 0.0) {
        evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
        evaluation.insert(QStringLiteral("gate"), QStringLiteral("market_environment"));
        evaluation.insert(QStringLiteral("reason"), QStringLiteral("invalid_market_reference"));
        evaluation.insert(QStringLiteral("marketEnvironmentGate"), QStringLiteral("blocked"));
        evaluation.insert(QStringLiteral("scopeGate"), QStringLiteral("skipped"));
        evaluation.insert(QStringLiteral("signalGate"), QStringLiteral("skipped"));
        evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("skipped"));
        return false;
    }

    if (context.marketSessionKnown && context.runtimeSessionKnown && !context.marketSessionOpen) {
        evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
        evaluation.insert(QStringLiteral("gate"), QStringLiteral("market_environment"));
        evaluation.insert(QStringLiteral("reason"), QStringLiteral("trading_session_closed"));
        evaluation.insert(QStringLiteral("marketEnvironmentGate"), QStringLiteral("blocked"));
        evaluation.insert(QStringLiteral("scopeGate"), QStringLiteral("skipped"));
        evaluation.insert(QStringLiteral("signalGate"), QStringLiteral("skipped"));
        evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("skipped"));
        return false;
    }

    if (context.liveTradingEnabled && context.runtimeSessionKnown && !context.runtimeSessionReady) {
        evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
        evaluation.insert(QStringLiteral("gate"), QStringLiteral("market_environment"));
        evaluation.insert(QStringLiteral("reason"), runtimeSessionBlockedReason(context.runtimeSessionSnapshot));
        evaluation.insert(QStringLiteral("marketEnvironmentGate"), QStringLiteral("blocked"));
        evaluation.insert(QStringLiteral("scopeGate"), QStringLiteral("skipped"));
        evaluation.insert(QStringLiteral("signalGate"), QStringLiteral("skipped"));
        evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("skipped"));
        return false;
    }

    evaluation.insert(QStringLiteral("marketEnvironmentGate"), context.liveTradingEnabled
        ? QStringLiteral("live")
        : QStringLiteral("shadow"));
    return true;
}

bool StrategyRuntimeRuleEvaluator::applyScopeGate(QVariantMap& evaluation,
                                                  const QVariantMap& strategy,
                                                  const MarketContext& context)
{
    if (!strategyAllowsMarketSymbol(strategy, context.symbol)) {
        evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
        evaluation.insert(QStringLiteral("gate"), QStringLiteral("scope"));
        evaluation.insert(QStringLiteral("reason"), QStringLiteral("symbol_outside_scope"));
        evaluation.insert(QStringLiteral("scopeGate"), QStringLiteral("blocked"));
        evaluation.insert(QStringLiteral("signalGate"), QStringLiteral("skipped"));
        evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("skipped"));
        return false;
    }

    evaluation.insert(QStringLiteral("scopeGate"), QStringLiteral("pass"));
    return true;
}

bool StrategyRuntimeRuleEvaluator::applySignalGate(QVariantMap& evaluation,
                                                   const QVariantMap& strategy,
                                                   const MarketContext& context)
{
    const QString action = determineAction(strategy, context.latestPrice, context.referencePrice);
    if (action.isEmpty()) {
        evaluation.insert(QStringLiteral("decision"), QStringLiteral("blocked"));
        evaluation.insert(QStringLiteral("gate"), QStringLiteral("signal"));
        evaluation.insert(QStringLiteral("reason"), QStringLiteral("no_action_candidate"));
        evaluation.insert(QStringLiteral("signalGate"), QStringLiteral("blocked"));
        evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("skipped"));
        return false;
    }

    evaluation.insert(QStringLiteral("candidateAction"), action);
    evaluation.insert(QStringLiteral("candidateStrength"), determineStrength(strategy, context.latestPrice, context.referencePrice));
    evaluation.insert(QStringLiteral("signalGate"), QStringLiteral("pass"));
    return true;
}

void StrategyRuntimeRuleEvaluator::applyExecutionGate(QVariantMap& evaluation,
                                                      const MarketContext& context)
{
    if (!context.liveTradingEnabled) {
        evaluation.insert(QStringLiteral("decision"), QStringLiteral("shadow_only"));
        evaluation.insert(QStringLiteral("gate"), QStringLiteral("execution"));
        evaluation.insert(QStringLiteral("reason"), QStringLiteral("runtime_read_only"));
        evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("shadow_only"));
        return;
    }

    evaluation.insert(QStringLiteral("decision"), QStringLiteral("candidate_ready"));
    evaluation.insert(QStringLiteral("gate"), QStringLiteral("execution"));
    evaluation.insert(QStringLiteral("reason"), QStringLiteral("auto_signal_chain_disabled"));
    evaluation.insert(QStringLiteral("executionGate"), QStringLiteral("pass"));
}

QString StrategyRuntimeRuleEvaluator::determineAction(const QVariantMap& strategy,
                                                      double latestPrice,
                                                      double referencePrice)
{
    if (latestPrice <= 0.0 || referencePrice <= 0.0) {
        return {};
    }

    const QString strategyType = normalizedStrategyType(strategy);
    if (strategyType == QStringLiteral("TREND")) {
        return latestPrice >= referencePrice ? QStringLiteral("BUY") : QStringLiteral("SELL");
    }
    if (strategyType == QStringLiteral("MEAN_REVERSION")) {
        return latestPrice < referencePrice ? QStringLiteral("BUY") : QStringLiteral("SELL");
    }
    if (strategyType == QStringLiteral("PORTFOLIO")) {
        return {};
    }

    const double delta = latestPrice - referencePrice;
    if (std::fabs(delta) < 1e-6) {
        return {};
    }
    return delta > 0.0 ? QStringLiteral("BUY") : QStringLiteral("SELL");
}

double StrategyRuntimeRuleEvaluator::determineStrength(const QVariantMap& strategy,
                                                       double latestPrice,
                                                       double referencePrice)
{
    Q_UNUSED(strategy);
    if (referencePrice <= 0.0) {
        return 0.0;
    }
    return std::fabs(latestPrice - referencePrice) / referencePrice;
}

QString StrategyRuntimeRuleEvaluator::resolveStrategyIdentifier(const QVariantMap& strategy)
{
    return firstNonEmptyStrategyText(strategy, {"strategy_id", "strategyId"});
}

QString StrategyRuntimeRuleEvaluator::resolveStrategyName(const QVariantMap& strategy)
{
    return firstNonEmptyStrategyText(strategy, {"strategy_name", "strategyName"});
}

QString StrategyRuntimeRuleEvaluator::normalizedStrategyType(const QVariantMap& strategy)
{
    return strategy.value(QStringLiteral("strategy_type")).toString().trimmed().toUpper();
}

QString StrategyRuntimeRuleEvaluator::normalizeStrategySymbol(const QString& rawSymbol)
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

bool StrategyRuntimeRuleEvaluator::strategyAllowsMarketSymbol(const QVariantMap& strategy, const QString& marketSymbol)
{
    const QStringList symbolPool = strategyLiveSymbolPool(strategy);
    if (symbolPool.isEmpty()) {
        return false;
    }

    return symbolPool.contains(normalizeStrategySymbol(marketSymbol));
}