#include "RiskConfigService.h"
#include "AppStoragePaths.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>
#include <QThread>

namespace {

constexpr int kPersistRetryCount = 3;
constexpr unsigned long kPersistRetryDelayMs = 25;

QString writableConfigBaseDir()
{
    return bridge::storage::configDir();
}

QJsonObject toJsonObject(const QVariantMap& map)
{
    return QJsonObject::fromVariantMap(map);
}

QVariantMap toVariantMap(const QJsonObject& object)
{
    return object.toVariantMap();
}

const QStringList& forwardDaysKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("forwardDays"),
        QStringLiteral("forward_days"),
        QStringLiteral("holdingPeriod"),
        QStringLiteral("holding_period")
    };
    return keys;
}

const QStringList& rebalanceDaysKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("rebalanceDays"),
        QStringLiteral("rebalance_days"),
        QStringLiteral("rebalancingPeriod"),
        QStringLiteral("rebalanceFrequency")
    };
    return keys;
}

const QStringList& commissionRateKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("commissionRate"),
        QStringLiteral("commission_rate"),
        QStringLiteral("commission"),
        QStringLiteral("transactionCost"),
        QStringLiteral("transaction_cost")
    };
    return keys;
}

const QStringList& slippageRateKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("slippageRate"),
        QStringLiteral("slippage_rate"),
        QStringLiteral("slippage"),
        QStringLiteral("slippageCost"),
        QStringLiteral("slippageLimit")
    };
    return keys;
}

const QStringList& riskFreeRateKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("riskFreeRate"),
        QStringLiteral("risk_free_rate")
    };
    return keys;
}

const QStringList& benchmarkSymbolKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("benchmarkSymbol")
    };
    return keys;
}

const QStringList& stopLossPercentKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("stopLossPercent"),
        QStringLiteral("stop_loss"),
        QStringLiteral("stopLoss")
    };
    return keys;
}

const QStringList& takeProfitPercentKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("takeProfitPercent"),
        QStringLiteral("take_profit"),
        QStringLiteral("takeProfit")
    };
    return keys;
}

const QStringList& maxDrawdownLimitKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("maxDrawdownLimit"),
        QStringLiteral("max_drawdown_limit")
    };
    return keys;
}

const QStringList& maxDailyLossKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("maxDailyLoss"),
        QStringLiteral("max_daily_loss")
    };
    return keys;
}

const QStringList& maxPositionPercentKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("maxPositionPercent"),
        QStringLiteral("maxSinglePositionRatio"),
        QStringLiteral("positionPercent"),
        QStringLiteral("position_size"),
        QStringLiteral("positionSize")
    };
    return keys;
}

const QStringList& maxTotalExposureKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("maxTotalExposure"),
        QStringLiteral("maxPositionRatio")
    };
    return keys;
}

const QStringList& positionSizingMethodKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("positionSizingMethod"),
        QStringLiteral("position_sizing_method")
    };
    return keys;
}

const QStringList& minWeightPercentKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("minWeightPercent"),
        QStringLiteral("min_weight_percent"),
        QStringLiteral("minPositionPercent"),
        QStringLiteral("minSinglePositionRatio")
    };
    return keys;
}

const QStringList& maxWeightPercentKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("maxWeightPercent"),
        QStringLiteral("max_weight_percent"),
        QStringLiteral("maxPositionPercent"),
        QStringLiteral("maxSinglePositionRatio"),
        QStringLiteral("position_size"),
        QStringLiteral("positionSize")
    };
    return keys;
}

const QStringList& autoStopEnabledKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("autoStopEnabled"),
        QStringLiteral("auto_stop_enabled")
    };
    return keys;
}

const QStringList& orderSizeLimitKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("orderSizeLimit"),
        QStringLiteral("maxOrderSize")
    };
    return keys;
}

const QStringList& turnoverLimitKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("turnoverLimit")
    };
    return keys;
}

const QStringList& slippageLimitKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("slippageLimit")
    };
    return keys;
}

const QStringList& level1BreakerKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("level1Breaker")
    };
    return keys;
}

const QStringList& level2BreakerKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("level2Breaker")
    };
    return keys;
}

const QStringList& level3BreakerKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("level3Breaker")
    };
    return keys;
}

const QStringList& metricPersistenceMinAbsIcKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("metricPersistenceMinAbsIc")
    };
    return keys;
}

const QStringList& metricPersistenceMinIrKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("metricPersistenceMinIr")
    };
    return keys;
}

const QStringList& metricPersistenceMinProfitFactorKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("metricPersistenceMinProfitFactor")
    };
    return keys;
}

const QStringList& varWarningPercentKeysStorage()
{
    static const QStringList keys = {
        QStringLiteral("varWarningPercent")
    };
    return keys;
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

QVariant resolvedConfiguredValue(const QVariantMap& map,
                                const QStringList& keys,
                                const QVariant& fallback = {})
{
    const QVariant resolved = firstConfiguredValue(map, keys);
    return resolved.isValid() ? resolved : fallback;
}

void setConfiguredValue(QVariantMap& map, const QStringList& keys, const QVariant& value)
{
    for (const QString& key : keys) {
        map.insert(key, value);
    }
}

void setConfiguredNumericValues(std::map<std::string, double>& map,
                                const QStringList& keys,
                                double value)
{
    for (const QString& key : keys) {
        map[key.toStdString()] = value;
    }
}

void setConfiguredStringValues(std::map<std::string, std::string>& map,
                               const QStringList& keys,
                               const QString& value)
{
    const std::string encodedValue = value.toStdString();
    for (const QString& key : keys) {
        map[key.toStdString()] = encodedValue;
    }
}

bool boolFromVariant(const QVariant& value, bool fallback)
{
    if (!value.isValid() || value.isNull()) {
        return fallback;
    }

    if (value.canConvert<bool>()) {
        return value.toBool();
    }

    const QString text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1") || text == QStringLiteral("yes")) {
        return true;
    }
    if (text == QStringLiteral("false") || text == QStringLiteral("0") || text == QStringLiteral("no")) {
        return false;
    }

    return fallback;
}

QVariantMap normalizeRiskConfiguration(const QVariantMap& rawConfiguration)
{
    QVariantMap normalized = rawConfiguration;

    const auto applyAliasGroup = [&normalized](const QStringList& keys) {
        const QVariant resolved = firstConfiguredValue(normalized, keys);
        if (!resolved.isValid()) {
            return;
        }

        for (const QString& key : keys) {
            normalized.insert(key, resolved);
        }
    };

    applyAliasGroup(maxTotalExposureKeysStorage());
    applyAliasGroup(maxPositionPercentKeysStorage());
    applyAliasGroup(maxDrawdownLimitKeysStorage());
    applyAliasGroup(stopLossPercentKeysStorage());
    applyAliasGroup(takeProfitPercentKeysStorage());
    applyAliasGroup(positionSizingMethodKeysStorage());
    applyAliasGroup(minWeightPercentKeysStorage());
    applyAliasGroup(maxWeightPercentKeysStorage());
    applyAliasGroup(rebalanceDaysKeysStorage());
    applyAliasGroup(commissionRateKeysStorage());
    applyAliasGroup(slippageRateKeysStorage());
    applyAliasGroup(riskFreeRateKeysStorage());
    applyAliasGroup(forwardDaysKeysStorage());
    applyAliasGroup(orderSizeLimitKeysStorage());
    applyAliasGroup(autoStopEnabledKeysStorage());

    if (!normalized.contains(QStringLiteral("maxDailyLoss"))
            && !normalized.contains(QStringLiteral("max_daily_loss"))) {
        setConfiguredValue(normalized, maxDailyLossKeysStorage(), -5.0);
    }

    return normalized;
}

} // namespace

namespace risk::config {

QVariantMap normalizedConfiguration(const QVariantMap& configuration)
{
    return normalizeRiskConfiguration(configuration);
}

const QStringList& forwardDaysKeys()
{
    return forwardDaysKeysStorage();
}

int forwardDays(const QVariantMap& configuration, int fallback)
{
    return resolvedConfiguredValue(configuration, forwardDaysKeysStorage(), fallback).toInt();
}

void setForwardDays(QVariantMap& configuration, int value)
{
    setConfiguredValue(configuration, forwardDaysKeysStorage(), value);
}

const QStringList& rebalanceDaysKeys()
{
    return rebalanceDaysKeysStorage();
}

int rebalanceDays(const QVariantMap& configuration, int fallback)
{
    return resolvedConfiguredValue(configuration, rebalanceDaysKeysStorage(), fallback).toInt();
}

void setRebalanceDays(QVariantMap& configuration, int value)
{
    setConfiguredValue(configuration, rebalanceDaysKeysStorage(), value);
}

void setRebalanceDays(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, rebalanceDaysKeysStorage(), value);
}

const QStringList& commissionRateKeys()
{
    return commissionRateKeysStorage();
}

double commissionRate(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, commissionRateKeysStorage(), fallback).toDouble();
}

void setCommissionRate(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, commissionRateKeysStorage(), value);
}

void setCommissionRate(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, commissionRateKeysStorage(), value);
}

const QStringList& slippageRateKeys()
{
    return slippageRateKeysStorage();
}

double slippageRate(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, slippageRateKeysStorage(), fallback).toDouble();
}

void setSlippageRate(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, slippageRateKeysStorage(), value);
}

void setSlippageRate(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, slippageRateKeysStorage(), value);
}

const QStringList& riskFreeRateKeys()
{
    return riskFreeRateKeysStorage();
}

double riskFreeRate(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, riskFreeRateKeysStorage(), fallback).toDouble();
}

void setRiskFreeRate(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, riskFreeRateKeysStorage(), value);
}

void setRiskFreeRate(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, riskFreeRateKeysStorage(), value);
}

const QStringList& benchmarkSymbolKeys()
{
    return benchmarkSymbolKeysStorage();
}

QString benchmarkSymbol(const QVariantMap& configuration, const QString& fallback)
{
    return resolvedConfiguredValue(configuration, benchmarkSymbolKeysStorage(), fallback).toString().trimmed();
}

void setBenchmarkSymbol(QVariantMap& configuration, const QString& value)
{
    setConfiguredValue(configuration, benchmarkSymbolKeysStorage(), value);
}

const QStringList& stopLossPercentKeys()
{
    return stopLossPercentKeysStorage();
}

double stopLossPercent(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, stopLossPercentKeysStorage(), fallback).toDouble();
}

void setStopLossPercent(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, stopLossPercentKeysStorage(), value);
}

void setStopLossPercent(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, stopLossPercentKeysStorage(), value);
}

const QStringList& takeProfitPercentKeys()
{
    return takeProfitPercentKeysStorage();
}

double takeProfitPercent(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, takeProfitPercentKeysStorage(), fallback).toDouble();
}

void setTakeProfitPercent(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, takeProfitPercentKeysStorage(), value);
}

void setTakeProfitPercent(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, takeProfitPercentKeysStorage(), value);
}

const QStringList& maxDrawdownLimitKeys()
{
    return maxDrawdownLimitKeysStorage();
}

double maxDrawdownLimit(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, maxDrawdownLimitKeysStorage(), fallback).toDouble();
}

void setMaxDrawdownLimit(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, maxDrawdownLimitKeysStorage(), value);
}

void setMaxDrawdownLimit(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, maxDrawdownLimitKeysStorage(), value);
}

const QStringList& maxDailyLossKeys()
{
    return maxDailyLossKeysStorage();
}

double maxDailyLoss(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, maxDailyLossKeysStorage(), fallback).toDouble();
}

void setMaxDailyLoss(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, maxDailyLossKeysStorage(), value);
}

void setMaxDailyLoss(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, maxDailyLossKeysStorage(), value);
}

const QStringList& maxPositionPercentKeys()
{
    return maxPositionPercentKeysStorage();
}

double maxPositionPercent(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, maxPositionPercentKeysStorage(), fallback).toDouble();
}

void setMaxPositionPercent(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, maxPositionPercentKeysStorage(), value);
}

void setMaxPositionPercent(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, maxPositionPercentKeysStorage(), value);
}

const QStringList& maxTotalExposureKeys()
{
    return maxTotalExposureKeysStorage();
}

double maxTotalExposure(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, maxTotalExposureKeysStorage(), fallback).toDouble();
}

void setMaxTotalExposure(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, maxTotalExposureKeysStorage(), value);
}

void setMaxTotalExposure(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, maxTotalExposureKeysStorage(), value);
}

const QStringList& positionSizingMethodKeys()
{
    return positionSizingMethodKeysStorage();
}

QString positionSizingMethod(const QVariantMap& configuration, const QString& fallback)
{
    return resolvedConfiguredValue(configuration, positionSizingMethodKeysStorage(), fallback).toString().trimmed();
}

void setPositionSizingMethod(QVariantMap& configuration, const QString& value)
{
    setConfiguredValue(configuration, positionSizingMethodKeysStorage(), value);
}

const QStringList& minWeightPercentKeys()
{
    return minWeightPercentKeysStorage();
}

double minWeightPercent(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, minWeightPercentKeysStorage(), fallback).toDouble();
}

void setMinWeightPercent(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, minWeightPercentKeysStorage(), value);
}

const QStringList& maxWeightPercentKeys()
{
    return maxWeightPercentKeysStorage();
}

double maxWeightPercent(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, maxWeightPercentKeysStorage(), fallback).toDouble();
}

void setMaxWeightPercent(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, maxWeightPercentKeysStorage(), value);
}

const QStringList& autoStopEnabledKeys()
{
    return autoStopEnabledKeysStorage();
}

bool autoStopEnabled(const QVariantMap& configuration, bool fallback)
{
    return boolFromVariant(resolvedConfiguredValue(configuration, autoStopEnabledKeysStorage(), fallback), fallback);
}

void setAutoStopEnabled(QVariantMap& configuration, bool value)
{
    setConfiguredValue(configuration, autoStopEnabledKeysStorage(), value);
}

void setAutoStopEnabled(std::map<std::string, double>& configuration, bool value)
{
    setConfiguredNumericValues(configuration, autoStopEnabledKeysStorage(), value ? 1.0 : 0.0);
}

void setAutoStopEnabled(std::map<std::string, std::string>& configuration, bool value)
{
    setConfiguredStringValues(configuration,
                              autoStopEnabledKeysStorage(),
                              value ? QStringLiteral("true") : QStringLiteral("false"));
}

const QStringList& orderSizeLimitKeys()
{
    return orderSizeLimitKeysStorage();
}

double orderSizeLimit(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, orderSizeLimitKeysStorage(), fallback).toDouble();
}

void setOrderSizeLimit(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, orderSizeLimitKeysStorage(), value);
}

void setOrderSizeLimit(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, orderSizeLimitKeysStorage(), value);
}

const QStringList& turnoverLimitKeys()
{
    return turnoverLimitKeysStorage();
}

double turnoverLimit(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, turnoverLimitKeysStorage(), fallback).toDouble();
}

void setTurnoverLimit(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, turnoverLimitKeysStorage(), value);
}

void setTurnoverLimit(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, turnoverLimitKeysStorage(), value);
}

const QStringList& slippageLimitKeys()
{
    return slippageLimitKeysStorage();
}

double slippageLimit(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, slippageLimitKeysStorage(), fallback).toDouble();
}

void setSlippageLimit(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, slippageLimitKeysStorage(), value);
}

void setSlippageLimit(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, slippageLimitKeysStorage(), value);
}

const QStringList& level1BreakerKeys()
{
    return level1BreakerKeysStorage();
}

double level1Breaker(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, level1BreakerKeysStorage(), fallback).toDouble();
}

void setLevel1Breaker(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, level1BreakerKeysStorage(), value);
}

void setLevel1Breaker(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, level1BreakerKeysStorage(), value);
}

const QStringList& level2BreakerKeys()
{
    return level2BreakerKeysStorage();
}

double level2Breaker(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, level2BreakerKeysStorage(), fallback).toDouble();
}

void setLevel2Breaker(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, level2BreakerKeysStorage(), value);
}

void setLevel2Breaker(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, level2BreakerKeysStorage(), value);
}

const QStringList& level3BreakerKeys()
{
    return level3BreakerKeysStorage();
}

double level3Breaker(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, level3BreakerKeysStorage(), fallback).toDouble();
}

void setLevel3Breaker(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, level3BreakerKeysStorage(), value);
}

void setLevel3Breaker(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, level3BreakerKeysStorage(), value);
}

const QStringList& metricPersistenceMinAbsIcKeys()
{
    return metricPersistenceMinAbsIcKeysStorage();
}

double metricPersistenceMinAbsIc(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, metricPersistenceMinAbsIcKeysStorage(), fallback).toDouble();
}

void setMetricPersistenceMinAbsIc(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, metricPersistenceMinAbsIcKeysStorage(), value);
}

const QStringList& metricPersistenceMinIrKeys()
{
    return metricPersistenceMinIrKeysStorage();
}

double metricPersistenceMinIr(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, metricPersistenceMinIrKeysStorage(), fallback).toDouble();
}

void setMetricPersistenceMinIr(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, metricPersistenceMinIrKeysStorage(), value);
}

const QStringList& metricPersistenceMinProfitFactorKeys()
{
    return metricPersistenceMinProfitFactorKeysStorage();
}

double metricPersistenceMinProfitFactor(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, metricPersistenceMinProfitFactorKeysStorage(), fallback).toDouble();
}

void setMetricPersistenceMinProfitFactor(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, metricPersistenceMinProfitFactorKeysStorage(), value);
}

const QStringList& varWarningPercentKeys()
{
    return varWarningPercentKeysStorage();
}

double varWarningPercent(const QVariantMap& configuration, double fallback)
{
    return resolvedConfiguredValue(configuration, varWarningPercentKeysStorage(), fallback).toDouble();
}

void setVarWarningPercent(QVariantMap& configuration, double value)
{
    setConfiguredValue(configuration, varWarningPercentKeysStorage(), value);
}

void setVarWarningPercent(std::map<std::string, double>& configuration, double value)
{
    setConfiguredNumericValues(configuration, varWarningPercentKeysStorage(), value);
}

} // namespace risk::config

RiskConfigService* RiskConfigService::m_instance = nullptr;
QMutex RiskConfigService::m_instanceMutex;

RiskConfigService* RiskConfigService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new RiskConfigService();
        m_instance->initialize();
    }
    return m_instance;
}

RiskConfigService::RiskConfigService(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
{
}

void RiskConfigService::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        return;
    }

    loadPersistedState();
    m_initialized = true;
    emit initializedChanged();
}

QVariantMap RiskConfigService::loadCurrentConfiguration()
{
    QMutexLocker locker(&m_mutex);
    if (!m_initialized) {
        loadPersistedState();
        m_initialized = true;
        emit initializedChanged();
    }
    return normalizeRiskConfiguration(m_currentConfiguration);
}

QVariantMap RiskConfigService::loadAppliedConfiguration()
{
    QMutexLocker locker(&m_mutex);
    if (!m_initialized) {
        loadPersistedState();
        m_initialized = true;
        emit initializedChanged();
    }
    return normalizeRiskConfiguration(m_appliedConfiguration);
}

bool RiskConfigService::saveConfiguration(const QVariantMap& configuration)
{
    QVariantMap savedConfiguration;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_initialized) {
            loadPersistedState();
            m_initialized = true;
            emit initializedChanged();
        }

        m_currentConfiguration = normalizeRiskConfiguration(configuration);
        if (!persistState()) {
            emit errorOccurred(QStringLiteral("风险配置保存失败"));
            return false;
        }
        savedConfiguration = m_currentConfiguration;
    }

    emit currentConfigurationChanged();
    emit configurationSaved(savedConfiguration);
    return true;
}

bool RiskConfigService::applyConfiguration(const QVariantMap& configuration)
{
    QVariantMap appliedConfiguration;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_initialized) {
            loadPersistedState();
            m_initialized = true;
            emit initializedChanged();
        }

        m_currentConfiguration = normalizeRiskConfiguration(configuration);
        m_appliedConfiguration = m_currentConfiguration;
        if (!persistState()) {
            emit errorOccurred(QStringLiteral("风险配置应用失败"));
            return false;
        }
        appliedConfiguration = m_appliedConfiguration;
    }

    emit currentConfigurationChanged();
    emit appliedConfigurationChanged();
    emit configurationSaved(appliedConfiguration);
    emit configurationApplied(appliedConfiguration);
    return true;
}

bool RiskConfigService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

QVariantMap RiskConfigService::currentConfiguration() const
{
    QMutexLocker locker(&m_mutex);
    return normalizeRiskConfiguration(m_currentConfiguration);
}

QVariantMap RiskConfigService::appliedConfiguration() const
{
    QMutexLocker locker(&m_mutex);
    return normalizeRiskConfiguration(m_appliedConfiguration);
}

QString RiskConfigService::configFilePath() const
{
    return bridge::storage::riskConfigurationFilePath();
}

void RiskConfigService::loadPersistedState()
{
    m_currentConfiguration.clear();
    m_appliedConfiguration.clear();

    QFile file(configFilePath());
    if (!file.exists()) {
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(QStringLiteral("无法读取风险配置文件"));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        emit errorOccurred(QStringLiteral("风险配置文件格式无效"));
        return;
    }

    const QJsonObject root = document.object();
    m_currentConfiguration = normalizeRiskConfiguration(toVariantMap(root.value(QStringLiteral("currentConfiguration")).toObject()));
    m_appliedConfiguration = normalizeRiskConfiguration(toVariantMap(root.value(QStringLiteral("appliedConfiguration")).toObject()));
}

bool RiskConfigService::persistState()
{
    const QString path = configFilePath();
    QFileInfo fileInfo(path);
    QDir dir = fileInfo.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        emit errorOccurred(QStringLiteral("无法创建风险配置目录"));
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("currentConfiguration"), toJsonObject(m_currentConfiguration));
    root.insert(QStringLiteral("appliedConfiguration"), toJsonObject(m_appliedConfiguration));

    const QJsonDocument document(root);
    const QByteArray payload = document.toJson(QJsonDocument::Indented);

    QString lastError;
    QString lastStage = QStringLiteral("open");
    for (int attempt = 1; attempt <= kPersistRetryCount; ++attempt) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            lastStage = QStringLiteral("open");
            lastError = file.errorString().trimmed();
        } else {
            const qint64 written = file.write(payload);
            if (written != payload.size()) {
                lastStage = QStringLiteral("write");
                lastError = file.errorString().trimmed();
                if (lastError.isEmpty()) {
                    lastError = QStringLiteral("写入长度不完整");
                }
                file.cancelWriting();
            } else if (!file.commit()) {
                lastStage = QStringLiteral("commit");
                lastError = file.errorString().trimmed();
            } else {
                return true;
            }
        }

        if (attempt < kPersistRetryCount) {
            QThread::msleep(kPersistRetryDelayMs * static_cast<unsigned long>(attempt));
        }
    }

    const QString detail = lastError.isEmpty() ? QStringLiteral("未知错误") : lastError;
    if (lastStage == QStringLiteral("open")) {
        emit errorOccurred(QStringLiteral("无法写入风险配置文件: %1").arg(detail));
    } else if (lastStage == QStringLiteral("write")) {
        emit errorOccurred(QStringLiteral("风险配置文件写入失败: %1").arg(detail));
    } else {
        emit errorOccurred(QStringLiteral("风险配置文件提交失败: %1").arg(detail));
    }
    return false;
}