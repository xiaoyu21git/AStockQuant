// CleaningResultModel.cpp
#include "CleaningResultModel.h"
#include <QDebug>

CleaningResultModel::CleaningResultModel(QObject* parent)
    : QAbstractListModel(parent) {
    // 初始化角色名
    m_roleNames[DateRole] = "date";
    m_roleNames[CodeRole] = "code";
    m_roleNames[NameRole] = "name";
    m_roleNames[CloseRole] = "close";
    m_roleNames[ChangeRole] = "change";
    m_roleNames[VolumeRole] = "volume";
    
    qDebug() << "CleaningResultModel: Created with" << m_maxDisplayCount << "max display count";
}

CleaningResultModel::~CleaningResultModel() {
    qDebug() << "CleaningResultModel: Destroyed";
}

CleaningResultModel::CleaningResult::CleaningResult(const QVariantMap& map) {
    date = map.value("date").toString();
    code = map.value("code").toString();
    name = map.value("name").toString();
    close = map.value("close").toDouble();
    change = map.value("change").toDouble();
    volume = map.value("volume").toDouble();
}

int CleaningResultModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return m_results.size();
}

QVariant CleaningResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size()) {
        return QVariant();
    }
    
    const CleaningResult& result = m_results.at(index.row());
    
    switch (role) {
    case DateRole:
        return result.date;
    case CodeRole:
        return result.code;
    case NameRole:
        return result.name;
    case CloseRole:
        return result.close;
    case ChangeRole:
        return result.change;
    case VolumeRole:
        return result.volume;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> CleaningResultModel::roleNames() const {
    return m_roleNames;
}

void CleaningResultModel::updateResults(const QVector<QVariantMap>& results) {
    qDebug() << "CleaningResultModel::updateResults: Updating with" << results.size() << "items";
    
    beginResetModel();
    
    m_results.clear();
    
    // 只取前m_maxDisplayCount条数据显示
    int displayCount = qMin(results.size(), m_maxDisplayCount);
    for (int i = 0; i < displayCount; ++i) {
        m_results.append(CleaningResult(results.at(i)));
    }
    
    endResetModel();
    
    emit countChanged();
    emit resultsUpdated();
    
    qDebug() << "CleaningResultModel::updateResults: Updated, now has" << m_results.size() << "items";
}

void CleaningResultModel::clearResults() {
    qDebug() << "CleaningResultModel::clearResults: Clearing all results";
    
    if (m_results.isEmpty()) {
        return;
    }
    
    beginResetModel();
    m_results.clear();
    endResetModel();
    
    emit countChanged();
    emit resultsUpdated();
}

void CleaningResultModel::appendResult(const QVariantMap& result) {
    if (m_results.size() >= m_maxDisplayCount) {
        qDebug() << "CleaningResultModel::appendResult: Max display count reached, ignoring";
        return;
    }
    
    qDebug() << "CleaningResultModel::appendResult: Appending result";
    
    beginInsertRows(QModelIndex(), m_results.size(), m_results.size());
    m_results.append(CleaningResult(result));
    endInsertRows();
    
    emit countChanged();
}

void CleaningResultModel::setMaxDisplayCount(int count) {
    if (count <= 0) {
        qWarning() << "CleaningResultModel::setMaxDisplayCount: Invalid count" << count;
        return;
    }
    
    if (m_maxDisplayCount != count) {
        m_maxDisplayCount = count;
        
        // 如果当前显示的数据超过新的最大数量，需要截断
        if (m_results.size() > m_maxDisplayCount) {
            beginResetModel();
            m_results.resize(m_maxDisplayCount);
            endResetModel();
            emit countChanged();
        }
        
        emit maxDisplayCountChanged();
        qDebug() << "CleaningResultModel::setMaxDisplayCount: Set to" << count;
    }
}