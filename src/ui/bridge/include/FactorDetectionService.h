#pragma once

#include <QDate>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#include "DataServiceCache.h"
#include "factor_check/FactorDetectionCoreService.h"
#include "factor_check/FactorSupportScopeCacheCore.h"
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
        QStringList factorIds;
        QString startDate;
        QString endDate;
        QVariantMap cacheSnapshot;
        QString dataSourceMode;
        int selectedDatasetId{-1};
    };

    struct Overrides {
        QHash<QString, int> requiredWarmupTradingDaysOverrideForTests;
        std::function<QString(const QString&)> resolveInstanceIdOverrideForTests;
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
        std::unordered_set<std::string> availableFieldSet;
        std::unordered_set<std::string> unusableFieldSet;
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
    QStringList dedupeFactorIds(const QStringList& factorIds) const;
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
    QVariantMap buildSupportInfo(const factor::bridge::check::SupportInfo& typedInfo) const;
    int uniqueTradeDateCount(const QVariantList& rows) const;
    QSet<QString> collectAvailableFields(const QVariantMap& cacheSnapshot,
                                         const DataServiceCache::DataSetInfo& dataSetInfo,
                                         const QVariantList& rows) const;
    QVariantMap collectFieldDiagnostics(const QVariantMap& cacheSnapshot) const;
    bool fieldHasUsableValues(const QVariantMap& fieldDiagnostics,
                              const QString& field) const;
    QString buildScopeKey(const Request& request) const;
    QString buildDefinitionFingerprint(const factor::FactorInstanceInfo& info) const;
    QString legacyCacheFilePath() const;
    factor::bridge::check::PersistedFactorEntryMap loadScopeEntries(
        const QString& filePath,
        const QString& scopeKey) const;
    void persistScopeEntries(const QString& filePath,
                             const QString& scopeKey,
                             const factor::bridge::check::PersistedFactorEntryMap& scopeEntries) const;
    QString resolveInstanceId(const QString& factorId,
                              const std::function<QString(const QString&)>& resolveOverride) const;
    factor::FactorInstanceInfo resolveInstanceInfo(
        const QString& resolvedInstanceId,
        const std::function<factor::FactorInstanceInfo(const QString&)>& instanceInfoOverride,
        const std::shared_ptr<factor::FactorInstanceManager>& instanceManager) const;
    std::shared_ptr<factor::BaseFactor> resolveFactorInstance(
        const QString& resolvedInstanceId,
        const std::function<std::shared_ptr<factor::BaseFactor>(const QString&)>& factorInstanceOverride,
        const std::shared_ptr<factor::FactorInstanceManager>& instanceManager) const;
    QVariantMap buildRuntimeFailureSupportMap(const QStringList& factorIds,
                                              const QString& reason) const;
    QVariantMap detectSingleFactor(const QString& factorId,
                                   const Request& request,
                                   const RuntimeContext& runtimeContext,
                                   const Overrides& overrides,
                                   const SharedContext& sharedContext) const;
    QVariantMap detectPendingFactors(const QStringList& factorIds,
                                     const Request& request,
                                     const RuntimeContext& runtimeContext,
                                     const Overrides& overrides) const;
};