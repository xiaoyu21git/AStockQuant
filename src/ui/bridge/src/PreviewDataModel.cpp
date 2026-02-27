// PreviewDataModel.cpp
#include "PreviewDataModel.h"
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

PreviewDataModel::PreviewDataModel(QObject* parent)
    : QAbstractListModel(parent) {
    
    // 初始化角色名称 - 注意：这些名称必须与QML中的model.xxx匹配
    // QML中使用model.date, model.code, model.name, model.open等
    m_roleNames[DateRole] = "date";
    m_roleNames[CodeRole] = "code";      // 注意：QML中使用model.code，对应这里的"code"
    m_roleNames[NameRole] = "name";
    m_roleNames[OpenRole] = "open";
    m_roleNames[CloseRole] = "close";
    m_roleNames[HighRole] = "high";
    m_roleNames[LowRole] = "low";
    m_roleNames[ChangeRole] = "change";
    m_roleNames[VolumeRole] = "volume";
    
    qDebug() << "PreviewDataModel: 创建，最大显示条数:" << m_maxDisplayCount;
}

PreviewDataModel::~PreviewDataModel() {
    qDebug() << "PreviewDataModel: 销毁，数据条数:" << m_data.size();
}

PreviewDataModel::PreviewItem::PreviewItem(const QVariantMap& map) {
    date = map.value("date", map.value("Date", "")).toString();
    // 支持多种可能的代码字段名
    code = map.value("symbol", map.value("code", map.value("Code", map.value("stockCode", "")))).toString();
    // 支持多种可能的名称字段名
    name = map.value("name", map.value("Name", map.value("stockName", map.value("股票名称", "")))).toString();
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
    return m_data.size();
}

QVariant PreviewDataModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_data.size()) {
        return QVariant();
    }
    
    const PreviewItem& item = m_data.at(index.row());
    
    switch (role) {
        case DateRole: return item.date;
        case CodeRole: return item.code;
        case NameRole: return item.name;
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
    qDebug() << "PreviewDataModel::updateData: 更新数据，条数:" << data.size();
    
    if (data.size() > m_maxDisplayCount) {
        qDebug() << "  数据量超过最大显示限制(" << m_maxDisplayCount 
                 << ")，仅使用前" << m_maxDisplayCount << "条";
    }
    
    beginResetModel();
    
    m_data.clear();
    int count = 0;
    for (const QVariantMap& map : data) {
        if (count >= m_maxDisplayCount) {
            break;
        }
        m_data.append(PreviewItem(map));
        count++;
    }
    
    endResetModel();
    
    qDebug() << "PreviewDataModel::updateData: 更新完成，当前条数:" << m_data.size();
    emit countChanged();
    emit dataUpdated();
}

void PreviewDataModel::clearData() {
    qDebug() << "PreviewDataModel::clearData: 清空所有数据";
    
    beginResetModel();
    m_data.clear();
    endResetModel();
    
    emit countChanged();
    emit dataUpdated();
}

void PreviewDataModel::appendData(const QVariantMap& item) {
    if (m_data.size() >= m_maxDisplayCount) {
        qDebug() << "PreviewDataModel::appendData: 已达最大显示条数，忽略新数据";
        return;
    }
    
    qDebug() << "PreviewDataModel::appendData: 添加数据项";
    
    beginInsertRows(QModelIndex(), m_data.size(), m_data.size());
    m_data.append(PreviewItem(item));
    endInsertRows();
    
    emit countChanged();
}

void PreviewDataModel::addDataBatch(const QVector<QVariantMap>& data) {
    if (data.isEmpty()) {
        return;
    }
    
    qDebug() << "PreviewDataModel::addDataBatch: 批量添加" << data.size() << "条数据";
    
    int startRow = m_data.size();
    int endRow = startRow + data.size() - 1;
    
    // 检查是否超过最大显示限制
    if (endRow >= m_maxDisplayCount) {
        endRow = m_maxDisplayCount - 1;
        if (startRow > endRow) {
            qDebug() << "PreviewDataModel::addDataBatch: 已达最大显示条数，无法添加";
            return;
        }
    }
    
    beginInsertRows(QModelIndex(), startRow, endRow);
    
    int count = 0;
    for (const QVariantMap& map : data) {
        if (m_data.size() >= m_maxDisplayCount) {
            break;
        }
        m_data.append(PreviewItem(map));
        count++;
    }
    
    endInsertRows();
    
    qDebug() << "PreviewDataModel::addDataBatch: 成功添加" << count << "条数据";
    emit countChanged();
}

void PreviewDataModel::setMaxDisplayCount(int count) {
    if (count <= 0) {
        qWarning() << "PreviewDataModel::setMaxDisplayCount: 无效的显示条数" << count;
        return;
    }
    
    if (m_maxDisplayCount != count) {
        m_maxDisplayCount = count;
        
        // 如果当前数据超过新的限制，需要截断
        if (m_data.size() > m_maxDisplayCount) {
            qDebug() << "PreviewDataModel::setMaxDisplayCount: 数据超过新限制，截断到" 
                     << m_maxDisplayCount << "条";
            
            beginResetModel();
            m_data.resize(m_maxDisplayCount);
            endResetModel();
            
            emit countChanged();
        }
        
        qDebug() << "PreviewDataModel::setMaxDisplayCount: 设置为" << count << "条";
        emit maxDisplayCountChanged();
    }
}

QVariantMap PreviewDataModel::getRow(int index) const {
    if (index < 0 || index >= m_data.size()) {
        qWarning() << "PreviewDataModel::getRow: 索引越界:" << index << "数据大小:" << m_data.size();
        return QVariantMap();
    }
    
    const PreviewItem& item = m_data.at(index);
    QVariantMap map;
    map["date"] = item.date;
    map["symbol"] = item.code;
    map["name"] = item.name;
    map["open"] = item.open;
    map["close"] = item.close;
    map["high"] = item.high;
    map["low"] = item.low;
    map["change"] = item.change;
    map["volume"] = item.volume;
    
    return map;
}
