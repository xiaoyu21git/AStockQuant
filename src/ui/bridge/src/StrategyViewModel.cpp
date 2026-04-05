// StrategyViewModel.cpp
// 策略视图模型实现 - 负责策略数据的视图展示

#include "../../ui/bridge/include/StrategyViewModel.h"
#include "../../ui/bridge/include/StrategyService.h"
#include <QDebug>
#include <QDateTime>
#include <QRegularExpression>
#include <QMetaObject>
#include <QMetaMethod>

StrategyViewModel::StrategyViewModel(QObject* parent)
    : QAbstractListModel(parent)
{
    qDebug() << "StrategyViewModel constructor";
}

StrategyViewModel::~StrategyViewModel()
{
    qDebug() << "StrategyViewModel destructor";
}

int StrategyViewModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return m_strategies.size();
}

QVariant StrategyViewModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_strategies.size() || index.row() < 0) {
        return QVariant();
    }
    
    const StrategyViewData& strategy = m_strategies.at(index.row());
    
    switch (role) {
        case StrategyIdRole:
            return strategy.strategyId;
        case StrategyNameRole:
            return strategy.strategyName;
        case StrategyTypeRole:
            return strategy.strategyType;
        case SubTypeRole:
            return strategy.subType;
        case DescriptionRole:
            return strategy.description;
        case StatusRole:
            return strategy.status;
        case ReturnsRole:
            return strategy.returns;
        case MaxDrawdownRole:
            return strategy.maxDrawdown;
        case SharpeRatioRole:
            return strategy.sharpeRatio;
        case WinRateRole:
            return strategy.winRate;
        case RunningDaysRole:
            return strategy.runningDays;
        case TradesCountRole:
            return strategy.tradesCount;
        case PositionRole:
            return strategy.position;
        case DailyPnLRole:
            return strategy.dailyPnL;
        case AssetTypeRole:
            return strategy.assetType;
        case TimeFrameRole:
            return strategy.timeFrame;
        case RiskLevelRole:
            return strategy.riskLevel;
        case TagsRole:
            return strategy.tags;
        case CreatedAtRole:
            return strategy.createdAt;
        case UpdatedAtRole:
            return strategy.updatedAt;
        case AuthorRole:
            return strategy.author;
        case VersionRole:
            return strategy.version;
        case Qt::DisplayRole:
            return strategy.strategyName;
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> StrategyViewModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[StrategyIdRole] = "strategyId";
    roles[StrategyNameRole] = "strategyName";
    roles[StrategyTypeRole] = "strategyType";
    roles[SubTypeRole] = "subType";
    roles[DescriptionRole] = "description";
    roles[StatusRole] = "status";
    roles[ReturnsRole] = "returns";
    roles[MaxDrawdownRole] = "maxDrawdown";
    roles[SharpeRatioRole] = "sharpeRatio";
    roles[WinRateRole] = "winRate";
    roles[RunningDaysRole] = "runningDays";
    roles[TradesCountRole] = "tradesCount";
    roles[PositionRole] = "position";
    roles[DailyPnLRole] = "dailyPnL";
    roles[AssetTypeRole] = "assetType";
    roles[TimeFrameRole] = "timeFrame";
    roles[RiskLevelRole] = "riskLevel";
    roles[TagsRole] = "tags";
    roles[CreatedAtRole] = "createdAt";
    roles[UpdatedAtRole] = "updatedAt";
    roles[AuthorRole] = "author";
    roles[VersionRole] = "version";
    return roles;
}

void StrategyViewModel::updateData(const QVariantList& strategies)
{
    //qDebug() << "StrategyViewModel::updateData: 更新数据，条数:" << strategies.size();
    
    beginResetModel();
    
    m_strategies.clear();
    
    for (const QVariant& strategyVariant : strategies) {
        QVariantMap strategyMap = strategyVariant.toMap();
        StrategyViewData strategy = StrategyViewData::fromVariantMap(strategyMap);
        m_strategies.append(strategy);
    }
    
    endResetModel();
    
   // qDebug() << "StrategyViewModel::updateData: 更新完成，当前条数:" << m_strategies.size();
    emit countChanged();
    emit dataUpdated();
}

void StrategyViewModel::clearData()
{
    qDebug() << "StrategyViewModel::clearData: 清空所有数据";
    
    beginResetModel();
    m_strategies.clear();
    endResetModel();
    
    emit countChanged();
    emit dataUpdated();
}

void StrategyViewModel::appendData(const QVariantMap& strategyData)
{
    qDebug() << "StrategyViewModel::appendData: 添加数据项";
    
    StrategyViewData strategy = StrategyViewData::fromVariantMap(strategyData);
    
    // 检查是否已存在相同ID的策略
    for (int i = 0; i < m_strategies.size(); ++i) {
        if (m_strategies.at(i).strategyId == strategy.strategyId) {
            qWarning() << "策略ID已存在:" << strategy.strategyId;
            return;
        }
    }
    
    int newRow = m_strategies.size();
    beginInsertRows(QModelIndex(), newRow, newRow);
    m_strategies.append(strategy);
    endInsertRows();
    
    emit countChanged();
    emit dataUpdated();
}

void StrategyViewModel::addDataBatch(const QVariantList& strategies)
{
    if (strategies.isEmpty()) {
        return;
    }
    
    qDebug() << "StrategyViewModel::addDataBatch: 批量添加" << strategies.size() << "条数据";
    
    int startRow = m_strategies.size();
    int endRow = startRow + strategies.size() - 1;
    
    beginInsertRows(QModelIndex(), startRow, endRow);
    
    for (const QVariant& strategyVariant : strategies) {
        QVariantMap strategyMap = strategyVariant.toMap();
        StrategyViewData strategy = StrategyViewData::fromVariantMap(strategyMap);
        m_strategies.append(strategy);
    }
    
    endInsertRows();
    
    qDebug() << "StrategyViewModel::addDataBatch: 成功添加" << strategies.size() << "条数据";
    emit countChanged();
    emit dataUpdated();
}

QVariantMap StrategyViewModel::getRow(int index) const
{
    if (index < 0 || index >= m_strategies.size()) {
        qWarning() << "StrategyViewModel::getRow: 索引越界:" << index << "数据大小:" << m_strategies.size();
        return QVariantMap();
    }
    
    return m_strategies.at(index).toVariantMap();
}

QVariantMap StrategyViewModel::getStrategyById(const QString& strategyId) const
{
    if (StrategyService* service = StrategyService::instance()) {
        return service->getStrategyById(strategyId);
    }
    return {};
}

QVariantList StrategyViewModel::getAllStrategies() const
{
    if (StrategyService* service = StrategyService::instance()) {
        return service->getAllStrategies();
    }
    return {};
}

QVariantList StrategyViewModel::searchStrategies(const QString& keyword) const
{
    if (StrategyService* service = StrategyService::instance()) {
        return service->searchStrategies(keyword);
    }
    return {};
}

QVariantList StrategyViewModel::filterStrategiesByType(const QString& strategyType) const
{
    if (StrategyService* service = StrategyService::instance()) {
        return service->getStrategiesByType(strategyType);
    }
    return {};
}

QVariantList StrategyViewModel::filterStrategiesByStatus(const QString& status) const
{
    if (StrategyService* service = StrategyService::instance()) {
        return service->getStrategiesByStatus(status);
    }
    return {};
}

void StrategyViewModel::updateStrategy(const QString& strategyId, const QVariantMap& strategyData)
{
    int index = findIndexById(strategyId);
    if (index == -1) {
        qWarning() << "未找到策略:" << strategyId;
        return;
    }
    
    // 更新策略数据
    StrategyViewData updatedStrategy = StrategyViewData::fromVariantMap(strategyData);
    m_strategies[index] = updatedStrategy;
    
    // 通知视图数据已更改
    QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, roleNames().keys());
    
    emit dataUpdated();
    
    qDebug() << "StrategyViewModel::updateStrategy 完成，更新策略:" << strategyId;
}

void StrategyViewModel::removeStrategy(const QString& strategyId)
{
    int index = findIndexById(strategyId);
    if (index == -1) {
        qWarning() << "未找到策略:" << strategyId;
        return;
    }
    
    // 从模型中删除
    beginRemoveRows(QModelIndex(), index, index);
    m_strategies.removeAt(index);
    endRemoveRows();
    
    emit countChanged();
    emit dataUpdated();
    
    qDebug() << "StrategyViewModel::removeStrategy 完成，删除策略:" << strategyId;
}

bool StrategyViewModel::updateStrategyStatus(const QString& strategyId, const QString& status)
{
    int index = findIndexById(strategyId);
    if (index == -1) {
        qWarning() << "未找到策略:" << strategyId;
        return false;
    }
    
    m_strategies[index].status = status;
    
    // 通知视图数据已更改
    QModelIndex modelIndex = createIndex(index, 0);
    QVector<int> roles = {StatusRole};
    emit dataChanged(modelIndex, modelIndex, roles);
    
    emit dataUpdated();
    
    qDebug() << "StrategyViewModel::updateStrategyStatus 完成，更新策略状态:" << strategyId << "->" << status;
    return true;
}

bool StrategyViewModel::updateStrategyPerformance(const QString& strategyId, const QVariantMap& performance)
{
    int index = findIndexById(strategyId);
    if (index == -1) {
        qWarning() << "未找到策略:" << strategyId;
        return false;
    }
    
    // 更新性能指标
    if (performance.contains("returns")) {
        m_strategies[index].returns = performance["returns"].toString();
    }
    if (performance.contains("maxDrawdown")) {
        m_strategies[index].maxDrawdown = performance["maxDrawdown"].toString();
    }
    if (performance.contains("sharpeRatio")) {
        m_strategies[index].sharpeRatio = performance["sharpeRatio"].toString();
    }
    if (performance.contains("winRate")) {
        m_strategies[index].winRate = performance["winRate"].toString();
    }
    if (performance.contains("runningDays")) {
        m_strategies[index].runningDays = performance["runningDays"].toInt();
    }
    if (performance.contains("tradesCount")) {
        m_strategies[index].tradesCount = performance["tradesCount"].toInt();
    }
    if (performance.contains("position")) {
        m_strategies[index].position = performance["position"].toDouble();
    }
    if (performance.contains("dailyPnL")) {
        m_strategies[index].dailyPnL = performance["dailyPnL"].toDouble();
    }
    
    // 通知视图数据已更改
    QModelIndex modelIndex = createIndex(index, 0);
    QVector<int> roles = {ReturnsRole, MaxDrawdownRole, SharpeRatioRole, WinRateRole, 
                         RunningDaysRole, TradesCountRole, PositionRole, DailyPnLRole};
    emit dataChanged(modelIndex, modelIndex, roles);
    
    emit dataUpdated();
    
    qDebug() << "StrategyViewModel::updateStrategyPerformance 完成，更新策略性能:" << strategyId;
    return true;
}

int StrategyViewModel::findIndexById(const QString& strategyId) const
{
    for (int i = 0; i < m_strategies.size(); ++i) {
        if (m_strategies.at(i).strategyId == strategyId) {
            return i;
        }
    }
    return -1;
}

QVariantMap StrategyViewModel::StrategyViewData::toVariantMap() const
{
    QVariantMap map;
    map["strategyId"] = strategyId;
    map["strategyName"] = strategyName;
    map["strategyType"] = strategyType;
    map["subType"] = subType;
    map["description"] = description;
    map["status"] = status;
    map["returns"] = returns;
    map["maxDrawdown"] = maxDrawdown;
    map["sharpeRatio"] = sharpeRatio;
    map["winRate"] = winRate;
    map["runningDays"] = runningDays;
    map["tradesCount"] = tradesCount;
    map["position"] = position;
    map["dailyPnL"] = dailyPnL;
    map["assetType"] = assetType;
    map["timeFrame"] = timeFrame;
    map["riskLevel"] = riskLevel;
    map["tags"] = tags;
    map["createdAt"] = createdAt;
    map["updatedAt"] = updatedAt;
    map["author"] = author;
    map["version"] = version;
    return map;
}

StrategyViewModel::StrategyViewData StrategyViewModel::StrategyViewData::fromVariantMap(const QVariantMap& map)
{
    StrategyViewData strategy;
    QVariantMap performance = map.value("performance_metrics").toMap();
    QVariantMap parameters = map.value("parameters").toMap();
    
    strategy.strategyId = map.value("strategy_id", map.value("strategyId")).toString();
    strategy.strategyName = map.value("strategy_name", map.value("strategyName")).toString();
    strategy.strategyType = map.value("strategy_type", map.value("strategyType")).toString();
    strategy.subType = map.value("sub_type", map.value("subType", parameters.value("strategy_subtype"))).toString();
    strategy.description = map.value("description").toString();
    strategy.status = map.value("status").toString();
    strategy.returns = map.value("returns", performance.value("returns")).toString();
    strategy.maxDrawdown = map.value("max_drawdown", map.value("maxDrawdown", performance.value("maxDrawdown"))).toString();
    strategy.sharpeRatio = map.value("sharpe_ratio", map.value("sharpeRatio", performance.value("sharpeRatio"))).toString();
    strategy.winRate = map.value("win_rate", map.value("winRate", performance.value("winRate"))).toString();
    strategy.runningDays = map.value("running_days", map.value("runningDays", performance.value("runningDays", 0))).toInt();
    strategy.tradesCount = map.value("trades_count", map.value("tradesCount", performance.value("tradesCount", 0))).toInt();
    strategy.position = map.value("position", performance.value("position", 0.0)).toDouble();
    strategy.dailyPnL = map.value("daily_pnl", map.value("dailyPnL", performance.value("dailyPnL", 0.0))).toDouble();
    strategy.assetType = map.value("asset_type", map.value("assetType")).toString();
    strategy.timeFrame = map.value("time_frame", map.value("timeFrame")).toString();
    strategy.riskLevel = map.value("risk_level", map.value("riskLevel")).toString();
    strategy.tags = map.value("tags").toStringList();
    strategy.createdAt = map.value("created_at", map.value("createdAt")).toString();
    strategy.updatedAt = map.value("updated_at", map.value("updatedAt")).toString();
    strategy.author = map.value("author", "系统").toString();
    strategy.version = map.value("version", "1.0").toString();
    
    return strategy;
}