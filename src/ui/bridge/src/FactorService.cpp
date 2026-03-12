// FactorService.cpp
// 因子服务层实现 - 负责业务逻辑

#include "../../ui/bridge/include/FactorService.h"
#include "../../ui/bridge/include/DatabaseConnectionManager.h"
#include "../../infrastructure/include/database/FactorRepository.h"
#include "../../infrastructure/include/database/DatabaseConfig.h"
#include "../../ui/bridge/include/DataServiceCache.h"
#include <QDebug>
#include <QDateTime>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>

using namespace astock::database;

FactorService::FactorService(QObject* parent)
    : QObject(parent)
    , m_repository(nullptr)
    , m_initialized(false)
{
    qDebug() << "FactorService constructor";
}

FactorService::~FactorService()
{
    qDebug() << "FactorService destructor";
}

void FactorService::initialize()
{
    qDebug() << "FactorService::initialize: 开始初始化";
    
    if (m_initialized) {
        qDebug() << "FactorService::initialize: 已经初始化，跳过";
        return;
    }
    
    try {
        // 初始化仓储
        initializeRepository();
        
        if (!m_repository) {
            qWarning() << "FactorService::initialize: 仓储初始化失败";
            emit errorOccurred("因子服务初始化失败：数据库连接错误");
            return;
        }
        
        m_initialized = true;
        qDebug() << "✅ FactorService::initialize: 因子服务初始化完成";
        
        // 初始化完成后自动加载数据（延迟执行，避免阻塞UI）
        QTimer::singleShot(0, this, [this]() {
            qDebug() << "FactorService::initialize: 自动加载因子数据";
            loadFactorsFromDatabase();
        });
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::initialize: Error:" << e.what();
        emit errorOccurred(QString("因子服务初始化失败: %1").arg(e.what()));
    }
}

QString FactorService::addFactor(const QVariantMap& factorData)
{
    qDebug() << "FactorService::addFactor 开始，数据:" << factorData;
    
    // 验证数据
    QString errorMessage;
    if (!validateFactorData(factorData, errorMessage)) {
        qWarning() << "因子数据验证失败:" << errorMessage;
        emit errorOccurred(errorMessage);
        return QString();
    }
    
    // 生成因子ID（如果未提供）
    QVariantMap dataToSave = factorData;
    if (!dataToSave.contains("factorId") || dataToSave["factorId"].toString().isEmpty()) {
        QString factorName = dataToSave["factorName"].toString();
        QString factorId = generateFactorId(factorName);
        dataToSave["factorId"] = factorId;
    }
    
    QString factorId = dataToSave["factorId"].toString();
    
    // 保存到数据库
    bool dbSuccess = saveFactorToDatabase(dataToSave);
    if (!dbSuccess) {
        QString errorMsg = QString("因子保存到数据库失败: %1").arg(factorId);
        qWarning() << errorMsg;
        emit errorOccurred(errorMsg);
        return QString();
    }
    
    // 保存到缓存
    saveFactorToCache(factorId, dataToSave);
    
    // 发出信号通知视图层
    emit factorAdded(factorId, dataToSave);
    emit dataChanged();
    
    qDebug() << "FactorService::addFactor 结束，新增因子ID:" << factorId;
    return factorId;
}

bool FactorService::updateFactor(const QString& factorId, const QVariantMap& factorData)
{
    qDebug() << "FactorService::updateFactor 开始，因子ID:" << factorId;
    
    // 验证数据
    QString errorMessage;
    if (!validateFactorData(factorData, errorMessage)) {
        qWarning() << "因子数据验证失败:" << errorMessage;
        emit errorOccurred(errorMessage);
        return false;
    }
    
    // 确保因子ID一致
    QVariantMap dataToUpdate = factorData;
    dataToUpdate["factorId"] = factorId;
    
    // 更新数据库
    bool dbSuccess = updateFactorInDatabase(factorId, dataToUpdate);
    if (!dbSuccess) {
        QString errorMsg = QString("因子更新到数据库失败: %1").arg(factorId);
        qWarning() << errorMsg;
        emit errorOccurred(errorMsg);
        return false;
    }
    
    // 更新缓存
    saveFactorToCache(factorId, dataToUpdate);
    
    // 发出信号通知视图层
    emit factorUpdated(factorId, dataToUpdate);
    emit dataChanged();
    
    qDebug() << "FactorService::updateFactor 结束，更新因子ID:" << factorId;
    return true;
}

bool FactorService::deleteFactor(const QString& factorId)
{
    qDebug() << "FactorService::deleteFactor 开始，因子ID:" << factorId;
    
    // 从数据库删除
    bool dbSuccess = deleteFactorFromDatabase(factorId);
    if (!dbSuccess) {
        QString errorMsg = QString("因子从数据库删除失败: %1").arg(factorId);
        qWarning() << errorMsg;
        emit errorOccurred(errorMsg);
        return false;
    }
    
    // 从缓存删除
    removeFactorFromCache(factorId);
    
    // 发出信号通知视图层
    emit factorDeleted(factorId);
    emit dataChanged();
    
    qDebug() << "FactorService::deleteFactor 结束，删除因子ID:" << factorId;
    return true;
}

QVariantMap FactorService::getFactorById(const QString& factorId)
{
    qDebug() << "FactorService::getFactorById 开始，因子ID:" << factorId;
    
    // 首先尝试从缓存获取
    QVariantMap cachedFactor = loadFactorFromCache(factorId);
    if (!cachedFactor.isEmpty()) {
        qDebug() << "从缓存获取因子:" << factorId;
        return cachedFactor;
    }
    
    // 缓存中没有，从数据库获取
    if (!m_repository) {
        qWarning() << "FactorService::getFactorById: Repository not initialized";
        return QVariantMap();
    }
    
    try {
        QVariantMap factorMap = m_repository->findById(factorId);
        if (!factorMap.isEmpty()) {
            // 保存到缓存
            saveFactorToCache(factorId, factorMap);
            qDebug() << "从数据库获取因子:" << factorId;
        }
        
        return factorMap;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::getFactorById: Error:" << e.what();
        return QVariantMap();
    }
}

QVariantList FactorService::getAllFactors()
{
    qDebug() << "FactorService::getAllFactors 开始";
    
    // 从数据库加载所有因子
    QVariantList factors = loadFactorsFromDatabase();
    
    qDebug() << "FactorService::getAllFactors 结束，获取因子数量:" << factors.size();
    return factors;
}

QVariantList FactorService::getFactorsByType(const QString& type)
{
    qDebug() << "FactorService::getFactorsByType 开始，类型:" << type;
    
    QVariantList allFactors = getAllFactors();
    QVariantList result;
    
    for (const QVariant& factorVariant : allFactors) {
        QVariantMap factorMap = factorVariant.toMap();
        QString majorCategory = factorMap.value("majorCategory").toString();
        QString subCategory = factorMap.value("subCategory").toString();
        
        if (majorCategory == type || subCategory == type) {
            result.append(factorMap);
        }
    }
    
    qDebug() << "FactorService::getFactorsByType 结束，找到因子数量:" << result.size();
    return result;
}

QVariantList FactorService::searchFactors(const QString& keyword)
{
    qDebug() << "FactorService::searchFactors 开始，关键词:" << keyword;
    
    QVariantList allFactors = getAllFactors();
    QVariantList result;
    
    if (keyword.isEmpty()) {
        return allFactors;
    }
    
    QRegularExpression regex(keyword, QRegularExpression::CaseInsensitiveOption);
    
    for (const QVariant& factorVariant : allFactors) {
        QVariantMap factorMap = factorVariant.toMap();
        
        QString factorName = factorMap.value("factorName").toString();
        QString displayName = factorMap.value("displayName").toString();
        QString description = factorMap.value("description").toString();
        QString majorCategory = factorMap.value("majorCategory").toString();
        QString subCategory = factorMap.value("subCategory").toString();
        QStringList tags = factorMap.value("tags").toStringList();
        
        // 搜索因子名称、显示名称、描述、标签
        if (regex.match(factorName).hasMatch() ||
            regex.match(displayName).hasMatch() ||
            regex.match(description).hasMatch() ||
            regex.match(majorCategory).hasMatch() ||
            regex.match(subCategory).hasMatch()) {
            result.append(factorMap);
        } else {
            // 搜索标签
            for (const QString& tag : tags) {
                if (regex.match(tag).hasMatch()) {
                    result.append(factorMap);
                    break;
                }
            }
        }
    }
    
    qDebug() << "FactorService::searchFactors 结束，找到因子数量:" << result.size();
    return result;
}

QVariantList FactorService::filterFactorsByCategory(const QString& category)
{
    qDebug() << "FactorService::filterFactorsByCategory 开始，类别:" << category;
    
    QVariantList allFactors = getAllFactors();
    QVariantList result;
    
    if (category.isEmpty() || category == "all") {
        return allFactors;
    }
    
    for (const QVariant& factorVariant : allFactors) {
        QVariantMap factorMap = factorVariant.toMap();
        QString majorCategory = factorMap.value("majorCategory").toString();
        QString subCategory = factorMap.value("subCategory").toString();
        
        if (majorCategory == category || subCategory == category) {
            result.append(factorMap);
        }
    }
    
    qDebug() << "FactorService::filterFactorsByCategory 结束，找到因子数量:" << result.size();
    return result;
}

QVariantList FactorService::filterFactorsByTags(const QStringList& tags)
{
    qDebug() << "FactorService::filterFactorsByTags 开始，标签:" << tags;
    
    QVariantList allFactors = getAllFactors();
    QVariantList result;
    
    if (tags.isEmpty()) {
        return result;
    }
    
    for (const QVariant& factorVariant : allFactors) {
        QVariantMap factorMap = factorVariant.toMap();
        QStringList factorTags = factorMap.value("tags").toStringList();
        
        bool matchAll = true;
        for (const QString& tag : tags) {
            if (!factorTags.contains(tag)) {
                matchAll = false;
                break;
            }
        }
        
        if (matchAll) {
            result.append(factorMap);
        }
    }
    
    qDebug() << "FactorService::filterFactorsByTags 结束，找到因子数量:" << result.size();
    return result;
}

bool FactorService::importFactors(const QVariantList& factors)
{
    qDebug() << "FactorService::importFactors 开始，导入因子数量:" << factors.size();
    
    int successCount = 0;
    int failCount = 0;
    
    for (const QVariant& factorVariant : factors) {
        QVariantMap factorMap = factorVariant.toMap();
        
        // 验证数据
        QString errorMessage;
        if (!validateFactorData(factorMap, errorMessage)) {
            qWarning() << "因子数据验证失败:" << errorMessage;
            failCount++;
            continue;
        }
        
        // 保存因子
        QString factorId = addFactor(factorMap);
        if (!factorId.isEmpty()) {
            successCount++;
        } else {
            failCount++;
        }
    }
    
    qDebug() << "FactorService::importFactors 结束，成功:" << successCount << "失败:" << failCount;
    return successCount > 0;
}

bool FactorService::exportFactors(const QString& format, const QString& filePath)
{
    qDebug() << "FactorService::exportFactors 开始，格式:" << format << "路径:" << filePath;
    
    QVariantList allFactors = getAllFactors();
    
    // 这里可以实现导出到文件的功能
    // 暂时只记录日志
    qDebug() << "导出因子数据，格式:" << format << "路径:" << filePath;
    qDebug() << "导出因子数量:" << allFactors.size();
    
    // 模拟导出成功
    return true;
}

bool FactorService::toggleFavorite(const QString& factorId)
{
    qDebug() << "FactorService::toggleFavorite 开始，因子ID:" << factorId;
    
    QVariantMap factor = getFactorById(factorId);
    if (factor.isEmpty()) {
        qWarning() << "未找到因子:" << factorId;
        return false;
    }
    
    // 切换收藏状态
    bool isFavorite = factor.value("isFavorite").toBool();
    factor["isFavorite"] = !isFavorite;
    
    // 更新因子
    bool success = updateFactor(factorId, factor);
    
    qDebug() << "FactorService::toggleFavorite 结束，新状态:" << !isFavorite << "成功:" << success;
    return success;
}

void FactorService::syncWithDatabase()
{
    qDebug() << "FactorService::syncWithDatabase 开始";
    
    // 清空缓存，重新从数据库加载
    clearAllCache();
    
    // 发出数据变更信号，让视图层重新加载
    emit dataChanged();
    
    qDebug() << "FactorService::syncWithDatabase 结束";
}

void FactorService::clearCache()
{
    qDebug() << "FactorService::clearCache 开始";
    
    clearAllCache();
    
    qDebug() << "FactorService::clearCache 结束";
}

// 私有方法实现

void FactorService::initializeRepository()
{
    try {
        // 使用全局数据库连接管理器获取数据库连接
        auto& dbManager = astock::database::DatabaseConnectionManager::instance();
        // 初始化数据库连接
        if (!dbManager.initialize()) {
            qWarning() << "FactorService::initializeRepository: 数据库连接初始化失败";
            return;
        }
        
        // 获取数据库连接
        auto database = dbManager.getDatabase();
        if (!database) {
            qWarning() << "FactorService::initializeRepository: 获取数据库连接失败";
            return;
        }
        
        // 创建因子仓储实例
        auto repository = std::make_shared<astock::database::FactorRepository>(database);
        if (!repository) {
            qWarning() << "FactorService::initializeRepository: Failed to create repository";
            return;
        }
        
        // 初始化数据库表
        if (!repository->initialize()) {
            qWarning() << "FactorService::initializeRepository: Failed to initialize database tables";
            return;
        }
        
        m_repository = repository;
        qDebug() << "✅ FactorService::initializeRepository: Repository initialized successfully";
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::initializeRepository: Error:" << e.what();
    }
}

bool FactorService::saveFactorToDatabase(const QVariantMap& factorData)
{
    qDebug() << "FactorService::saveFactorToDatabase 开始，因子ID:" << factorData.value("factorId").toString();
    
    if (!m_repository) {
        qWarning() << "FactorService::saveFactorToDatabase: Repository not initialized";
        return false;
    }
    
    try {
        bool success = m_repository->save(factorData);
        qDebug() << "FactorService::saveFactorToDatabase 结果:" << success;
        
        return success;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::saveFactorToDatabase: Error:" << e.what();
        return false;
    }
}

bool FactorService::updateFactorInDatabase(const QString& factorId, const QVariantMap& factorData)
{
    if (!m_repository) {
        qWarning() << "FactorService::updateFactorInDatabase: Repository not initialized";
        return false;
    }
    
    try {
        bool success = m_repository->update(factorId, factorData);
        return success;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::updateFactorInDatabase: Error:" << e.what();
        return false;
    }
}

bool FactorService::deleteFactorFromDatabase(const QString& factorId)
{
    if (!m_repository) {
        qWarning() << "FactorService::deleteFactorFromDatabase: Repository not initialized";
        return false;
    }
    
    try {
        bool success = m_repository->remove(factorId);
        return success;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::deleteFactorFromDatabase: Error:" << e.what();
        return false;
    }
}

QVariantList FactorService::loadFactorsFromDatabase()
{
    qDebug() << "FactorService::loadFactorsFromDatabase: 开始加载因子数据";
    
    if (!m_repository) {
        qWarning() << "FactorService::loadFactorsFromDatabase: Repository not initialized";
        qDebug() << "尝试重新初始化仓储...";
        initializeRepository();
        
        if (!m_repository) {
            qCritical() << "FactorService::loadFactorsFromDatabase: 仓储初始化失败，无法加载数据";
            return QVariantList();
        }
    }
    
    try {
        qDebug() << "FactorService::loadFactorsFromDatabase: 调用 m_repository->findAll()...";
        // 从数据库加载所有因子
        auto factorMaps = m_repository->findAll();
        qDebug() << "FactorService::loadFactorsFromDatabase: 数据库查询返回" << factorMaps.size() << "个因子";
        
        // 转换为QVariantList
        QVariantList factors;
        for (const auto& factorMap : factorMaps) {
            factors.append(factorMap);
        }
        
        // 发出加载完成信号
        emit factorsLoaded(factors);
        
        return factors;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::loadFactorsFromDatabase: Error:" << e.what();
        return QVariantList();
    }
}

void FactorService::saveFactorToCache(const QString& factorId, const QVariantMap& factorData)
{
    try {
        // 保存到内存缓存
        QMutexLocker locker(&m_mutex);
        m_memoryCache[factorId] = factorData;
        
        // 保存到全局缓存
        QString cacheKey = QString("factor_%1").arg(factorId);
        QVariantList factorList;
        factorList.append(factorData);
        
        DataServiceCache::getInstance().storeData(cacheKey, factorList);
        
        qDebug() << "FactorService::saveFactorToCache: Saved factor to cache:" << factorId;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::saveFactorToCache: Error:" << e.what();
    }
}

QVariantMap FactorService::loadFactorFromCache(const QString& factorId)
{
    try {
        // 首先尝试从内存缓存获取
        {
            QMutexLocker locker(&m_mutex);
            if (m_memoryCache.contains(factorId)) {
                qDebug() << "FactorService::loadFactorFromCache: Loaded from memory cache:" << factorId;
                return m_memoryCache[factorId];
            }
        }
        
        // 从全局缓存获取
        QString cacheKey = QString("factor_%1").arg(factorId);
        QVariantList cachedData = DataServiceCache::getInstance().getData(cacheKey);
        
        if (!cachedData.isEmpty() && cachedData[0].canConvert<QVariantMap>()) {
            QVariantMap factorData = cachedData[0].toMap();
            
            // 保存到内存缓存
            QMutexLocker locker(&m_mutex);
            m_memoryCache[factorId] = factorData;
            
            qDebug() << "FactorService::loadFactorFromCache: Loaded from global cache:" << factorId;
            return factorData;
        }
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::loadFactorFromCache: Error:" << e.what();
    }
    
    return QVariantMap();
}

void FactorService::removeFactorFromCache(const QString& factorId)
{
    try {
        // 从内存缓存删除
        {
            QMutexLocker locker(&m_mutex);
            m_memoryCache.remove(factorId);
        }
        
        // 从全局缓存删除
        QString cacheKey = QString("factor_%1").arg(factorId);
        DataServiceCache::getInstance().removeData(cacheKey);
        
        qDebug() << "FactorService::removeFactorFromCache: Removed factor from cache:" << factorId;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::removeFactorFromCache: Error:" << e.what();
    }
}

void FactorService::clearAllCache()
{
    try {
        // 清空内存缓存
        {
            QMutexLocker locker(&m_mutex);
            m_memoryCache.clear();
        }
        
        // 清空所有因子相关的全局缓存
        // 这里可以添加更精确的缓存清理逻辑
        
        qDebug() << "FactorService::clearAllCache: Cleared all cache";
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::clearAllCache: Error:" << e.what();
    }
}

bool FactorService::validateFactorData(const QVariantMap& factorData, QString& errorMessage)
{
    // 检查必要字段
    if (!factorData.contains("factorName") || factorData["factorName"].toString().isEmpty()) {
        errorMessage = "因子名称不能为空";
        return false;
    }
    
    if (!factorData.contains("displayName") || factorData["displayName"].toString().isEmpty()) {
        errorMessage = "显示名称不能为空";
        return false;
    }
    
    if (!factorData.contains("majorCategory") || factorData["majorCategory"].toString().isEmpty()) {
        errorMessage = "主类别不能为空";
        return false;
    }
    
    // 检查数值范围
    if (factorData.contains("icValue")) {
        double icValue = factorData["icValue"].toDouble();
        if (icValue < -1.0 || icValue > 1.0) {
            errorMessage = "IC值必须在-1.0到1.0之间";
            return false;
        }
    }
    
    if (factorData.contains("irValue")) {
        double irValue = factorData["irValue"].toDouble();
        if (irValue < 0.0) {
            errorMessage = "IR值不能为负数";
            return false;
        }
    }
    
    if (factorData.contains("validityDays")) {
        int validityDays = factorData["validityDays"].toInt();
        if (validityDays < 1 || validityDays > 365) {
            errorMessage = "有效天数必须在1到365之间";
            return false;
        }
    }
    
    if (factorData.contains("turnoverRate")) {
        double turnoverRate = factorData["turnoverRate"].toDouble();
        if (turnoverRate < 0.0 || turnoverRate > 1.0) {
            errorMessage = "换手率必须在0.0到1.0之间";
            return false;
        }
    }
    
    return true;
}

QString FactorService::generateFactorId(const QString& factorName)
{
    // 生成唯一的因子ID：因子名称_时间戳
    QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString sanitizedName = factorName.toLower().replace(QRegularExpression("[^a-z0-9_]"), "_");
    return QString("%1_%2").arg(sanitizedName).arg(timestamp);
}
       