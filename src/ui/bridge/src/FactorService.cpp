// FactorService.cpp
// 因子服务层实现 - 负责业务逻辑

#include "../../ui/bridge/include/FactorService.h"
#include "../../ui/bridge/include/FactorViewModel.h"
#include "../../ui/bridge/include/DatabaseConnectionManager.h"
#include "../../infrastructure/include/database/FactorRepository.h"
#include "../../infrastructure/include/database/DatabaseConfig.h"
#include "../../ui/bridge/include/DataServiceCache.h"
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>

using namespace astock::database;

namespace {

QStringList variantToStringList(const QVariant& value)
{
    QStringList result;

    const QVariantList list = value.toList();
    for (const QVariant& item : list) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty()) {
            result.append(text);
        }
    }

    if (result.isEmpty()) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            result.append(text);
        }
    }

    return result;
}

QString normalizeFactorType(const QString& majorCategory)
{
    if (majorCategory == "价值因子") {
        return "value";
    }
    if (majorCategory == "动量因子") {
        return "momentum";
    }
    if (majorCategory == "规模因子") {
        return "size";
    }
    if (majorCategory == "质量因子") {
        return "quality";
    }
    if (majorCategory == "成长因子") {
        return "growth";
    }

    return majorCategory.trimmed().toLower();
}

QString makeUnsupportedMetricError(const QString& factorType, const QString& metric)
{
    return QString("因子类型 %1 暂不支持参数: %2").arg(factorType, metric);
}

} // namespace

// 单例实例定义
FactorService* FactorService::m_instance = nullptr;
QMutex FactorService::m_instanceMutex;

// 单例访问方法
FactorService* FactorService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new FactorService();
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
    , m_autoInitialize(true)
    , m_viewModel(new FactorViewModel(this))
{
    qDebug() << "FactorService constructor (autoInitialize=true)";
    qDebug() << "FactorService: 创建FactorViewModel实例，地址:" << m_viewModel;

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

    QMutexLocker locker(&m_initMutex);
    if (m_initialized) {
        qDebug() << "FactorService::initialize: 已经初始化，跳过";
        return;
    }

    try {
        initializeRepository();

        if (!m_repository) {
            qWarning() << "FactorService::initialize: 仓储初始化失败";
            return;
        }

        m_initialized = true;
        qDebug() << "✅ FactorService::initialize: 因子服务初始化完成";

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
    }
}

QString FactorService::addFactor(const QVariantMap& factorData)
{
    qDebug() << "FactorService::addFactor 开始，数据:" << factorData;

    QString errorMessage;
    if (!validateFactorData(factorData, errorMessage)) {
        qWarning() << "因子数据验证失败:" << errorMessage;
        return QString();
    }

    QVariantMap dataToSave = factorData;
    if (!dataToSave.contains("factorId") || dataToSave["factorId"].toString().isEmpty()) {
        QString factorName = dataToSave["factorName"].toString();
        QString factorId = generateFactorId(factorName);
        dataToSave["factorId"] = factorId;
    }

    QString factorId = dataToSave["factorId"].toString();

    bool dbSuccess = saveFactorToDatabase(dataToSave);
    if (!dbSuccess) {
        QString errorMsg = QString("因子保存到数据库失败: %1").arg(factorId);
        qWarning() << errorMsg;
        return QString();
    }

    {
        QWriteLocker locker(&m_rwLock);
        m_memoryCache[factorId] = dataToSave;

        QString cacheKey = QString("factor_%1").arg(factorId);
        QVariantList factorList;
        factorList.append(dataToSave);
        DataServiceCache::getInstance().storeData(cacheKey, factorList);
    }

    if (m_viewModel) {
        m_viewModel->appendData(dataToSave);
    }

    emit factorAdded(factorId, dataToSave);
    emit dataChanged();

    qDebug() << "FactorService::addFactor 结束，新增因子ID:" << factorId;
    return factorId;
}

bool FactorService::updateFactor(const QString& factorId, const QVariantMap& factorData)
{
    qDebug() << "FactorService::updateFactor 开始，因子ID:" << factorId;

    QString errorMessage;
    if (!validateFactorData(factorData, errorMessage)) {
        qWarning() << "因子数据验证失败:" << errorMessage;
        return false;
    }

    QVariantMap dataToUpdate = factorData;
    dataToUpdate["factorId"] = factorId;

    bool dbSuccess = updateFactorInDatabase(factorId, dataToUpdate);
    if (!dbSuccess) {
        QString errorMsg = QString("因子更新到数据库失败: %1").arg(factorId);
        qWarning() << errorMsg;
        return false;
    }

    saveFactorToCache(factorId, dataToUpdate);

    if (m_viewModel) {
        m_viewModel->updateFactor(factorId, dataToUpdate);
    }

    emit factorUpdated(factorId, dataToUpdate);
    emit dataChanged();

    qDebug() << "FactorService::updateFactor 结束，更新因子ID:" << factorId;
    return true;
}

bool FactorService::deleteFactor(const QString& factorId)
{
    qDebug() << "FactorService::deleteFactor 开始，因子ID:" << factorId;

    bool dbSuccess = deleteFactorFromDatabase(factorId);
    if (!dbSuccess) {
        QString errorMsg = QString("因子从数据库删除失败: %1").arg(factorId);
        qWarning() << errorMsg;
        return false;
    }

    removeFactorFromCache(factorId);

    if (m_viewModel) {
        m_viewModel->removeFactor(factorId);
    }

    emit factorDeleted(factorId);
    emit dataChanged();

    qDebug() << "FactorService::deleteFactor 结束，删除因子ID:" << factorId;
    return true;
}

QVariantMap FactorService::getFactorById(const QString& factorId)
{
    qDebug() << "FactorService::getFactorById 开始，因子ID:" << factorId;

    QVariantMap cachedFactor = loadFactorFromCache(factorId);
    if (!cachedFactor.isEmpty() && cachedFactor.contains("parameters")) {
        qDebug() << "从缓存获取因子:" << factorId;
        return cachedFactor;
    }

    if (!cachedFactor.isEmpty()) {
        qDebug() << "缓存中的因子缺少参数信息，重新从数据库加载:" << factorId;
    }

    if (!m_repository) {
        qWarning() << "FactorService::getFactorById: Repository not initialized";
        return QVariantMap();
    }

    try {
        QVariantMap factorMap = m_repository->findById(factorId);
        if (!factorMap.isEmpty()) {
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
        QString factorType = normalizeFactorType(majorCategory);
        QVariantMap parameters = factorInfo.value("parameters").toMap();

        auto buildErrorResult = [&](const QString& errorMessage) {
            result["factorId"] = factorId;
            result["date"] = date;
            result["stockValues"] = QVariantMap();
            result["count"] = 0;
            result["status"] = "error";
            result["error"] = errorMessage;
        };

        auto buildSuccessResult = [&](const QVariantMap& stockValues, const QString& message = QString()) {
            result["factorId"] = factorId;
            result["date"] = date;
            result["stockValues"] = stockValues;
            result["count"] = stockValues.size();
            result["status"] = "success";
            if (!message.isEmpty()) {
                result["message"] = message;
            }
        };
        
        // 根据因子类型计算因子值
        if (factorName == "pe_ttm_factor" || factorType == "value") {
            const QStringList requestedMetrics = variantToStringList(parameters.value("valuationMetrics"));
            QString selectedMetric = requestedMetrics.isEmpty() ? "pe_ttm" : requestedMetrics.first().trimmed().toLower();
            QString sql;
            QString columnName;

            if (selectedMetric == "pe_ttm") {
                sql = "SELECT symbol, pe_ratio FROM daily_bar WHERE trade_date = :date AND pe_ratio IS NOT NULL";
                columnName = "pe_ratio";
            } else if (selectedMetric == "pb") {
                sql = "SELECT symbol, pb_ratio FROM daily_bar WHERE trade_date = :date AND pb_ratio IS NOT NULL";
                columnName = "pb_ratio";
            } else {
                buildErrorResult(makeUnsupportedMetricError("value", selectedMetric));
                return result;
            }

            std::map<QString, QVariant> params;
            params[":date"] = date;
            
            auto queryResult = database->executeQuery(sql, params);
            
            QVariantMap stockValues;
            for (size_t i = 0; i < queryResult.rowCount(); i++) {
                const auto& row = queryResult.getRow(i);
                QString symbol = row.getString("symbol");
                double rawValue = row.getDouble(columnName);
                if (rawValue > 0) {
                    stockValues[symbol] = 1.0 / rawValue;
                }
            }

            buildSuccessResult(stockValues);

        } else if (factorName == "momentum_60d" || factorType == "momentum") {
            const int window = (std::max)(1, parameters.value("window", 60).toInt());
            const int skipRecent = (std::max)(0, parameters.value("skipRecent", 20).toInt());
            const QString momentumType = parameters.value("type", "simple").toString().trimmed().toLower();

            if (momentumType != "simple" && momentumType != "rank") {
                buildErrorResult(makeUnsupportedMetricError("momentum", momentumType));
                return result;
            }

            const QDate currentDate = QDate::fromString(date, "yyyy-MM-dd");
            if (!currentDate.isValid()) {
                buildErrorResult(QString("非法日期: %1").arg(date));
                return result;
            }

            const QDate endDate = currentDate.addDays(-skipRecent);
            const QDate startDate = endDate.addDays(-window);
            QString sql = "SELECT curr.symbol, curr.close AS current_close, prev.close AS previous_close "
                          "FROM cleaned_daily_bar curr "
                          "JOIN cleaned_daily_bar prev ON curr.symbol = prev.symbol "
                          "WHERE curr.trade_date = :end_date AND prev.trade_date = :start_date";
            std::map<QString, QVariant> params;
            params[":end_date"] = endDate.toString("yyyy-MM-dd");
            params[":start_date"] = startDate.toString("yyyy-MM-dd");
            
            auto queryResult = database->executeQuery(sql, params);
            
            QVariantMap stockValues;
            std::vector<std::pair<QString, double>> momentumValues;
            for (size_t i = 0; i < queryResult.rowCount(); i++) {
                const auto& row = queryResult.getRow(i);
                QString symbol = row.getString("symbol");
                double currentClose = row.getDouble("current_close");
                double previousClose = row.getDouble("previous_close");
                if (currentClose > 0 && previousClose > 0) {
                    double momentum = (currentClose - previousClose) / previousClose;
                    momentumValues.emplace_back(symbol, momentum);
                    stockValues[symbol] = momentum;
                }
            }

            if (momentumType == "rank" && momentumValues.size() > 1) {
                std::sort(momentumValues.begin(), momentumValues.end(), [](const auto& left, const auto& right) {
                    return left.second < right.second;
                });

                QVariantMap rankedValues;
                const double denominator = static_cast<double>(momentumValues.size() - 1);
                for (size_t index = 0; index < momentumValues.size(); ++index) {
                    rankedValues[momentumValues[index].first] = static_cast<double>(index) / denominator;
                }
                buildSuccessResult(rankedValues);
            } else {
                buildSuccessResult(stockValues);
            }

        } else if (factorType == "size") {
            const QString sizeMetric = parameters.value("sizeMetric", "market_cap").toString().trimmed().toLower();
            if (sizeMetric != "market_cap") {
                buildErrorResult(makeUnsupportedMetricError("size", sizeMetric));
                return result;
            }

            QString sql = "SELECT symbol, market_cap FROM daily_bar WHERE trade_date = :date AND market_cap IS NOT NULL AND market_cap > 0";
            std::map<QString, QVariant> params;
            params[":date"] = date;

            auto queryResult = database->executeQuery(sql, params);

            QVariantMap stockValues;
            for (size_t i = 0; i < queryResult.rowCount(); i++) {
                const auto& row = queryResult.getRow(i);
                QString symbol = row.getString("symbol");
                double marketCap = row.getDouble("market_cap");
                if (marketCap > 0) {
                    stockValues[symbol] = -std::log(marketCap);
                }
            }

            buildSuccessResult(stockValues);

        } else {
            // 其他因子：返回空结果，表示需要外部计算
            buildSuccessResult(QVariantMap(), "因子需要外部计算");
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
    
    try {
        QVariantMap batchResult;
        int totalRows = 0;

        for (const QString& date : dates) {
            QString cacheKey = QString("factor_values_%1_%2").arg(factorId).arg(date);
            QVariantList cachedData = DataServiceCache::getInstance().getData(cacheKey);

            if (!cachedData.isEmpty() && cachedData[0].canConvert<QVariantMap>()) {
                QVariantMap cachedResult = cachedData[0].toMap();
                if (cachedResult["status"].toString() == "success") {
                    batchResult[date] = cachedResult;
                    totalRows += cachedResult["count"].toInt();
                    qDebug() << "FactorService::getFactorValuesBatch: 从缓存获取数据，日期:" << date;
                    continue;
                }
            }

            QVariantMap dayResult = getFactorValues(factorId, date);
            if (dayResult.isEmpty()) {
                dayResult["factorId"] = factorId;
                dayResult["date"] = date;
                dayResult["stockValues"] = QVariantMap();
                dayResult["count"] = 0;
                dayResult["status"] = "error";
                dayResult["error"] = "因子值计算失败";
            }

            batchResult[date] = dayResult;
            if (dayResult["status"].toString() == "success") {
                totalRows += dayResult["count"].toInt();
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
