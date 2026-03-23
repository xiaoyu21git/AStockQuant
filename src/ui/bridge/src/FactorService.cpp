// FactorService.cpp
// 因子服务层实现 - 负责业务逻辑

#include "../../ui/bridge/include/FactorService.h"
#include "../../ui/bridge/include/FactorViewModel.h"
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
#include <QRandomGenerator>

using namespace astock::database;

// 单例实例定义
FactorService* FactorService::m_instance = nullptr;
QMutex FactorService::m_instanceMutex;

// 单例访问方法
FactorService* FactorService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new FactorService();
        // 自动初始化单例实例
        m_instance->initialize();
    }
    return m_instance;
}

FactorService::FactorService(QObject* parent)
    : QObject(parent)
    , m_repository(nullptr)
    , m_initialized(false)
    , m_isLoading(false)
    , m_cacheLoaded(false)
    , m_autoInitialize(true)  // 默认自动初始化
    , m_viewModel(new FactorViewModel(this))  // 创建FactorViewModel实例
{
    qDebug() << "FactorService constructor (autoInitialize=true)";
    qDebug() << "FactorService: 创建FactorViewModel实例，地址:" << m_viewModel;
    
    // 连接信号到视图模型
    connect(this, &FactorService::factorsLoaded, this, [this](const QVariantList& factors) {
        qDebug() << "FactorService: factorsLoaded 信号收到，因子数量:" << factors.size();
        if (m_viewModel) {
            qDebug() << "FactorService: 更新视图模型数据";
            m_viewModel->updateData(factors);
        } else {
            qWarning() << "FactorService: 视图模型为空，无法更新数据";
        }
    });
    
    connect(this, &FactorService::dataChanged, this, [this]() {
        qDebug() << "FactorService: dataChanged 信号触发";
        // 数据变更时，只通知视图模型数据已变更
        // 不重新加载数据，避免重复加载
        qDebug() << "FactorService: 数据变更通知已发送";
    });
}

FactorService::~FactorService()
{
    qDebug() << "FactorService destructor";
}

void FactorService::initialize()
{
    qDebug() << "FactorService::initialize: 开始初始化";
    
    // 使用初始化专用互斥锁，防止并发初始化
    QMutexLocker locker(&m_initMutex);
    
    if (m_initialized) {
        qDebug() << "FactorService::initialize: 已经初始化，跳过";
        return;
    }
    
    try {
        // 初始化仓储
        initializeRepository();
        
        if (!m_repository) {
            qWarning() << "FactorService::initialize: 仓储初始化失败";
            //emit errorOccurred("因子服务初始化失败：数据库连接错误");
            return;
        }
        
        m_initialized = true;
        qDebug() << "✅ FactorService::initialize: 因子服务初始化完成";
        
        // 初始化完成后自动加载数据（延迟执行，避免阻塞UI）
        QTimer::singleShot(0, this, [this]() {
            qDebug() << "FactorService::initialize: 自动加载因子数据";
            if (!m_isLoading) {
                m_isLoading = true;
                loadFactorsFromDatabase();
                m_isLoading = false;
            }
        });
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::initialize: Error:" << e.what();
        //emit errorOccurred(QString("因子服务初始化失败: %1").arg(e.what()));
    }
}

QString FactorService::addFactor(const QVariantMap& factorData)
{
    qDebug() << "FactorService::addFactor 开始，数据:" << factorData;
    
    // 验证数据
    QString errorMessage;
    if (!validateFactorData(factorData, errorMessage)) {
        qWarning() << "因子数据验证失败:" << errorMessage;
        //emit errorOccurred(errorMessage);
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
        //emit errorOccurred(errorMsg);
        return QString();
    }
    
    // 数据库保存成功，现在保存到缓存
    // 使用写锁保护整个缓存操作，确保原子性
    {
        QWriteLocker locker(&m_rwLock);
        m_memoryCache[factorId] = dataToSave;
        
        // 保存到全局缓存
        QString cacheKey = QString("factor_%1").arg(factorId);
        QVariantList factorList;
        factorList.append(dataToSave);
        DataServiceCache::getInstance().storeData(cacheKey, factorList);
    }
    
    // 更新视图模型
    if (m_viewModel) {
        m_viewModel->appendData(dataToSave);
    }
    
    // 发出信号通知视图层（在锁外发出，避免死锁）
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
        //emit errorOccurred(errorMessage);
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
       // emit errorOccurred(errorMsg);
        return false;
    }
    
    // 更新缓存
    saveFactorToCache(factorId, dataToUpdate);
    
    // 更新视图模型
    if (m_viewModel) {
        m_viewModel->updateFactor(factorId, dataToUpdate);
    }
    
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
        //emit errorOccurred(errorMsg);
        return false;
    }
    
    // 从缓存删除
    removeFactorFromCache(factorId);
    
    // 更新视图模型
    if (m_viewModel) {
        m_viewModel->removeFactor(factorId);
    }
    
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
    
    // 首先检查缓存是否已加载
    if (m_cacheLoaded) {
        // 从内存缓存获取所有因子
        QReadLocker locker(&m_rwLock);
        if (!m_memoryCache.isEmpty()) {
            QVariantList factors;
            for (const auto& factor : m_memoryCache) {
                factors.append(factor);
            }
            qDebug() << "FactorService::getAllFactors 从缓存获取，数量:" << factors.size();
            return factors;
        }
    }
    
    // 缓存未加载或为空，从数据库加载
    QVariantList factors = loadFactorsFromDatabase();
    
    // 更新缓存加载标志
    if (!factors.isEmpty()) {
        m_cacheLoaded = true;
    }
    
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
    
    if (factors.isEmpty()) {
        qDebug() << "FactorService::importFactors: 导入列表为空，直接返回成功";
        return true;
    }
    
    if (!m_repository) {
        qWarning() << "FactorService::importFactors: Repository not initialized";
        //emit errorOccurred("因子服务未初始化，无法导入数据");
        return false;
    }
    
    // 验证所有因子数据
    std::vector<QVariantMap> validFactors;
    QVariantList failedFactors;
    
    for (const QVariant& factorVariant : factors) {
        QVariantMap factorMap = factorVariant.toMap();
        
        // 验证数据
        QString errorMessage;
        if (!validateFactorData(factorMap, errorMessage)) {
            qWarning() << "因子数据验证失败:" << errorMessage;
            failedFactors.append(factorMap);
            continue;
        }
        
        // 生成因子ID（如果未提供）
        if (!factorMap.contains("factorId") || factorMap["factorId"].toString().isEmpty()) {
            QString factorName = factorMap["factorName"].toString();
            QString factorId = generateFactorId(factorName);
            factorMap["factorId"] = factorId;
        }
        
        validFactors.push_back(factorMap);
    }
    
    if (validFactors.empty()) {
        qWarning() << "FactorService::importFactors: 所有因子数据验证失败";
       // emit importFailed(failedFactors);
        return false;
    }
    
    // 使用仓储的批量保存方法，它支持事务
    size_t savedCount = m_repository->saveBatch(validFactors);
    
    if (savedCount == validFactors.size()) {
        // 全部成功，更新缓存
        {
            QWriteLocker locker(&m_rwLock);
            for (const auto& factor : validFactors) {
                QString factorId = factor["factorId"].toString();
                m_memoryCache[factorId] = factor;
            }
        }
        
        // 发出数据变更信号
        emit dataChanged();
        
        qDebug() << "FactorService::importFactors 结束，成功导入" << savedCount << "个因子";
        return true;
    } else {
        // 部分或全部失败
        qWarning() << "FactorService::importFactors: 批量导入失败，成功:" << savedCount << "，总数:" << validFactors.size();
        
        // 清空缓存，因为事务失败
        clearAllCache();
        
        // 发出失败信号
        emit importFailed(failedFactors);
        emit errorOccurred(QString("导入失败，成功:%1，失败:%2").arg(savedCount).arg(validFactors.size() - savedCount));
        
        return false;
    }
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
        // 首先确保数据库连接管理器已初始化
        // 这会配置ConnectionPool，确保FactorRepository使用的连接池有正确的配置
        auto& dbManager = astock::database::DatabaseConnectionManager::instance();
        if (!dbManager.initialize()) {
            qWarning() << "FactorService::initializeRepository: Database connection manager initialization failed";
            //emit errorOccurred("数据库连接初始化失败");
            return;
        }
        
        qDebug() << "✅ FactorService::initializeRepository: Database connection manager initialized";
        
        // 创建因子仓储实例（使用新的无参数构造函数）
        auto repository = std::make_shared<astock::database::FactorRepository>();
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
            //emit errorOccurred(QString("仓储初始化失败: %1").arg(e.what()));
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
   // qDebug() << "FactorService::loadFactorsFromDatabase: 开始加载因子数据";
    try {
        qDebug() << "FactorService::loadFactorsFromDatabase: 调用 m_repository->findAll()...";
        // 从数据库加载所有因子
        auto factorMaps = m_repository->findAll();
        //qDebug() << "FactorService::loadFactorsFromDatabase: 数据库查询返回" << factorMaps.size() << "个因子";
        
        // 转换为QVariantList
        QVariantList factors;
        
        // 保存到内存缓存
        {
            QWriteLocker locker(&m_rwLock);
            m_memoryCache.clear(); // 清空现有缓存
            
            for (const auto& factorMap : factorMaps) {
                QString factorId = factorMap["factorId"].toString();
                if (!factorId.isEmpty()) {
                    m_memoryCache[factorId] = factorMap;
                }
                factors.append(factorMap);
            }
            
            // 设置缓存已加载标志
            m_cacheLoaded = true;
        }
        
        // 直接更新视图模型

        if (m_viewModel) {
           // qDebug() << "FactorService::loadFactorsFromDatabase: 更新视图模型，因子数量:" << factors.size();
            m_viewModel->updateData(factors);
        } else {
            qWarning() << "FactorService::loadFactorsFromDatabase: 视图模型为空，无法更新";
        }
        
        // 发出加载完成信号
        emit factorsLoaded(factors);
        
        //qDebug() << "FactorService::loadFactorsFromDatabase: 加载完成，缓存因子数量:" << m_memoryCache.size();
        return factors;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::loadFactorsFromDatabase: Error:" << e.what();
        return QVariantList();
    }
}

void FactorService::saveFactorToCache(const QString& factorId, const QVariantMap& factorData)
{
    try {
        // 保存到内存缓存 - 使用写锁保护整个操作
        QWriteLocker locker(&m_rwLock);
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
        // 首先尝试从内存缓存获取 - 使用读锁
        {
            QReadLocker locker(&m_rwLock);
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
            
            // 保存到内存缓存 - 使用写锁
            QWriteLocker locker(&m_rwLock);
            m_memoryCache[factorId] = factorData;
            
            //qDebug() << "FactorService::loadFactorFromCache: Loaded from global cache:" << factorId;
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
        // 从内存缓存删除 - 使用写锁
        {
            QWriteLocker locker(&m_rwLock);
            m_memoryCache.remove(factorId);
        }
        
        // 从全局缓存删除
        QString cacheKey = QString("factor_%1").arg(factorId);
        DataServiceCache::getInstance().removeData(cacheKey);
        
        //qDebug() << "FactorService::removeFactorFromCache: Removed factor from cache:" << factorId;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::removeFactorFromCache: Error:" << e.what();
    }
}

void FactorService::clearAllCache()
{
    try {
        // 清空内存缓存 - 使用写锁
        {
            QWriteLocker locker(&m_rwLock);
            m_memoryCache.clear();
            m_cacheLoaded = false;  // 重置缓存加载标志
        }
        
        // 清空所有因子相关的全局缓存
        // 这里可以添加更精确的缓存清理逻辑
        
        qDebug() << "FactorService::clearAllCache: Cleared all cache, cacheLoaded reset to false";
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::clearAllCache: Error:" << e.what();
    }
}

void FactorService::updateCacheBatch(const std::vector<QVariantMap>& factors)
{
    try {
        QWriteLocker locker(&m_rwLock);
        
        for (const auto& factor : factors) {
            QString factorId = factor["factorId"].toString();
            if (!factorId.isEmpty()) {
                m_memoryCache[factorId] = factor;
                
                // 同时更新全局缓存
                QString cacheKey = QString("factor_%1").arg(factorId);
                QVariantList factorList;
                factorList.append(factor);
                DataServiceCache::getInstance().storeData(cacheKey, factorList);
            }
        }
        
        qDebug() << "FactorService::updateCacheBatch: Updated cache with" << factors.size() << "factors";
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::updateCacheBatch: Error:" << e.what();
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

// 新增方法实现：获取因子值（带缓存）
QVariantMap FactorService::getFactorValues(const QString& factorId, const QString& date)
{
    qDebug() << "FactorService::getFactorValues 开始，因子ID:" << factorId << "日期:" << date;
    
    // 生成缓存键
    QString cacheKey = QString("factor_values_%1_%2").arg(factorId).arg(date);
    
    // 首先尝试从缓存获取
    QVariantList cachedData = DataServiceCache::getInstance().getData(cacheKey);
    if (!cachedData.isEmpty() && cachedData[0].canConvert<QVariantMap>()) {
        qDebug() << "FactorService::getFactorValues: 从缓存获取数据，因子ID:" << factorId << "日期:" << date;
        return cachedData[0].toMap();
    }
    
    QVariantMap result;
    
    // 获取因子信息
    QVariantMap factorInfo = getFactorById(factorId);
    if (factorInfo.isEmpty()) {
        qWarning() << "FactorService::getFactorValues: 未找到因子:" << factorId;
        result["status"] = "error";
        result["error"] = "未找到因子";
        return result;
    }
    
    // 连接到数据库
    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    auto database = dbManager.getDatabase();
    if (!database) {
        qWarning() << "FactorService::getFactorValues: 无法获取数据库连接";
        result["status"] = "error";
        result["error"] = "数据库连接失败";
        return result;
    }
    
    try {
        QString factorName = factorInfo["factorName"].toString();
        QString majorCategory = factorInfo["majorCategory"].toString();
        
        // 根据因子类型计算因子值
        if (factorName == "pe_ttm_factor" || majorCategory == "价值因子") {
            // 市盈率TTM因子：从daily_bar表获取pe_ratio（使用原始数据表）
            QString sql = "SELECT symbol, pe_ratio FROM daily_bar WHERE trade_date = :date AND pe_ratio IS NOT NULL";
            std::map<QString, QVariant> params;
            params[":date"] = date;
            
            auto queryResult = database->executeQuery(sql, params);
            
            QVariantMap stockValues;
            for (size_t i = 0; i < queryResult.rowCount(); i++) {
                const auto& row = queryResult.getRow(i);
                QString symbol = row.getString("symbol");
                double peRatio = row.getDouble("pe_ratio");
                // 市盈率越低越好，所以取倒数
                double factorValue = (peRatio > 0) ? 1.0 / peRatio : 0.0;
                stockValues[symbol] = factorValue;
            }
            
            result["factorId"] = factorId;
            result["date"] = date;
            result["stockValues"] = stockValues;
            result["count"] = stockValues.size();
            result["status"] = "success";
            
        } else if (factorName == "momentum_60d" || majorCategory == "动量因子") {
            // 60日动量因子：计算过去60日的收益率
            QString sql = "SELECT symbol, close FROM cleaned_daily_bar WHERE trade_date = :date";
            std::map<QString, QVariant> params;
            params[":date"] = date;
            
            auto queryResult = database->executeQuery(sql, params);
            
            QVariantMap stockValues;
            for (size_t i = 0; i < queryResult.rowCount(); i++) {
                const auto& row = queryResult.getRow(i);
                QString symbol = row.getString("symbol");
                double closePrice = row.getDouble("close");
                
                // 获取60天前的收盘价
                QDate currentDate = QDate::fromString(date, "yyyy-MM-dd");
                QDate startDate = currentDate.addDays(-60);
                QString startDateStr = startDate.toString("yyyy-MM-dd");
                
                QString sqlPrev = "SELECT close FROM cleaned_daily_bar WHERE symbol = :symbol AND trade_date = :prev_date";
                std::map<QString, QVariant> paramsPrev;
                paramsPrev[":symbol"] = symbol;
                paramsPrev[":prev_date"] = startDateStr;
                
                auto queryResultPrev = database->executeQuery(sqlPrev, paramsPrev);
                
                if (!queryResultPrev.isEmpty()) {
                    const auto& rowPrev = queryResultPrev.getRow(0);
                    double prevClose = rowPrev.getDouble("close");
                    if (prevClose > 0) {
                        double momentum = (closePrice - prevClose) / prevClose;
                        stockValues[symbol] = momentum;
                    }
                }
            }
            
            result["factorId"] = factorId;
            result["date"] = date;
            result["stockValues"] = stockValues;
            result["count"] = stockValues.size();
            result["status"] = "success";
            
        } else {
            // 其他因子：返回空结果，表示需要外部计算
            result["factorId"] = factorId;
            result["date"] = date;
            result["stockValues"] = QVariantMap();
            result["count"] = 0;
            result["status"] = "success";
            result["message"] = "因子需要外部计算";
        }
        
        // 将结果保存到缓存
        if (result["status"].toString() == "success") {
            QVariantList cacheData;
            cacheData.append(result);
            DataServiceCache::getInstance().storeData(cacheKey, cacheData);
            qDebug() << "FactorService::getFactorValues: 数据已缓存，因子ID:" << factorId << "日期:" << date;
        }
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::getFactorValues: 数据库错误:" << e.what();
        result["status"] = "error";
        result["error"] = QString::fromStdString(e.what());
    }
    
    qDebug() << "FactorService::getFactorValues 结束，返回股票数量:" << result["count"].toInt();
    return result;
}

// 新增方法实现：批量获取因子值（简化版：先保证回测流程跑通）
QVariantMap FactorService::getFactorValuesBatch(const QString& factorId, const QStringList& dates)
{
    qDebug() << "FactorService::getFactorValuesBatch 开始，因子ID:" << factorId << "日期数量:" << dates.size();
    
    QVariantMap result;
    
    // 获取因子信息
    QVariantMap factorInfo = getFactorById(factorId);
    if (factorInfo.isEmpty()) {
        qWarning() << "FactorService::getFactorValuesBatch: 未找到因子:" << factorId;
        return result;
    }
    
    // 策略二：按需加载，延迟计算
    // 1. 先检查缓存，避免查询数据库
    // 2. 只查询实际需要的日期
    // 3. 使用BETWEEN查询替代IN子句
    
    // 连接到数据库
    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    auto database = dbManager.getDatabase();
    if (!database) {
        qWarning() << "FactorService::getFactorValuesBatch: 无法获取数据库连接";
        result["status"] = "error";
        result["error"] = "数据库连接失败";
        return result;
    }
    
    try {
        // 步骤1：过滤掉无效日期（数据库中不存在的日期）
        QStringList validDates;
        QMap<QString, QVariantMap> dateData;
        
        // 先检查缓存
        for (const QString& date : dates) {
            QString cacheKey = QString("factor_values_%1_%2").arg(factorId).arg(date);
            QVariantList cachedData = DataServiceCache::getInstance().getData(cacheKey);
            
            if (!cachedData.isEmpty() && cachedData[0].canConvert<QVariantMap>()) {
                QVariantMap cachedResult = cachedData[0].toMap();
                if (cachedResult["status"].toString() == "success") {
                    dateData[date] = cachedResult["stockValues"].toMap();
                    qDebug() << "FactorService::getFactorValuesBatch: 从缓存获取数据，日期:" << date;
                } else {
                    validDates.append(date);
                }
            } else {
                validDates.append(date);
            }
        }
        
        qDebug() << "FactorService::getFactorValuesBatch: 需要查询的日期数量:" << validDates.size() 
                 << "，从缓存获取的日期数量:" << (dates.size() - validDates.size());
        
        // 步骤2：如果有需要查询的日期，尝试从缓存获取清洗后的数据
        if (!validDates.isEmpty()) {
            // 获取日期范围
            QString minDate = validDates.first();
            QString maxDate = validDates.first();
            
            for (const QString& date : validDates) {
                if (date < minDate) minDate = date;
                if (date > maxDate) maxDate = date;
            }
            
            qDebug() << "FactorService::getFactorValuesBatch: 尝试从缓存获取数据，不查询数据库";
            
            // 尝试从缓存获取清洗后的数据（使用DataServiceCache）
            // 使用符号为空表示所有股票
            QString cacheKey = QString("data:stock:ALL_%1_%2").arg(minDate).arg(maxDate);
            qDebug() << "FactorService::getFactorValuesBatch: 缓存键:" << cacheKey;
            QVariantList cachedData = DataServiceCache::getInstance().getData(cacheKey);
            
            if (!cachedData.isEmpty()) {
                qDebug() << "FactorService::getFactorValuesBatch: 从缓存获取到" << cachedData.size() << "条数据";
                int processedCount = 0;
                
                // 检查缓存数据类型
                QVariant firstItem = cachedData.first();
                if (firstItem.canConvert<QVariantMap>()) {
                        // 标准数据格式：包含trade_date, symbol, close等字段
                        for (int i = 0; i < cachedData.size(); i++) {
                            const QVariant& item = cachedData[i];
                            if (!item.canConvert<QVariantMap>()) {
                                qDebug() << "FactorService::getFactorValuesBatch: 第" << i << "项不是有效的Map";
                                continue;
                            }
                            
                            QVariantMap dataMap = item.toMap();
                            
                            // 增强的字段提取逻辑：支持多种字段名变体
                            QString date = extractDateFromDataMap(dataMap, i);
                            QString symbol = extractSymbolFromDataMap(dataMap, i);
                            double closePrice = extractClosePriceFromDataMap(dataMap, i);
                            
                            if (!date.isEmpty() && !symbol.isEmpty() && closePrice >= 0) {
                                if (!dateData.contains(date)) {
                                    dateData[date] = QVariantMap();
                                }
                                dateData[date][symbol] = closePrice;
                                processedCount++;
                                
                                // 只打印前几条数据的调试信息
                                if (processedCount <= 5) {
                                    qDebug() << "FactorService::getFactorValuesBatch: 处理数据 - 日期:" << date 
                                             << "股票:" << symbol << "收盘价:" << closePrice;
                                }
                            } else {
                                // 数据不完整，记录调试信息
                                logDataExtractionDebugInfo(dataMap, i, date, symbol, closePrice);
                            }
                        }
                } else {
                    // 数据可能是其他格式，比如DataManager存储的原始数据库结果
                    qDebug() << "FactorService::getFactorValuesBatch: 缓存数据格式不标准，跳过处理";
                    // 尝试记录更多信息以帮助调试
                    qDebug() << "FactorService::getFactorValuesBatch: 第一个项目的类型:" << firstItem.typeName();
                    if (firstItem.canConvert<QString>()) {
                        qDebug() << "FactorService::getFactorValuesBatch: 可以转换为字符串:" << firstItem.toString().left(100);
                    }
                }
                
                qDebug() << "FactorService::getFactorValuesBatch: 成功处理" << processedCount << "条数据";
                if (processedCount > 0) {
                    qDebug() << "FactorService::getFactorValuesBatch: dateData包含" << dateData.size() << "个日期";
                    for (const QString& dateKey : dateData.keys()) {
                        qDebug() << "FactorService::getFactorValuesBatch: 日期" << dateKey << "有" << dateData[dateKey].size() << "只股票";
                    }
                } else {
                    qDebug() << "FactorService::getFactorValuesBatch: 缓存数据格式不匹配，尝试数据库查询";
                    // 尝试从数据库查询
                    QVariantList dbData = queryDatabaseData(minDate, maxDate);
                    if (!dbData.isEmpty()) {
                        // 处理数据库查询结果
                        for (const QVariant& item : dbData) {
                            QVariantMap dataMap = item.toMap();
                            QString date = dataMap.value("trade_date").toString();
                            QString symbol = dataMap.value("symbol").toString();
                            double closePrice = dataMap.value("close").toDouble();
                            
                            if (!date.isEmpty() && !symbol.isEmpty()) {
                                if (!dateData.contains(date)) {
                                    dateData[date] = QVariantMap();
                                }
                                dateData[date][symbol] = closePrice;
                            }
                        }
                        qDebug() << "FactorService::getFactorValuesBatch: 从数据库获取到" << dbData.size() << "条数据";
                    } else {
                        qDebug() << "FactorService::getFactorValuesBatch: 数据库查询也返回空结果";
                    }
                }
            } else {
                qDebug() << "FactorService::getFactorValuesBatch: 缓存中没有数据，尝试数据库查询";
                // 尝试从数据库查询
                QVariantList dbData = queryDatabaseData(minDate, maxDate);
                if (!dbData.isEmpty()) {
                    // 处理数据库查询结果
                    for (const QVariant& item : dbData) {
                        QVariantMap dataMap = item.toMap();
                        QString date = dataMap.value("trade_date").toString();
                        QString symbol = dataMap.value("symbol").toString();
                        double closePrice = dataMap.value("close").toDouble();
                        
                        if (!date.isEmpty() && !symbol.isEmpty()) {
                            if (!dateData.contains(date)) {
                                dateData[date] = QVariantMap();
                            }
                            dateData[date][symbol] = closePrice;
                        }
                    }
                    qDebug() << "FactorService::getFactorValuesBatch: 从数据库获取到" << dbData.size() << "条数据";
                } else {
                    qDebug() << "FactorService::getFactorValuesBatch: 数据库查询返回空结果";
                }
            }
        }
        
        // 步骤3：构建结果并缓存
        QVariantMap batchResult;
        int totalRows = 0;
        
        for (const QString& date : dates) {
            QString cacheKey = QString("factor_values_%1_%2").arg(factorId).arg(date);
            
            if (dateData.contains(date) && !dateData[date].isEmpty()) {
                QVariantMap dayResult;
                dayResult["factorId"] = factorId;
                dayResult["date"] = date;
                dayResult["stockValues"] = dateData[date];
                dayResult["count"] = dateData[date].size();
                dayResult["status"] = "success";
                
                batchResult[date] = dayResult;
                totalRows += dateData[date].size();
                
                // 缓存数据
                QVariantList cacheData;
                cacheData.append(dayResult);
                DataServiceCache::getInstance().storeData(cacheKey, cacheData);
            } else {
                // 如果没有数据，返回空结果
                QVariantMap dayResult;
                dayResult["factorId"] = factorId;
                dayResult["date"] = date;
                dayResult["stockValues"] = QVariantMap();
                dayResult["count"] = 0;
                dayResult["status"] = "success";
                dayResult["message"] = "该日期无数据";
                
                batchResult[date] = dayResult;
            }
        }
        
        result["factorId"] = factorId;
        result["dates"] = dates;
        result["batchResults"] = batchResult;
        result["totalCount"] = totalRows;
        result["status"] = "success";
        
        qDebug() << "FactorService::getFactorValuesBatch: 批量查询完成，总行数:" << totalRows;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::getFactorValuesBatch: 数据库错误:" << e.what();
        result["status"] = "error";
        result["error"] = QString::fromStdString(e.what());
    }
    
    qDebug() << "FactorService::getFactorValuesBatch 结束";
    return result;
}

// 新增方法实现：获取因子值范围（优化版：从缓存获取，避免遍历整年数据）
QVariantMap FactorService::getFactorValuesRange(const QString& factorId, 
                                               const QString& startDate, 
                                               const QString& endDate)
{
    qDebug() << "FactorService::getFactorValuesRange 开始，因子ID:" << factorId 
             << "开始日期:" << startDate << "结束日期:" << endDate;
    
    // 生成缓存键 - DataServiceCache::storeData会添加"manager:"前缀
    // 所以这里应该使用原始键，不带前缀
    QString cacheKey = QString("factor_values_range_%1_%2_%3")
        .arg(factorId)
        .arg(startDate)
        .arg(endDate);
    
    // 首先尝试从缓存获取
    QVariantList cachedData = DataServiceCache::getInstance().getData(cacheKey);
    if (!cachedData.isEmpty() && cachedData[0].canConvert<QVariantMap>()) {
        QVariantMap cachedResult = cachedData[0].toMap();
        if (cachedResult["status"].toString() == "success") {
            qDebug() << "FactorService::getFactorValuesRange: 从缓存获取数据，因子ID:" << factorId 
                     << "开始日期:" << startDate << "结束日期:" << endDate;
            return cachedResult;
        }
    }
    
    QVariantMap result;
    
    // 获取因子信息
    QVariantMap factorInfo = getFactorById(factorId);
    if (factorInfo.isEmpty()) {
        qWarning() << "FactorService::getFactorValuesRange: 未找到因子:" << factorId;
        result["status"] = "error";
        result["error"] = "未找到因子";
        return result;
    }
    
    // 使用缓存装饰器查询数据，避免直接遍历数据库
    QString factorName = factorInfo["factorName"].toString();
    QString majorCategory = factorInfo["majorCategory"].toString();
    
    // 连接到数据库
    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    auto database = dbManager.getDatabase();
    if (!database) {
        qWarning() << "FactorService::getFactorValuesRange: 无法获取数据库连接";
        result["status"] = "error";
        result["error"] = "数据库连接失败";
        return result;
    }
    
    try {
        QVariantList values;
        
        // 根据因子类型获取数据 - 使用缓存装饰器
        if (factorName == "pe_ttm_factor" || majorCategory == "价值因子") {
            // 市盈率TTM因子：从缓存获取每日数据，然后计算平均值
            // 使用DataServiceCacheDecorator查询缓存数据
            QVariantList cachedStockData = DataServiceCache::getInstance().getCachedData("", startDate, endDate);
            
            if (!cachedStockData.isEmpty()) {
                // 从缓存数据中提取每日的市盈率平均值
                QMap<QString, QList<double>> datePeValues;
                
                for (const QVariant& item : cachedStockData) {
                    QVariantMap dataMap = item.toMap();
                    QString date = dataMap.value("trade_date").toString();
                    double peRatio = dataMap.value("pe_ratio").toDouble();
                    
                    if (!date.isEmpty() && peRatio > 0) {
                        datePeValues[date].append(peRatio);
                    }
                }
                
                // 计算每日平均值
                for (auto it = datePeValues.begin(); it != datePeValues.end(); ++it) {
                    QString date = it.key();
                    QList<double> peList = it.value();
                    
                    double sum = 0.0;
                    for (double pe : peList) {
                        sum += pe;
                    }
                    double avgPe = sum / peList.size();
                    
                    // 市盈率越低越好，所以取倒数
                    double factorValue = (avgPe > 0) ? 1.0 / avgPe : 0.0;
                    
                    QVariantMap dayValue;
                    dayValue["date"] = date;
                    dayValue["value"] = factorValue;
                    values.append(dayValue);
                }
            } else {
                // 缓存中没有数据，使用优化查询（限制返回行数）
                QString sql = "SELECT trade_date, AVG(pe_ratio) as avg_pe FROM cleaned_daily_bar "
                             "WHERE trade_date BETWEEN :start_date AND :end_date "
                             "AND pe_ratio IS NOT NULL "
                             "GROUP BY trade_date ORDER BY trade_date LIMIT 100";
                std::map<QString, QVariant> params;
                params[":start_date"] = startDate;
                params[":end_date"] = endDate;
                
                auto queryResult = database->executeQuery(sql, params);
                
                for (size_t i = 0; i < queryResult.rowCount(); i++) {
                    const auto& row = queryResult.getRow(i);
                    QString date = row.getString("trade_date");
                    double avgPe = row.getDouble("avg_pe");
                    // 市盈率越低越好，所以取倒数
                    double factorValue = (avgPe > 0) ? 1.0 / avgPe : 0.0;
                    
                    QVariantMap dayValue;
                    dayValue["date"] = date;
                    dayValue["value"] = factorValue;
                    values.append(dayValue);
                }
            }
            
        } else if (factorName == "momentum_60d" || majorCategory == "动量因子") {
            // 60日动量因子：从缓存获取每日收盘价数据
            QVariantList cachedStockData = DataServiceCache::getInstance().getCachedData("", startDate, endDate);
            
            if (!cachedStockData.isEmpty()) {
                // 从缓存数据中提取每日的收盘价平均值
                QMap<QString, QList<double>> dateCloseValues;
                
                for (const QVariant& item : cachedStockData) {
                    QVariantMap dataMap = item.toMap();
                    QString date = dataMap.value("trade_date").toString();
                    double closePrice = dataMap.value("close").toDouble();
                    
                    if (!date.isEmpty() && closePrice > 0) {
                        dateCloseValues[date].append(closePrice);
                    }
                }
                
                // 计算每日平均值
                for (auto it = dateCloseValues.begin(); it != dateCloseValues.end(); ++it) {
                    QString date = it.key();
                    QList<double> closeList = it.value();
                    
                    double sum = 0.0;
                    for (double close : closeList) {
                        sum += close;
                    }
                    double avgClose = sum / closeList.size();
                    
                    QVariantMap dayValue;
                    dayValue["date"] = date;
                    dayValue["value"] = avgClose;
                    values.append(dayValue);
                }
            } else {
                // 缓存中没有数据，使用优化查询（限制返回行数）
                QString sql = "SELECT trade_date, AVG(close) as avg_close FROM cleaned_daily_bar "
                             "WHERE trade_date BETWEEN :start_date AND :end_date "
                             "GROUP BY trade_date ORDER BY trade_date LIMIT 100";
                std::map<QString, QVariant> params;
                params[":start_date"] = startDate;
                params[":end_date"] = endDate;
                
                auto queryResult = database->executeQuery(sql, params);
                
                for (size_t i = 0; i < queryResult.rowCount(); i++) {
                    const auto& row = queryResult.getRow(i);
                    QString date = row.getString("trade_date");
                    double avgClose = row.getDouble("avg_close");
                    
                    QVariantMap dayValue;
                    dayValue["date"] = date;
                    dayValue["value"] = avgClose;
                    values.append(dayValue);
                }
            }
            
        } else {
            // 默认：从缓存获取每日收盘价数据
            QVariantList cachedStockData = DataServiceCache::getInstance().getCachedData("", startDate, endDate);
            
            if (!cachedStockData.isEmpty()) {
                // 从缓存数据中提取每日的收盘价平均值
                QMap<QString, QList<double>> dateCloseValues;
                
                for (const QVariant& item : cachedStockData) {
                    QVariantMap dataMap = item.toMap();
                    QString date = dataMap.value("trade_date").toString();
                    double closePrice = dataMap.value("close").toDouble();
                    
                    if (!date.isEmpty() && closePrice > 0) {
                        dateCloseValues[date].append(closePrice);
                    }
                }
                
                // 计算每日平均值
                for (auto it = dateCloseValues.begin(); it != dateCloseValues.end(); ++it) {
                    QString date = it.key();
                    QList<double> closeList = it.value();
                    
                    double sum = 0.0;
                    for (double close : closeList) {
                        sum += close;
                    }
                    double avgClose = sum / closeList.size();
                    
                    QVariantMap dayValue;
                    dayValue["date"] = date;
                    dayValue["value"] = avgClose;
                    values.append(dayValue);
                }
            } else {
                // 缓存中没有数据，使用优化查询（限制返回行数）
                QString sql = "SELECT trade_date, AVG(close) as avg_close FROM cleaned_daily_bar "
                             "WHERE trade_date BETWEEN :start_date AND :end_date "
                             "GROUP BY trade_date ORDER BY trade_date LIMIT 100";
                std::map<QString, QVariant> params;
                params[":start_date"] = startDate;
                params[":end_date"] = endDate;
                
                auto queryResult = database->executeQuery(sql, params);
                
                for (size_t i = 0; i < queryResult.rowCount(); i++) {
                    const auto& row = queryResult.getRow(i);
                    QString date = row.getString("trade_date");
                    double avgClose = row.getDouble("avg_close");
                    
                    QVariantMap dayValue;
                    dayValue["date"] = date;
                    dayValue["value"] = avgClose;
                    values.append(dayValue);
                }
            }
        }
        
        result["factorId"] = factorId;
        result["startDate"] = startDate;
        result["endDate"] = endDate;
        result["values"] = values;
        result["count"] = values.size();
        result["status"] = "success";
        
        // 将结果保存到缓存
        QVariantList cacheData;
        cacheData.append(result);
        DataServiceCache::getInstance().storeData(cacheKey, cacheData);
        qDebug() << "FactorService::getFactorValuesRange: 数据已缓存，因子ID:" << factorId 
                 << "开始日期:" << startDate << "结束日期:" << endDate;
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::getFactorValuesRange: 数据库错误:" << e.what();
        result["status"] = "error";
        result["error"] = QString::fromStdString(e.what());
    }
    
    qDebug() << "FactorService::getFactorValuesRange 结束，返回数据数量:" << result["count"].toInt();
    return result;
}

// 新增方法实现：获取可用日期
QStringList FactorService::getAvailableDates(const QString& factorId)
{
    qDebug() << "FactorService::getAvailableDates 开始，因子ID:" << factorId;
    
    QStringList dates;
    
    // 获取因子信息
    QVariantMap factorInfo = getFactorById(factorId);
    if (factorInfo.isEmpty()) {
        qWarning() << "FactorService::getAvailableDates: 未找到因子:" << factorId;
        return dates;
    }
    
    // 连接到数据库
    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    auto database = dbManager.getDatabase();
    if (!database) {
        qWarning() << "FactorService::getAvailableDates: 无法获取数据库连接";
        return dates;
    }
    
    try {
        // 从cleaned_daily_bar表获取有数据的日期（清洗后的数据）
        QString sql = "SELECT DISTINCT trade_date FROM cleaned_daily_bar WHERE trade_date IS NOT NULL ORDER BY trade_date DESC LIMIT 30";
        auto queryResult = database->executeQuery(sql, {});
        
        for (size_t i = 0; i < queryResult.rowCount(); i++) {
            const auto& row = queryResult.getRow(i);
            QString date = row.getString("trade_date");
            dates.append(date);
        }
        
        qDebug() << "FactorService::getAvailableDates: 从数据库获取到" << dates.size() << "个可用日期";
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::getAvailableDates: 数据库错误:" << e.what();
    }
    
    qDebug() << "FactorService::getAvailableDates 结束，返回日期数量:" << dates.size();
    return dates;
}

// 新增方法实现：获取可用股票
QStringList FactorService::getAvailableStocks(const QString& factorId, const QString& date)
{
    qDebug() << "FactorService::getAvailableStocks 开始，因子ID:" << factorId << "日期:" << date;
    
    QStringList stocks;
    
    // 获取因子信息
    QVariantMap factorInfo = getFactorById(factorId);
    if (factorInfo.isEmpty()) {
        qWarning() << "FactorService::getAvailableStocks: 未找到因子:" << factorId;
        return stocks;
    }
    
    // 连接到数据库
    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    auto database = dbManager.getDatabase();
    if (!database) {
        qWarning() << "FactorService::getAvailableStocks: 无法获取数据库连接";
        return stocks;
    }
    
    try {
        // 从cleaned_daily_bar表获取指定日期的股票列表（清洗后的数据）
        QString sql = "SELECT DISTINCT symbol FROM cleaned_daily_bar WHERE trade_date = :date ORDER BY symbol";
        std::map<QString, QVariant> params;
        params[":date"] = date;
        
        auto queryResult = database->executeQuery(sql, params);
        
        for (size_t i = 0; i < queryResult.rowCount(); i++) {
            const auto& row = queryResult.getRow(i);
            QString symbol = row.getString("symbol");
            stocks.append(symbol);
        }
        
        qDebug() << "FactorService::getAvailableStocks: 从数据库获取到" << stocks.size() << "只股票";
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::getAvailableStocks: 数据库错误:" << e.what();
    }
    
    qDebug() << "FactorService::getAvailableStocks 结束，返回股票数量:" << stocks.size();
    return stocks;
}

// 私有方法：查询数据库数据
QVariantList FactorService::queryDatabaseData(const QString& minDate, const QString& maxDate)
{
    qDebug() << "FactorService::queryDatabaseData 开始，日期范围:" << minDate << "到" << maxDate;
    
    QVariantList result;
    
    // 连接到数据库
    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    auto database = dbManager.getDatabase();
    if (!database) {
        qWarning() << "FactorService::queryDatabaseData: 无法获取数据库连接";
        return result;
    }
    
    try {
        // 从cleaned_daily_bar表查询指定日期范围内的所有股票数据
        QString sql = "SELECT trade_date, symbol, close FROM cleaned_daily_bar "
                     "WHERE trade_date BETWEEN :start_date AND :end_date "
                     "ORDER BY trade_date, symbol";
        std::map<QString, QVariant> params;
        params[":start_date"] = minDate;
        params[":end_date"] = maxDate;
        
        auto queryResult = database->executeQuery(sql, params);
        
        for (size_t i = 0; i < queryResult.rowCount(); i++) {
            const auto& row = queryResult.getRow(i);
            QVariantMap dataMap;
            dataMap["trade_date"] = row.getString("trade_date");
            dataMap["symbol"] = row.getString("symbol");
            dataMap["close"] = row.getDouble("close");
            result.append(dataMap);
        }
        
        qDebug() << "FactorService::queryDatabaseData: 从数据库获取到" << result.size() << "条数据";
        
    } catch (const std::exception& e) {
        qWarning() << "FactorService::queryDatabaseData: 数据库错误:" << e.what();
    }
    
    return result;
}

// 新增辅助方法：从数据映射中提取日期
QString FactorService::extractDateFromDataMap(const QVariantMap& dataMap, int itemIndex)
{
    // 检查所有可能的日期字段名称
    QString date;
    
    // 主要字段名（按优先级）
    if (dataMap.contains("trade_date")) {
        date = dataMap.value("trade_date").toString();
    } else if (dataMap.contains("date")) {
        date = dataMap.value("date").toString();
    } else if (dataMap.contains("Date")) {
        date = dataMap.value("Date").toString();
    } else if (dataMap.contains("TRADE_DATE")) {
        date = dataMap.value("TRADE_DATE").toString();
    } else if (dataMap.contains("DATE")) {
        date = dataMap.value("DATE").toString();
    } else if (dataMap.contains("tradeDate")) {
        date = dataMap.value("tradeDate").toString();
    } else if (dataMap.contains("tradeDateStr")) {
        date = dataMap.value("tradeDateStr").toString();
    } else if (dataMap.contains("交易日期")) {
        date = dataMap.value("交易日期").toString();
    } else if (dataMap.contains("交易日")) {
        date = dataMap.value("交易日").toString();
    } else {
        // 尝试查找任何看起来像日期的字段
        for (const QString& key : dataMap.keys()) {
            if (key.contains("date", Qt::CaseInsensitive) || 
                key.contains("time", Qt::CaseInsensitive) ||
                key.contains("日期", Qt::CaseSensitive) ||
                key.contains("天", Qt::CaseSensitive)) {
                QVariant possibleDate = dataMap.value(key);
                if (possibleDate.canConvert<QString>()) {
                    QString dateStr = possibleDate.toString();
                    // 简单的日期格式验证
                    if (dateStr.length() >= 8 && (dateStr.contains("-") || dateStr.length() == 8)) {
                        date = dateStr;
                        qDebug() << "FactorService::extractDateFromDataMap: 第" << itemIndex 
                                 << "项从字段" << key << "提取到日期:" << date;
                        break;
                    }
                }
            }
        }
    }
    
    return date;
}

// 新增辅助方法：从数据映射中提取股票代码
QString FactorService::extractSymbolFromDataMap(const QVariantMap& dataMap, int itemIndex)
{
    // 检查所有可能的股票代码字段名称
    QString symbol;
    
    // 主要字段名（按优先级）
    if (dataMap.contains("symbol")) {
        symbol = dataMap.value("symbol").toString();
    } else if (dataMap.contains("code")) {
        symbol = dataMap.value("code").toString();
    } else if (dataMap.contains("stock_code")) {
        symbol = dataMap.value("stock_code").toString();
    } else if (dataMap.contains("stockCode")) {
        symbol = dataMap.value("stockCode").toString();
    } else if (dataMap.contains("SYMBOL")) {
        symbol = dataMap.value("SYMBOL").toString();
    } else if (dataMap.contains("CODE")) {
        symbol = dataMap.value("CODE").toString();
    } else if (dataMap.contains("股票代码")) {
        symbol = dataMap.value("股票代码").toString();
    } else if (dataMap.contains("代码")) {
        symbol = dataMap.value("代码").toString();
    } else if (dataMap.contains("ticker")) {
        symbol = dataMap.value("ticker").toString();
    } else {
        // 尝试查找任何看起来像股票代码的字段
        for (const QString& key : dataMap.keys()) {
            if (key.contains("symbol", Qt::CaseInsensitive) || 
                key.contains("code", Qt::CaseInsensitive) ||
                key.contains("股票", Qt::CaseSensitive) ||
                key.contains("代码", Qt::CaseSensitive)) {
                QVariant possibleSymbol = dataMap.value(key);
                if (possibleSymbol.canConvert<QString>()) {
                    QString symbolStr = possibleSymbol.toString();
                    // 简单的股票代码格式验证（6位数字或带后缀）
                    if (symbolStr.length() >= 4) {
                        symbol = symbolStr;
                        qDebug() << "FactorService::extractSymbolFromDataMap: 第" << itemIndex 
                                 << "项从字段" << key << "提取到股票代码:" << symbol;
                        break;
                    }
                }
            }
        }
    }
    
    return symbol;
}

// 新增辅助方法：从数据映射中提取收盘价
double FactorService::extractClosePriceFromDataMap(const QVariantMap& dataMap, int itemIndex)
{
    // 检查所有可能的收盘价字段名称
    double closePrice = -1.0;
    
    // 主要字段名（按优先级）
    if (dataMap.contains("close")) {
        QVariant closeValue = dataMap.value("close");
        if (closeValue.isValid() && closeValue.canConvert<double>()) {
            closePrice = closeValue.toDouble();
        }
    } else if (dataMap.contains("Close")) {
        QVariant closeValue = dataMap.value("Close");
        if (closeValue.isValid() && closeValue.canConvert<double>()) {
            closePrice = closeValue.toDouble();
        }
    } else if (dataMap.contains("closing_price")) {
        QVariant closeValue = dataMap.value("closing_price");
        if (closeValue.isValid() && closeValue.canConvert<double>()) {
            closePrice = closeValue.toDouble();
        }
    } else if (dataMap.contains("CLOSE")) {
        QVariant closeValue = dataMap.value("CLOSE");
        if (closeValue.isValid() && closeValue.canConvert<double>()) {
            closePrice = closeValue.toDouble();
        }
    } else if (dataMap.contains("收盘价")) {
        QVariant closeValue = dataMap.value("收盘价");
        if (closeValue.isValid() && closeValue.canConvert<double>()) {
            closePrice = closeValue.toDouble();
        }
    } else if (dataMap.contains("收盘")) {
        QVariant closeValue = dataMap.value("收盘");
        if (closeValue.isValid() && closeValue.canConvert<double>()) {
            closePrice = closeValue.toDouble();
        }
    } else {
        // 尝试查找任何看起来像价格的字段
        for (const QString& key : dataMap.keys()) {
            if (key.contains("close", Qt::CaseInsensitive) || 
                key.contains("price", Qt::CaseInsensitive) ||
                key.contains("收盘", Qt::CaseSensitive) ||
                key.contains("价", Qt::CaseSensitive)) {
                QVariant possiblePrice = dataMap.value(key);
                if (possiblePrice.isValid() && possiblePrice.canConvert<double>()) {
                    double price = possiblePrice.toDouble();
                    // 简单的价格验证（正数）
                    if (price > 0) {
                        closePrice = price;
                        qDebug() << "FactorService::extractClosePriceFromDataMap: 第" << itemIndex 
                                 << "项从字段" << key << "提取到收盘价:" << closePrice;
                        break;
                    }
                }
            }
        }
    }
    
    return closePrice;
}

// 新增辅助方法：记录数据提取调试信息
void FactorService::logDataExtractionDebugInfo(const QVariantMap& dataMap, int itemIndex, 
                                              const QString& extractedDate, 
                                              const QString& extractedSymbol, 
                                              double extractedClosePrice)
{
    if (extractedDate.isEmpty()) {
        qDebug() << "FactorService::getFactorValuesBatch: 第" << itemIndex << "项缺少日期字段";
        qDebug() << "可用字段:" << dataMap.keys();
        
        // 尝试查找任何看起来像日期的字段
        for (const QString& key : dataMap.keys()) {
            if (key.contains("date", Qt::CaseInsensitive) || 
                key.contains("time", Qt::CaseInsensitive) ||
                key.contains("日期", Qt::CaseSensitive)) {
                QVariant possibleDate = dataMap.value(key);
                qDebug() << "可能包含日期的字段:" << key << "=" << possibleDate;
            }
        }
    }
    
    if (extractedSymbol.isEmpty()) {
        qDebug() << "FactorService::getFactorValuesBatch: 第" << itemIndex << "项缺少股票代码字段";
        qDebug() << "可用字段:" << dataMap.keys();
        
        // 尝试查找任何看起来像股票代码的字段
        for (const QString& key : dataMap.keys()) {
            if (key.contains("symbol", Qt::CaseInsensitive) || 
                key.contains("code", Qt::CaseInsensitive) ||
                key.contains("股票", Qt::CaseSensitive) ||
                key.contains("代码", Qt::CaseSensitive)) {
                QVariant possibleSymbol = dataMap.value(key);
                qDebug() << "可能包含股票代码的字段:" << key << "=" << possibleSymbol;
            }
        }
    }
    
    if (extractedClosePrice < 0) {
        qDebug() << "FactorService::getFactorValuesBatch: 第" << itemIndex << "项收盘价字段无效或缺失";
        qDebug() << "可用字段:" << dataMap.keys();
        
        // 尝试查找任何看起来像价格的字段
        for (const QString& key : dataMap.keys()) {
            if (key.contains("close", Qt::CaseInsensitive) || 
                key.contains("price", Qt::CaseInsensitive) ||
                key.contains("收盘", Qt::CaseSensitive) ||
                key.contains("价", Qt::CaseSensitive)) {
                QVariant possiblePrice = dataMap.value(key);
                qDebug() << "可能包含价格的字段:" << key << "=" << possiblePrice;
            }
        }
    }
}
