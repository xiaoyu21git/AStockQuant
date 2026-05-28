// StrategyViewModel.cpp
// 策略视图模型实现 - 负责策略数据的视图展示

#include "../../ui/bridge/include/StrategyViewModel.h"
#include "../../ui/bridge/include/StrategyService.h"
#include "../../ui/bridge/include/StrategyStructureResolvers.h"

#include <QDebug>
#include <QDateTime>
#include <QRegularExpression>
#include <QMetaObject>
#include <QMetaMethod>

namespace {

domain::strategy::StrategyAggregate assembleStrategyAggregate(const QVariantMap& strategy)
{
    return bridge::config::buildStrategyAggregate(strategy);
}

domain::backtest::ResolvedStrategyIdentity projectResolvedIdentity(
    const domain::strategy::StrategyAggregate& aggregate)
{
    domain::backtest::ResolvedStrategyIdentity identity;
    identity.storedType = aggregate.identity.storedType;
    identity.validStoredType = aggregate.identity.storedType != domain::backtest::StrategyStoredType::Unknown;
    identity.behavior = domain::backtest::ResolvedStrategyBehavior{
        aggregate.identity.behaviorKind,
        aggregate.identity.behaviorKind != domain::backtest::StrategyBehaviorKind::Custom
            || aggregate.identity.storedType == domain::backtest::StrategyStoredType::Custom
    };
    return identity;
}

} // namespace

StrategyViewModel::StrategyViewModel(QObject* parent)
    : QAbstractListModel(parent)
{
    qDebug() << "StrategyViewModel constructor";
}

StrategyViewModel::~StrategyViewModel()
{
    qDebug() << "StrategyViewModel destructor";
}

StrategyViewModel::StrategyAssetCategory StrategyViewModel::assetCategoryFromIndex(const QVariant& value)
{
    switch (value.toInt()) {
    case static_cast<int>(StrategyAssetCategory::Stock):
        return StrategyAssetCategory::Stock;
    case static_cast<int>(StrategyAssetCategory::Future):
        return StrategyAssetCategory::Future;
    case static_cast<int>(StrategyAssetCategory::Option):
        return StrategyAssetCategory::Option;
    case static_cast<int>(StrategyAssetCategory::Fund):
        return StrategyAssetCategory::Fund;
    case static_cast<int>(StrategyAssetCategory::Index):
        return StrategyAssetCategory::Index;
    case static_cast<int>(StrategyAssetCategory::MultiAsset):
        return StrategyAssetCategory::MultiAsset;
    case static_cast<int>(StrategyAssetCategory::Unknown):
    default:
        return StrategyAssetCategory::Unknown;
    }
}

int StrategyViewModel::assetCategoryIndex(StrategyAssetCategory assetType)
{
    return static_cast<int>(assetType);
}

StrategyViewModel::StrategyTimeFrameKind StrategyViewModel::timeFrameFromIndex(const QVariant& value)
{
    switch (value.toInt()) {
    case static_cast<int>(StrategyTimeFrameKind::Tick):
        return StrategyTimeFrameKind::Tick;
    case static_cast<int>(StrategyTimeFrameKind::Minute1):
        return StrategyTimeFrameKind::Minute1;
    case static_cast<int>(StrategyTimeFrameKind::Minute5):
        return StrategyTimeFrameKind::Minute5;
    case static_cast<int>(StrategyTimeFrameKind::Minute15):
        return StrategyTimeFrameKind::Minute15;
    case static_cast<int>(StrategyTimeFrameKind::Minute30):
        return StrategyTimeFrameKind::Minute30;
    case static_cast<int>(StrategyTimeFrameKind::Hour1):
        return StrategyTimeFrameKind::Hour1;
    case static_cast<int>(StrategyTimeFrameKind::Daily):
        return StrategyTimeFrameKind::Daily;
    case static_cast<int>(StrategyTimeFrameKind::Weekly):
        return StrategyTimeFrameKind::Weekly;
    case static_cast<int>(StrategyTimeFrameKind::Monthly):
        return StrategyTimeFrameKind::Monthly;
    case static_cast<int>(StrategyTimeFrameKind::Custom):
        return StrategyTimeFrameKind::Custom;
    case static_cast<int>(StrategyTimeFrameKind::Unknown):
    default:
        return StrategyTimeFrameKind::Unknown;
    }
}

int StrategyViewModel::timeFrameIndex(StrategyTimeFrameKind timeFrame)
{
    return static_cast<int>(timeFrame);
}

StrategyViewModel::StrategyRiskLevelKind StrategyViewModel::riskLevelFromIndex(const QVariant& value)
{
    switch (value.toInt()) {
    case static_cast<int>(StrategyRiskLevelKind::Low):
        return StrategyRiskLevelKind::Low;
    case static_cast<int>(StrategyRiskLevelKind::Medium):
        return StrategyRiskLevelKind::Medium;
    case static_cast<int>(StrategyRiskLevelKind::High):
        return StrategyRiskLevelKind::High;
    case static_cast<int>(StrategyRiskLevelKind::VeryHigh):
        return StrategyRiskLevelKind::VeryHigh;
    case static_cast<int>(StrategyRiskLevelKind::Unknown):
    default:
        return StrategyRiskLevelKind::Unknown;
    }
}

int StrategyViewModel::riskLevelIndex(StrategyRiskLevelKind riskLevel)
{
    return static_cast<int>(riskLevel);
}

double StrategyViewModel::parseNumericValue(const QVariant& value)
{
    bool ok = false;
    const double directValue = value.toDouble(&ok);
    if (ok) {
        return directValue;
    }

    QString text = value.toString().trimmed();
    if (text.endsWith(QLatin1Char('%'))) {
        text.chop(1);
    }
    text.remove(QLatin1Char(','));
    const double parsedValue = text.toDouble(&ok);
    return ok ? parsedValue : 0.0;
}

QDateTime StrategyViewModel::parseDateTimeValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }

    if (value.canConvert<QDateTime>()) {
        const QDateTime dateTime = value.toDateTime();
        if (dateTime.isValid()) {
            return dateTime;
        }
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return {};
    }

    QDateTime dateTime = QDateTime::fromString(text, Qt::ISODate);
    if (!dateTime.isValid()) {
        dateTime = QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    if (!dateTime.isValid()) {
        dateTime = QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd"));
    }
    return dateTime;
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
            switch (strategy.strategyIdentity.storedType) {
            case domain::backtest::StrategyStoredType::TrendFollowing:
                return QStringLiteral("TREND");
            case domain::backtest::StrategyStoredType::MeanReversion:
                return QStringLiteral("MEAN_REVERSION");
            case domain::backtest::StrategyStoredType::Alpha:
                return QStringLiteral("ALPHA");
            case domain::backtest::StrategyStoredType::Arbitrage:
                return QStringLiteral("ARBITRAGE");
            case domain::backtest::StrategyStoredType::HighFrequency:
                return QStringLiteral("HFT");
            case domain::backtest::StrategyStoredType::Portfolio:
                return QStringLiteral("PORTFOLIO");
            case domain::backtest::StrategyStoredType::Custom:
                return QStringLiteral("CUSTOM");
            case domain::backtest::StrategyStoredType::Unknown:
            default:
                return QVariant();
            }
        case SubTypeRole:
            if (!strategy.strategyIdentity.behavior.valid) {
                return QVariant();
            }
            switch (strategy.strategyIdentity.behavior.kind) {
            case domain::backtest::StrategyBehaviorKind::TrendFollowing:
                return QStringLiteral("trend_following");
            case domain::backtest::StrategyBehaviorKind::MeanReversion:
                return QStringLiteral("mean_reversion");
            case domain::backtest::StrategyBehaviorKind::Momentum:
                return QStringLiteral("momentum");
            case domain::backtest::StrategyBehaviorKind::Arbitrage:
                return QStringLiteral("arbitrage");
            case domain::backtest::StrategyBehaviorKind::MultiFactor:
                return QStringLiteral("multi_factor");
            case domain::backtest::StrategyBehaviorKind::MachineLearning:
                return QStringLiteral("machine_learning");
            case domain::backtest::StrategyBehaviorKind::EventDriven:
                return QStringLiteral("event_driven");
            case domain::backtest::StrategyBehaviorKind::HighFrequency:
                return QStringLiteral("high_frequency");
            case domain::backtest::StrategyBehaviorKind::Custom:
            default:
                return QStringLiteral("custom");
            }
        case DescriptionRole:
            return strategy.description;
        case StatusRole:
            return strategy_view::strategyLifecycleStatusIndex(strategy.status);
        case ReturnsRole:
            return strategy.performance.returns;
        case MaxDrawdownRole:
            return strategy.performance.maxDrawdown;
        case SharpeRatioRole:
            return strategy.performance.sharpeRatio;
        case WinRateRole:
            return strategy.performance.winRate;
        case RunningDaysRole:
            return strategy.performance.runningDays;
        case TradesCountRole:
            return strategy.performance.tradesCount;
        case PositionRole:
            return strategy.performance.position;
        case DailyPnLRole:
            return strategy.performance.dailyPnL;
        case AssetTypeRole:
            return assetCategoryIndex(strategy.assetType);
        case TimeFrameRole:
            return timeFrameIndex(strategy.timeFrame);
        case RiskLevelRole:
            return riskLevelIndex(strategy.riskLevel);
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
    roles[StatusRole] = "statusIndex";
    roles[ReturnsRole] = "returns";
    roles[MaxDrawdownRole] = "maxDrawdown";
    roles[SharpeRatioRole] = "sharpeRatio";
    roles[WinRateRole] = "winRate";
    roles[RunningDaysRole] = "runningDays";
    roles[TradesCountRole] = "tradesCount";
    roles[PositionRole] = "position";
    roles[DailyPnLRole] = "dailyPnL";
    roles[AssetTypeRole] = "assetTypeIndex";
    roles[TimeFrameRole] = "timeFrameIndex";
    roles[RiskLevelRole] = "riskLevelIndex";
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

QVariantList StrategyViewModel::filterStrategiesByType(int strategyTypeIndex) const
{
    if (StrategyService* service = StrategyService::instance()) {
        return service->getStrategiesByType(strategyTypeIndex);
    }
    return {};
}

QVariantList StrategyViewModel::filterStrategiesByStatus(int statusIndex) const
{
    if (StrategyService* service = StrategyService::instance()) {
        return service->getStrategiesByStatus(statusIndex);
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

bool StrategyViewModel::updateStrategyStatus(const QString& strategyId, int statusIndex)
{
    int index = findIndexById(strategyId);
    if (index == -1) {
        qWarning() << "未找到策略:" << strategyId;
        return false;
    }

    const strategy_view::StrategyLifecycleStatus status =
        strategy_view::strategyLifecycleStatusFromIndex(statusIndex);
    if (!strategy_view::isKnownStrategyLifecycleStatus(status)) {
        qWarning() << "无效的策略状态索引:" << statusIndex;
        return false;
    }
    
    m_strategies[index].status = status;
    
    // 通知视图数据已更改
    QModelIndex modelIndex = createIndex(index, 0);
    QVector<int> roles = {StatusRole};
    emit dataChanged(modelIndex, modelIndex, roles);
    
    emit dataUpdated();
    
    qDebug() << "StrategyViewModel::updateStrategyStatus 完成，更新策略状态:" << strategyId << "->"
             << strategy_view::strategyLifecycleStatusIndex(m_strategies[index].status);
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
        m_strategies[index].performance.returns = parseNumericValue(performance.value("returns"));
    }
    if (performance.contains("maxDrawdown")) {
        m_strategies[index].performance.maxDrawdown = parseNumericValue(performance.value("maxDrawdown"));
    }
    if (performance.contains("sharpeRatio")) {
        m_strategies[index].performance.sharpeRatio = parseNumericValue(performance.value("sharpeRatio"));
    }
    if (performance.contains("winRate")) {
        m_strategies[index].performance.winRate = parseNumericValue(performance.value("winRate"));
    }
    if (performance.contains("runningDays")) {
        m_strategies[index].performance.runningDays = performance.value("runningDays").toInt();
    }
    if (performance.contains("tradesCount")) {
        m_strategies[index].performance.tradesCount = performance.value("tradesCount").toInt();
    }
    if (performance.contains("position")) {
        m_strategies[index].performance.position = parseNumericValue(performance.value("position"));
    }
    if (performance.contains("dailyPnL")) {
        m_strategies[index].performance.dailyPnL = parseNumericValue(performance.value("dailyPnL"));
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
    switch (strategyIdentity.storedType) {
    case domain::backtest::StrategyStoredType::TrendFollowing:
        map["strategyType"] = QStringLiteral("TREND");
        break;
    case domain::backtest::StrategyStoredType::MeanReversion:
        map["strategyType"] = QStringLiteral("MEAN_REVERSION");
        break;
    case domain::backtest::StrategyStoredType::Alpha:
        map["strategyType"] = QStringLiteral("ALPHA");
        break;
    case domain::backtest::StrategyStoredType::Arbitrage:
        map["strategyType"] = QStringLiteral("ARBITRAGE");
        break;
    case domain::backtest::StrategyStoredType::HighFrequency:
        map["strategyType"] = QStringLiteral("HFT");
        break;
    case domain::backtest::StrategyStoredType::Portfolio:
        map["strategyType"] = QStringLiteral("PORTFOLIO");
        break;
    case domain::backtest::StrategyStoredType::Custom:
        map["strategyType"] = QStringLiteral("CUSTOM");
        break;
    case domain::backtest::StrategyStoredType::Unknown:
    default:
        break;
    }
    if (strategyIdentity.behavior.valid) {
        switch (strategyIdentity.behavior.kind) {
        case domain::backtest::StrategyBehaviorKind::TrendFollowing:
            map["subType"] = QStringLiteral("trend_following");
            break;
        case domain::backtest::StrategyBehaviorKind::MeanReversion:
            map["subType"] = QStringLiteral("mean_reversion");
            break;
        case domain::backtest::StrategyBehaviorKind::Momentum:
            map["subType"] = QStringLiteral("momentum");
            break;
        case domain::backtest::StrategyBehaviorKind::Arbitrage:
            map["subType"] = QStringLiteral("arbitrage");
            break;
        case domain::backtest::StrategyBehaviorKind::MultiFactor:
            map["subType"] = QStringLiteral("multi_factor");
            break;
        case domain::backtest::StrategyBehaviorKind::MachineLearning:
            map["subType"] = QStringLiteral("machine_learning");
            break;
        case domain::backtest::StrategyBehaviorKind::EventDriven:
            map["subType"] = QStringLiteral("event_driven");
            break;
        case domain::backtest::StrategyBehaviorKind::HighFrequency:
            map["subType"] = QStringLiteral("high_frequency");
            break;
        case domain::backtest::StrategyBehaviorKind::Custom:
        default:
            map["subType"] = QStringLiteral("custom");
            break;
        }
    }
    if (strategyIdentity.validStoredType) {
        map["strategyTypeIndex"] = strategyIdentity.storedTypeIndex();
    }
    if (strategyIdentity.behavior.valid) {
        map["strategyBehaviorKind"] = strategyIdentity.behavior.index();
    }
    map["description"] = description;
    if (strategy_view::isKnownStrategyLifecycleStatus(status)) {
        map["statusIndex"] = strategy_view::strategyLifecycleStatusIndex(status);
    }
    map["returns"] = performance.returns;
    map["maxDrawdown"] = performance.maxDrawdown;
    map["sharpeRatio"] = performance.sharpeRatio;
    map["winRate"] = performance.winRate;
    map["runningDays"] = performance.runningDays;
    map["tradesCount"] = performance.tradesCount;
    map["position"] = performance.position;
    map["dailyPnL"] = performance.dailyPnL;
    map["assetTypeIndex"] = assetCategoryIndex(assetType);
    map["timeFrameIndex"] = timeFrameIndex(timeFrame);
    map["riskLevelIndex"] = riskLevelIndex(riskLevel);
    map["tags"] = tags;
    map["createdAt"] = createdAt.isValid() ? createdAt.toString(Qt::ISODate) : QString();
    map["updatedAt"] = updatedAt.isValid() ? updatedAt.toString(Qt::ISODate) : QString();
    map["author"] = author;
    map["version"] = version;
    return map;
}

StrategyViewModel::StrategyViewData StrategyViewModel::StrategyViewData::fromVariantMap(const QVariantMap& map)
{
    StrategyViewData strategy;
    QVariantMap performance = map.value("performance_metrics").toMap();
    const domain::strategy::StrategyAggregate aggregate = assembleStrategyAggregate(map);
    
    strategy.strategyId = map.value("strategy_id", map.value("strategyId")).toString();
    strategy.strategyName = map.value("strategy_name", map.value("strategyName")).toString();
    strategy.strategyIdentity = projectResolvedIdentity(aggregate);
    strategy.description = map.value("description").toString();
    strategy.status = aggregate.lifecycle.status;
    strategy.performance.returns = parseNumericValue(map.value("returns", performance.value("returns")));
    strategy.performance.maxDrawdown = parseNumericValue(map.value("max_drawdown", map.value("maxDrawdown", performance.value("maxDrawdown"))));
    strategy.performance.sharpeRatio = parseNumericValue(map.value("sharpe_ratio", map.value("sharpeRatio", performance.value("sharpeRatio"))));
    strategy.performance.winRate = parseNumericValue(map.value("win_rate", map.value("winRate", performance.value("winRate"))));
    strategy.performance.runningDays = map.value("running_days", map.value("runningDays", performance.value("runningDays", 0))).toInt();
    strategy.performance.tradesCount = map.value("trades_count", map.value("tradesCount", performance.value("tradesCount", 0))).toInt();
    strategy.performance.position = parseNumericValue(map.value("position", performance.value("position", 0.0)));
    strategy.performance.dailyPnL = parseNumericValue(map.value("daily_pnl", map.value("dailyPnL", performance.value("dailyPnL", 0.0))));
    strategy.assetType = assetCategoryFromIndex(map.value("assetTypeIndex"));
    strategy.timeFrame = timeFrameFromIndex(map.value("timeFrameIndex"));
    strategy.riskLevel = riskLevelFromIndex(map.value("riskLevelIndex"));
    strategy.tags = map.value("tags").toStringList();
    strategy.createdAt = parseDateTimeValue(map.value("created_at", map.value("createdAt")));
    strategy.updatedAt = parseDateTimeValue(map.value("updated_at", map.value("updatedAt")));
    strategy.author = map.value("author", "系统").toString();
    strategy.version = map.value("version", "1.0").toString();
    
    return strategy;
}