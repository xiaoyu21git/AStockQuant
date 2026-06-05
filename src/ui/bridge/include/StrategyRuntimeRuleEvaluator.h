#pragma once

#include <QString>
#include <QVariantMap>

class StrategyRuntimeRuleEvaluator {
public:
    struct MarketContext {
        QString symbol;
        double latestPrice = 0.0;
        double referencePrice = 0.0;
        QString marketEventType;
        bool liveTradingEnabled = false;
        bool marketSessionKnown = false;
        bool marketSessionOpen = false;
        QVariantMap marketSessionSnapshot;
        bool runtimeSessionKnown = false;
        bool runtimeSessionReady = false;
        QVariantMap runtimeSessionSnapshot;
        QVariantMap tradingConfiguration;
    };

    QVariantMap evaluateMarketCandidate(const QVariantMap& strategy,
                                        const MarketContext& context) const;

    static QString determineAction(const QVariantMap& strategy,
                                   double latestPrice,
                                   double referencePrice);
    static double determineStrength(const QVariantMap& strategy,
                                    double latestPrice,
                                    double referencePrice);
    static QString normalizeStrategySymbol(const QString& rawSymbol);

private:
    static QVariantMap buildBaseEvaluation(const QVariantMap& strategy,
                                           const MarketContext& context);
    static bool applyMarketEnvironmentGate(QVariantMap& evaluation,
                                           const MarketContext& context);
    static bool applyScopeGate(QVariantMap& evaluation,
                               const QVariantMap& strategy,
                               const MarketContext& context);
    static bool applySignalGate(QVariantMap& evaluation,
                                const QVariantMap& strategy,
                                const MarketContext& context);
    static void applyExecutionGate(QVariantMap& evaluation,
                                   const MarketContext& context);
    static QString resolveStrategyIdentifier(const QVariantMap& strategy);
    static QString resolveStrategyName(const QVariantMap& strategy);
    static QString normalizedStrategyType(const QVariantMap& strategy);
    static bool strategyAllowsMarketSymbol(const QVariantMap& strategy, const QString& marketSymbol);
};