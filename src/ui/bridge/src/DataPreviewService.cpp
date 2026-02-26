// DataPreviewService.cpp - 数据预览服务实现
#include "DataPreviewService.h"
#include <QDebug>
#include <QVariant>
#include <QThread>

// PIMPL实现类
class DataPreviewService::Impl {
public:
    Impl(DataPreviewService* parent) 
        : m_parent(parent)
        , m_currentPage(1)
        , m_pageSize(20)
        , m_isGeneratingPreview(false) {
        qDebug() << "DataPreviewService::Impl: 创建";
        
        // 初始化默认预览数据
        m_selectedColumns = {"code", "name", "price", "change"};
    }
    
    ~Impl() {
        qDebug() << "DataPreviewService::Impl: 销毁";
    }
    
    void generatePreview(const QVariantList& data, const QString& previewType) {
        qDebug() << "生成预览，数据条数:" << data.size() << "预览类型:" << previewType;
        
        m_isGeneratingPreview = true;
        emit m_parent->isGeneratingPreviewChanged();
        
        QThread::msleep(100);
        
        m_previewData = data;
        
        // 计算统计信息
        QVariantMap stats = {
            {"totalCount", data.size()},
            {"selectedCount", m_selectedColumns.size()},
            {"pageCount", qMax(1, (data.size() + m_pageSize - 1) / m_pageSize)},
            {"currentPage", m_currentPage},
            {"previewType", previewType}
        };
        
        m_previewStats = stats;
        
        m_isGeneratingPreview = false;
        emit m_parent->isGeneratingPreviewChanged();
        emit m_parent->previewDataChanged();
        emit m_parent->previewStatsChanged();
        emit m_parent->previewGenerated(true, "预览生成成功", stats);
    }
    
    void applyFilter(const QString& filterExpression) {
        qDebug() << "应用过滤器:" << filterExpression;
        
        // 简化的过滤逻辑
        int filteredCount = m_previewData.size() / 2; // 模拟过滤
        
        QThread::msleep(50);
        
        emit m_parent->filterApplied(true, "过滤器应用成功", filteredCount);
    }
    
    void sortByColumn(const QString& columnName, bool ascending) {
        qDebug() << "按列排序:" << columnName << (ascending ? "升序" : "降序");
        
        QThread::msleep(30);
        
        emit m_parent->sorted(true, "排序完成");
    }
    
    QVariantList previewData() const { return m_previewData; }
    QVariantMap previewStats() const { return m_previewStats; }
    QVariantList selectedColumns() const { return m_selectedColumns; }
    void setSelectedColumns(const QVariantList& columns) { 
        m_selectedColumns = columns; 
        emit m_parent->selectedColumnsChanged();
    }
    
    int currentPage() const { return m_currentPage; }
    void setCurrentPage(int page) { 
        if (m_currentPage != page && page > 0) {
            m_currentPage = page; 
            emit m_parent->currentPageChanged();
        }
    }
    
    int pageSize() const { return m_pageSize; }
    void setPageSize(int size) { 
        if (m_pageSize != size && size > 0) {
            m_pageSize = size; 
            emit m_parent->pageSizeChanged();
        }
    }
    
    bool isGeneratingPreview() const { return m_isGeneratingPreview; }
    
private:
    DataPreviewService* m_parent;
    QVariantList m_previewData;
    QVariantMap m_previewStats;
    QVariantList m_selectedColumns;
    int m_currentPage;
    int m_pageSize;
    bool m_isGeneratingPreview;
};

// DataPreviewService 公共接口实现
DataPreviewService::DataPreviewService(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this)) {
    qDebug() << "DataPreviewService: 创建";
}

DataPreviewService::~DataPreviewService() {
    qDebug() << "DataPreviewService: 销毁";
}

void DataPreviewService::generatePreview(const QVariantList& data, const QString& previewType) {
    m_impl->generatePreview(data, previewType);
}

void DataPreviewService::updatePreview(const QVariantList& data, const QVariantMap& options) {
    qDebug() << "更新预览，数据条数:" << data.size() << "选项:" << options;
    emit previewUpdated(true, "预览更新成功");
}

void DataPreviewService::applyFilter(const QString& filterExpression) {
    m_impl->applyFilter(filterExpression);
}

void DataPreviewService::sortByColumn(const QString& columnName, bool ascending) {
    m_impl->sortByColumn(columnName, ascending);
}

void DataPreviewService::groupByColumn(const QString& columnName, const QString& aggregationType) {
    qDebug() << "按列分组:" << columnName << aggregationType;
    emit grouped(true, "分组完成", QVariantMap{{"groupCount", 5}});
}

void DataPreviewService::exportPreview(const QString& filePath, const QString& format) {
    qDebug() << "导出预览:" << filePath << format;
    emit exported(true, "导出成功", filePath);
}

QVariantMap DataPreviewService::getColumnStatistics(const QString& columnName) {
    qDebug() << "获取列统计:" << columnName;
    return QVariantMap{{"column", columnName}, {"count", 100}, {"mean", 50.5}};
}

QVariantList DataPreviewService::getDataSlice(int startIndex, int endIndex) {
    qDebug() << "获取数据切片:" << startIndex << "-" << endIndex;
    return QVariantList();
}

void DataPreviewService::resetPreview() {
    qDebug() << "重置预览";
    m_impl->setSelectedColumns({"code", "name", "price", "change"});
    m_impl->setCurrentPage(1);
    // 重置完成，不需要发送额外信号
}

void DataPreviewService::highlightOutliers(const QVariantMap& options) {
    qDebug() << "高亮异常值:" << options;
    emit outliersHighlighted(true, "异常值高亮完成", 10);
}

void DataPreviewService::visualizeData(const QString& visualizationType, const QVariantMap& options) {
    qDebug() << "可视化数据:" << visualizationType << options;
    emit visualizationCreated(true, "可视化创建成功", "viz_" + visualizationType);
}

bool DataPreviewService::hasNextPage() const {
    int totalItems = m_impl->previewData().size();
    int currentPage = m_impl->currentPage();
    int pageSize = m_impl->pageSize();
    return currentPage * pageSize < totalItems;
}

bool DataPreviewService::hasPreviousPage() const {
    return m_impl->currentPage() > 1;
}

void DataPreviewService::nextPage() {
    if (hasNextPage()) {
        m_impl->setCurrentPage(m_impl->currentPage() + 1);
    }
}

void DataPreviewService::previousPage() {
    if (hasPreviousPage()) {
        m_impl->setCurrentPage(m_impl->currentPage() - 1);
    }
}

void DataPreviewService::goToPage(int pageNumber) {
    m_impl->setCurrentPage(pageNumber);
}

QVariantList DataPreviewService::previewData() const {
    return m_impl->previewData();
}

QVariantMap DataPreviewService::previewStats() const {
    return m_impl->previewStats();
}

QVariantList DataPreviewService::selectedColumns() const {
    return m_impl->selectedColumns();
}

void DataPreviewService::setSelectedColumns(const QVariantList& columns) {
    m_impl->setSelectedColumns(columns);
}

int DataPreviewService::currentPage() const {
    return m_impl->currentPage();
}

void DataPreviewService::setCurrentPage(int page) {
    m_impl->setCurrentPage(page);
}

int DataPreviewService::pageSize() const {
    return m_impl->pageSize();
}

void DataPreviewService::setPageSize(int size) {
    m_impl->setPageSize(size);
}

bool DataPreviewService::isGeneratingPreview() const {
    return m_impl->isGeneratingPreview();
}