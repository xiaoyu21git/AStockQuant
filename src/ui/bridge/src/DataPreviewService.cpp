// DataPreviewService.cpp - 数据预览服务实现
#include "DataPreviewService.h"
//#include "GlobalModels.h"
#include "DataServiceCache.h"
#include "PreviewDataModel.h"
#include <QDebug>
#include <QVariant>
#include <QThread>
#include <QDate>
#include <QSet>

// PIMPL实现类
class DataPreviewService::Impl {
public:
    Impl(DataPreviewService* parent) 
        : m_parent(parent)
        , m_currentPage(1)
        , m_pageSize(20)
        , m_isGeneratingPreview(false)
        , m_previewModel(nullptr) {
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
    
    void loadLastCleaningResult() {
        qDebug() << "DataPreviewService::Impl: 从缓存加载最后一次清洗结果";
        
        m_isGeneratingPreview = true;
        emit m_parent->isGeneratingPreviewChanged();
        
        QThread::msleep(50);
        
        // 获取DataServiceCache实例
        DataServiceCache& cache = DataServiceCache::getInstance();
        
        // 检查缓存是否已初始化
        bool cacheInitialized = cache.isCacheEnabled();
        qDebug() << "DataPreviewService::Impl: 缓存是否已初始化:" << cacheInitialized;
        
        if (!cacheInitialized) {
            qDebug() << "DataPreviewService::Impl: 缓存未初始化，尝试初始化...";
            bool initSuccess = cache.initializeCache();
            qDebug() << "DataPreviewService::Impl: 缓存初始化结果:" << initSuccess;
        }
        
        QVariantList cachedData;
        bool cacheHit = false;
        
        // 使用ID-based接口查询清洗结果数据集
        // 构建查询条件：查找包含"last_cleaning_result"标签的数据集
        DataServiceCache::DataSetQuery query;
        query.tags = QStringList{"last_cleaning_result"};
        
        auto dataSetInfos = cache.queryDataSets(query);
        qDebug() << "DataPreviewService::Impl: 查询到" << dataSetInfos.size() << "个清洗结果数据集";
        
        if (!dataSetInfos.isEmpty()) {
            // 按创建时间排序，获取最新的数据集
            std::sort(dataSetInfos.begin(), dataSetInfos.end(), 
                [](const DataServiceCache::DataSetInfo& a, const DataServiceCache::DataSetInfo& b) {
                    return a.createdTime > b.createdTime;
                });
            
            int latestDataId = dataSetInfos.first().id;
            qDebug() << "DataPreviewService::Impl: 加载最新清洗结果数据集，ID:" << latestDataId 
                     << "，名称:" << dataSetInfos.first().displayName;
            
            cachedData = cache.getDataSetById(latestDataId);
            cacheHit = !cachedData.isEmpty();
            
            if (cacheHit) {
                qDebug() << "DataPreviewService::Impl: 缓存命中，加载" << cachedData.size() << "条数据";
                m_previewData = cachedData;
                
                // 如果设置了预览模型，直接更新模型数据
                if (m_previewModel) {
                    qDebug() << "DataPreviewService::Impl: 更新预览模型数据";
                    // 转换为QVector<QVariantMap>格式
                    QVector<QVariantMap> dataVector;
                    dataVector.reserve(cachedData.size());
                    for (const auto& item : cachedData) {
                        dataVector.append(item.toMap());
                    }
                    m_previewModel->updateData(dataVector);
                }
            } else {
                qDebug() << "DataPreviewService::Impl: 数据集ID存在但数据为空";
            }
        } else {
            qDebug() << "DataPreviewService::Impl: 未找到清洗结果数据集，尝试使用传统字符串键作为后备";
            
            // 向后兼容：尝试使用传统字符串键
            QString cacheKey = "last_cleaning_result";
            qDebug() << "DataPreviewService::Impl: 尝试从缓存键获取数据:" << cacheKey;
            cachedData = cache.getData(cacheKey);
            cacheHit = !cachedData.isEmpty();
            
            if (cacheHit) {
                qDebug() << "DataPreviewService::Impl: 传统缓存键命中，加载" << cachedData.size() << "条数据";
                m_previewData = cachedData;
                
                // 如果设置了预览模型，直接更新模型数据
                if (m_previewModel) {
                    qDebug() << "DataPreviewService::Impl: 更新预览模型数据";
                    QVector<QVariantMap> dataVector;
                    dataVector.reserve(cachedData.size());
                    for (const auto& item : cachedData) {
                        dataVector.append(item.toMap());
                    }
                    m_previewModel->updateData(dataVector);
                }
            } else {
                qDebug() << "DataPreviewService::Impl: 缓存未命中，无可用清洗结果";
                m_previewData = QVariantList();
                
                // 如果设置了预览模型，清空模型数据
                if (m_previewModel) {
                    m_previewModel->clearData();
                }
            }
        }
        
        // 计算统计信息
        QVariantMap stats = {
            {"totalCount", m_previewData.size()},
            {"selectedCount", m_selectedColumns.size()},
            {"pageCount", qMax(1, (m_previewData.size() + m_pageSize - 1) / m_pageSize)},
            {"currentPage", m_currentPage},
            {"previewType", "cached"},
            {"cached", cacheHit},
            {"cacheInitialized", cacheInitialized},
            {"usingIdBasedInterface", !dataSetInfos.isEmpty()} // 标记是否使用了ID-based接口
        };
        
        m_previewStats = stats;
        
        m_isGeneratingPreview = false;
        emit m_parent->isGeneratingPreviewChanged();
        emit m_parent->previewDataChanged();
        emit m_parent->previewStatsChanged();
        emit m_parent->previewGenerated(true, cacheHit ? "从缓存加载完成" : "无缓存数据", stats);
    }
    
    QVariantList getCachedPreviewData() {
        // 实现从缓存获取预览数据
        DataServiceCache& cache = DataServiceCache::getInstance();
        
        // 使用ID-based接口查询清洗结果数据集
        DataServiceCache::DataSetQuery query;
        query.tags = QStringList{"last_cleaning_result"};
        
        auto dataSetInfos = cache.queryDataSets(query);
        qDebug() << "DataPreviewService::Impl::getCachedPreviewData: 查询到" << dataSetInfos.size() << "个清洗结果数据集";
        
        if (!dataSetInfos.isEmpty()) {
            // 按创建时间排序，获取最新的数据集
            std::sort(dataSetInfos.begin(), dataSetInfos.end(), 
                [](const DataServiceCache::DataSetInfo& a, const DataServiceCache::DataSetInfo& b) {
                    return a.createdTime > b.createdTime;
                });
            
            int latestDataId = dataSetInfos.first().id;
            qDebug() << "DataPreviewService::Impl::getCachedPreviewData: 加载最新清洗结果数据集，ID:" << latestDataId;
            
            QVariantList cachedData = cache.getDataSetById(latestDataId);
            
            qDebug() << "DataPreviewService::Impl::getCachedPreviewData: 缓存" 
                     << (cachedData.isEmpty() ? "未命中" : "命中，" + QString::number(cachedData.size()) + "条数据");
            
            return cachedData;
        } else {
            qDebug() << "DataPreviewService::Impl::getCachedPreviewData: 未找到清洗结果数据集，尝试传统字符串键";
            
            // 向后兼容：尝试使用传统字符串键
            QString cacheKey = "last_cleaning_result";
            QVariantList cachedData = cache.getData(cacheKey);
            
            qDebug() << "DataPreviewService::Impl::getCachedPreviewData: 传统缓存键" 
                     << (cachedData.isEmpty() ? "未命中" : "命中，" + QString::number(cachedData.size()) + "条数据");
            
            return cachedData;
        }
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
    
    // previewModel属性访问器
    PreviewDataModel* previewModel() const { return m_previewModel; }
    void setPreviewModel(PreviewDataModel* model) {
        if (m_previewModel != model) {
            m_previewModel = model;
            emit m_parent->previewModelChanged();
        }
    }

    // dataSetInfos属性访问器
    QVariantList dataSetInfos() const {
        // 直接从DataServiceCache获取
        DataServiceCache& cache = DataServiceCache::getInstance();
        auto dataSetInfos = cache.getAllDataSetInfos();
        
        // 转换为QVariantList
        QVariantList result;
        for (const auto& info : dataSetInfos) {
            QVariantMap map;
            map["id"] = info.id;
            map["displayName"] = info.displayName;
            map["description"] = info.description;
            map["sourceType"] = info.sourceType;
            map["createdTime"] = info.createdTime;
            map["rowCount"] = info.rowCount;
            map["stockCodes"] = info.stockCodes;
            map["startDate"] = info.startDate.isValid() ? info.startDate.toString("yyyy-MM-dd") : "";
            map["endDate"] = info.endDate.isValid() ? info.endDate.toString("yyyy-MM-dd") : "";
            map["tags"] = info.tags;
            result.append(map);
        }
        
        qDebug() << "DataPreviewService::Impl::dataSetInfos: 返回" << result.size() << "个数据集";
        return result;
    }

    // 新添加的方法实现
    int calculateTimeSpanFromModel() {
        if (!m_previewModel || m_previewModel->count() == 0) {
            return 0;
        }
        
        QSet<QDate> uniqueDates;
        for (int i = 0; i < m_previewModel->count(); i++) {
            QVariantMap rowData = m_previewModel->getRow(i);
            if (rowData.contains("date")) {
                QDate date = QDate::fromString(rowData["date"].toString(), "yyyy-MM-dd");
                if (date.isValid()) {
                    uniqueDates.insert(date);
                }
            }
        }
        
        return uniqueDates.size();
    }
    
    int calculateStockCountFromModel() {
        // if (!m_previewModel || m_previewModel->count() == 0) {
        //     return 0;
        // }
        
        // // QSet<QString> uniqueStocks;
        // // for (int i = 0; i < m_previewModel->count(); i++) {
        // //     QVariantMap rowData = m_previewModel->getRow(i);
        // //     if (rowData.contains("Code")) {
        // //         uniqueStocks.insert(rowData["Code"].toString());
        // //     }
        // // }
        
        return m_previewModel->count();
    }
    
    double calculateAvgChangeFromModel() {
        if (!m_previewModel || m_previewModel->count() == 0) {
            return 0.0;
        }
        
        double totalChange = 0.0;
        int count = 0;
        for (int i = 0; i < m_previewModel->count(); i++) {
            QVariantMap rowData = m_previewModel->getRow(i);
            if (rowData.contains("change")) {
                bool ok;
                double change = rowData["change"].toDouble(&ok);
                if (ok) {
                    totalChange += change;
                    count++;
                }
            }
        }
        
        return count > 0 ? totalChange / count : 0.0;
    }
    
    void refreshDataSetList() {
        qDebug() << "DataPreviewService::Impl::refreshDataSetList: 刷新数据集列表";
        // 触发dataSetInfosChanged信号，让QML自动更新
        emit m_parent->dataSetInfosChanged();
    }

private:
    DataPreviewService* m_parent;
    QVariantList m_previewData;
    QVariantMap m_previewStats;
    QVariantList m_selectedColumns;
    int m_currentPage;
    int m_pageSize;
    bool m_isGeneratingPreview;
    PreviewDataModel* m_previewModel;
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

void DataPreviewService::loadLastCleaningResult() {
    m_impl->loadLastCleaningResult();
}

QVariantList DataPreviewService::getCachedPreviewData() {
    return m_impl->getCachedPreviewData();
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

PreviewDataModel* DataPreviewService::previewModel() const {
    return m_impl->previewModel();
}

void DataPreviewService::setPreviewModel(PreviewDataModel* model) {
    m_impl->setPreviewModel(model);
}

QVariantList DataPreviewService::dataSetInfos() const {
    return m_impl->dataSetInfos();
}

QStringList DataPreviewService::getAllCacheKeys() {
    // 获取DataServiceCache实例
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 尝试获取所有数据集键
    QStringList cacheKeys = cache.getAllDataKeys();
    
    qDebug() << "DataPreviewService::getAllCacheKeys: 找到" << cacheKeys.size() << "个数据集键";
    
    // 确保包含常用的预览键
    if (!cacheKeys.contains("last_cleaning_result")) {
        // 如果cache中有数据但键名不是"last_cleaning_result"，我们可能需要调整
        // 这里先返回找到的键
    }
    
    return cacheKeys;
}

// 新接口实现：获取所有数据集信息
QVariantList DataPreviewService::getAllDataSetInfos() {
    // 获取DataServiceCache实例
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 获取所有数据集信息
    auto dataSetInfos = cache.getAllDataSetInfos();
    
    qDebug() << "DataPreviewService::getAllDataSetInfos: 找到" << dataSetInfos.size() << "个数据集";
    
    // 转换为QVariantList
    QVariantList result;
    for (const auto& info : dataSetInfos) {
        QVariantMap map;
        map["id"] = info.id;
        map["displayName"] = info.displayName;
        map["description"] = info.description;
        map["sourceType"] = info.sourceType;
        map["createdTime"] = info.createdTime;
        map["rowCount"] = info.rowCount;
        map["stockCodes"] = info.stockCodes;
        map["startDate"] = info.startDate.isValid() ? info.startDate.toString("yyyy-MM-dd") : "";
        map["endDate"] = info.endDate.isValid() ? info.endDate.toString("yyyy-MM-dd") : "";
        map["tags"] = info.tags;
        result.append(map);
    }
    
    return result;
}

// 新接口实现：通过ID加载数据集
void DataPreviewService::loadDataSetById(int dataId) {
    qDebug() << "DataPreviewService::loadDataSetById: 加载数据集，ID:" << dataId;
    
    // 获取DataServiceCache实例
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 通过ID获取数据
    QVariantList cachedData = cache.getDataSetById(dataId);
    
    if (cachedData.isEmpty()) {
        qDebug() << "DataPreviewService::loadDataSetById: 缓存未命中，ID:" << dataId;
        emit error(QString("未找到缓存数据，ID: %1").arg(dataId));
        return;
    }
    
    qDebug() << "DataPreviewService::loadDataSetById: 缓存命中，加载"
             << cachedData.size() << "条数据";
    
    // 获取数据集信息以便显示名称
    DataServiceCache::DataSetInfo info = cache.getDataSetInfo(dataId);
    QString previewType = QString("dataset_%1_%2").arg(dataId).arg(info.displayName);
    
    // 使用现有的generatePreview方法显示数据
    generatePreview(cachedData, previewType);
}

// 新接口实现：按名称查找ID
int DataPreviewService::findDataSetIdByName(const QString& displayName) {
    // 获取DataServiceCache实例
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    int dataId = cache.findDataSetId(displayName);
    qDebug() << "DataPreviewService::findDataSetIdByName: 名称:" << displayName << "=> ID:" << dataId;
    
    return dataId;
}

void DataPreviewService::loadCachedDataset(const QString& cacheKey) {
    qDebug() << "DataPreviewService::loadCachedDataset: 加载缓存数据集，键:" << cacheKey;

    // 获取DataServiceCache实例
    DataServiceCache& cache = DataServiceCache::getInstance();

    QVariantList cachedData = cache.getData(cacheKey);

    if (cachedData.isEmpty()) {
        qDebug() << "DataPreviewService::loadCachedDataset: 缓存未命中，键:" << cacheKey;
        emit error(QString("未找到缓存数据: %1").arg(cacheKey));
        return;
    }

    qDebug() << "DataPreviewService::loadCachedDataset: 缓存命中，加载"
             << cachedData.size() << "条数据";

    // 使用现有的generatePreview方法显示数据
    generatePreview(cachedData, QString("cached_%1").arg(cacheKey));
}

// 新增的统计方法实现
int DataPreviewService::calculateTimeSpanFromModel() {
    return m_impl->calculateTimeSpanFromModel();
}

int DataPreviewService::calculateStockCountFromModel() {
    return m_impl->calculateStockCountFromModel();
}

double DataPreviewService::calculateAvgChangeFromModel() {
    return m_impl->calculateAvgChangeFromModel();
}

void DataPreviewService::refreshDataSetList() {
    m_impl->refreshDataSetList();
}