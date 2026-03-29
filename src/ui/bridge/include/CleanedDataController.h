#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>

// 前向声明（DataServiceCache在全局命名空间中）
class DataServiceCache;

namespace ui::bridge {

/**
 * @brief 清洗后数据控制器 - QML可用的接口
 * 
 * 提供QML友好的接口，让因子回测页面可以访问清洗后的缓存数据
 * 遵循不在QML内部操作数据的原则，所有数据处理都在C++端完成
 */
class CleanedDataController : public QObject
{
    Q_OBJECT
    
    // QML属性
    Q_PROPERTY(bool isAvailable READ isAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(int availableDatasets READ availableDatasets NOTIFY datasetsChanged)
    Q_PROPERTY(QString currentSymbol READ currentSymbol WRITE setCurrentSymbol NOTIFY symbolChanged)
    Q_PROPERTY(QString currentStartDate READ currentStartDate WRITE setCurrentStartDate NOTIFY startDateChanged)
    Q_PROPERTY(QString currentEndDate READ currentEndDate WRITE setCurrentEndDate NOTIFY endDateChanged)
    Q_PROPERTY(QVariantList datasetList READ datasetList NOTIFY datasetListChanged)
    Q_PROPERTY(QVariantMap selectedDatasetInfo READ selectedDatasetInfo NOTIFY selectedDatasetChanged)
    Q_PROPERTY(QVariantMap selectedDatasetFieldDiagnostics READ selectedDatasetFieldDiagnostics NOTIFY selectedDatasetDiagnosticsChanged)
    
public:
    explicit CleanedDataController(QObject* parent = nullptr);
    ~CleanedDataController();
    
    // QML可调用的方法
    Q_INVOKABLE bool initialize();
    Q_INVOKABLE void refreshDatasets();
    Q_INVOKABLE void loadCleanedData(const QString& symbol, 
                                    const QString& startDate, 
                                    const QString& endDate);
    Q_INVOKABLE void loadDatasetById(int datasetId);
    Q_INVOKABLE void searchDatasets(const QString& symbol = "",
                                   const QString& startDate = "",
                                   const QString& endDate = "",
                                   const QString& cleaningRule = "",
                                   double minDataQuality = 0.0);
    Q_INVOKABLE void clearSelection();
    
    // 获取数据库中清洗后数据的日期范围
    Q_INVOKABLE QVariantMap getDataDateRange();
    
    // 属性访问器
    bool isAvailable() const { return m_initialized; }
    bool isLoading() const { return m_loading; }
    int availableDatasets() const { return m_datasetList.size(); }
    QString currentSymbol() const { return m_currentSymbol; }
    void setCurrentSymbol(const QString& symbol);
    QString currentStartDate() const { return m_currentStartDate; }
    void setCurrentStartDate(const QString& date);
    QString currentEndDate() const { return m_currentEndDate; }
    void setCurrentEndDate(const QString& date);
    QVariantList datasetList() const { return m_datasetList; }
    QVariantMap selectedDatasetInfo() const { return m_selectedDatasetInfo; }
    QVariantMap selectedDatasetFieldDiagnostics() const { return m_selectedDatasetFieldDiagnostics; }
    
signals:
    void availabilityChanged(bool available);
    void loadingChanged(bool loading);
    void datasetsChanged(int count);
    void symbolChanged(const QString& symbol);
    void startDateChanged(const QString& date);
    void endDateChanged(const QString& date);
    void datasetListChanged();
    void selectedDatasetChanged();
    void selectedDatasetDiagnosticsChanged();
    
    // 操作结果信号
    void dataLoaded(const QVariantList& data, const QVariantMap& datasetInfo);
    void datasetsFound(const QVariantList& datasets);
    void errorOccurred(const QString& error);
    void initializationCompleted(bool success);
    
private:
    // 内部方法
    void updateLoadingState(bool loading);
    void updateDatasetList(const QVariantList& datasets);
    void updateSelectedDataset(int datasetId);
    QVariantMap buildFieldDiagnostics(const QVariantList& data, const QVariantMap& datasetInfo) const;
    void emitDataLoaded(const QVariantList& data);
    
    // 数据成员
    bool m_initialized{false};
    bool m_loading{false};
    QString m_currentSymbol;
    QString m_currentStartDate;
    QString m_currentEndDate;
    QVariantList m_datasetList;
    QVariantMap m_selectedDatasetInfo;
    QVariantMap m_selectedDatasetFieldDiagnostics;
    int m_currentDatasetId{-1};
    
    // 缓存实例（使用全局命名空间中的DataServiceCache）
    ::DataServiceCache* m_cache{nullptr};
};

} // namespace ui::bridge
