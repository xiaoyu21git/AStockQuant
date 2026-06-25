// PreviewDataModel.cpp
#include "PreviewDataModel.h"
#include "foundation/log/logging.hpp"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

namespace {

QString normalizePreviewCategory(const QString& source)
{
    return source.trimmed();
}

QString previewDataTypeGroup(const QString& dataType)
{
    // 每种 dataType 保留独立分类，不再合并为粗粒度的 "kline" / "financial" / "other"
    // 这样 QML 面板会为 kline_daily、kline_weekly、financial 等各自生成独立 Tab
    const QString normalized = dataType.trimmed().toLower();
    if (normalized.isEmpty()) {
        return QStringLiteral("other");
    }
    return normalized;
}

QString previewTypeDisplayName(const QString& category)
{
    const QString normalized = category.trimmed().toLower();
    if (normalized == QStringLiteral("kline")) {
        return QStringLiteral("K线");
    }
    if (normalized == QStringLiteral("financial")) {
        return QStringLiteral("财务");
    }
    if (normalized == QStringLiteral("kline_daily") || normalized.contains(QStringLiteral("日线"))) {
        return QStringLiteral("日线");
    }
    if (normalized == QStringLiteral("kline_weekly") || normalized.contains(QStringLiteral("周线"))) {
        return QStringLiteral("周线");
    }
    if (normalized == QStringLiteral("kline_monthly") || normalized.contains(QStringLiteral("月线"))) {
        return QStringLiteral("月线");
    }
    if (normalized == QStringLiteral("minute_data") || normalized.contains(QStringLiteral("分钟"))) {
        return QStringLiteral("分钟");
    }
    if (normalized == QStringLiteral("realtime") || normalized.contains(QStringLiteral("实时"))) {
        return QStringLiteral("实时");
    }
    if (normalized == QStringLiteral("historical") || normalized.contains(QStringLiteral("历史"))) {
        return QStringLiteral("历史");
    }
    if (normalized == QStringLiteral("news") || normalized.contains(QStringLiteral("舆情"))) {
        return QStringLiteral("舆情");
    }
    if (normalized == QStringLiteral("policy") || normalized.contains(QStringLiteral("政策"))) {
        return QStringLiteral("政策");
    }
    if (normalized == QStringLiteral("alternative") || normalized.contains(QStringLiteral("另类"))) {
        return QStringLiteral("另类");
    }
    if (normalized == QStringLiteral("derivatives") || normalized.contains(QStringLiteral("衍生品"))) {
        return QStringLiteral("衍生品");
    }
    if (normalized == QStringLiteral("index_constituents") || normalized.contains(QStringLiteral("指数成分"))) {
        return QStringLiteral("指数成分");
    }
    if (normalized == QStringLiteral("index_list") || normalized.contains(QStringLiteral("指数列表"))) {
        return QStringLiteral("指数列表");
    }
    return QStringLiteral("其他");
}

bool isDailyFamilyPreviewCategory(const QString& category)
{
    const QString normalized = category.trimmed().toLower();
    if (normalized.isEmpty()) {
        return false;
    }

    // 日线族：所有 K 线 + 历史 + 分钟 + 实时类型，共享日线族列布局
    return normalized.startsWith(QStringLiteral("kline"))
        || normalized == QStringLiteral("historical")
        || normalized == QStringLiteral("minute_data")
        || normalized == QStringLiteral("realtime")
        || normalized.contains(QStringLiteral("日线"))
        || normalized.contains(QStringLiteral("周线"))
        || normalized.contains(QStringLiteral("月线"))
        || normalized.contains(QStringLiteral("分钟"))
        || normalized.contains(QStringLiteral("实时"))
        || normalized.contains(QStringLiteral("历史"));
}

QString preferredPreviewCategory(const QVector<PreviewDataModel::PreviewItem>& items)
{
    QString firstCategory;
    for (const PreviewDataModel::PreviewItem& item : items) {
        const QString category = item.dataType.trimmed().isEmpty()
            ? item.source.trimmed()
            : item.dataType.trimmed();
        if (category.isEmpty()) {
            continue;
        }

        if (firstCategory.isEmpty()) {
            firstCategory = category;
        }

        if (!isDailyFamilyPreviewCategory(category)) {
            return category;
        }
    }

    return firstCategory;
}

}

PreviewDataModel::PreviewDataModel(QObject* parent)
    : QAbstractListModel(parent) {
    
    // 初始化角色名称 - 注意：这些名称必须与QML中的model.xxx匹配
    // QML中使用model.date, model.code, model.name, model.open等
    m_roleNames[DateRole] = "date";
    m_roleNames[CodeRole] = "code";      // 注意：QML中使用model.code，对应这里的"code"
    m_roleNames[NameRole] = "name";
    m_roleNames[TimeRangeRole] = "timeRange";
    m_roleNames[RecordCountRole] = "recordCount";
    m_roleNames[SourceRole] = "source";
    m_roleNames[DataTypeRole] = "dataType";
    m_roleNames[OpenRole] = "open";
    m_roleNames[CloseRole] = "close";
    m_roleNames[HighRole] = "high";
    m_roleNames[LowRole] = "low";
    m_roleNames[ChangeRole] = "change";
    m_roleNames[VolumeRole] = "volume";
    
    INTERNAL_DEBUG_STREAM << "PreviewDataModel: 创建，最大显示条数:" << m_maxDisplayCount;
}

PreviewDataModel::~PreviewDataModel() {
    INTERNAL_DEBUG_STREAM << "PreviewDataModel: 销毁，数据条数:" << m_allData.size();
}

QString PreviewDataModel::previewItemCategoryKey(const PreviewItem& item)
{
    if (!item.dataType.trimmed().isEmpty()) {
        return item.dataType.trimmed();
    }
    return item.source.trimmed();
}

bool PreviewDataModel::previewItemMatchesCategory(const PreviewItem& item, const QString& category)
{
    const QString normalizedCategory = normalizePreviewCategory(category);
    if (normalizedCategory.isEmpty()) {
        return false;
    }

    const QString itemCategory = previewItemCategoryKey(item);
    if (itemCategory == normalizedCategory) {
        return true;
    }

    return previewDataTypeGroup(itemCategory) == normalizedCategory;
}

PreviewDataModel::PreviewItem::PreviewItem(const QVariantMap& map) {
    date = map.value("date", map.value("Date", "")).toString();
    // 支持多种可能的代码字段名
    code = map.value("symbol", map.value("code", map.value("Code", map.value("stockCode", "")))).toString();
    // 支持多种可能的名称字段名
    name = map.value("name", map.value("Name", map.value("stockName", map.value("股票名称", "")))).toString();
    timeRange = map.value("timeRange", map.value("time_range", date)).toString();
    source = map.value("source", map.value("dataSource", map.value("type", ""))).toString();
    dataType = map.value("dataType", map.value("data_type", map.value("dataSourceType", ""))).toString();
    recordCount = map.value("recordCount", map.value("records", 0)).toInt();
    open = map.value("open", map.value("Open", 0.0)).toDouble();
    close = map.value("close", map.value("Close", map.value("price", 0.0))).toDouble();
    high = map.value("high", map.value("High", 0.0)).toDouble();
    low = map.value("low", map.value("Low", 0.0)).toDouble();
    change = map.value("change", map.value("Change", map.value("changePercent", 0.0))).toDouble();
    volume = map.value("volume", map.value("Volume", 0.0)).toDouble();
    
    // 如果没有涨跌幅，计算涨跌幅
    if (change == 0.0 && close > 0.0 && open > 0.0) {
        change = ((close - open) / open) * 100.0;
    }
}

int PreviewDataModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    const int totalCountForCategory = countForCategory(m_currentCategory);
    const int startIndex = (m_currentPage - 1) * m_pageSize;
    if (startIndex < 0 || startIndex >= totalCountForCategory) {
        return 0;
    }
    return qMin(m_pageSize, totalCountForCategory - startIndex);
}

QVariant PreviewDataModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0) {
        return QVariant();
    }
    
    const int actualIndex = actualIndexForVisibleRow(index.row());
    if (actualIndex < 0 || actualIndex >= m_allData.size()) {
        return QVariant();
    }

    const PreviewItem& item = m_allData.at(actualIndex);
    
    switch (role) {
        case DateRole: return item.date;
        case CodeRole: return item.code;
        case NameRole: return item.name;
        case TimeRangeRole: return item.timeRange;
        case RecordCountRole: return item.recordCount;
        case SourceRole: return item.source;
        case DataTypeRole: return item.dataType;
        case OpenRole: return item.open;
        case CloseRole: return item.close;
        case HighRole: return item.high;
        case LowRole: return item.low;
        case ChangeRole: return item.change;
        case VolumeRole: return item.volume;
        default: return QVariant();
    }
}

QHash<int, QByteArray> PreviewDataModel::roleNames() const {
    return m_roleNames;
}

void PreviewDataModel::updateData(const QVector<QVariantMap>& data) {
    //  更新数据，条数:" << data.size();
    
    if (data.size() > m_maxDisplayCount) {
        INTERNAL_DEBUG_STREAM << "  数据量超过最大显示限制(" << m_maxDisplayCount 
                 << ")，仅使用前" << m_maxDisplayCount << "条";
    }
    
    beginResetModel();
    
    m_allData.clear();
    int count = 0;
    for (const QVariantMap& map : data) {
        if (count >= m_maxDisplayCount) {
            break;
        }
        m_allData.append(PreviewItem(map));
        count++;
    }

    if (!m_allData.isEmpty()) {
        const QString preferredCategory = preferredPreviewCategory(m_allData);
        if (!preferredCategory.isEmpty()
            && (m_currentCategory.isEmpty()
                || isDailyFamilyPreviewCategory(m_currentCategory)
                || countForCategory(m_currentCategory) == 0)) {
            m_currentCategory = preferredCategory;
        }
    } else {
        m_currentCategory.clear();
    }

    m_currentPage = 1;
    
    endResetModel();
    
    //  更新完成，当前条数:" << m_allData.size();
    emit countChanged();
    emit totalCountChanged();
    emit currentCategoryChanged();
    emit categoryCountsChanged();
    emit currentPageChanged();
    emit paginationChanged();
    emit dataUpdated();
}

void PreviewDataModel::clearData() {
    //  清空所有数据";
    
    beginResetModel();
    m_allData.clear();
    m_currentPage = 1;
    m_currentCategory.clear();
    endResetModel();
    
    emit countChanged();
    emit totalCountChanged();
    emit currentCategoryChanged();
    emit categoryCountsChanged();
    emit currentPageChanged();
    emit paginationChanged();
    emit dataUpdated();
}

void PreviewDataModel::appendData(const QVariantMap& item) {
    if (m_allData.size() >= m_maxDisplayCount) {
        INTERNAL_DEBUG_STREAM << "PreviewDataModel::appendData: 已达最大显示条数，忽略新数据";
        return;
    }
    
    INTERNAL_DEBUG_STREAM << "PreviewDataModel::appendData: 添加数据项";
    
    beginResetModel();
    m_allData.append(PreviewItem(item));
    const QString preferredCategory = preferredPreviewCategory(m_allData);
    if (!preferredCategory.isEmpty()
        && (m_currentCategory.isEmpty()
            || isDailyFamilyPreviewCategory(m_currentCategory)
            || countForCategory(m_currentCategory) == 0)) {
        m_currentCategory = preferredCategory;
    }
    endResetModel();

    emit countChanged();
    emit totalCountChanged();
    emit currentCategoryChanged();
    emit categoryCountsChanged();
    emit paginationChanged();
}

void PreviewDataModel::addDataBatch(const QVector<QVariantMap>& data) {
    if (data.isEmpty()) {
        return;
    }
    
    INTERNAL_DEBUG_STREAM << "PreviewDataModel::addDataBatch: 批量添加" << data.size() << "条数据";
    
    if (m_allData.size() >= m_maxDisplayCount) {
        INTERNAL_DEBUG_STREAM << "PreviewDataModel::addDataBatch: 已达最大显示条数，无法添加";
        return;
    }

    beginResetModel();

    int count = 0;
    for (const QVariantMap& map : data) {
        if (m_allData.size() >= m_maxDisplayCount) {
            break;
        }
        m_allData.append(PreviewItem(map));
        count++;
    }

    const QString preferredCategory = preferredPreviewCategory(m_allData);
    if (!preferredCategory.isEmpty()
        && (m_currentCategory.isEmpty()
            || isDailyFamilyPreviewCategory(m_currentCategory)
            || countForCategory(m_currentCategory) == 0)) {
        m_currentCategory = preferredCategory;
    }
    
    endResetModel();
    
    INTERNAL_DEBUG_STREAM << "PreviewDataModel::addDataBatch: 成功添加" << count << "条数据";
    emit countChanged();
    emit totalCountChanged();
    emit currentCategoryChanged();
    emit categoryCountsChanged();
    emit paginationChanged();
}

void PreviewDataModel::nextPage()
{
    setCurrentPage(m_currentPage + 1);
}

void PreviewDataModel::previousPage()
{
    setCurrentPage(m_currentPage - 1);
}

void PreviewDataModel::firstPage()
{
    setCurrentPage(1);
}

void PreviewDataModel::lastPage()
{
    setCurrentPage(totalPages());
}

int PreviewDataModel::totalCount() const
{
    return countForCategory(m_currentCategory);
}

int PreviewDataModel::categoryCount(const QString& category) const
{
    return countForCategory(category);
}

int PreviewDataModel::totalPages() const
{
    if (m_pageSize <= 0 || totalCount() <= 0) {
        return 1;
    }
    return qMax(1, (totalCount() + m_pageSize - 1) / m_pageSize);
}

QString PreviewDataModel::pageSummary() const
{
    if (totalCount() <= 0) {
        return QStringLiteral("第 0 / 0 页，共 0 条");
    }
    return QStringLiteral("%1，第 %2 / %3 页，共 %4 条")
        .arg(previewTypeDisplayName(m_currentCategory))
        .arg(m_currentPage)
        .arg(totalPages())
        .arg(totalCount());
}

void PreviewDataModel::setCurrentCategory(const QString& category)
{
    const QString normalized = normalizePreviewCategory(category);
    if (m_currentCategory == normalized) {
        return;
    }

    beginResetModel();
    m_currentCategory = normalized;
    m_currentPage = 1;
    endResetModel();

    emit countChanged();
    emit totalCountChanged();
    emit currentCategoryChanged();
    emit currentPageChanged();
    emit paginationChanged();
}

void PreviewDataModel::setCurrentPage(int page)
{
    const int maxPage = totalPages();
    const int nextPage = qBound(1, page, maxPage);
    if (m_currentPage == nextPage) {
        return;
    }

    beginResetModel();
    m_currentPage = nextPage;
    endResetModel();

    emit countChanged();
    emit currentPageChanged();
    emit paginationChanged();
}

int PreviewDataModel::klineCount() const
{
    // 聚合所有 K 线族类型（kline_daily / kline_weekly / kline_monthly / historical / minute_data / realtime）
    int total = 0;
    for (const auto& item : m_allData) {
        if (isDailyFamilyPreviewCategory(item.dataType)) {
            ++total;
        }
    }
    return total;
}

int PreviewDataModel::financialCount() const
{
    int total = 0;
    for (const auto& item : m_allData) {
        const QString dt = item.dataType.trimmed().toLower();
        if (dt == QStringLiteral("financial")) {
            ++total;
        }
    }
    return total;
}

int PreviewDataModel::otherCount() const
{
    int total = 0;
    for (const auto& item : m_allData) {
        const QString dt = item.dataType.trimmed().toLower();
        if (dt.isEmpty()) {
            ++total;
            continue;
        }
        if (!isDailyFamilyPreviewCategory(dt) && dt != QStringLiteral("financial")) {
            ++total;
        }
    }
    return total;
}

void PreviewDataModel::setPageSize(int size)
{
    if (size <= 0) {
        INTERNAL_WARN_STREAM << "PreviewDataModel::setPageSize: 无效的页大小" << size;
        return;
    }

    if (m_pageSize == size) {
        return;
    }

    beginResetModel();
    m_pageSize = size;
    m_currentPage = qBound(1, m_currentPage, totalPages());
    endResetModel();

    emit countChanged();
    emit pageSizeChanged();
    emit paginationChanged();
}

void PreviewDataModel::setMaxDisplayCount(int count) {
    if (count <= 0) {
        INTERNAL_WARN_STREAM << "PreviewDataModel::setMaxDisplayCount: 无效的显示条数" << count;
        return;
    }
    
    if (m_maxDisplayCount != count) {
        m_maxDisplayCount = count;
        
        // 如果当前数据超过新的限制，需要截断
        if (m_allData.size() > m_maxDisplayCount) {
            INTERNAL_DEBUG_STREAM << "PreviewDataModel::setMaxDisplayCount: 数据超过新限制，截断到" 
                     << m_maxDisplayCount << "条";
            
            beginResetModel();
            m_allData.resize(m_maxDisplayCount);
            endResetModel();
            
            emit countChanged();
        }
        
        INTERNAL_DEBUG_STREAM << "PreviewDataModel::setMaxDisplayCount: 设置为" << count << "条";
        emit maxDisplayCountChanged();
    }
}

QVariantMap PreviewDataModel::getRow(int index) const {
    if (index < 0 || index >= rowCount()) {
        INTERNAL_WARN_STREAM << "PreviewDataModel::getRow: 索引越界:" << index << "数据大小:" << rowCount();
        return QVariantMap();
    }
    
    const int actualIndex = (m_currentPage - 1) * m_pageSize + index;
    const PreviewItem& item = m_allData.at(actualIndex);
    QVariantMap map;
    map["date"] = item.date;
    map["symbol"] = item.code;
    map["name"] = item.name;
    map["timeRange"] = item.timeRange;
    map["recordCount"] = item.recordCount;
    map["source"] = item.source;
    map["dataType"] = item.dataType;
    map["open"] = item.open;
    map["close"] = item.close;
    map["high"] = item.high;
    map["low"] = item.low;
    map["change"] = item.change;
    map["volume"] = item.volume;
    
    return map;
}

int PreviewDataModel::countForCategory(const QString& category) const
{
    const QString normalized = normalizePreviewCategory(category);
    int count = 0;
    for (const PreviewItem& item : m_allData) {
        if (previewItemMatchesCategory(item, normalized)) {
            ++count;
        }
    }
    return count;
}

int PreviewDataModel::actualIndexForVisibleRow(int visibleRow) const
{
    if (visibleRow < 0) {
        return -1;
    }

    const QString normalized = normalizePreviewCategory(m_currentCategory);
    int visibleIndex = 0;
    int currentPageRow = 0;
    const int pageStart = (m_currentPage - 1) * m_pageSize;
    for (int i = 0; i < m_allData.size(); ++i) {
        if (!previewItemMatchesCategory(m_allData.at(i), normalized)) {
            continue;
        }

        if (visibleIndex >= pageStart && currentPageRow == visibleRow) {
            return i;
        }

        if (visibleIndex >= pageStart) {
            ++currentPageRow;
        }
        ++visibleIndex;
    }

    return -1;
}

