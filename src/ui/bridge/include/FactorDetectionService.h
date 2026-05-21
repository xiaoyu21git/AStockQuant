#pragma once

#include <QDate>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <mutex>

#include "DataServiceCache.h"
#include "../../../domain/factor/include/FactorInstanceManager.h"

namespace astock {
namespace database {
class QtMySQLDatabase;
}
}

namespace factor {
class BaseFactor;
class DataAvailabilityChecker;
}

class FactorDetectionService final
{
public:
    struct RuntimeContext {
        std::shared_ptr<astock::database::QtMySQLDatabase> database;
        std::shared_ptr<factor::DataAvailabilityChecker> dataChecker;
        std::shared_ptr<factor::FactorInstanceManager> instanceManager;
        QString errorMessage;
    };

    struct Request {
        QVariantList factorIds;
        QString startDate;
        QString endDate;
        QVariantMap cacheSnapshot;
        QString dataSourceMode;
        int selectedDatasetId{-1};
    };

    struct Overrides {
        QHash<QString, int> requiredWarmupTradingDaysOverrideForTests;
        std::function<QString(const QVariant&)> resolveInstanceIdOverrideForTests;
        std::function<factor::FactorInstanceInfo(const QString&)> instanceInfoOverrideForTests;
        std::function<std::shared_ptr<factor::BaseFactor>(const QString&)> factorInstanceOverrideForTests;
        std::function<QString()> cacheFilePathOverrideForTests;
    };

    struct DetectionResult {
        QVariantMap supportMap;
        QString scopeKey;
    };

    RuntimeContext resolveRuntimeContext(
        const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
        const std::shared_ptr<factor::DataAvailabilityChecker>& dataChecker,
        const std::shared_ptr<factor::FactorInstanceManager>& instanceManager,
        bool skipInstanceRefreshForTests) const;

    DetectionResult buildSupportMap(const Request& request,
                                    const RuntimeContext& runtimeContext,
                                    const Overrides& overrides) const;

    QString persistentCacheFilePath(const QString& dataSourceMode,
                                    int selectedDatasetId) const;

private:
    struct SharedContext {
        QSet<QString> availableFields;
        QVariantMap fieldDiagnostics;
        int availableTradeDateCount{0};
        bool useCacheMode{false};
        bool hasValidDataSet{false};
        bool hasUsableDataSetRows{false};
        DataServiceCache::DataSetInfo dataSetInfo;
    };

    mutable QHash<QString, std::shared_ptr<factor::BaseFactor>> m_factorInstanceCache;
    mutable QHash<QString, factor::FactorInstanceInfo> m_instanceInfoCache;
    mutable std::mutex m_factorInstanceCacheMutex;
    mutable std::mutex m_instanceInfoCacheMutex;

    QString normalizedDataSourceMode(const QString& dataSourceMode) const;
    QVariantList dedupeFactorIds(const QVariantList& factorIds) const;
    QStringList dedupeStringList(const QStringList& values) const;
    QVariantList toVariantList(const QStringList& values) const;
    QDate parseSupportDate(const QVariant& value) const;
    factor::FactorType resolveRuntimeType(const factor::FactorInstanceInfo& info,
                                          const std::shared_ptr<factor::BaseFactor>& factorInstance) const;
    bool configHasCustomExpression(const factor::FactorInstanceInfo& info) const;
    bool configNeutralizationEnabled(const factor::FactorInstanceInfo& info) const;
    QStringList declaredRequiredFieldsFromConfig(const factor::FactorInstanceInfo& info) const;
    QStringList normalizedRequiredFields(factor::FactorType runtimeType,
                                         const factor::DataRequirements& requirements) const;
    QVariantMap buildSupportInfo(const QString& factorId,
                                 const QString& instanceId,
                                 factor::FactorType runtimeType,
                                 const QString& category,
                                 const QString& reason,
                                 const QStringList& requiredFields,
                                 const QStringList& missingFields,
                                 factor::SourceTable sourceTable,
                                 bool supported) const;
    int uniqueTradeDateCount(const QVariantList& rows) const;
    QSet<QString> collectAvailableFields(const QVariantMap& cacheSnapshot,
                                         const DataServiceCache::DataSetInfo& dataSetInfo,
                                         const QVariantList& rows) const;
    QVariantMap collectFieldDiagnostics(const QVariantMap& cacheSnapshot) const;
    bool fieldHasUsableValues(const QVariantMap& fieldDiagnostics,
                              const QString& field) const;
    QVariantMap normalizedSupportCacheSnapshot(const QVariantMap& cacheSnapshot) const;
    QString buildScopeKey(const Request& request) const;
    QString buildDefinitionFingerprint(const factor::FactorInstanceInfo& info) const;
    QString legacyCacheFilePath() const;
    QVariantMap loadCacheRoot(const QString& filePath) const;
    bool persistCacheRoot(const QString& filePath, const QVariantMap& root) const;
    QVariantMap loadScopeEntries(const QString& filePath, const QString& scopeKey) const;
    void persistScopeEntries(const QString& filePath,
                             const QString& scopeKey,
                             const QVariantMap& scopeEntries) const;
    QString resolveInstanceId(const QVariant& factorId,
                              const std::function<QString(const QVariant&)>& resolveOverride) const;
    factor::FactorInstanceInfo resolveInstanceInfo(
        const QString& resolvedInstanceId,
        const std::function<factor::FactorInstanceInfo(const QString&)>& instanceInfoOverride,
        const std::shared_ptr<factor::FactorInstanceManager>& instanceManager) const;
    std::shared_ptr<factor::BaseFactor> resolveFactorInstance(
        const QString& resolvedInstanceId,
        const std::function<std::shared_ptr<factor::BaseFactor>(const QString&)>& factorInstanceOverride,
        const std::shared_ptr<factor::FactorInstanceManager>& instanceManager) const;
    QVariantMap buildRuntimeFailureSupportMap(const QVariantList& factorIds,
                                              const QString& reason) const;
    QVariantMap detectSingleFactor(const QVariant& factorIdValue,
                                   const Request& request,
                                   const RuntimeContext& runtimeContext,
                                   const Overrides& overrides,
                                   const SharedContext& sharedContext) const;
    QVariantMap detectPendingFactors(const QVariantList& factorIds,
                                     const Request& request,
                                     const RuntimeContext& runtimeContext,
                                     const Overrides& overrides) const;
};