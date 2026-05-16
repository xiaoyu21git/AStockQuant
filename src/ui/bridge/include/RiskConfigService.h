#pragma once

#include <QObject>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <map>
#include <string>

namespace risk::config {

inline constexpr int kDefaultForwardDays = 30;
inline constexpr int kDefaultRebalanceDays = 15;
inline constexpr double kDefaultCommissionRate = 0.001;
inline constexpr double kDefaultSlippageRate = 0.001;
inline constexpr double kDefaultRiskFreeRate = 0.02;
inline constexpr double kDefaultStopLossPercent = 0.10;
inline constexpr double kDefaultTakeProfitPercent = 0.20;
inline constexpr double kDefaultMaxDrawdownLimit = 0.12;
inline constexpr double kDefaultMaxDailyLoss = 0.05;
inline constexpr double kDefaultMaxPositionPercent = 0.15;
inline constexpr double kDefaultMaxTotalExposure = 0.67;

QVariantMap normalizedConfiguration(const QVariantMap& configuration);

const QStringList& forwardDaysKeys();
int forwardDays(const QVariantMap& configuration, int fallback = kDefaultForwardDays);
void setForwardDays(QVariantMap& configuration, int value);

const QStringList& rebalanceDaysKeys();
int rebalanceDays(const QVariantMap& configuration, int fallback = kDefaultRebalanceDays);
void setRebalanceDays(QVariantMap& configuration, int value);
void setRebalanceDays(std::map<std::string, double>& configuration, double value);

const QStringList& commissionRateKeys();
double commissionRate(const QVariantMap& configuration, double fallback = kDefaultCommissionRate);
void setCommissionRate(QVariantMap& configuration, double value);
void setCommissionRate(std::map<std::string, double>& configuration, double value);

const QStringList& slippageRateKeys();
double slippageRate(const QVariantMap& configuration, double fallback = kDefaultSlippageRate);
void setSlippageRate(QVariantMap& configuration, double value);
void setSlippageRate(std::map<std::string, double>& configuration, double value);

const QStringList& riskFreeRateKeys();
double riskFreeRate(const QVariantMap& configuration, double fallback = kDefaultRiskFreeRate);
void setRiskFreeRate(QVariantMap& configuration, double value);
void setRiskFreeRate(std::map<std::string, double>& configuration, double value);

const QStringList& benchmarkSymbolKeys();
QString benchmarkSymbol(const QVariantMap& configuration, const QString& fallback = {});
void setBenchmarkSymbol(QVariantMap& configuration, const QString& value);

const QStringList& stopLossPercentKeys();
double stopLossPercent(const QVariantMap& configuration, double fallback = kDefaultStopLossPercent);
void setStopLossPercent(QVariantMap& configuration, double value);
void setStopLossPercent(std::map<std::string, double>& configuration, double value);

const QStringList& takeProfitPercentKeys();
double takeProfitPercent(const QVariantMap& configuration, double fallback = kDefaultTakeProfitPercent);
void setTakeProfitPercent(QVariantMap& configuration, double value);
void setTakeProfitPercent(std::map<std::string, double>& configuration, double value);

const QStringList& maxDrawdownLimitKeys();
double maxDrawdownLimit(const QVariantMap& configuration, double fallback = kDefaultMaxDrawdownLimit);
void setMaxDrawdownLimit(QVariantMap& configuration, double value);
void setMaxDrawdownLimit(std::map<std::string, double>& configuration, double value);

const QStringList& maxDailyLossKeys();
double maxDailyLoss(const QVariantMap& configuration, double fallback = kDefaultMaxDailyLoss);
void setMaxDailyLoss(QVariantMap& configuration, double value);
void setMaxDailyLoss(std::map<std::string, double>& configuration, double value);

const QStringList& maxPositionPercentKeys();
double maxPositionPercent(const QVariantMap& configuration, double fallback = kDefaultMaxPositionPercent);
void setMaxPositionPercent(QVariantMap& configuration, double value);
void setMaxPositionPercent(std::map<std::string, double>& configuration, double value);

const QStringList& maxTotalExposureKeys();
double maxTotalExposure(const QVariantMap& configuration, double fallback = kDefaultMaxTotalExposure);
void setMaxTotalExposure(QVariantMap& configuration, double value);
void setMaxTotalExposure(std::map<std::string, double>& configuration, double value);

const QStringList& positionSizingMethodKeys();
QString positionSizingMethod(const QVariantMap& configuration, const QString& fallback = {});
void setPositionSizingMethod(QVariantMap& configuration, const QString& value);

const QStringList& minWeightPercentKeys();
double minWeightPercent(const QVariantMap& configuration, double fallback = 0.0);
void setMinWeightPercent(QVariantMap& configuration, double value);

const QStringList& maxWeightPercentKeys();
double maxWeightPercent(const QVariantMap& configuration, double fallback = 0.0);
void setMaxWeightPercent(QVariantMap& configuration, double value);

const QStringList& autoStopEnabledKeys();
bool autoStopEnabled(const QVariantMap& configuration, bool fallback = false);
void setAutoStopEnabled(QVariantMap& configuration, bool value);
void setAutoStopEnabled(std::map<std::string, double>& configuration, bool value);
void setAutoStopEnabled(std::map<std::string, std::string>& configuration, bool value);

const QStringList& orderSizeLimitKeys();
double orderSizeLimit(const QVariantMap& configuration, double fallback = 0.0);
void setOrderSizeLimit(QVariantMap& configuration, double value);
void setOrderSizeLimit(std::map<std::string, double>& configuration, double value);

const QStringList& turnoverLimitKeys();
double turnoverLimit(const QVariantMap& configuration, double fallback = 0.0);
void setTurnoverLimit(QVariantMap& configuration, double value);
void setTurnoverLimit(std::map<std::string, double>& configuration, double value);

const QStringList& slippageLimitKeys();
double slippageLimit(const QVariantMap& configuration, double fallback = 0.0);
void setSlippageLimit(QVariantMap& configuration, double value);
void setSlippageLimit(std::map<std::string, double>& configuration, double value);

const QStringList& level1BreakerKeys();
double level1Breaker(const QVariantMap& configuration, double fallback = 0.0);
void setLevel1Breaker(QVariantMap& configuration, double value);
void setLevel1Breaker(std::map<std::string, double>& configuration, double value);

const QStringList& level2BreakerKeys();
double level2Breaker(const QVariantMap& configuration, double fallback = 0.0);
void setLevel2Breaker(QVariantMap& configuration, double value);
void setLevel2Breaker(std::map<std::string, double>& configuration, double value);

const QStringList& level3BreakerKeys();
double level3Breaker(const QVariantMap& configuration, double fallback = 0.0);
void setLevel3Breaker(QVariantMap& configuration, double value);
void setLevel3Breaker(std::map<std::string, double>& configuration, double value);

const QStringList& metricPersistenceMinAbsIcKeys();
double metricPersistenceMinAbsIc(const QVariantMap& configuration, double fallback = 0.03);
void setMetricPersistenceMinAbsIc(QVariantMap& configuration, double value);

const QStringList& metricPersistenceMinIrKeys();
double metricPersistenceMinIr(const QVariantMap& configuration, double fallback = 0.0);
void setMetricPersistenceMinIr(QVariantMap& configuration, double value);

const QStringList& metricPersistenceMinProfitFactorKeys();
double metricPersistenceMinProfitFactor(const QVariantMap& configuration, double fallback = 1.5);
void setMetricPersistenceMinProfitFactor(QVariantMap& configuration, double value);

const QStringList& varWarningPercentKeys();
double varWarningPercent(const QVariantMap& configuration, double fallback = 0.0);
void setVarWarningPercent(QVariantMap& configuration, double value);
void setVarWarningPercent(std::map<std::string, double>& configuration, double value);

} // namespace risk::config

class RiskConfigService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(QVariantMap currentConfiguration READ currentConfiguration NOTIFY currentConfigurationChanged)
    Q_PROPERTY(QVariantMap appliedConfiguration READ appliedConfiguration NOTIFY appliedConfigurationChanged)

public:
    static RiskConfigService* instance();

    RiskConfigService(const RiskConfigService&) = delete;
    RiskConfigService& operator=(const RiskConfigService&) = delete;

    Q_INVOKABLE void initialize();
    Q_INVOKABLE QVariantMap loadCurrentConfiguration();
    Q_INVOKABLE QVariantMap loadAppliedConfiguration();
    Q_INVOKABLE bool saveConfiguration(const QVariantMap& configuration);
    Q_INVOKABLE bool applyConfiguration(const QVariantMap& configuration);

    bool isInitialized() const;
    QVariantMap currentConfiguration() const;
    QVariantMap appliedConfiguration() const;

signals:
    void initializedChanged();
    void currentConfigurationChanged();
    void appliedConfigurationChanged();
    void configurationSaved(const QVariantMap& configuration);
    void configurationApplied(const QVariantMap& configuration);
    void errorOccurred(const QString& message);

private:
    explicit RiskConfigService(QObject* parent = nullptr);

    QString configFilePath() const;
    void loadPersistedState();
    bool persistState();

    static RiskConfigService* m_instance;
    static QMutex m_instanceMutex;

    mutable QMutex m_mutex;
    bool m_initialized;
    QVariantMap m_currentConfiguration;
    QVariantMap m_appliedConfiguration;
};