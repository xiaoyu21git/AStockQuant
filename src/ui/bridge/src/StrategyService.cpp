#include "StrategyService.h"
#include "StrategyViewModel.h"
#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "../../ui/bridge/include/DatabaseConnectionManager.h"
#include "database/StrategyRepository.h"
#include <QDebug>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <QReadWriteLock>
#include <QCoreApplication>
#include <QRandomGenerator>
#include <QDir>

using namespace astock::database;

namespace {

constexpr int MAX_BACKTEST_HISTORY_ITEMS = 20;

QString normalizePersistedStatus(const QString& rawStatus)
{
    const QStringList validStatuses = {"ACTIVE", "INACTIVE", "TESTING", "ARCHIVED"};
    if (validStatuses.contains(rawStatus)) {
        return rawStatus;
    }

    if (!rawStatus.isEmpty()) {
        qWarning() << "StrategyService: 转换无效状态" << rawStatus << "为ACTIVE";
    }
    return "ACTIVE";
}

QVariantMap mergePerformanceMetrics(const QVariantMap& existingStrategy,
                                   const QVariantMap& incomingPerformance,
                                   const QString& updatedAt)
{
    QVariantMap mergedPerformance = existingStrategy.value("performance_metrics").toMap();
    for (auto it = incomingPerformance.constBegin(); it != incomingPerformance.constEnd(); ++it) {
        mergedPerformance.insert(it.key(), it.value());
    }

    QVariantMap historyEntry = incomingPerformance.value("backtestHistoryEntry").toMap();
    QVariantList backtestHistory = mergedPerformance.value("backtestHistory").toList();
    if (!historyEntry.isEmpty()) {
        historyEntry["recordedAt"] = historyEntry.value("recordedAt", updatedAt).toString();
        mergedPerformance["latestBacktest"] = historyEntry;
        backtestHistory.prepend(historyEntry);
        while (backtestHistory.size() > MAX_BACKTEST_HISTORY_ITEMS) {
            backtestHistory.removeLast();
        }
        mergedPerformance["backtestHistory"] = backtestHistory;
    }

    mergedPerformance["lastBacktestAt"] = incomingPerformance.value("lastBacktestAt", updatedAt).toString();
    return mergedPerformance;
}

// 策略类型描述
const QMap<QString, QString> STRATEGY_TYPE_DESCRIPTIONS = {
    {"TREND", "趋势跟踪策略 - 跟随市场趋势进行交易"},
    {"MEAN_REVERSION", "均值回归策略 - 假设价格会回归均值水平"},
    {"ALPHA", "阿尔法策略 - 寻找超越市场的超额收益"},
    {"ARBITRAGE", "套利策略 - 利用市场定价差异获取无风险收益"},
    {"HFT", "高频交易策略 - 利用极短时间窗口的交易机会"},
    {"PORTFOLIO", "组合策略 - 多个策略的组合配置"},
    {"CUSTOM", "自定义策略 - 用户自定义的交易逻辑"}
};

// 策略类型映射
const QMap<QString, QStringList> STRATEGY_TYPE_MAPPING = {
    {"trend_following", {"TREND", "趋势跟踪"}},
    {"mean_reversion", {"MEAN_REVERSION", "均值回归"}},
    {"momentum", {"ALPHA", "动量"}},
    {"arbitrage", {"ARBITRAGE", "套利"}},
    {"machine_learning", {"ALPHA", "机器学习"}},
    {"multi_factor", {"ALPHA", "多因子"}},
    {"custom", {"CUSTOM", "自定义"}}
};

QVariantList buildStrategyListFromCache(const QMap<QString, QVariantMap>& memoryCache)
{
    QVariantList strategies;
    for (const QVariantMap& strategy : memoryCache) {
        strategies.append(strategy);
    }
    return strategies;
}

QString firstNonEmptyStrategyText(const QVariantMap& strategy, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QString text = strategy.value(QString::fromUtf8(key)).toString().trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}

QStringList strategyTags(const QVariantMap& strategy)
{
    const QVariant tagsValue = strategy.value(QStringLiteral("tags"));
    QStringList tags;
    if (tagsValue.canConvert<QStringList>()) {
        tags = tagsValue.toStringList();
    } else {
        const QVariantList tagList = tagsValue.toList();
        for (const QVariant& tagValue : tagList) {
            const QString tagText = tagValue.toString().trimmed();
            if (!tagText.isEmpty()) {
                tags.append(tagText);
            }
        }
    }
    tags.removeDuplicates();
    return tags;
}

bool matchesStrategyType(const QVariantMap& strategy, const QString& strategyType)
{
    const QString normalizedType = strategyType.trimmed();
    if (normalizedType.isEmpty() || normalizedType.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    const QString primaryType = firstNonEmptyStrategyText(strategy, {"strategy_type", "strategyType"});
    const QString subType = firstNonEmptyStrategyText(strategy, {"sub_type", "subType"});
    return primaryType == normalizedType || subType == normalizedType;
}

bool matchesStrategyStatus(const QVariantMap& strategy, const QString& status)
{
    const QString normalizedStatus = status.trimmed();
    if (normalizedStatus.isEmpty() || normalizedStatus.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    return firstNonEmptyStrategyText(strategy, {"status"}) == normalizedStatus;
}

bool matchesStrategyKeyword(const QVariantMap& strategy, const QString& keyword)
{
    const QString normalizedKeyword = keyword.trimmed();
    if (normalizedKeyword.isEmpty()) {
        return true;
    }

    const QStringList searchableTexts = {
        firstNonEmptyStrategyText(strategy, {"strategy_name", "strategyName"}),
        firstNonEmptyStrategyText(strategy, {"description"}),
        firstNonEmptyStrategyText(strategy, {"strategy_code", "strategyCode"}),
        firstNonEmptyStrategyText(strategy, {"strategy_type", "strategyType"}),
        firstNonEmptyStrategyText(strategy, {"sub_type", "subType"}),
        firstNonEmptyStrategyText(strategy, {"asset_type", "assetType"}),
        firstNonEmptyStrategyText(strategy, {"time_frame", "timeFrame"})
    };

    for (const QString& text : searchableTexts) {
        if (!text.isEmpty() && text.contains(normalizedKeyword, Qt::CaseInsensitive)) {
            return true;
        }
    }

    const QStringList tags = strategyTags(strategy);
    for (const QString& tag : tags) {
        if (tag.contains(normalizedKeyword, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

QString eventStringValue(const engine::EventFormat& event, const std::string& key)
{
    const auto metadataIt = event.metadata.find(key);
    if (metadataIt != event.metadata.end()) {
        return QString::fromStdString(metadataIt->second).trimmed();
    }

    auto dataValue = event.get<std::string>(key);
    if (dataValue.has_value()) {
        return QString::fromStdString(*dataValue).trimmed();
    }

    auto dataInt = event.get<int64_t>(key);
    if (dataInt.has_value()) {
        return QString::number(*dataInt);
    }

    auto dataDouble = event.get<double>(key);
    if (dataDouble.has_value()) {
        return QString::number(*dataDouble, 'f', 6);
    }

    return {};
}

double eventNumericValue(const engine::EventFormat& event, const std::string& key, double fallback = 0.0)
{
    auto dataDouble = event.get<double>(key);
    if (dataDouble.has_value()) {
        return *dataDouble;
    }

    auto dataInt = event.get<int64_t>(key);
    if (dataInt.has_value()) {
        return static_cast<double>(*dataInt);
    }

    const QString metadataValue = eventStringValue(event, key);
    if (metadataValue.isEmpty()) {
        return fallback;
    }

    bool ok = false;
    const double numericValue = metadataValue.toDouble(&ok);
    return ok ? numericValue : fallback;
}

QString normalizedStrategyType(const QVariantMap& strategy)
{
    return strategy.value("strategy_type").toString().trimmed().toUpper();
}

bool strategyStatusAllowsSignals(const QVariantMap& strategy)
{
    const QString status = strategy.value("status").toString().trimmed().toUpper();
    return status == "ACTIVE" || status == "TESTING";
}

void refreshViewModelFromCache(StrategyViewModel* viewModel,
                               const QMap<QString, QVariantMap>& memoryCache)
{
    if (!viewModel) {
        return;
    }

    viewModel->updateData(buildStrategyListFromCache(memoryCache));
}

} // namespace

StrategyService* StrategyService::m_instance = nullptr;
QMutex StrategyService::m_instanceMutex;

StrategyService* StrategyService::instance() {
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new StrategyService(qApp);
    }
    return m_instance;
}

StrategyService::StrategyService(QObject* parent) 
    : QObject(parent)
    , m_initialized(false)
    , m_isLoading(false)
    , m_cacheLoaded(false)
    , m_autoInitialize(true)
    , m_eventBusIntegrated(false)
    , m_viewModel(new StrategyViewModel(this)) {
    // 连接信号到视图模型
    connect(this, &StrategyService::strategiesLoaded, this, [this](const QVariantList& strategies) {
        if (m_viewModel) {
            m_viewModel->updateData(strategies);
        } else {
            qWarning() << "StrategyService: 视图模型为空，无法更新数据";
        }
    });
}

StrategyService::~StrategyService() {
    clearAllCache();
}

void StrategyService::initialize() {
    QMutexLocker locker(&m_initMutex);
    
    if (m_initialized.load()) {
        return;
    }
    
    if (m_isLoading.load()) {
        return;
    }
    
    m_isLoading = true;
    emit isLoadingChanged();
    
    try {
        // 初始化仓储
        initializeRepository();
        
        // 加载缓存
        loadStrategiesFromDatabase();

        initializeEventBusIntegration();
        
        m_initialized = true;
        m_cacheLoaded = true;
        m_isLoading = false;
        
        emit initializedChanged();
        emit isLoadingChanged();
        emit cacheLoadedChanged();
    } catch (const std::exception& e) {
        m_isLoading = false;
        emit isLoadingChanged();
        qWarning() << "StrategyService: 初始化失败 -" << e.what();
        emit errorOccurred(QString("初始化失败: %1").arg(e.what()));
    }
}

bool StrategyService::publishSyntheticMarketEvent(const QVariantMap& marketEvent)
{
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        qWarning() << "StrategyService: EventBus unavailable, synthetic market event not published";
        return false;
    }

    const QString symbol = marketEvent.value("symbol").toString().trimmed();
    const double price = marketEvent.value("price").toDouble();
    if (symbol.isEmpty() || price <= 0.0) {
        qWarning() << "StrategyService: invalid synthetic market event" << marketEvent;
        return false;
    }

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        engine::EventTypes::MARKET_TICK,
        "APP_SYNTHETIC_MARKET",
        0);
    event.set("symbol", symbol.toStdString());
    event.set("price", price);
    event.metadata["symbol"] = symbol.toStdString();
    event.metadata["price"] = QString::number(price, 'f', 6).toStdString();
    event.metadata["source"] = "StrategyService.publishSyntheticMarketEvent";

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::NORMAL));
    if (!result) {
        qWarning() << "StrategyService: failed to publish synthetic market event" << QString::fromStdString(result.message);
        return false;
    }
    return true;
}

void StrategyService::initializeEventBusIntegration()
{
    QMutexLocker locker(&m_eventBusMutex);
    if (m_eventBusIntegrated.load()) {
        return;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        qWarning() << "StrategyService: EventBus not ready, skip event integration";
        return;
    }

    m_marketTickSubscription = bus->subscribe(engine::EventTypes::MARKET_TICK,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("market.tick"));
        });

    m_marketBarSubscription = bus->subscribe(engine::EventTypes::MARKET_BAR,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("market.bar"));
        });

    m_eventBusIntegrated = true;
    qDebug() << "StrategyService: EventBus integration initialized";
}

void StrategyService::handleMarketEvent(const engine::EventFormat& event, const QString& marketEventType)
{
    const QString symbol = eventStringValue(event, "symbol");
    if (symbol.isEmpty()) {
        return;
    }

    double referencePrice = eventNumericValue(event, "open", 0.0);
    const double closePrice = eventNumericValue(event, "close", 0.0);
    const double tickPrice = eventNumericValue(event, "price", 0.0);
    const double latestPrice = closePrice > 0.0 ? closePrice : tickPrice;

    if (latestPrice <= 0.0) {
        return;
    }

    if (referencePrice <= 0.0) {
        referencePrice = m_latestMarketPriceBySymbol.value(symbol, 0.0);
    }

    m_latestMarketPriceBySymbol.insert(symbol, latestPrice);
    if (referencePrice <= 0.0) {
        return;
    }

    QReadLocker locker(&m_rwLock);
    for (auto it = m_memoryCache.constBegin(); it != m_memoryCache.constEnd(); ++it) {
        const QVariantMap strategy = it.value();
        if (!strategyStatusAllowsSignals(strategy)) {
            continue;
        }
        publishStrategySignalForMarket(strategy, symbol, latestPrice, referencePrice, marketEventType,
                                       QString::fromStdString(event.id));
    }
}

void StrategyService::publishStrategySignalForMarket(const QVariantMap& strategy,
                                                    const QString& symbol,
                                                    double latestPrice,
                                                    double referencePrice,
                                                    const QString& marketEventType,
                                                    const QString& eventId)
{
    const QString action = determineSignalAction(strategy, latestPrice, referencePrice);
    if (action.isEmpty()) {
        return;
    }

    const double strength = determineSignalStrength(strategy, latestPrice, referencePrice);
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

    const QString strategyId = strategy.value("strategy_id").toString();
    const QString strategyName = strategy.value("strategy_name").toString();
    engine::EventFormat signalEvent = engine::EventFormat::create_from_strings(
        engine::EventTypes::STRATEGY_SIGNAL,
        "STRATEGY_SERVICE",
        0);
    signalEvent.correlation_id = eventId.toStdString();
    signalEvent.set("strategy_id", strategyId.toStdString());
    signalEvent.set("strategy_name", strategyName.toStdString());
    signalEvent.set("symbol", symbol.toStdString());
    signalEvent.set("action", action.toStdString());
    signalEvent.set("price", latestPrice);
    signalEvent.set("reference_price", referencePrice);
    signalEvent.set("strength", strength);
    signalEvent.metadata["strategy_id"] = strategyId.toStdString();
    signalEvent.metadata["strategy_name"] = strategyName.toStdString();
    signalEvent.metadata["symbol"] = symbol.toStdString();
    signalEvent.metadata["action"] = action.toStdString();
    signalEvent.metadata["price"] = QString::number(latestPrice, 'f', 6).toStdString();
    signalEvent.metadata["reference_price"] = QString::number(referencePrice, 'f', 6).toStdString();
    signalEvent.metadata["strength"] = QString::number(strength, 'f', 6).toStdString();
    signalEvent.metadata["market_event_type"] = marketEventType.toStdString();
    signalEvent.metadata["strategy_type"] = normalizedStrategyType(strategy).toStdString();

    const auto result = bus->publish(signalEvent, static_cast<int>(engine::EventPriority::NORMAL));
    if (!result) {
        qWarning() << "StrategyService: failed to publish strategy signal" << QString::fromStdString(result.message);
        return;
    }

    QVariantMap signalData;
    signalData.insert("strategyId", strategyId);
    signalData.insert("strategyName", strategyName);
    signalData.insert("symbol", symbol);
    signalData.insert("action", action);
    signalData.insert("price", latestPrice);
    signalData.insert("referencePrice", referencePrice);
    signalData.insert("strength", strength);
    signalData.insert("marketEventType", marketEventType);
    emit strategySignalPublished(signalData);
}

QString StrategyService::determineSignalAction(const QVariantMap& strategy,
                                              double latestPrice,
                                              double referencePrice) const
{
    if (latestPrice <= 0.0 || referencePrice <= 0.0) {
        return {};
    }

    const QString strategyType = normalizedStrategyType(strategy);
    if (strategyType == "TREND") {
        return latestPrice >= referencePrice ? QStringLiteral("BUY") : QStringLiteral("SELL");
    }
    if (strategyType == "MEAN_REVERSION") {
        return latestPrice < referencePrice ? QStringLiteral("BUY") : QStringLiteral("SELL");
    }
    if (strategyType == "PORTFOLIO") {
        return latestPrice >= referencePrice ? QStringLiteral("BUY") : QStringLiteral("SELL");
    }

    const double delta = latestPrice - referencePrice;
    if (std::fabs(delta) < 1e-6) {
        return {};
    }
    return delta > 0.0 ? QStringLiteral("BUY") : QStringLiteral("SELL");
}

double StrategyService::determineSignalStrength(const QVariantMap& strategy,
                                                double latestPrice,
                                                double referencePrice) const
{
    Q_UNUSED(strategy);
    if (referencePrice <= 0.0) {
        return 0.0;
    }
    return std::fabs(latestPrice - referencePrice) / referencePrice;
}

QString StrategyService::createStrategy(const QVariantMap& strategyData) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QString();
    }

    QString strategyId;
    QVariantMap completeStrategy;
    {
        QWriteLocker locker(&m_rwLock);

        // 验证策略数据
        QString errorMessage;
        if (!validateStrategyData(strategyData, errorMessage)) {
            qWarning() << "StrategyService: 策略数据验证失败 -" << errorMessage;
            emit errorOccurred(errorMessage);
            return QString();
        }

        // 生成策略代码
        QString strategyCode = generateStrategyCode(
            strategyData.value("strategy_name").toString(),
            strategyData.value("strategy_type").toString()
        );

        // 生成策略ID（使用生成的策略代码作为ID）
        strategyId = generateStrategyId(strategyData.value("strategy_name").toString());

        // 构建完整的策略数据
        completeStrategy = strategyData;
        completeStrategy["strategy_id"] = strategyId;
        completeStrategy["strategy_code"] = strategyCode;

        // 设置默认状态，数据库只支持：'ACTIVE', 'INACTIVE', 'TESTING', 'ARCHIVED'
        completeStrategy["status"] = normalizePersistedStatus(strategyData.value("status").toString());

        completeStrategy["created_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        completeStrategy["updated_at"] = completeStrategy["created_at"];

        QString savedStrategyId = saveStrategyToDatabase(completeStrategy);
        if (savedStrategyId.isEmpty()) {
            qWarning() << "StrategyService: 保存策略到数据库失败";
            emit errorOccurred("保存策略到数据库失败");
            return QString();
        }

        strategyId = savedStrategyId;
        completeStrategy["strategy_id"] = strategyId;
        saveStrategyToCache(strategyId, completeStrategy);
    }

    if (m_viewModel) {
        m_viewModel->appendData(completeStrategy);
    }
    
    // 发送信号
    emit strategyCreated(strategyId, completeStrategy);
    emit dataChanged();
    
    qDebug() << "StrategyService: 创建策略成功 - ID:" << strategyId << "名称:" 
             << strategyData.value("strategy_name").toString();
    
    return strategyId;
}

bool StrategyService::updateStrategy(const QString& strategyId, const QVariantMap& strategyData) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    QVariantMap updatedStrategy;
    {
        QWriteLocker locker(&m_rwLock);

        QString errorMessage;
        if (!validateStrategyData(strategyData, errorMessage)) {
            qWarning() << "StrategyService: 策略数据验证失败 -" << errorMessage;
            emit errorOccurred(errorMessage);
            return false;
        }

        QVariantMap existingStrategy = loadStrategyFromCache(strategyId);
        if (existingStrategy.isEmpty()) {
            qWarning() << "StrategyService: 策略不存在 - ID:" << strategyId;
            return false;
        }

        updatedStrategy = existingStrategy;
        for (auto it = strategyData.begin(); it != strategyData.end(); ++it) {
            updatedStrategy[it.key()] = it.value();
        }

        updatedStrategy["status"] = normalizePersistedStatus(updatedStrategy.value("status").toString());

        updatedStrategy["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

        if (!updateStrategyInDatabase(strategyId, updatedStrategy)) {
            qWarning() << "StrategyService: 更新策略到数据库失败";
            emit errorOccurred("更新策略到数据库失败");
            return false;
        }

        saveStrategyToCache(strategyId, updatedStrategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategy(strategyId, updatedStrategy);
    }
    
    // 发送信号
    emit strategyUpdated(strategyId, updatedStrategy);
    emit dataChanged();
    
    qDebug() << "StrategyService: 更新策略成功 - ID:" << strategyId;
    
    return true;
}

bool StrategyService::deleteStrategy(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    {
        QWriteLocker locker(&m_rwLock);

        if (!deleteStrategyFromDatabase(strategyId)) {
            qWarning() << "StrategyService: 从数据库删除策略失败 - ID:" << strategyId;
            emit errorOccurred(QString("从数据库删除策略失败: %1").arg(strategyId));
            return false;
        }

        removeStrategyFromCache(strategyId);
    }

    if (m_viewModel) {
        m_viewModel->removeStrategy(strategyId);
    }
    
    // 发送信号
    emit strategyDeleted(strategyId);
    emit dataChanged();
    
    qDebug() << "StrategyService: 删除策略成功 - ID:" << strategyId;
    
    return true;
}

QVariantMap StrategyService::getStrategyById(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantMap();
    }
    
    QReadLocker locker(&m_rwLock);
    
    // 从缓存获取
    QVariantMap strategy = loadStrategyFromCache(strategyId);
    if (!strategy.isEmpty()) {
        return strategy;
    }
    
    // 从数据库获取
    strategy = m_repository->findById(strategyId);
    if (!strategy.isEmpty()) {
        saveStrategyToCache(strategyId, strategy);
    }
    
    return strategy;
}

QVariantMap StrategyService::getStrategyByCode(const QString& strategyCode) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantMap();
    }
    
    QReadLocker locker(&m_rwLock);
    
    // 从数据库获取
    QVariantMap strategy = m_repository->findByCode(strategyCode);
    if (!strategy.isEmpty()) {
        QString strategyId = strategy.value("strategy_id").toString();
        saveStrategyToCache(strategyId, strategy);
    }
    
    return strategy;
}

QVariantList StrategyService::getAllStrategies() {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantList();
    }
    
    QReadLocker locker(&m_rwLock);
    
    // 从缓存构建列表
    return buildStrategyListFromCache(m_memoryCache);
}

QVariantList StrategyService::getStrategiesByType(const QString& strategyType) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantList();
    }
    
    QReadLocker locker(&m_rwLock);
    
    QVariantList result;
    for (const QVariantMap& strategy : m_memoryCache.values()) {
        if (matchesStrategyType(strategy, strategyType)) {
            result.append(strategy);
        }
    }
    
    return result;
}

QVariantList StrategyService::getStrategiesByStatus(const QString& status) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantList();
    }
    
    QReadLocker locker(&m_rwLock);
    
    QVariantList result;
    for (const QVariantMap& strategy : m_memoryCache.values()) {
        if (matchesStrategyStatus(strategy, status)) {
            result.append(strategy);
        }
    }
    
    return result;
}

QVariantList StrategyService::searchStrategies(const QString& keyword) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantList();
    }
    
    QReadLocker locker(&m_rwLock);
    
    QVariantList result;
    for (const QVariantMap& strategy : m_memoryCache.values()) {
        if (matchesStrategyKeyword(strategy, keyword)) {
            result.append(strategy);
        }
    }
    
    return result;
}

bool StrategyService::importStrategies(const QVariantList& strategies) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QWriteLocker locker(&m_rwLock);
    
    QVariantList failedStrategies;
    std::vector<QVariantMap> successStrategies;
    
    for (const QVariant& strategyVar : strategies) {
        QVariantMap strategyData = strategyVar.toMap();
        
        QString errorMessage;
        if (!validateStrategyData(strategyData, errorMessage)) {
            strategyData["error"] = errorMessage;
            failedStrategies.append(strategyData);
            continue;
        }
        
        // 生成策略代码（策略ID由数据库自增生成）
        if (!strategyData.contains("strategy_code")) {
            QString strategyCode = generateStrategyCode(
                strategyData.value("strategy_name").toString(),
                strategyData.value("strategy_type").toString()
            );
            strategyData["strategy_code"] = strategyCode;
        }
        
        // 保存到数据库
        QString savedStrategyId = saveStrategyToDatabase(strategyData);
        if (savedStrategyId.isEmpty()) {
            strategyData["error"] = "保存到数据库失败";
            failedStrategies.append(strategyData);
            continue;
        }
        
        // 使用数据库返回的ID
        strategyData["strategy_id"] = savedStrategyId;
        
        successStrategies.push_back(strategyData);
    }
    
    // 更新缓存
    if (!successStrategies.empty()) {
        updateCacheBatch(successStrategies);
        refreshViewModelFromCache(m_viewModel, m_memoryCache);
        emit dataChanged();
    }
    
    // 发送失败信号
    if (!failedStrategies.isEmpty()) {
        emit importFailed(failedStrategies);
    }
    
    return failedStrategies.isEmpty();
}

bool StrategyService::exportStrategies(const QString& format, const QString& filePath) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QReadLocker locker(&m_rwLock);
    
    // 获取所有策略
    QVariantList strategies = getAllStrategies();
    
    // 构建导出数据
    QVariantMap exportData;
    exportData["version"] = "1.0";
    exportData["export_date"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    exportData["strategy_count"] = strategies.size();
    exportData["strategies"] = strategies;
    
    // 转换为JSON
    QJsonDocument doc = QJsonDocument::fromVariant(exportData);
    QByteArray jsonData = doc.toJson(QJsonDocument::Indented);
    
    // 保存到文件
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "StrategyService: 无法打开文件 -" << filePath << "错误:" << file.errorString();
        emit errorOccurred(QString("无法打开文件: %1").arg(file.errorString()));
        return false;
    }
    
    file.write(jsonData);
    file.close();
    
    qDebug() << "StrategyService: 导出策略成功 - 数量:" << strategies.size() << "路径:" << filePath;
    
    return true;
}

bool StrategyService::activateStrategy(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QWriteLocker locker(&m_rwLock);
    
    // 更新状态 - 使用通用update方法
    QVariantMap updateData;
    updateData["status"] = "ACTIVE";
    updateData["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    if (!m_repository->update(strategyId, updateData)) {
        qWarning() << "StrategyService: 激活策略失败 - ID:" << strategyId;
        emit errorOccurred(QString("激活策略失败: %1").arg(strategyId));
        return false;
    }
    
    // 更新缓存
    QVariantMap strategy = loadStrategyFromCache(strategyId);
    if (!strategy.isEmpty()) {
        strategy["status"] = "ACTIVE";
        strategy["updated_at"] = updateData["updated_at"];
        saveStrategyToCache(strategyId, strategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategyStatus(strategyId, "ACTIVE");
    }
    
    emit strategyActivated(strategyId);
    emit dataChanged();
    
    return true;
}

bool StrategyService::deactivateStrategy(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QWriteLocker locker(&m_rwLock);
    
    // 更新状态 - 使用通用update方法
    QVariantMap updateData;
    updateData["status"] = "INACTIVE";
    updateData["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    if (!m_repository->update(strategyId, updateData)) {
        qWarning() << "StrategyService: 停用策略失败 - ID:" << strategyId;
        emit errorOccurred(QString("停用策略失败: %1").arg(strategyId));
        return false;
    }
    
    // 更新缓存
    QVariantMap strategy = loadStrategyFromCache(strategyId);
    if (!strategy.isEmpty()) {
        strategy["status"] = "INACTIVE";
        strategy["updated_at"] = updateData["updated_at"];
        saveStrategyToCache(strategyId, strategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategyStatus(strategyId, "INACTIVE");
    }
    
    emit strategyDeactivated(strategyId);
    emit dataChanged();
    
    return true;
}

bool StrategyService::archiveStrategy(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QWriteLocker locker(&m_rwLock);
    
    // 更新状态 - 使用通用update方法
    QVariantMap updateData;
    updateData["status"] = "ARCHIVED";
    updateData["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    if (!m_repository->update(strategyId, updateData)) {
        qWarning() << "StrategyService: 归档策略失败 - ID:" << strategyId;
        emit errorOccurred(QString("归档策略失败: %1").arg(strategyId));
        return false;
    }
    
    // 更新缓存
    QVariantMap strategy = loadStrategyFromCache(strategyId);
    if (!strategy.isEmpty()) {
        strategy["status"] = "ARCHIVED";
        strategy["updated_at"] = updateData["updated_at"];
        saveStrategyToCache(strategyId, strategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategyStatus(strategyId, "ARCHIVED");
    }
    
    emit dataChanged();
    
    return true;
}

bool StrategyService::duplicateStrategy(const QString& sourceStrategyId, const QString& newName) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QReadLocker locker(&m_rwLock);
    
    // 获取源策略
    QVariantMap sourceStrategy = getStrategyById(sourceStrategyId);
    if (sourceStrategy.isEmpty()) {
        qWarning() << "StrategyService: 源策略不存在 - ID:" << sourceStrategyId;
        emit errorOccurred(QString("源策略不存在: %1").arg(sourceStrategyId));
        return false;
    }
    
    // 创建副本
    QVariantMap newStrategy = sourceStrategy;
    
    // 更新名称
    newStrategy["strategy_name"] = newName;
    
    // 生成新的代码（策略ID由数据库自动生成）
    QString newStrategyCode = generateStrategyCode(
        newName,
        newStrategy.value("strategy_type").toString()
    );
    
    // 移除ID，让数据库自动生成
    newStrategy.remove("strategy_id");
    newStrategy["strategy_code"] = newStrategyCode;
    
    // 重置状态和时间
    newStrategy["status"] = "DRAFT";
    newStrategy["created_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    newStrategy["updated_at"] = newStrategy["created_at"];
    
    // 移除性能指标
    newStrategy.remove("performance_metrics");
    
    // 创建新策略
    return createStrategy(newStrategy) != QString();
}

bool StrategyService::updateStrategyParameters(const QString& strategyId, const QVariantMap& parameters) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }
    
    QWriteLocker locker(&m_rwLock);
    
    // 获取现有策略
    QVariantMap existingStrategy = loadStrategyFromCache(strategyId);
    if (existingStrategy.isEmpty()) {
        existingStrategy = m_repository->findById(strategyId);
        if (existingStrategy.isEmpty()) {
            qWarning() << "StrategyService: 策略不存在 - ID:" << strategyId;
            return false;
        }
    }
    
    // 构建更新数据
    QVariantMap updateData;
    updateData["parameters"] = parameters;
    updateData["updated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    // 更新数据库 - 使用通用update方法
    if (!m_repository->update(strategyId, updateData)) {
        qWarning() << "StrategyService: 更新策略参数失败 - ID:" << strategyId;
        emit errorOccurred(QString("更新策略参数失败: %1").arg(strategyId));
        return false;
    }
    
    // 更新缓存
    QVariantMap strategy = loadStrategyFromCache(strategyId);
    if (!strategy.isEmpty()) {
        strategy["parameters"] = parameters;
        strategy["updated_at"] = updateData["updated_at"];
        saveStrategyToCache(strategyId, strategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategy(strategyId, strategy);
    }
    
    emit dataChanged();
    
    return true;
}

QVariantMap StrategyService::getStrategyParameters(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantMap();
    }
    
    QReadLocker locker(&m_rwLock);
    
    QVariantMap strategy = getStrategyById(strategyId);
    if (strategy.isEmpty()) {
        return QVariantMap();
    }
    
    return strategy.value("parameters").toMap();
}

bool StrategyService::updateStrategyPerformance(const QString& strategyId, const QVariantMap& performance) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return false;
    }

    QVariantMap strategy;
    QString updatedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    QVariantMap mergedPerformance;
    {
        QWriteLocker locker(&m_rwLock);

        QVariantMap existingStrategy = loadStrategyFromCache(strategyId);
        if (existingStrategy.isEmpty()) {
            existingStrategy = m_repository->findById(strategyId);
            if (existingStrategy.isEmpty()) {
                qWarning() << "StrategyService: 策略不存在 - ID:" << strategyId;
                return false;
            }
        }

        mergedPerformance = mergePerformanceMetrics(existingStrategy, performance, updatedAt);

        QVariantMap updateData;
        updateData["performance_metrics"] = mergedPerformance;
        updateData["updated_at"] = updatedAt;

        if (!m_repository->update(strategyId, updateData)) {
            qWarning() << "StrategyService: 更新策略性能失败 - ID:" << strategyId;
            emit errorOccurred(QString("更新策略性能失败: %1").arg(strategyId));
            return false;
        }

        strategy = existingStrategy;
        strategy["performance_metrics"] = mergedPerformance;
        strategy["updated_at"] = updatedAt;
        saveStrategyToCache(strategyId, strategy);
    }

    if (m_viewModel) {
        m_viewModel->updateStrategyPerformance(strategyId, mergedPerformance);
    }

    emit dataChanged();

    return true;
}

QVariantMap StrategyService::getStrategyPerformance(const QString& strategyId) {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return QVariantMap();
    }
    
    QReadLocker locker(&m_rwLock);
    
    QVariantMap strategy = getStrategyById(strategyId);
    if (strategy.isEmpty()) {
        return QVariantMap();
    }
    
    return strategy.value("performance_metrics").toMap();
}

void StrategyService::syncWithDatabase() {
    if (!m_initialized.load()) {
        qWarning() << "StrategyService: 服务未初始化";
        return;
    }

    {
        QWriteLocker locker(&m_rwLock);
        clearAllCache();
    }

    loadStrategiesFromDatabase();

    if (m_viewModel) {
        refreshViewModelFromCache(m_viewModel, m_memoryCache);
    }
    emit dataChanged();
    
    qDebug() << "StrategyService: 与数据库同步完成";
}

void StrategyService::clearCache() {
    QWriteLocker locker(&m_rwLock);
    clearAllCache();
    m_cacheLoaded = false;
    emit cacheLoadedChanged();
    qDebug() << "StrategyService: 缓存已清除";
}

QVariantMap StrategyService::createDefaultStrategy(const QString& strategyType, const QString& strategyName) {
    QString name = strategyName.isEmpty() ? QString("%1策略").arg(strategyType) : strategyName;
    
    // 根据前端类型映射后端类型
    QString backendType = "CUSTOM";
    QString displayName = "自定义";
    
    if (STRATEGY_TYPE_MAPPING.contains(strategyType)) {
        backendType = STRATEGY_TYPE_MAPPING[strategyType][0];
        displayName = STRATEGY_TYPE_MAPPING[strategyType][1];
    }
    
    QVariantMap strategy;
    
    if (backendType == "TREND") {
        strategy = createTrendFollowingStrategy(name);
    } else if (backendType == "MEAN_REVERSION") {
        strategy = createMeanReversionStrategy(name);
    } else if (backendType == "ALPHA") {
        strategy = createAlphaStrategy(name);
    } else if (backendType == "ARBITRAGE") {
        strategy = createArbitrageStrategy(name);
    } else {
        strategy = createCustomStrategy(name, backendType);
    }

    QVariantMap parameters = strategy.value("parameters").toMap();
    parameters["strategy_subtype"] = strategyType;
    if (strategyType == "machine_learning") {
        parameters["feature_window"] = parameters.value("feature_window", 60);
        parameters["prediction_days"] = parameters.value("prediction_days", 1);
        parameters["training_days"] = parameters.value("training_days", 1000);
        parameters["confidence_threshold"] = parameters.value("confidence_threshold", 0.6);
    } else if (strategyType == "multi_factor") {
        parameters["factor_types"] = parameters.value("factor_types", QStringList{"value", "quality", "growth", "momentum"});
    } else if (strategyType == "high_frequency") {
        parameters["execution_timeframe"] = parameters.value("execution_timeframe", "5min");
    } else if (strategyType == "event_driven") {
        parameters["event_types"] = parameters.value("event_types", QStringList{"earnings_release", "merger_announcement"});
    } else if (strategyType == "custom") {
        parameters["custom_code"] = parameters.value("custom_code", "# custom strategy\n");
    }
    strategy["parameters"] = parameters;
    strategy["sub_type"] = strategyType;
    
    // 添加描述
    strategy["description"] = QString("%1 - %2").arg(name).arg(STRATEGY_TYPE_DESCRIPTIONS.value(backendType, ""));
    
    return strategy;
}

QVariantMap StrategyService::getStrategyTemplate(const QString& strategyType) {
    return createDefaultStrategy(strategyType, "");
}

QStringList StrategyService::getAvailableStrategyTypes() {
    return STRATEGY_TYPE_MAPPING.keys();
}

QVariantMap StrategyService::getStrategyTypeDescriptions() {
    QVariantMap descriptions;
    for (auto it = STRATEGY_TYPE_DESCRIPTIONS.begin(); it != STRATEGY_TYPE_DESCRIPTIONS.end(); ++it) {
        descriptions[it.key()] = it.value();
    }
    return descriptions;
}

// ============ 私有方法实现 ============

void StrategyService::initializeRepository() {
    // 首先确保数据库连接管理器已初始化
    // 这会配置ConnectionPool，确保StrategyRepository使用的连接池有正确的配置
    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    if (!dbManager.initialize()) {
        qWarning() << "StrategyService::initializeRepository: Database connection manager initialization failed";
        throw std::runtime_error("数据库连接初始化失败");
    }
    
    qDebug() << "✅ StrategyService::initializeRepository: Database connection manager initialized";
    
    // 创建策略仓储实例
    m_repository = std::make_shared<StrategyRepository>();
    if (!m_repository->initialize()) {
        throw std::runtime_error("策略仓储初始化失败");
    }
    
    qDebug() << "✅ StrategyService::initializeRepository: Repository initialized successfully";
}

QString StrategyService::saveStrategyToDatabase(const QVariantMap& strategyData) {
    if (!m_repository) {
        return QString();
    }
    return m_repository->save(strategyData);
}

bool StrategyService::updateStrategyInDatabase(const QString& strategyId, const QVariantMap& strategyData) {
    if (!m_repository) {
        return false;
    }
    return m_repository->update(strategyId, strategyData);
}

bool StrategyService::deleteStrategyFromDatabase(const QString& strategyId) {
    if (!m_repository) {
        return false;
    }
    return m_repository->remove(strategyId);
}

QVariantList StrategyService::loadStrategiesFromDatabase() {
    qDebug() << "=======================================";
    qDebug() << "StrategyService::loadStrategiesFromDatabase: 开始加载策略数据";
    qDebug() << "=======================================";
    QVariantList strategies;
    
    if (!m_repository) {
        qWarning() << "StrategyService::loadStrategiesFromDatabase: 仓储未初始化";
        qDebug() << "m_repository 为空指针";
        return strategies;
    }
    
    try {
        qDebug() << "调用 m_repository->findAll()...";
        auto strategyMaps = m_repository->findAll();
        qDebug() << "数据库查询返回" << strategyMaps.size() << "个策略";
        
        if (strategyMaps.empty()) {
            qWarning() << "数据库查询返回空结果集";
        } else {
            // 输出前几个策略的信息用于调试
            for (size_t i = 0; i < std::min(strategyMaps.size(), (size_t)3); ++i) {
                const auto& strategy = strategyMaps[i];
                qDebug() << "策略" << i << ":";
                qDebug() << "  ID:" << strategy.value("strategy_id").toString();
                qDebug() << "  名称:" << strategy.value("strategy_name").toString();
                qDebug() << "  类型:" << strategy.value("strategy_type").toString();
                qDebug() << "  状态:" << strategy.value("status").toString();
            }
        }
        
        // 清空缓存
        qDebug() << "清空内存缓存...";
        m_memoryCache.clear();
        
        // 加载到缓存
        qDebug() << "加载策略到缓存...";
        for (const auto& strategy : strategyMaps) {
            QString strategyId = strategy.value("strategy_id").toString();
            if (!strategyId.isEmpty()) {
                m_memoryCache[strategyId] = strategy;
            }
            strategies.append(strategy);
        }
        
        m_cacheLoaded = true;
        qDebug() << "缓存状态更新，发出cacheLoadedChanged信号";
        emit cacheLoadedChanged();

        // 发出加载完成信号
        qDebug() << "发出strategiesLoaded信号，策略数量:" << strategies.size();
        emit strategiesLoaded(strategies);
        
        qDebug() << "StrategyService::loadStrategiesFromDatabase: 加载完成";
        qDebug() << "缓存策略数量:" << m_memoryCache.size();
        qDebug() << "=======================================";
        return strategies;
        
    } catch (const std::exception& e) {
        qCritical() << "StrategyService::loadStrategiesFromDatabase: Error:" << e.what();
        qDebug() << "=======================================";
        return QVariantList();
    }
}

void StrategyService::saveStrategyToCache(const QString& strategyId, const QVariantMap& strategyData) {
    m_memoryCache[strategyId] = strategyData;
}

QVariantMap StrategyService::loadStrategyFromCache(const QString& strategyId) {
    return m_memoryCache.value(strategyId);
}

void StrategyService::removeStrategyFromCache(const QString& strategyId) {
    m_memoryCache.remove(strategyId);
}

void StrategyService::clearAllCache() {
    m_memoryCache.clear();
}

void StrategyService::updateCacheBatch(const std::vector<QVariantMap>& strategies) {
    for (const auto& strategy : strategies) {
        QString strategyId = strategy.value("strategy_id").toString();
        if (!strategyId.isEmpty()) {
            m_memoryCache[strategyId] = strategy;
        }
    }
}

bool StrategyService::validateStrategyData(const QVariantMap& strategyData, QString& errorMessage) {
    // 检查必填字段
    if (!strategyData.contains("strategy_name") || strategyData.value("strategy_name").toString().isEmpty()) {
        errorMessage = "策略名称不能为空";
        return false;
    }
    
    if (!strategyData.contains("strategy_type") || strategyData.value("strategy_type").toString().isEmpty()) {
        errorMessage = "策略类型不能为空";
        return false;
    }
    
    // 验证策略类型
    QString strategyType = strategyData.value("strategy_type").toString();
    QStringList validTypes = {"TREND", "MEAN_REVERSION", "ALPHA", "ARBITRAGE", "HFT", "PORTFOLIO", "CUSTOM"};
    if (!validTypes.contains(strategyType)) {
        errorMessage = QString("无效的策略类型: %1").arg(strategyType);
        return false;
    }
    
    // 验证状态（如果提供）
    if (strategyData.contains("status")) {
        QString status = strategyData.value("status").toString();
        // 数据库只支持：'ACTIVE', 'INACTIVE', 'TESTING', 'ARCHIVED'
        // 将DRAFT转换为ACTIVE，其他无效状态也转换
        QStringList validStatuses = {"ACTIVE", "INACTIVE", "TESTING", "ARCHIVED"};
        if (!validStatuses.contains(status)) {
            // 如果状态无效，转换为ACTIVE并记录警告
            qWarning() << "StrategyService: 转换无效状态" << status << "为ACTIVE";
            // 不返回错误，而是转换状态
        }
    }
    
    return true;
}

QString StrategyService::generateStrategyId(const QString& strategyName) {
    // 使用名称哈希加随机数生成ID
    QString nameHash = QString::number(qHash(strategyName));
    QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString random = QString::number(QRandomGenerator::global()->generate());
    
    return QString("STR_%1_%2_%3").arg(nameHash.left(6)).arg(timestamp.right(6)).arg(random.right(4));
}

QString StrategyService::generateStrategyCode(const QString& strategyName, const QString& strategyType) {
    // 生成简写代码
    QString codePrefix;
    if (strategyType == "TREND") codePrefix = "TRD";
    else if (strategyType == "MEAN_REVERSION") codePrefix = "MR";
    else if (strategyType == "ALPHA") codePrefix = "ALPHA";
    else if (strategyType == "ARBITRAGE") codePrefix = "ARB";
    else codePrefix = "CST";
    
    // 使用名称的前几个字符，转换为大写，移除空格
    QString namePart = strategyName.left(6).toUpper().replace(" ", "_").replace("-", "_");
    
    // 添加时间戳确保唯一性
    QString timestamp = QDateTime::currentDateTime().toString("yyMMddHHmm");
    
    return QString("%1_%2_%3").arg(codePrefix).arg(namePart).arg(timestamp);
}

QVariantMap StrategyService::createTrendFollowingStrategy(const QString& name) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategy_type"] = "TREND";
    strategy["description"] = QString("趋势跟踪策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["fast_period"] = 10;
    parameters["slow_period"] = 30;
    parameters["position_size"] = 0.1;
    parameters["stop_loss"] = 0.05;
    parameters["take_profit"] = 0.15;
    strategy["parameters"] = parameters;
    
    return strategy;
}

QVariantMap StrategyService::createMeanReversionStrategy(const QString& name) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategy_type"] = "MEAN_REVERSION";
    strategy["description"] = QString("均值回归策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["boll_period"] = 20;
    parameters["boll_std"] = 2.0;
    parameters["position_size"] = 0.1;
    parameters["reversion_threshold"] = 0.5;
    strategy["parameters"] = parameters;
    
    return strategy;
}

QVariantMap StrategyService::createAlphaStrategy(const QString& name) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategy_type"] = "ALPHA";
    strategy["description"] = QString("阿尔法策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["top_n"] = 10;
    parameters["rebalance_days"] = 20;
    parameters["momentum_period"] = 60;
    parameters["position_size"] = 0.1;
    strategy["parameters"] = parameters;
    
    return strategy;
}

QVariantMap StrategyService::createArbitrageStrategy(const QString& name) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategy_type"] = "ARBITRAGE";
    strategy["description"] = QString("套利策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["spread_threshold"] = 0.02;
    parameters["entry_z_score"] = 2.0;
    parameters["exit_z_score"] = 0.5;
    strategy["parameters"] = parameters;
    
    return strategy;
}

QVariantMap StrategyService::createCustomStrategy(const QString& name, const QString& type) {
    QVariantMap strategy;
    strategy["strategy_name"] = name;
    strategy["strategy_type"] = type;
    strategy["description"] = QString("自定义策略 - %1").arg(name);
    strategy["author"] = "系统";
    strategy["language"] = "Python";
    strategy["version"] = "1.0";
    
    // 默认参数
    QVariantMap parameters;
    parameters["custom_param1"] = "value1";
    parameters["custom_param2"] = 0.5;
    strategy["parameters"] = parameters;
    
    return strategy;
}

StrategyViewModel* StrategyService::getViewModel() { 
    // 确保ViewModel总是有效
    if (!m_viewModel) {
        // 创建ViewModel作为Service的子对象
        m_viewModel = new StrategyViewModel(this);
        qDebug() << "StrategyService: 创建默认视图模型，地址:" << m_viewModel;
    }
    return m_viewModel; 
}

void StrategyService::setViewModel(StrategyViewModel* viewModel) { 
    if (m_viewModel && m_viewModel != viewModel) {
        // 删除旧的ViewModel，如果它是Service的子对象
        if (m_viewModel->parent() == this) {
            m_viewModel->deleteLater();
        }
    }
    m_viewModel = viewModel; 
    qDebug() << "StrategyService: 设置视图模型，地址:" << m_viewModel;
}
