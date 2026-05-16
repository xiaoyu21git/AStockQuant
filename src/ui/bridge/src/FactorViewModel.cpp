// FactorViewModel.cpp
// 因子视图模型实现 - 只负责视图更新
// 设计模式：像PreviewDataModel一样，通过Q_INVOKABLE方法更新数据

#include "../../ui/bridge/include/FactorViewModel.h"
#include "../../ui/bridge/include/FactorService.h"
#include <QDebug>
#include <QDateTime>
#include <QRegularExpression>
#include <QMetaObject>
#include <QMetaMethod>

FactorViewModel::FactorViewModel(QObject* parent)
    : QAbstractListModel(parent)
{
    qDebug() << "FactorViewModel constructor";
}

FactorViewModel::~FactorViewModel()
{
    qDebug() << "FactorViewModel destructor";
}

int FactorViewModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return m_factors.size();
}

QVariant FactorViewModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_factors.size() || index.row() < 0) {
        return QVariant();
    }
    
    const FactorViewData& factor = m_factors.at(index.row());
    
    switch (role) {
        case FactorIdRole:
            return factor.factorId;
        case FactorNameRole:
            return factor.factorName;
        case FactorTypeRole:
            return factor.factorType;
        case DisplayNameRole:
            return factor.displayName;
        case MajorCategoryRole:
            return factor.majorCategory;
        case SubCategoryRole:
            return factor.subCategory;
        case DescriptionRole:
            return factor.description;
        case IcValueRole:
            return factor.icValue;
        case IrValueRole:
            return factor.irValue;
        case ValidityDaysRole:
            return factor.validityDays;
        case TurnoverRateRole:
            return factor.turnoverRate;
        case IsRecommendedRole:
            return factor.isRecommended;
        case IsFavoriteRole:
            return factor.isFavorite;
        case StatusRole:
            return factor.status;
        case TagsRole:
            return factor.tags;
        case CreatorRole:
            return factor.creator;
        case CreateDateRole:
            return factor.createDate;
        case GroupReturnsRole:
            return QVariant::fromValue(factor.groupReturns);
        case Qt::DisplayRole:
            return factor.displayName;
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> FactorViewModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[FactorIdRole] = "factorId";
    roles[FactorNameRole] = "factorName";
    roles[FactorTypeRole] = "factorType";
    roles[DisplayNameRole] = "displayName";
    roles[MajorCategoryRole] = "majorCategory";
    roles[SubCategoryRole] = "subCategory";
    roles[DescriptionRole] = "description";
    roles[IcValueRole] = "icValue";
    roles[IrValueRole] = "irValue";
    roles[ValidityDaysRole] = "validityDays";
    roles[TurnoverRateRole] = "turnoverRate";
    roles[IsRecommendedRole] = "isRecommended";
    roles[IsFavoriteRole] = "isFavorite";
    roles[StatusRole] = "status";
    roles[TagsRole] = "tags";
    roles[CreatorRole] = "creator";
    roles[CreateDateRole] = "createDate";
    roles[GroupReturnsRole] = "groupReturns";
    return roles;
}

void FactorViewModel::updateData(const QVariantList& factors)
{
   // qDebug() << "FactorViewModel::updateData: 更新数据，条数:" << factors.size();

    if (hasSameData(factors)) {
        return;
    }
    
    beginResetModel();
    
    m_factors.clear();
    
    for (const QVariant& factorVariant : factors) {
        QVariantMap factorMap = factorVariant.toMap();
        FactorViewData factor = FactorViewData::fromVariantMap(factorMap);
        m_factors.append(factor);
    }
    
    endResetModel();
    
    //qDebug() << "FactorViewModel::updateData: 更新完成，当前条数:" << m_factors.size();
    emit countChanged();
    emit dataUpdated();
}

void FactorViewModel::clearData()
{
    //qDebug() << "FactorViewModel::clearData: 清空所有数据";
    
    beginResetModel();
    m_factors.clear();
    endResetModel();
    
    emit countChanged();
    emit dataUpdated();
}

void FactorViewModel::appendData(const QVariantMap& factorData)
{
    qDebug() << "FactorViewModel::appendData: 添加数据项";
    
    FactorViewData factor = FactorViewData::fromVariantMap(factorData);
    
    // 检查是否已存在相同ID的因子
    for (int i = 0; i < m_factors.size(); ++i) {
        if (m_factors.at(i).factorId == factor.factorId) {
            qWarning() << "因子ID已存在:" << factor.factorId;
            return;
        }
    }
    
    int newRow = m_factors.size();
    beginInsertRows(QModelIndex(), newRow, newRow);
    m_factors.append(factor);
    endInsertRows();
    
    emit countChanged();
    emit dataUpdated();
}

void FactorViewModel::addDataBatch(const QVariantList& factors)
{
    if (factors.isEmpty()) {
        return;
    }
    
    qDebug() << "FactorViewModel::addDataBatch: 批量添加" << factors.size() << "条数据";
    
    int startRow = m_factors.size();
    int endRow = startRow + factors.size() - 1;
    
    beginInsertRows(QModelIndex(), startRow, endRow);
    
    for (const QVariant& factorVariant : factors) {
        QVariantMap factorMap = factorVariant.toMap();
        FactorViewData factor = FactorViewData::fromVariantMap(factorMap);
        m_factors.append(factor);
    }
    
    endInsertRows();
    
    qDebug() << "FactorViewModel::addDataBatch: 成功添加" << factors.size() << "条数据";
    emit countChanged();
    emit dataUpdated();
}

QVariantMap FactorViewModel::getRow(int index) const
{
    if (index < 0 || index >= m_factors.size()) {
        qWarning() << "FactorViewModel::getRow: 索引越界:" << index << "数据大小:" << m_factors.size();
        return QVariantMap();
    }
    
    return m_factors.at(index).toVariantMap();
}

QVariantMap FactorViewModel::getFactorById(const QString& factorId) const
{
    if (FactorService* service = FactorService::instance()) {
        return service->getFactorById(factorId);
    }
    return {};
}

QVariantList FactorViewModel::getAllFactors() const
{
    if (FactorService* service = FactorService::instance()) {
        return service->getAllFactors();
    }
    return {};
}

QVariantList FactorViewModel::searchFactors(const QString& keyword) const
{
    if (FactorService* service = FactorService::instance()) {
        return service->searchFactors(keyword);
    }
    return {};
}

QVariantList FactorViewModel::filterFactorsByCategory(const QString& category) const
{
    if (FactorService* service = FactorService::instance()) {
        return service->filterFactorsByCategory(category);
    }
    return {};
}

QVariantList FactorViewModel::filterFactorsByTags(const QStringList& tags) const
{
    if (FactorService* service = FactorService::instance()) {
        return service->filterFactorsByTags(tags);
    }
    return {};
}

void FactorViewModel::updateFactor(const QString& factorId, const QVariantMap& factorData)
{
    int index = findIndexById(factorId);
    if (index == -1) {
        qWarning() << "未找到因子:" << factorId;
        return;
    }
    
    // 更新因子数据
    FactorViewData updatedFactor = FactorViewData::fromVariantMap(factorData);
    m_factors[index] = updatedFactor;
    
    // 通知视图数据已更改
    QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, roleNames().keys());
    
    emit dataUpdated();
    
    //qDebug() << "FactorViewModel::updateFactor 完成，更新因子:" << factorId;
}

void FactorViewModel::removeFactor(const QString& factorId)
{
    int index = findIndexById(factorId);
    if (index == -1) {
        qWarning() << "未找到因子:" << factorId;
        return;
    }
    
    // 从模型中删除
    beginRemoveRows(QModelIndex(), index, index);
    m_factors.removeAt(index);
    endRemoveRows();
    
    emit countChanged();
    emit dataUpdated();
    
    qDebug() << "FactorViewModel::removeFactor 完成，删除因子:" << factorId;
}

int FactorViewModel::findIndexById(const QString& factorId) const
{
    for (int i = 0; i < m_factors.size(); ++i) {
        if (m_factors.at(i).factorId == factorId) {
            return i;
        }
    }
    return -1;
}

bool FactorViewModel::hasSameData(const QVariantList& factors) const
{
    if (m_factors.size() != factors.size()) {
        return false;
    }

    for (int i = 0; i < factors.size(); ++i) {
        const FactorViewData incomingFactor = FactorViewData::fromVariantMap(factors.at(i).toMap());
        if (!(m_factors.at(i) == incomingFactor)) {
            return false;
        }
    }

    return true;
}

QVariantMap FactorViewModel::FactorViewData::toVariantMap() const
{
    QVariantMap map;
    map["factorId"] = factorId;
    map["factorName"] = factorName;
    map["factorType"] = factorType;
    map["displayName"] = displayName;
    map["majorCategory"] = majorCategory;
    map["subCategory"] = subCategory;
    map["description"] = description;
    map["icValue"] = icValue;
    map["irValue"] = irValue;
    map["validityDays"] = validityDays;
    map["turnoverRate"] = turnoverRate;
    map["isRecommended"] = isRecommended;
    map["isFavorite"] = isFavorite;
    map["status"] = status;
    map["tags"] = tags;
    map["creator"] = creator;
    map["createDate"] = createDate;
    map["groupReturns"] = QVariant::fromValue(groupReturns);
    return map;
}

FactorViewModel::FactorViewData FactorViewModel::FactorViewData::fromVariantMap(const QVariantMap& map)
{
    FactorViewData factor;
    bool factorTypeOk = false;
    
    factor.factorId = map.value("factorId").toString();
    factor.factorName = map.value("factorName").toString();
    factor.factorType = map.value("factorType").toInt(&factorTypeOk);
    if (!factorTypeOk) {
        factor.factorType = -1;
    }
    factor.displayName = map.value("displayName").toString();
    factor.majorCategory = map.value("majorCategory").toString();
    factor.subCategory = map.value("subCategory").toString();
    factor.description = map.value("description").toString();
    factor.icValue = map.value("icValue").toDouble();
    factor.irValue = map.value("irValue").toDouble();
    factor.validityDays = map.value("validityDays").toInt();
    factor.turnoverRate = map.value("turnoverRate").toDouble();
    factor.isRecommended = map.value("isRecommended").toBool();
    factor.isFavorite = map.value("isFavorite").toBool();
    factor.status = map.value("status").toString();
    factor.tags = map.value("tags").toStringList();
    factor.creator = map.value("creator").toString();
    factor.createDate = map.value("createDate").toString();
    
    QVariant groupReturnsVar = map.value("groupReturns");
    if (groupReturnsVar.canConvert<QVector<double>>()) {
        factor.groupReturns = groupReturnsVar.value<QVector<double>>();
    } else if (groupReturnsVar.canConvert<QVariantList>()) {
        QVariantList list = groupReturnsVar.toList();
        factor.groupReturns.resize(list.size());
        for (int i = 0; i < list.size(); ++i) {
            factor.groupReturns[i] = list[i].toDouble();
        }
    }
    
    return factor;
}

bool FactorViewModel::FactorViewData::operator==(const FactorViewData& other) const
{
    return factorId == other.factorId
        && factorName == other.factorName
        && factorType == other.factorType
        && displayName == other.displayName
        && majorCategory == other.majorCategory
        && subCategory == other.subCategory
        && description == other.description
        && qFuzzyCompare(icValue + 1.0, other.icValue + 1.0)
        && qFuzzyCompare(irValue + 1.0, other.irValue + 1.0)
        && validityDays == other.validityDays
        && qFuzzyCompare(turnoverRate + 1.0, other.turnoverRate + 1.0)
        && isRecommended == other.isRecommended
        && isFavorite == other.isFavorite
        && status == other.status
        && tags == other.tags
        && creator == other.creator
        && createDate == other.createDate
        && groupReturns == other.groupReturns;
}