#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class DataService : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList fetchedData READ fetchedData NOTIFY fetchedDataChanged)

public:
    explicit DataService(QObject* parent = nullptr);
    ~DataService() override;

    Q_INVOKABLE void queryData(const QString& symbol,
                               const QString& startDate,
                               const QString& endDate);
    Q_INVOKABLE void cleanData(const QVariantList& data,
                               const QVariantMap& rules);
    Q_INVOKABLE void queryAndCleanData(const QString& symbol,
                                       const QString& startDate,
                                       const QString& endDate,
                                       const QVariantMap& rules);
    Q_INVOKABLE void loadFromDatabase(const QString& symbol,
                                      const QString& startDate,
                                      const QString& endDate);
    Q_INVOKABLE void cleanDataAsync(const QVariantList& data,
                                    const QVariantMap& rules);

    Q_INVOKABLE void loadIndexConstituents(const QString& indexSymbol);
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
    void cleaningCompleted(bool success, const QString& message, const QVariantList& cleanedData);

    void error(const QString& errorMessage);
    void dataLoadedFromDatabase(bool success, const QString& message, int count);
    void fetchedDataChanged();

private:
    DataService(const DataService&) = delete;
    DataService& operator=(const DataService&) = delete;

    QVariantList getIndexConstituents(const QString& indexSymbol);
    QVariantList fetchConstituentKlineData(const QVariantList& constituents,
                                          const QString& dataType,
                                          const QString& startDate,
                                          const QString& endDate);
    QVariantList fetchBatchKlineData(const QString& tableName,
                                     const QStringList& fields,
                                     const QStringList& symbols,
                                     const QString& startDate,
                                     const QString& endDate);
    QVariantList fetchKlineData(const QString& symbol,
                                const QString& dataType,
                                const QString& startDate,
                                const QString& endDate);
    QVariantList fetchAllMarketKlineData(const QString& dataType,
                                         const QString& startDate,
                                         const QString& endDate);
    QVariantList fetchFinancialData(const QString& symbol,
                                    const QString& startDate,
                                    const QString& endDate);
    QVariantList fetchNewsData(const QString& symbol,
                               const QString& startDate,
                               const QString& endDate);

    class Impl;
    std::unique_ptr<Impl> m_impl;
    QVariantList m_fetchedData;
};