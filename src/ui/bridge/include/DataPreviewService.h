// DataPreviewService.h - 数据预览服务，负责数据预览和分析
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class DataPreviewService : public QObject {
    Q_OBJECT
    
    // QML属性
    Q_PROPERTY(QVariantList previewData READ previewData NOTIFY previewDataChanged)
    Q_PROPERTY(QVariantMap previewStats READ previewStats NOTIFY previewStatsChanged)
    Q_PROPERTY(QVariantList selectedColumns READ selectedColumns WRITE setSelectedColumns NOTIFY selectedColumnsChanged)
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(int pageSize READ pageSize WRITE setPageSize NOTIFY pageSizeChanged)
    Q_PROPERTY(bool isGeneratingPreview READ isGeneratingPreview NOTIFY isGeneratingPreviewChanged)
    
public:
    explicit DataPreviewService(QObject* parent = nullptr);
    ~DataPreviewService();
    
    // QML可调用的方法
    Q_INVOKABLE void generatePreview(const QVariantList& data, const QString& previewType = "default");
    Q_INVOKABLE void updatePreview(const QVariantList& data, const QVariantMap& options);
    Q_INVOKABLE void applyFilter(const QString& filterExpression);
    Q_INVOKABLE void sortByColumn(const QString& columnName, bool ascending = true);
    Q_INVOKABLE void groupByColumn(const QString& columnName, const QString& aggregationType = "count");
    Q_INVOKABLE void exportPreview(const QString& filePath, const QString& format = "csv");
    Q_INVOKABLE QVariantMap getColumnStatistics(const QString& columnName);
    Q_INVOKABLE QVariantList getDataSlice(int startIndex, int endIndex);
    Q_INVOKABLE void resetPreview();
    Q_INVOKABLE void highlightOutliers(const QVariantMap& options);
    Q_INVOKABLE void visualizeData(const QString& visualizationType, const QVariantMap& options);
    
    // 分页相关方法
    Q_INVOKABLE bool hasNextPage() const;
    Q_INVOKABLE bool hasPreviousPage() const;
    Q_INVOKABLE void nextPage();
    Q_INVOKABLE void previousPage();
    Q_INVOKABLE void goToPage(int pageNumber);
    
    // 属性getter/setter
    QVariantList previewData() const;
    QVariantMap previewStats() const;
    QVariantList selectedColumns() const;
    void setSelectedColumns(const QVariantList& columns);
    int currentPage() const;
    void setCurrentPage(int page);
    int pageSize() const;
    void setPageSize(int size);
    bool isGeneratingPreview() const;
    
signals:
    // 属性变化信号
    void previewDataChanged();
    void previewStatsChanged();
    void selectedColumnsChanged();
    void currentPageChanged();
    void pageSizeChanged();
    void isGeneratingPreviewChanged();
    
    // 操作结果信号
    void previewGenerated(bool success, const QString& message, const QVariantMap& stats);
    void previewUpdated(bool success, const QString& message);
    void filterApplied(bool success, const QString& message, int filteredCount);
    void sorted(bool success, const QString& message);
    void grouped(bool success, const QString& message, const QVariantMap& groupStats);
    void exported(bool success, const QString& message, const QString& filePath);
    void outliersHighlighted(bool success, const QString& message, int outlierCount);
    void visualizationCreated(bool success, const QString& message, const QString& visualizationId);
    
    // 进度信号
    void progress(int progress, const QString& message);
    void error(const QString& errorMessage);
    
private:
    DataPreviewService(const DataPreviewService&) = delete;
    DataPreviewService& operator=(const DataPreviewService&) = delete;
    
    // 私有实现方法
    class Impl;
    std::unique_ptr<Impl> m_impl;
};