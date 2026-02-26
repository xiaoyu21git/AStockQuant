// DataCleaningService_refactored.cpp - 完全重构版本
// 使用foundation线程池，移除Qt线程，桥接只负责转发
// 逻辑部分完全在C++实现

#include "DataCleaningService.h"
#include "DataCleaningEngine.h"
#include "DataServiceCache.h"
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QMap>
#include <QVector>
#include <memory>
#include <algorithm>
#include "foundation/thread/thread_pool.hpp"
#include <atomic>

// PIMPL实现类
class DataCleaningService::Impl {
public:
    struct CleaningTask {
        QString requestId;
        QVariantList data;
        QVariantMap rules;
        QDateTime startTime;
        bool cancelled{false};
        
        CleaningTask() = default;
        CleaningTask(const QString& id, const QVariantList& d, const QVariantMap& r)
            : requestId(id), data(d), rules(r), startTime(QDateTime::currentDateTime()) {}
    };
    
    Impl(DataCleaningService* parent) 
        : m_parent(parent)
        , m_maxPreviewRecords(1000)
        , m_cacheEnabled(true)
        , m_asyncThreadCount(2) {
        qDebug() << "DataCleaningService::Impl: 创建";
    }
    
    ~Impl() {
        qDebug() << "DataCleaningService::Impl: 销毁";
        
        // 取消所有进行中的任务
        cancelAllTasks();
    }
    
    bool initialize() {
        try {
            qDebug() << "DataCleaningService::Impl: 初始化清洗服务...";
            
            // 初始化清洗引擎
            m_cleaningEngine = std::make_unique<DataCleaningEngine>();
            if (!m_cleaningEngine) {
                qCritical() << "DataCleaningService::Impl: 无法创建清洗引擎";
                return false;
            }
            
            // 初始化缓存服务（如果可用）
            m_cacheService = &DataServiceCache::getInstance();
            if (m_cacheService) {
                // 尝试初始化缓存，但不作为必要条件
                bool cacheInitialized = m_cacheService->initializeCache();
                if (!cacheInitialized) {
                    qWarning() << "DataCleaningService::Impl: 缓存服务初始化失败，继续无缓存运行";
                } else {
                    qDebug() << "✅ DataCleaningService::Impl: 缓存服务初始化成功";
                }
            } else {
                qWarning() << "DataCleaningService::Impl: 无法获取缓存服务实例，继续无缓存运行";
            }
            
            // 创建foundation线程池
            m_threadPool = foundation::thread::ThreadPoolFactory::create_fixed(m_asyncThreadCount);
            if (!m_threadPool) {
                qCritical() << "DataCleaningService::Impl: 无法创建foundation线程池";
                return false;
            }
            
            m_initialized = true;
            qDebug() << "✅ DataCleaningService::Impl: 清洗服务初始化成功";
            qDebug() << "   最大预览记录数:" << m_maxPreviewRecords;
            qDebug() << "   异步线程数:" << m_asyncThreadCount;
            qDebug() << "   缓存启用:" << m_cacheEnabled;
            
            return true;
            
        } catch (const std::exception& e) {
            QString error = QString("清洗服务初始化失败: %1").arg(e.what());
            qCritical() << "DataCleaningService::Impl:" << error;
            emit m_parent->cleaningError("", error);
            return false;
        }
    }
    
    // 同步预览清洗效果
    QVariantList previewCleaning(const QVariantList& data, const QVariantMap& rules) {
        if (!isReady()) {
            return QVariantList();
        }
        
        try {
            qDebug() << "DataCleaningService::Impl: 开始同步预览清洗";
            qDebug() << "   数据记录数:" << data.size();
            qDebug() << "   规则数量:" << rules.size();
            
            // 限制预览数据量
            QVariantList previewData = data;
            if (previewData.size() > m_maxPreviewRecords) {
                qDebug() << "   数据量超过最大预览限制(" << m_maxPreviewRecords 
                         << ")，仅使用前" << m_maxPreviewRecords << "条记录";
                previewData = previewData.mid(0, m_maxPreviewRecords);
            }
            
            // 转换规则格式
            QVector<DataCleaningEngine::CleaningRule> cleaningRules = convertRules(rules);
            
            // 执行同步清洗
            QVariantList cleanedData = m_cleaningEngine->cleanData(previewData, cleaningRules);
            
            // 获取清洗统计
            auto stats = m_cleaningEngine->getLastCleaningStats();
            
            qDebug() << "DataCleaningService::Impl: 同步预览完成";
            qDebug() << "   原始记录数:" << stats.totalRecords;
            qDebug() << "   清洗后记录数:" << stats.cleanedRecords;
            qDebug() << "   移除记录数:" << stats.removedRecords;
            qDebug() << "   耗时:" << stats.durationMs << "ms";
            
            return cleanedData;
            
        } catch (const std::exception& e) {
            QString error = QString("预览清洗失败: %1").arg(e.what());
            qCritical() << "DataCleaningService::Impl:" << error;
            return QVariantList();
        }
    }
    
    // 异步执行完整清洗 - 使用foundation线程池
    void executeCleaningAsync(const QString& requestId,
                             const QVariantList& data,
                             const QVariantMap& rules) {
        if (!isReady()) {
            emit m_parent->cleaningError(requestId, "清洗服务未初始化");
            return;
        }
        
        // 检查重复请求
        {
            QMutexLocker locker(&m_tasksMutex);
            if (m_activeTasks.contains(requestId)) {
                qWarning() << "DataCleaningService::Impl: 请求" << requestId << "已在处理中";
                return;
            }
            
            // 创建任务并直接插入
            m_activeTasks.insert(requestId, CleaningTask(requestId, data, rules));
        }
        
        // 使用foundation线程池提交任务
        auto task = [this, requestId, data, rules]() {
            executeCleaningTask(requestId, data, rules);
        };
        
        m_threadPool->post(std::move(task));
        
        // 发送开始信号
        QString description = QString("清洗 %1 条记录").arg(data.size());
        emit m_parent->cleaningStarted(requestId, description);
        
        qDebug() << "DataCleaningService::Impl: 启动异步清洗，请求ID:" << requestId
                 << "，数据记录数:" << data.size()
                 << "，规则数量:" << rules.size();
    }
    
    // 带缓存的清洗
    QVariantList cleanWithCache(const QString& requestId,
                               const QVariantList& data,
                               const QVariantMap& rules) {
        if (!isReady()) {
            return QVariantList();
        }
        
        // 检查缓存
        if (m_cacheEnabled && m_cacheService && m_cacheService->isCacheEnabled()) {
            QString cacheKey = generateCacheKey(requestId, data, rules);
            
            // 尝试从缓存获取
            QVariantList cachedResult = m_cacheService->getCachedCleaningResult(cacheKey);
            if (!cachedResult.isEmpty()) {
                qDebug() << "DataCleaningService::Impl: 缓存命中，请求ID:" << requestId;
                emit m_parent->cacheHit(requestId, cacheKey);
                return cachedResult;
            }
            
            emit m_parent->cacheMiss(requestId, cacheKey);
        }
        
        // 缓存未命中，执行清洗
        qDebug() << "DataCleaningService::Impl: 缓存未命中，执行清洗，请求ID:" << requestId;
        
        // 转换规则格式
        QVector<DataCleaningEngine::CleaningRule> cleaningRules = convertRules(rules);
        
        // 执行清洗
        QVariantList cleanedData = m_cleaningEngine->cleanData(data, cleaningRules);
        
        // 缓存结果
        if (m_cacheEnabled && m_cacheService && m_cacheService->isCacheEnabled() && !cleanedData.isEmpty()) {
            QString cacheKey = generateCacheKey(requestId, data, rules);
            m_cacheService->cacheCleaningResult(cacheKey, cleanedData);
            qDebug() << "DataCleaningService::Impl: 清洗结果已缓存，请求ID:" << requestId;
        }
        
        return cleanedData;
    }
    
    // 取消清洗操作
    void cancelCleaning(const QString& requestId) {
        QMutexLocker locker(&m_tasksMutex);
        
        if (requestId.isEmpty()) {
            // 取消所有任务
            for (auto it = m_activeTasks.begin(); it != m_activeTasks.end(); ++it) {
                it.value().cancelled = true;
            }
            m_activeTasks.clear();
            qDebug() << "DataCleaningService::Impl: 取消所有清洗任务";
        } else {
            // 取消指定任务
            if (m_activeTasks.contains(requestId)) {
                m_activeTasks[requestId].cancelled = true;
                m_activeTasks.remove(requestId);
                qDebug() << "DataCleaningService::Impl: 取消清洗任务，请求ID:" << requestId;
            }
        }
    }
    
    // 规则管理
    QVariantMap getDefaultRules() const {
        if (!isReady()) {
            return QVariantMap();
        }
        
        // 使用DataCleaningEngine的默认规则集
        auto defaultRules = m_cleaningEngine->createDefaultRuleSet();
        return convertCleaningRulesToMap(defaultRules);
    }
    
    void addCustomRule(const QString& ruleName, const QVariantMap& ruleConfig) {
        QMutexLocker locker(&m_customRulesMutex);
        m_customRules[ruleName] = ruleConfig;
        
        emit m_parent->rulesUpdated();
        qDebug() << "DataCleaningService::Impl: 添加自定义规则:" << ruleName;
    }
    
    QVariantMap getCustomRules() const {
        QMutexLocker locker(&m_customRulesMutex);
        return m_customRules;
    }
    
    // 统计和状态
    CleaningStats getLastCleaningStats(const QString& requestId) const {
        CleaningStats stats;
        
        if (!isReady()) {
            return stats;
        }
        
        if (requestId.isEmpty()) {
            // 获取全局统计
            QMutexLocker locker(&m_statsMutex);
            if (!m_allStats.isEmpty()) {
                return m_allStats.last();
            }
        } else {
            // 获取指定请求的统计
            QMutexLocker locker(&m_statsMutex);
            if (m_requestStats.contains(requestId)) {
                return m_requestStats[requestId];
            }
        }
        
        return stats;
    }
    
    bool isCleaningInProgress() const {
        QMutexLocker locker(&m_tasksMutex);
        return !m_activeTasks.isEmpty();
    }
    
    // 配置
    void setCacheEnabled(bool enabled) { m_cacheEnabled = enabled; }
    bool isCacheEnabled() const { return m_cacheEnabled; }
    
    void setMaxPreviewRecords(int maxRecords) { m_maxPreviewRecords = maxRecords; }
    int getMaxPreviewRecords() const { return m_maxPreviewRecords; }
    
    void setAsyncThreadCount(int count) { 
        m_asyncThreadCount = std::max(1, std::min(count, 8));
        // 线程池创建后不能动态修改线程数，这里只是更新配置
        qDebug() << "DataCleaningService::Impl: 设置异步线程数:" << m_asyncThreadCount;
    }
    
    int getAsyncThreadCount() const { return m_asyncThreadCount; }
    
    // 新增方法：重置统计
    void resetStats() {
        QMutexLocker locker(&m_statsMutex);
        m_requestStats.clear();
        m_allStats.clear();
        
        // 注意：DataCleaningEngine的resetCleaningStats方法是私有的，无法访问
        // 我们只需重置Service的统计，引擎的统计会在下次清洗时重置
        
        qDebug() << "DataCleaningService::Impl: 统计已重置";
    }
    
    // 新增方法：获取所有统计
    QVariantMap getAllCleaningStats() const {
        QVariantMap allStatsMap;
        
        if (!isReady()) {
            return allStatsMap;
        }
        
        QMutexLocker locker(&m_statsMutex);
        
        // 添加全局统计摘要
        QVariantMap summary;
        summary["totalRequests"] = m_allStats.size();
        
        int totalRecords = 0;
        int totalCleaned = 0;
        int totalRemoved = 0;
        qint64 totalDurationMs = 0;
        
        for (const auto& stats : m_allStats) {
            totalRecords += stats.totalRecords;
            totalCleaned += stats.cleanedRecords;
            totalRemoved += stats.removedRecords;
            totalDurationMs += stats.durationMs;
        }
        
        if (m_allStats.size() > 0) {
            summary["averageRecords"] = totalRecords / m_allStats.size();
            summary["averageCleaned"] = totalCleaned / m_allStats.size();
            summary["averageRemoved"] = totalRemoved / m_allStats.size();
            summary["averageDurationMs"] = totalDurationMs / m_allStats.size();
        }
        
        summary["totalRecordsProcessed"] = totalRecords;
        summary["totalCleanedRecords"] = totalCleaned;
        summary["totalRemovedRecords"] = totalRemoved;
        summary["totalDurationMs"] = totalDurationMs;
        
        allStatsMap["summary"] = summary;
        
        // 添加详细的请求统计
        QVariantMap requestStatsMap;
        for (auto it = m_requestStats.begin(); it != m_requestStats.end(); ++it) {
            requestStatsMap[it.key()] = it.value().toVariantMap();
        }
        
        allStatsMap["requestStats"] = requestStatsMap;
        
        return allStatsMap;
    }
    
    // 新增方法：获取活动请求列表
    QStringList getActiveCleaningRequests() const {
        QMutexLocker locker(&m_tasksMutex);
        return m_activeTasks.keys();
    }
    
    // 新增方法：删除自定义规则
    void removeCustomRule(const QString& ruleName) {
        QMutexLocker locker(&m_customRulesMutex);
        
        if (m_customRules.contains(ruleName)) {
            m_customRules.remove(ruleName);
            emit m_parent->rulesUpdated();
            qDebug() << "DataCleaningService::Impl: 删除自定义规则:" << ruleName;
        } else {
            qWarning() << "DataCleaningService::Impl: 尝试删除不存在的规则:" << ruleName;
        }
    }
    
private:
    bool isReady() const {
        return m_initialized && m_cleaningEngine != nullptr;
    }
    
    void cancelAllTasks() {
        cancelCleaning(QString());
        
        if (m_threadPool) {
            m_threadPool->shutdown(true);
        }
    }
    
    QString generateCacheKey(const QString& requestId,
                           const QVariantList& data,
                           const QVariantMap& rules) {
        // 生成缓存键：请求ID + 数据量 + 规则数量
        QString key = QString("%1_%2_%3")
            .arg(requestId)
            .arg(data.size())
            .arg(rules.size());
        
        // 添加规则摘要
        for (const auto& ruleName : rules.keys()) {
            key += "_" + ruleName;
        }
        
        return key;
    }
    
    QVector<DataCleaningEngine::CleaningRule> convertRules(const QVariantMap& rules) const {
        QVector<DataCleaningEngine::CleaningRule> cleaningRules;
        qDebug() << "DataCleaningService::Impl::convertRules: 开始转换规则，规则数量:" << rules.size();
        
        // 转换时间范围规则
        if (rules.contains("timeRange")) {
            QVariantMap timeRange = rules["timeRange"].toMap();
            // 如果timeRange是空对象，也视为启用（向后兼容）
            bool enabled = timeRange.isEmpty() || timeRange.value("enabled", true).toBool();
            if (enabled) {
                DataCleaningEngine::CleaningRule rule(
                    DataCleaningEngine::RULE_TIME_RANGE,
                    "时间范围过滤",
                    "过滤指定时间范围之外的数据"
                );
                rule.parameters["startDate"] = timeRange["start"].toString();
                rule.parameters["endDate"] = timeRange["end"].toString();
                rule.enabled = true;
                cleaningRules.append(rule);
                qDebug() << "  时间范围规则: start=" << rule.parameters["startDate"] << ", end=" << rule.parameters["endDate"];
            }
        }
        
        // 转换价格过滤规则
        if (rules.contains("priceFilter") && rules["priceFilter"].toMap()["enabled"].toBool()) {
            QVariantMap priceFilter = rules["priceFilter"].toMap();
            DataCleaningEngine::CleaningRule rule(
                DataCleaningEngine::RULE_PRICE_FILTER,
                "价格过滤",
                "过滤价格异常的数据"
            );
            rule.parameters["minPrice"] = priceFilter["min"].toDouble();
            rule.parameters["maxPrice"] = priceFilter["max"].toDouble();
            rule.parameters["checkOpen"] = true;
            rule.parameters["checkHigh"] = true;
            rule.parameters["checkLow"] = true;
            rule.parameters["checkClose"] = true;
            rule.enabled = true;
            cleaningRules.append(rule);
            qDebug() << "  价格过滤规则: min=" << rule.parameters["minPrice"] << ", max=" << rule.parameters["maxPrice"];
        }
        
        // 转换成交量过滤规则
        if (rules.contains("volumeFilter") && rules["volumeFilter"].toMap()["enabled"].toBool()) {
            QVariantMap volumeFilter = rules["volumeFilter"].toMap();
            DataCleaningEngine::CleaningRule rule(
                DataCleaningEngine::RULE_VOLUME_FILTER,
                "成交量过滤",
                "过滤成交量异常的数据"
            );
            // 兼容两种参数名：min/minVolume 和 maxVolume
            if (volumeFilter.contains("minVolume")) {
                rule.parameters["minVolume"] = volumeFilter["minVolume"].toDouble();
            } else if (volumeFilter.contains("min")) {
                rule.parameters["minVolume"] = volumeFilter["min"].toDouble();
            } else {
                rule.parameters["minVolume"] = 0.0; // 默认值
            }
            
            if (volumeFilter.contains("maxVolume")) {
                rule.parameters["maxVolume"] = volumeFilter["maxVolume"].toDouble();
            } else {
                rule.parameters["maxVolume"] = 1000000000.0; // 默认值10亿
            }
            rule.enabled = true;
            cleaningRules.append(rule);
            qDebug() << "  成交量过滤规则: minVolume=" << rule.parameters["minVolume"] << ", maxVolume=" << rule.parameters["maxVolume"];
        }
        
        // 转换完整性检查规则
        if (rules.contains("completenessFilter") && rules["completenessFilter"].toBool()) {
            DataCleaningEngine::CleaningRule rule(
                DataCleaningEngine::RULE_COMPLETENESS_CHECK,
                "完整性检查",
                "检查数据字段完整性"
            );
            rule.parameters["requiredFields"] = QStringList{"symbol", "date", "open", "high", "low", "close", "volume"};
            rule.enabled = true;
            cleaningRules.append(rule);
            qDebug() << "  完整性检查规则已启用";
        }
        
        // 转换异常值检测规则
        if (rules.contains("outlierFilter") && rules["outlierFilter"].toBool()) {
            DataCleaningEngine::CleaningRule rule(
                DataCleaningEngine::RULE_OUTLIER_DETECTION,
                "异常值检测",
                "检测并过滤异常值"
            );
            rule.parameters["priceDeviation"] = 3.0; // 3倍标准差
            rule.parameters["volumeDeviation"] = 5.0; // 5倍标准差
            rule.enabled = true;
            cleaningRules.append(rule);
            qDebug() << "  异常值检测规则已启用";
        }
        
        // 转换市场过滤规则
        if (rules.contains("market") && rules["market"].toMap()["aShares"].toBool()) {
            // A股市场过滤规则
            DataCleaningEngine::CleaningRule rule(
                DataCleaningEngine::RULE_FORMAT_VALIDATION,
                "A股市场验证",
                "验证A股股票代码格式"
            );
            rule.parameters["symbolPattern"] = "^[0-9]{6}\\.[A-Z]{2}$";
            rule.enabled = true;
            cleaningRules.append(rule);
            qDebug() << "  A股市场验证规则已启用";
        }
        
        qDebug() << "DataCleaningService::Impl::convertRules: 转换完成，生成" << cleaningRules.size() << "个清洗规则";
        return cleaningRules;
    }
    
    QVariantMap convertCleaningRulesToMap(const QVector<DataCleaningEngine::CleaningRule>& rules) const {
        QVariantMap rulesMap;
        
        for (const auto& rule : rules) {
            QVariantMap ruleMap;
            ruleMap["name"] = rule.name;
            ruleMap["description"] = rule.description;
            ruleMap["parameters"] = rule.parameters;
            ruleMap["enabled"] = rule.enabled;
            
            rulesMap[rule.name] = ruleMap;
        }
        
        return rulesMap;
    }
    
    void onCleaningTaskCompleted(const QString& requestId,
                                bool success,
                                const QString& message,
                                const QVariantList& cleanedData,
                                const CleaningStats& stats) {
        // 从活动任务中移除
        {
            QMutexLocker locker(&m_tasksMutex);
            m_activeTasks.remove(requestId);
        }
        
        // 更新统计
        {
            QMutexLocker locker(&m_statsMutex);
            m_requestStats[requestId] = stats;
            m_allStats.append(stats);
            
            // 保持最近100个统计
            if (m_allStats.size() > 100) {
                m_allStats.removeFirst();
            }
        }
        
        // 发送完成信号
        emit m_parent->cleaningCompleted(requestId, success, message, cleanedData);
        emit m_parent->cleaningStatsUpdated(requestId, stats);
        
        if (success) {
            qDebug() << "DataCleaningService::Impl: 清洗任务完成，请求ID:" << requestId
                     << "，消息:" << message;
        } else {
            qCritical() << "DataCleaningService::Impl: 清洗任务失败，请求ID:" << requestId
                       << "，错误:" << message;
        }
    }
    
    // 执行清洗任务（在工作线程中）
    void executeCleaningTask(const QString& requestId,
                            const QVariantList& data,
                            const QVariantMap& rules) {
        qDebug() << "DataCleaningService::Impl: 开始执行清洗任务，请求ID:" << requestId 
                 << "，数据量:" << data.size();
        
        // 使用QPointer跟踪父对象，避免悬空指针
        QPointer<DataCleaningService> parentService = m_parent;
        DataCleaningEngine* engine = m_cleaningEngine.get();
        
        try {
            // 检查父对象是否仍然有效
            if (!parentService) {
                qCritical() << "DataCleaningService::Impl: 父服务对象已销毁";
                return;
            }
            
            // 检查引擎是否有效
            if (!engine) {
                qCritical() << "DataCleaningService::Impl: 清洗引擎未初始化";
                throw std::runtime_error("清洗引擎未初始化");
            }
            
            // 发送进度信号到主线程
            QMetaObject::invokeMethod(parentService, [parentService, requestId]() {
                if (parentService) {
                    emit parentService->cleaningProgress(requestId, 0, "开始数据清洗...");
                }
            }, Qt::QueuedConnection);
            
            // 检查是否被取消
            {
                QMutexLocker locker(&m_tasksMutex);
                if (m_activeTasks.contains(requestId)) {
                    auto& task = m_activeTasks[requestId];
                    if (task.cancelled) {
                        qDebug() << "DataCleaningService::Impl: 任务已被取消，请求ID:" << requestId;
                        
                        // 从活动任务中移除
                        m_activeTasks.remove(requestId);
                        
                        // 发送取消信号到主线程
                        QMetaObject::invokeMethod(parentService, [parentService, requestId]() {
                            if (parentService) {
                                emit parentService->cleaningError(requestId, "清洗任务已取消");
                            }
                        }, Qt::QueuedConnection);
                        
                        return;
                    }
                }
            }
            
            // 转换规则格式
            QVector<DataCleaningEngine::CleaningRule> cleaningRules = convertRules(rules);
            qDebug() << "DataCleaningService::Impl: 转换规则完成，规则数:" << cleaningRules.size();
            
            // 连接进度信号
            QString requestIdCopy = requestId;
            auto progressConnection = QObject::connect(engine, &DataCleaningEngine::cleaningProgress,
                parentService, [requestIdCopy, parentService](int progress, const QString& message) {
                    // 检查父对象是否仍然有效
                    if (!parentService) {
                        return;
                    }
                    
                    qDebug() << "清洗进度: 请求ID:" << requestIdCopy << "进度:" << progress << "% 消息:" << message;
                    // 转发进度信号，使用请求ID
                    emit parentService->cleaningProgress(requestIdCopy, progress, message);
                }, Qt::QueuedConnection);
            
            // 执行清洗
            qDebug() << "DataCleaningService::Impl: 开始执行清洗，数据量:" << data.size();
            QVariantList cleanedData = engine->cleanData(data, cleaningRules);
            qDebug() << "DataCleaningService::Impl: 清洗完成，结果数据量:" << cleanedData.size();
            
            // 断开连接
            QObject::disconnect(progressConnection);
            
            // 获取清洗统计
            auto engineStats = engine->getLastCleaningStats();
            CleaningStats stats;
            stats.totalRecords = engineStats.totalRecords;
            stats.cleanedRecords = engineStats.cleanedRecords;
            stats.removedRecords = engineStats.removedRecords;
            stats.durationMs = engineStats.durationMs;
            stats.startTime = QDateTime::currentDateTime().addMSecs(-stats.durationMs);
            stats.endTime = QDateTime::currentDateTime();
            stats.ruleStats = engineStats.ruleStats;
            
            qDebug() << "DataCleaningService::Impl: 清洗统计 - 总记录:" << stats.totalRecords
                     << "，清洗后:" << stats.cleanedRecords
                     << "，移除:" << stats.removedRecords
                     << "，耗时:" << stats.durationMs << "ms";
            
            // 发送完成信号到主线程
            QMetaObject::invokeMethod(parentService, [this, parentService, requestId, cleanedData, stats]() {
                // 检查父对象是否仍然有效
                if (!parentService) {
                    qWarning() << "DataCleaningService::Impl: 父服务对象已销毁，无法发送完成信号";
                    return;
                }
                
                onCleaningTaskCompleted(requestId, true, "清洗完成", cleanedData, stats);
            }, Qt::QueuedConnection);
            
        } catch (const std::exception& e) {
            QString error = QString("清洗失败: %1").arg(e.what());
            qCritical() << "DataCleaningService::Impl: " << error;
            
            // 发送错误信号到主线程
            if (parentService) {
                QMetaObject::invokeMethod(parentService, 
                    [this, parentService, requestId, error]() {
                        // 检查父对象是否仍然有效
                        if (!parentService) {
                            qWarning() << "DataCleaningService::Impl: 父服务对象已销毁，无法发送错误信号";
                            return;
                        }
                        
                        onCleaningTaskCompleted(requestId, false, error, QVariantList(), CleaningStats());
                    }, 
                    Qt::QueuedConnection
                );
            }
        }
    }
    
    DataCleaningService* m_parent;
    std::unique_ptr<DataCleaningEngine> m_cleaningEngine;
    DataServiceCache* m_cacheService{nullptr};
    std::shared_ptr<foundation::thread::IExecutor> m_threadPool;
    
    // 配置
    int m_maxPreviewRecords;
    bool m_cacheEnabled;
    int m_asyncThreadCount;
    
    // 状态管理
    bool m_initialized{false};
    QMap<QString, CleaningTask> m_activeTasks;
    mutable QMutex m_tasksMutex;
    
    // 统计
    mutable QMutex m_statsMutex;
    QMap<QString, CleaningStats> m_requestStats;
    QVector<CleaningStats> m_allStats;
    
    // 自定义规则
    mutable QMutex m_customRulesMutex;
    QVariantMap m_customRules;
};

// ============ DataCleaningService 公共接口实现 ============

DataCleaningService::DataCleaningService(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this)) {
    qDebug() << "DataCleaningService: 创建";
}

DataCleaningService::~DataCleaningService() {
    qDebug() << "DataCleaningService: 销毁";
}

bool DataCleaningService::initialize() {
    if (m_initialized) {
        return true;
    }
    
    bool success = m_impl->initialize();
    if (success) {
        m_initialized = true;
        qDebug() << "✅ DataCleaningService: 初始化成功";
    } else {
        qCritical() << "❌ DataCleaningService: 初始化失败";
    }
    
    return success;
}

// 核心接口实现
QVariantList DataCleaningService::previewCleaning(const QVariantList& data, 
                                                 const QVariantMap& rules) {
    return m_impl->previewCleaning(data, rules);
}

void DataCleaningService::executeCleaningAsync(const QString& requestId,
                                              const QVariantList& data,
                                              const QVariantMap& rules) {
    m_impl->executeCleaningAsync(requestId, data, rules);
}

void DataCleaningService::batchCleanAsync(const QStringList& requestIds,
                                         const QVector<QVariantList>& dataList,
                                         const QVariantMap& rules) {
    // 简化实现：依次执行
    for (int i = 0; i < requestIds.size() && i < dataList.size(); i++) {
        executeCleaningAsync(requestIds[i], dataList[i], rules);
    }
}

QVariantList DataCleaningService::cleanWithCache(const QString& requestId,
                                                const QVariantList& data,
                                                const QVariantMap& rules) {
    return m_impl->cleanWithCache(requestId, data, rules);
}

void DataCleaningService::cancelCleaning(const QString& requestId) {
    m_impl->cancelCleaning(requestId);
}

// 规则管理
QVariantMap DataCleaningService::getDefaultRules() const {
    return m_impl->getDefaultRules();
}

QVariantMap DataCleaningService::getTechnicalAnalysisRules() const {
    // 实现技术分析规则集
    QVariantMap rules;
    // 可以调用m_impl->m_cleaningEngine->createTechnicalAnalysisRuleSet()
    return rules;
}

QVariantMap DataCleaningService::getFundamentalAnalysisRules() const {
    // 实现基本面分析规则集
    QVariantMap rules;
    // 可以调用m_impl->m_cleaningEngine->createFundamentalAnalysisRuleSet()
    return rules;
}

void DataCleaningService::addCustomRule(const QString& ruleName, 
                                       const QVariantMap& ruleConfig) {
    m_impl->addCustomRule(ruleName, ruleConfig);
}

void DataCleaningService::removeCustomRule(const QString& ruleName) {
    m_impl->removeCustomRule(ruleName);
}

QVariantMap DataCleaningService::getCustomRules() const {
    return m_impl->getCustomRules();
}

// 统计和状态
CleaningStats DataCleaningService::getLastCleaningStats(const QString& requestId) const {
    return m_impl->getLastCleaningStats(requestId);
}

QVariantMap DataCleaningService::getAllCleaningStats() const {
    return m_impl->getAllCleaningStats();
}

void DataCleaningService::resetStats() {
    m_impl->resetStats();
}

bool DataCleaningService::isCleaningInProgress() const {
    return m_impl->isCleaningInProgress();
}

QStringList DataCleaningService::getActiveCleaningRequests() const {
    return m_impl->getActiveCleaningRequests();
}

// 配置
void DataCleaningService::setCacheEnabled(bool enabled) {
    m_impl->setCacheEnabled(enabled);
}

bool DataCleaningService::isCacheEnabled() const {
    return m_impl->isCacheEnabled();
}

void DataCleaningService::setMaxPreviewRecords(int maxRecords) {
    m_impl->setMaxPreviewRecords(maxRecords);
}

int DataCleaningService::getMaxPreviewRecords() const {
    return m_impl->getMaxPreviewRecords();
}

void DataCleaningService::setAsyncThreadCount(int count) {
    m_impl->setAsyncThreadCount(count);
}

int DataCleaningService::getAsyncThreadCount() const {
    return m_impl->getAsyncThreadCount();
}

// 工厂函数
std::shared_ptr<DataCleaningService> createDataCleaningService(QObject* parent) {
    auto service = std::make_shared<DataCleaningService>(parent);
    
    // 初始化服务
    bool initialized = service->initialize();
    if (!initialized) {
        qWarning() << "DataCleaningService创建失败";
        return nullptr;
    }
    
    qDebug() << "✅ DataCleaningService创建成功";
    return service;
}