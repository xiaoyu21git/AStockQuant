#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <map>
#include <memory>

class DataService : public QObject {
    Q_OBJECT
    friend class DataServiceTestAccess;

    Q_PROPERTY(QVariantList fetchedData READ fetchedData NOTIFY fetchedDataChanged)

public:
    explicit DataService(QObject* parent = nullptr);
    ~DataService() override;

    Q_INVOKABLE void cleanDataAsync(const QVariantList& data,
                                    const QVariantMap& rules);

    Q_INVOKABLE void loadIndexConstituents(const QString& indexSymbol,
                                           const QString& snapshotDate = QString());
    Q_INVOKABLE QVariantList getAvailableIndices();
    Q_INVOKABLE void fetchDataByType(const QString& dataSource,
                                     const QString& symbol,
                                     const QString& dataType,
                                     const QString& startDate,
                                     const QString& endDate,
                                     const QVariantMap& options = QVariantMap());

    QVariantList fetchedData() const;

signals:
    void queryProgress(int progress, const QString& message);
    void queryCompleted(bool success, const QString& message, const QVariantList& data);

    void cleaningProgress(int progress, const QString& message);
    void cleaningProgressDetail(int progress,
                                const QString& message,
                                const QString& currentStock,
                                int keptRecords,
                                int removedRecords);
    void cleaningCompleted(bool success, const QString& message, const QVariantList& cleanedData);

    void error(const QString& errorMessage);
    void fetchedDataChanged();

private:
    DataService(const DataService&) = delete;
    DataService& operator=(const DataService&) = delete;

    bool cleanDataSyncForTests(const QVariantList& data,
                              const QVariantMap& rules,
                              QVariantList* cleanedData,
                              QString* errorMessage);

    QVariantList getIndexConstituents(const QString& indexSymbol,
                                      const QString& snapshotDate = QString());
    QVariantList fetchConstituentKlineData(const QVariantList& constituents,
                                          const QString& dataType,
                                          const QString& startDate,
                                          const QString& endDate,
                                          int progressStart = -1,
                                          int progressSpan = 0,
                                          const QString& progressLabel = QString());
    QVariantList fetchBatchKlineData(const QString& tableName,
                                     const QStringList& fields,
                                     const QStringList& symbols,
                                     const QString& startDate,
                                     const QString& endDate);
    QVariantList fetchPriceTableData(const QString& tableName,
                                     const QString& symbol,
                                     const QString& startDate,
                                     const QString& endDate);
    QVariantList fetchPriceTableDataForSymbols(const QString& tableName,
                                               const QStringList& symbols,
                                               const QString& startDate,
                                          const QString& endDate,
                                          int progressStart = -1,
                                          int progressSpan = 0,
                                          const QString& progressLabel = QString());
    QVariantList fetchAggregatedKlineData(const QString& period,
                                          const QString& symbol,
                                          const QString& startDate,
                                          const QString& endDate);
    QVariantList fetchAggregatedKlineDataForSymbols(const QString& period,
                                                    const QStringList& symbols,
                                                    const QString& startDate,
                                              const QString& endDate,
                                              int progressStart = -1,
                                              int progressSpan = 0,
                                              const QString& progressLabel = QString());
    QVariantList fetchMinuteData(const QString& symbol,
                                 const QString& startDate,
                                 const QString& endDate);
    QVariantList fetchMinuteDataForSymbols(const QStringList& symbols,
                                           const QString& startDate,
                                           const QString& endDate);
    QVariantList fetchKlineData(const QString& symbol,
                                const QString& dataType,
                                const QString& startDate,
                                                                const QString& endDate,
                                                                int progressStart = -1,
                                                                int progressSpan = 0,
                                                                const QString& progressLabel = QString());
    QVariantList fetchAllMarketKlineData(const QString& dataType,
                                         const QString& startDate,
                                                                                 const QString& endDate,
                                                                                 int progressStart = -1,
                                                                                 int progressSpan = 0,
                                                                                 const QString& progressLabel = QString());
    QVariantList fetchFinancialData(const QString& symbol,
                                    const QString& startDate,
                                    const QString& endDate);
    QVariantList fetchFinancialDataForSymbols(const QStringList& symbols,
                                              const QString& startDate,
                                                                                            const QString& endDate,
                                                                                            int progressStart = -1,
                                                                                            int progressSpan = 0,
                                                                                            const QString& progressLabel = QString());
    QVariantList fetchHistoricalData(const QString& symbol,
                                     const QString& startDate,
                                     const QString& endDate);
    QVariantList fetchRealtimeData(const QString& symbol,
                                   const QString& startDate,
                                   const QString& endDate);
    QVariantList fetchRealtimeDataForSymbols(const QStringList& symbols,
                                                                                         const QString& endDate,
                                                                                         int progressStart = -1,
                                                                                         int progressSpan = 0,
                                                                                         const QString& progressLabel = QString());
    QVariantList fetchNewsData(const QString& symbol,
                               const QString& startDate,
                               const QString& endDate);
    QVariantList fetchNewsDataForSymbols(const QStringList& symbols,
                                                                                 const QString& startDate,
                                                                                 const QString& endDate,
                                                                                 int progressStart = -1,
                                                                                 int progressSpan = 0,
                                                                                 const QString& progressLabel = QString());
    QVariantList fetchPolicyData(const QString& symbol,
                                 const QString& startDate,
                                 const QString& endDate,
                                 const QVariantMap& options = QVariantMap());
    QVariantList fetchPolicyDataForSymbols(const QStringList& symbols,
                                           const QString& startDate,
                                           const QString& endDate,
                                                                                     const QVariantMap& options = QVariantMap(),
                                                                                     int progressStart = -1,
                                                                                     int progressSpan = 0,
                                                                                     const QString& progressLabel = QString());
    QVariantList fetchAlternativeData(const QString& symbol,
                                      const QString& startDate,
                                      const QString& endDate,
                                      const QVariantMap& options = QVariantMap());
    QVariantList fetchAlternativeDataForSymbols(const QStringList& symbols,
                                                const QString& startDate,
                                                const QString& endDate,
                                                                                                const QVariantMap& options = QVariantMap(),
                                                                                                int progressStart = -1,
                                                                                                int progressSpan = 0,
                                                                                                const QString& progressLabel = QString());
    QVariantList fetchDerivativesData(const QString& symbol,
                                      const QString& startDate,
                                      const QString& endDate,
                                      const QVariantMap& options = QVariantMap());
    QVariantList fetchDerivativesDataForSymbols(const QStringList& symbols,
                                                const QString& startDate,
                                                const QString& endDate,
                                                                                                const QVariantMap& options = QVariantMap(),
                                                                                                int progressStart = -1,
                                                                                                int progressSpan = 0,
                                                                                                const QString& progressLabel = QString());
    QVariantList fetchGenericTimeSeriesData(const QString& tableName,
                                            const QStringList& symbols,
                                            const QString& startDate,
                                            const QString& endDate,
                                            const QStringList& dateColumns,
                                            const QStringList& symbolColumns,
                                                                                        int progressStart = -1,
                                                                                        int progressSpan = 0,
                                                                                        const QString& progressLabel = QString());
    bool checkDatabaseConnectionForFetch() const;
    QVariantList executeVariantQueryForFetch(const QString& sql,
                                            const std::map<QString, QVariant>& params) const;
    QString resolveNewsTable() const;

    class Impl;
    std::unique_ptr<Impl> m_impl;
    QVariantList m_fetchedData;
    std::function<bool()> m_checkDatabaseConnectionOverrideForTests;
    std::function<QVariantList(const QString&, const std::map<QString, QVariant>&)> m_executeVariantQueryOverrideForTests;
};