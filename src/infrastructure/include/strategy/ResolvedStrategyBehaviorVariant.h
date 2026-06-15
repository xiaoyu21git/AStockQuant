#pragma once

#include "../../../domain/types/ResolvedStrategyBehavior.h"

#include <QMetaType>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <initializer_list>

namespace domain::backtest {

inline QVariant firstConfiguredNonEmptyValue(const QVariantMap& map,
                                            std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QVariant value = map.value(QString::fromUtf8(key));
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

namespace detail {

inline constexpr char kParametersKey[] = "parameters";
inline constexpr char kStrategyScopeContextSnapshotKey[] = "strategyScopeContextSnapshot";
inline constexpr char kLegacyStrategyScopeContextKey[] = "strategy_scope_context";
inline constexpr char kStrategyBehaviorKindKey[] = "strategyBehaviorKind";
inline constexpr char kLegacyStrategyBehaviorKindKey[] = "strategy_behavior_kind";
inline constexpr char kStrategyTypeIndexKey[] = "strategyTypeIndex";

inline QVariant configuredValue(const QVariantMap& map, const char* key)
{
    return map.value(QString::fromUtf8(key));
}

inline QVariantMap configuredMap(const QVariantMap& map, const char* key)
{
    return configuredValue(map, key).toMap();
}

inline QVariantMap configuredMap(const QVariantMap& map,
                                 const char* key,
                                 const QVariant& fallbackValue)
{
    return map.value(QString::fromUtf8(key), fallbackValue).toMap();
}

inline QVariantMap strategyParameters(const QVariantMap& strategy)
{
    return configuredMap(strategy, kParametersKey);
}

inline QVariantMap strategyScopeContext(const QVariantMap& strategy,
                                        const QVariantMap& parameters)
{
    return configuredMap(
        strategy,
        kStrategyScopeContextSnapshotKey,
        configuredValue(parameters, kLegacyStrategyScopeContextKey));
}

inline QVariant configuredStrategyBehaviorValue(const QVariantMap& map)
{
    return firstConfiguredNonEmptyValue(
        map,
        {kStrategyBehaviorKindKey, kLegacyStrategyBehaviorKindKey});
}

inline QVariant configuredStoredTypeValue(const QVariantMap& strategy)
{
    return configuredValue(strategy, kStrategyTypeIndexKey);
}

inline QVariant effectiveStrategyBehaviorValue(const QVariantMap& strategy)
{
    const QVariantMap parameters = strategyParameters(strategy);
    const QVariantMap scopeContext = strategyScopeContext(strategy, parameters);
    const QVariant scopeValue = configuredStrategyBehaviorValue(scopeContext);
    if (scopeValue.isValid()) {
        return scopeValue;
    }

    const QVariant parameterValue = configuredStrategyBehaviorValue(parameters);
    if (parameterValue.isValid()) {
        return parameterValue;
    }

    return configuredStrategyBehaviorValue(strategy);
}

} // namespace detail

inline ResolvedStrategyIdentity resolveStrategyStoredType(const QVariant& rawValue)
{
    if (!rawValue.isValid() || rawValue.isNull()) {
        return {};
    }

    bool ok = false;
    const int storedTypeIndex = rawValue.toInt(&ok);
    return ok ? resolveStrategyStoredType(storedTypeIndex) : ResolvedStrategyIdentity{};
}

inline ResolvedStrategyBehavior resolveStrategyBehavior(const QVariant& rawValue)
{
    bool ok = false;
    const int behaviorIndex = rawValue.toInt(&ok);
    return ok ? resolveStrategyBehavior(behaviorIndex) : ResolvedStrategyBehavior{};
}

inline ResolvedStrategyBehavior resolveStrategyBehavior(const QVariantMap& strategy)
{
    return resolveStrategyBehavior(detail::effectiveStrategyBehaviorValue(strategy));
}

inline ResolvedStrategyIdentity resolveStrategyIdentity(const QVariantMap& strategy)
{
    ResolvedStrategyIdentity identity = resolveStrategyStoredType(
        detail::configuredStoredTypeValue(strategy));
    identity.behavior = resolveStrategyBehavior(strategy);
    return identity;
}

} // namespace domain::backtest