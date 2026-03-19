#include "DataCleaningEngine.h"
#include "DataCleaningPersistence.h"

#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <QUuid>

DataCleaningEngine::DataCleaningEngine(QObject *parent)
    : QObject(parent)
{
    // 初始化默认规则集
    m_rules = createDefaultRuleSet();
    qDebug() << "DataCleaningEngine initialized with" << m_rules.size() << "default rules";
}

DataCleaningEngine::~DataCleaningEngine()
{
    // 清理资源
    // 确保所有定时器都已停止
    // 清空上下文数据
    m_seenKeys.clear();
    m_cleaningContext.clear();
}

void DataCleaningEngine::addRule(const CleaningRule& rule)
{
    QMutexLocker locker(&m_mutex);
    
    // 检查是否已存在同名规则
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].name == rule.name) {
            m_rules[i] = rule; // 更新现有规则
            emit rulesUpdated();
            return;
        }
    }
    
    // 添加新规则
    m_rules.append(rule);
    emit rulesUpdated();
    qDebug() << "Added cleaning rule:" << rule.name;
}

void DataCleaningEngine::removeRule(const QString& ruleName)
{
    QMutexLocker locker(&m_mutex);
    
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].name == ruleName) {
            m_rules.removeAt(i);
            emit rulesUpdated();
            qDebug() << "Removed cleaning rule:" << ruleName;
            return;
        }
    }
    
    qWarning() << "Rule not found:" << ruleName;
}

void DataCleaningEngine::setRuleEnabled(const QString& ruleName, bool enabled)
{
    QMutexLocker locker(&m_mutex);
    
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].name == ruleName) {
            m_rules[i].enabled = enabled;
            emit rulesUpdated();
            qDebug() << "Rule" << ruleName << (enabled ? "enabled" : "disabled");
            return;
        }
    }
    
    qWarning() << "Rule not found:" << ruleName;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningEngine::getRules() const
{
    QMutexLocker locker(&m_mutex);
    return m_rules;
}

QVariantList DataCleaningEngine::cleanData(const QVariantList& data,
                                      const QVector<CleaningRule>& rules)
{
    // 重置内部状态，确保每次清洗都是独立的
    {
        QMutexLocker locker(&m_mutex);
        m_seenKeys.clear();  // 清空重复键检测
        m_cleaningContext.clear();  // 清空清洗上下文
    }

    // 先在不加锁的情况下准备数据
    int total = data.size();

    // 检查数据是否为空
    if (total == 0) {
        qWarning() << "No data to clean";
        CleaningStats stats;
        stats.totalRecords = 0;
        stats.cleanedRecords = 0;
        stats.removedRecords = 0;
        stats.startTime = QDateTime::currentDateTime();
        stats.endTime = QDateTime::currentDateTime();
        stats.durationMs = 0;
        // 不发送信号到UI
        // emit cleaningCompleted(stats);
        qDebug() << "数据清洗完成：无数据可清洗";
        return QVariantList(); // 返回空列表
    }
    
    // 使用传入的规则或默认规则
    QVector<CleaningRule> rulesToUse = rules.isEmpty() ? getRules() : rules;
    
    // 过滤启用的规则
    QVector<CleaningRule> enabledRules;
    for (const auto& rule : rulesToUse) {
        if (rule.enabled && validateRuleParameters(rule)) {
            enabledRules.append(rule);
        }
    }
    
    if (enabledRules.isEmpty()) {
        qWarning() << "No enabled rules to apply";
        // 发送清洗完成信号，但也要发送一个初始进度信号让UI知道开始了
        emit cleaningProgress(0, QString("开始数据清洗，共%1条记录").arg(total));
        
        // 使用QTimer::singleShot延迟发送完成信号，避免信号堆积
        // 使用QPointer保护，防止对象在定时器触发前被销毁
        QPointer<DataCleaningEngine> self = this;
        QTimer::singleShot(100, [self, total]() {
            if (!self) {
                qWarning() << "DataCleaningEngine对象已销毁，跳过进度信号发送";
                return;
            }
            
            emit self->cleaningProgress(100, QString("清洗完成: 没有启用的规则，返回原始数据"));
            
            CleaningStats stats;
            stats.totalRecords = total;
            stats.cleanedRecords = total;
            stats.removedRecords = 0;
            stats.startTime = QDateTime::currentDateTime().addMSecs(-100);
            stats.endTime = QDateTime::currentDateTime();
            stats.durationMs = 100;
            emit self->cleaningCompleted(stats);
        });
        
        return data; // 返回原始数据
    }
    
    qDebug() << "Starting data cleaning with" << enabledRules.size() << "enabled rules, total records:" << total;
    
    // 使用局部变量进行清洗，避免在锁内进行复杂操作
    QVector<QString> seenKeys;
    QVariantMap cleaningContext;
    QVariantList cleanedData;
    CleaningStats stats;
    stats.totalRecords = total;
    stats.startTime = QDateTime::currentDateTime();
    
    // 预分配内存以提高性能
    cleanedData.reserve(total);
    seenKeys.reserve(total);
    
    // 只发送一次开始信号，避免频繁发射信号
    emit cleaningProgress(0, QString("开始数据清洗，共%1条记录").arg(total));
    
    int totalRecords = total; // 总记录数
    int processedRecords = 0; // 已处理的记录数（包括有效和跳过的）
    int validProcessed = 0;   // 有效处理的记录数（通过格式验证）
    int cleanedRecords = 0;   // 清洗后保留的记录数
    int lastProgress = -1;
    
    // 辅助函数：精确计算进度并发送更新
    auto updateProgress = [&](int processed, int total, int valid, int cleaned, int& lastProgressRef) {
        // 确保不出现除零错误
        if (total <= 0) {
            return;
        }
        
        // 精确计算进度，考虑处理完所有记录时进度必须为100%
        int currentProgress = 0;
        if (processed > 0) {
            currentProgress = static_cast<int>((processed * 100.0) / total);
            // 确保进度值在0-100范围内
            if (currentProgress > 100) currentProgress = 100;
            if (currentProgress < 0) currentProgress = 0;
            // 当处理完所有记录时，强制进度为100%
            if (processed == total) {
                currentProgress = 100;
            }
        }
        
        // 记录计算出的进度
        lastProgressRef = currentProgress;
        
        // 确定是否需要发射信号：
        // 1. 处理完所有记录时（100%）
        // 2. 进度变化超过5%时
        // 3. 当处理少量记录时，至少每处理500条记录发射一次
        bool shouldEmit = false;
        bool isFinal = (processed == total);
        
        if (isFinal) {
            shouldEmit = true; // 最终状态必须发射
        } else if (lastProgressRef < 0) {
            shouldEmit = true; // 第一次发射
        } else if (std::abs(currentProgress - lastProgressRef) >= 5) {
            shouldEmit = true; // 进度变化超过5%
        } else if (processed % 500 == 0) {
            shouldEmit = true; // 每处理500条记录发射一次
        }
        
        if (shouldEmit) {
            lastProgressRef = currentProgress;
            
            int currentRemoved = valid - cleaned;
            QString message;
            
            if (isFinal) {
                // 最终完成消息
                int totalSkipped = total - valid; // 跳过的记录数
                message = QString("数据清洗完成: 共%1条，有效%2条，跳过%3条，保留%4条，移除%5条")
                            .arg(total).arg(valid).arg(totalSkipped).arg(cleaned).arg(currentRemoved);
                
                // 确保统计信息是最新的
                stats.cleanedRecords = cleaned;
                stats.removedRecords = currentRemoved;
                
                // 发送100%进度信号，确保UI看到完成状态
                // 使用debug日志确认信号发送
                qDebug() << "发送最终100%进度信号:" << message;
                emit cleaningProgress(100, message);
                
                // 更新统计并标记最终状态
                stats.totalRecords = total;
                stats.cleanedRecords = cleaned;
                stats.removedRecords = currentRemoved;
                stats.endTime = QDateTime::currentDateTime();
                stats.durationMs = stats.startTime.msecsTo(stats.endTime);
            } else {
                // 中间进度消息
                message = QString("正在清洗: %1/%2 (%3%) - 有效: %4, 保留: %5, 移除: %6")
                            .arg(processed).arg(total).arg(currentProgress)
                            .arg(valid).arg(cleaned).arg(currentRemoved);
                
                // 更新中间统计信息
                stats.cleanedRecords = cleaned;
                stats.removedRecords = currentRemoved;
                
                // 使用debug日志确认信号发送
                qDebug() << "发送进度信号:" << currentProgress << "%" << message;
                emit cleaningProgress(currentProgress, message);
            }
            
            // 减少UI事件处理频率，避免崩溃
            if (processed % 2000 == 0 && QThread::currentThread() == QCoreApplication::instance()->thread()) {
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::ExcludeSocketNotifiers);
            }
        }
    };
    
    try {
        for (const QVariant& item : data) {
            processedRecords++;
            
            // 安全检查：确保item有效且可以转换为map
            if (!item.isValid() || item.isNull()) {
                // 发送进度更新，但不增加有效处理计数
                updateProgress(processedRecords, totalRecords, validProcessed, cleanedData.size(), lastProgress);
                continue;
            }
            
            QVariantMap record;
            // 安全地检查并转换为map
            if (item.canConvert<QVariantMap>()) {
                record = item.toMap();
            } else {
                // 如果无法转换为map，跳过这条记录
                updateProgress(processedRecords, totalRecords, validProcessed, cleanedData.size(), lastProgress);
                continue;
            }
            
            // 验证数据格式
            if (!validateDataFormat(record)) {
                updateProgress(processedRecords, totalRecords, validProcessed, cleanedData.size(), lastProgress);
                continue;
            }
            
            validProcessed++;
            
            bool passedAllRules = true;
            
            // 应用每条规则
            for (const CleaningRule& rule : enabledRules) {
                try {
                    if (!executeRule(rule, record, cleaningContext, seenKeys)) {
                        passedAllRules = false;
                        break;
                    }
                } catch (const std::exception& e) {
                    qWarning() << "Error executing rule" << rule.name << ":" << e.what();
                    passedAllRules = false;
                    break;
                } catch (...) {
                    qWarning() << "Unknown error executing rule" << rule.name;
                    passedAllRules = false;
                    break;
                }
            }
            
            if (passedAllRules) {
                cleanedData.append(record);
            }
            
            // 更新进度 - 根据更新策略自动决定何时发射信号
            updateProgress(processedRecords, totalRecords, validProcessed, cleanedData.size(), lastProgress);
        }
    } catch (const std::exception& e) {
        qCritical() << "Data cleaning failed with exception:" << e.what();
        
        // 确保发送最终进度信号，避免UI卡在中间状态
        // 使用最后记录的进度，而不是重置为0%
        try {
            emit cleaningProgress(lastProgress, QString("数据清洗失败: %1").arg(e.what()));
        } catch (...) {
            qWarning() << "Failed to emit cleaningProgress after exception";
        }
        
        // 返回部分结果
        stats.endTime = QDateTime::currentDateTime();
        stats.durationMs = stats.startTime.msecsTo(stats.endTime);
        stats.cleanedRecords = cleanedData.size();
        stats.removedRecords = validProcessed - cleanedData.size();
        
        try {
            emit cleaningCompleted(stats);
        } catch (...) {
            qWarning() << "Failed to emit cleaningCompleted after exception";
        }
        
        // 更新最后的统计
        {
            QMutexLocker locker(&m_mutex);
            m_lastStats = stats;
        }
        
        return cleanedData;
    } catch (...) {
        qCritical() << "Data cleaning failed with unknown exception";
        
        // 确保发送最终进度信号
        // 使用最后记录的进度，而不是重置为0%
        try {
            emit cleaningProgress(lastProgress, "数据清洗失败: 未知错误");
        } catch (...) {
            qWarning() << "Failed to emit cleaningProgress after unknown exception";
        }
        
        stats.endTime = QDateTime::currentDateTime();
        stats.durationMs = stats.startTime.msecsTo(stats.endTime);
        stats.cleanedRecords = cleanedData.size();
        stats.removedRecords = validProcessed - cleanedData.size();
        
        try {
            emit cleaningCompleted(stats);
        } catch (...) {
            qWarning() << "Failed to emit cleaningCompleted after unknown exception";
        }
        
        {
            QMutexLocker locker(&m_mutex);
            m_lastStats = stats;
        }
        
        return cleanedData;
    }
    
    // 更新最终的统计数据，考虑跳过的记录
    stats.cleanedRecords = cleanedData.size();
    // 被规则过滤掉的记录数 = 处理的有效记录 - 清洗后的记录
    stats.removedRecords = validProcessed - cleanedData.size();
    stats.endTime = QDateTime::currentDateTime();
    stats.durationMs = stats.startTime.msecsTo(stats.endTime);
    
    int skipped = totalRecords - validProcessed; // 跳过的记录数（无效格式）
    
    qDebug() << "Data cleaning completed:"
             << "original:" << totalRecords
             << "valid:" << validProcessed
             << "skipped:" << skipped
             << "cleaned:" << cleanedData.size()
             << "removed:" << stats.removedRecords
             << "duration:" << stats.durationMs << "ms";
    
    // 确保发送最终的100%进度信号，使用更详细的完成消息
    QString finalMessage = QString("数据清洗完成: 共%1条，有效%2条，跳过%3条，保留%4条，移除%5条")
                           .arg(totalRecords).arg(validProcessed).arg(skipped)
                           .arg(cleanedData.size()).arg(stats.removedRecords);
    
    // 发送100%进度信号，确保UI看到完成状态
    emit cleaningProgress(100, finalMessage);
    
    // 更新最后的统计
    {
        QMutexLocker locker(&m_mutex);
        m_lastStats = stats;
    }
    
    // 立即发送清洗完成信号，确保UI接收到完成状态
    // 在发送完成信号前，确保已经发送了100%进度信号
    emit cleaningCompleted(stats);
    
    // 添加额外的安全延迟，防止信号丢失
    QPointer<DataCleaningEngine> self = this;
    QTimer::singleShot(100, [self, stats]() {
        if (!self) {
            qWarning() << "DataCleaningEngine对象已销毁，跳过安全信号检查";
            return;
        }
        // 安全检查：确保信号已送达
        qDebug() << "数据清洗完成信号已安全发送，统计信息已更新";
    });
    
    return cleanedData;
}

// 新增方法：清洗并保存到数据库
QVariantList DataCleaningEngine::cleanDataWithPersistence(const QVariantList& data,
                                                         const QVector<CleaningRule>& rules,
                                                         bool autoSave)
{
    // 执行清洗
    QVariantList cleanedData = cleanData(data, rules);
    
    // 如果启用了自动保存，保存到数据库
    if (autoSave && !cleanedData.isEmpty()) {
        saveCleaningResult(cleanedData);
    }
    
    return cleanedData;
}

// 新增方法：保存清洗结果到数据库
bool DataCleaningEngine::saveCleaningResult(const QVariantList& cleanedData)
{
    try {
        // 创建持久化服务
        ui::bridge::DataCleaningPersistence persistence;
        
        // 生成任务ID
        QString taskId = QUuid::createUuid().toString();
        
        // 准备统计信息
        QVariantMap stats;
        stats["task_id"] = taskId;
        stats["original_record_count"] = m_lastStats.totalRecords;
        stats["cleaned_record_count"] = m_lastStats.cleanedRecords;
        stats["removed_record_count"] = m_lastStats.removedRecords;
        stats["data_quality_score"] = calculateQualityScore(m_lastStats);
        stats["status"] = "COMPLETED";
        stats["start_time"] = m_lastStats.startTime.toString(Qt::ISODate);
        stats["end_time"] = m_lastStats.endTime.toString(Qt::ISODate);
        stats["duration_ms"] = m_lastStats.durationMs;
        
        // 保存到数据库
        bool success = persistence.saveCleaningResult(taskId, cleanedData, stats);
        
        if (success) {
            emit dataSaved(taskId);
            qDebug() << "清洗结果保存成功，任务ID:" << taskId;
        } else {
            qWarning() << "清洗结果保存失败，任务ID:" << taskId;
        }
        
        return success;
    } catch (const std::exception& e) {
        qCritical() << "保存清洗结果时发生异常:" << e.what();
        return false;
    } catch (...) {
        qCritical() << "保存清洗结果时发生未知异常";
        return false;
    }
}

// 新增方法：从数据库加载清洗结果
QVariantList DataCleaningEngine::loadCleanedData(const QString& taskId)
{
    try {
        // 创建持久化服务
        ui::bridge::DataCleaningPersistence persistence;
        
        // 从数据库加载数据
        QVariantList loadedData = persistence.loadCleanedData(taskId);
        
        if (!loadedData.isEmpty()) {
            emit dataLoaded(taskId, loadedData);
            qDebug() << "清洗结果加载成功，任务ID:" << taskId << "记录数:" << loadedData.size();
        } else {
            qWarning() << "清洗结果加载失败或为空，任务ID:" << taskId;
        }
        
        return loadedData;
    } catch (const std::exception& e) {
        qCritical() << "加载清洗结果时发生异常:" << e.what();
        return QVariantList();
    } catch (...) {
        qCritical() << "加载清洗结果时发生未知异常";
        return QVariantList();
    }
}

// 新增方法：计算数据质量评分
double DataCleaningEngine::calculateQualityScore(const CleaningStats& stats)
{
    if (stats.totalRecords == 0) {
        return 0.0;
    }
    
    // 计算清洗率
    double cleaningRate = static_cast<double>(stats.cleanedRecords) / stats.totalRecords;
    
    // 计算移除率
    double removalRate = static_cast<double>(stats.removedRecords) / stats.totalRecords;
    
    // 质量评分公式：清洗率 * 100 - 移除率 * 50
    // 清洗率越高越好，移除率越低越好
    double qualityScore = cleaningRate * 100.0 - removalRate * 50.0;
    
    // 确保评分在0-100范围内
    if (qualityScore > 100.0) qualityScore = 100.0;
    if (qualityScore < 0.0) qualityScore = 0.0;
    
    return qualityScore;
}

// 批量清洗数据
QVector<QVariantList> DataCleaningEngine::batchCleanData(const QVector<QVariantList>& dataList,
                                                        const QVector<CleaningRule>& rules)
{
    QVector<QVariantList> results;
    results.reserve(dataList.size());
    
    for (int i = 0; i < dataList.size(); ++i) {
        emit cleaningProgress(static_cast<int>((i * 100.0) / dataList.size()), 
                             QString("批量清洗中: %1/%2").arg(i + 1).arg(dataList.size()));
        
        QVariantList cleaned = cleanData(dataList[i], rules);
        results.append(cleaned);
    }
    
    emit cleaningProgress(100, "批量清洗完成");
    return results;
}

// 获取上次清洗的统计信息
DataCleaningEngine::CleaningStats DataCleaningEngine::getLastCleaningStats() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastStats;
}

// 创建默认规则集
QVector<DataCleaningEngine::CleaningRule> DataCleaningEngine::createDefaultRuleSet()
{
    QVector<CleaningRule> rules;

    // 1. 时间范围过滤 - 使用动态日期范围（过去一年）
    CleaningRule timeRange(RULE_TIME_RANGE, "时间范围过滤", "过滤指定时间范围外的数据");
    QDate startDate = QDateTime::currentDateTime().addDays(-365).date();
    QDate endDate = QDateTime::currentDateTime().date();
    timeRange.parameters["startDate"] = startDate.toString("yyyy-MM-dd");
    timeRange.parameters["endDate"] = endDate.toString("yyyy-MM-dd");
    rules.append(timeRange);

    // 2. 价格过滤 - 过滤异常价格
    CleaningRule priceFilter(RULE_PRICE_FILTER, "价格过滤", "过滤异常价格数据");
    priceFilter.parameters["minPrice"] = 0.01;
    priceFilter.parameters["maxPrice"] = 10000.0;
    rules.append(priceFilter);

    // 3. 成交量过滤 - 过滤异常成交量
    CleaningRule volumeFilter(RULE_VOLUME_FILTER, "成交量过滤", "过滤异常成交量数据");
    volumeFilter.parameters["minVolume"] = 0;
    volumeFilter.parameters["maxVolume"] = 1000000000; // 10亿
    rules.append(volumeFilter);

    // 4. 完整性检查 - 检查必要字段
    CleaningRule completenessCheck(RULE_COMPLETENESS_CHECK, "完整性检查", "检查数据完整性");
    QStringList requiredFields = {"date", "open", "high", "low", "close", "volume"};
    completenessCheck.parameters["requiredFields"] = requiredFields;
    rules.append(completenessCheck);

    // 5. 异常值检测 - 使用IQR方法
    CleaningRule outlierDetection(RULE_OUTLIER_DETECTION, "异常值检测", "检测并过滤异常值");
    outlierDetection.parameters["method"] = "iqr";
    outlierDetection.parameters["threshold"] = 1.5;
    rules.append(outlierDetection);

    // 6. 重复数据删除 - 基于唯一键
    CleaningRule duplicateRemoval(RULE_DUPLICATE_REMOVAL, "重复数据删除", "删除重复数据");
    duplicateRemoval.parameters["keyFields"] = QStringList{"date", "symbol"};
    rules.append(duplicateRemoval);

    // 7. 格式验证 - 验证数据格式
    CleaningRule formatValidation(RULE_FORMAT_VALIDATION, "格式验证", "验证数据格式");
    formatValidation.parameters["dateFormat"] = "yyyy-MM-dd";
    rules.append(formatValidation);

    return rules;
}

// 创建技术分析规则集
QVector<DataCleaningEngine::CleaningRule> DataCleaningEngine::createTechnicalAnalysisRuleSet()
{
    QVector<CleaningRule> rules = createDefaultRuleSet();
    
    // 添加技术分析特定规则
    CleaningRule technicalValidation(RULE_CUSTOM_FILTER, "技术指标验证", "验证技术指标数据");
    technicalValidation.parameters["indicators"] = QStringList{"ma5", "ma10", "ma20", "rsi", "macd"};
    rules.append(technicalValidation);
    
    return rules;
}

// 创建基本面分析规则集
QVector<DataCleaningEngine::CleaningRule> DataCleaningEngine::createFundamentalAnalysisRuleSet()
{
    QVector<CleaningRule> rules = createDefaultRuleSet();
    
    // 添加基本面分析特定规则
    CleaningRule fundamentalValidation(RULE_CUSTOM_FILTER, "基本面数据验证", "验证基本面数据");
    fundamentalValidation.parameters["fields"] = QStringList{"pe", "pb", "roe", "dividend_yield"};
    rules.append(fundamentalValidation);
    
    return rules;
}

// 验证数据格式
bool DataCleaningEngine::validateDataFormat(const QVariantMap& data) const
{
    // 检查必要字段是否存在
    QStringList requiredFields = {"date", "open", "high", "low", "close", "volume"};
    
    for (const QString& field : requiredFields) {
        if (!data.contains(field)) {
            qWarning() << "Missing required field:" << field;
            return false;
        }
        
        QVariant value = data[field];
        if (!value.isValid() || value.isNull()) {
            qWarning() << "Invalid value for field:" << field;
            return false;
        }
    }
    
    // 验证日期格式
    QString dateStr = data["date"].toString();
    QDate date = QDate::fromString(dateStr, "yyyy-MM-dd");
    if (!date.isValid()) {
        qWarning() << "Invalid date format:" << dateStr;
        return false;
    }
    
    // 验证价格数据
    QStringList priceFields = {"open", "high", "low", "close"};
    for (const QString& field : priceFields) {
        bool ok;
        double price = data[field].toDouble(&ok);
        if (!ok || price <= 0) {
            qWarning() << "Invalid price for field:" << field << "value:" << data[field];
            return false;
        }
    }
    
    // 验证成交量
    bool ok;
    double volume = data["volume"].toDouble(&ok);
    if (!ok || volume < 0) {
        qWarning() << "Invalid volume:" << data["volume"];
        return false;
    }
    
    return true;
}

// 导出规则到JSON
QVariantMap DataCleaningEngine::exportRulesToJson() const
{
    QVariantMap json;
    QVariantList rulesArray;
    
    QMutexLocker locker(&m_mutex);
    for (const CleaningRule& rule : m_rules) {
        QVariantMap ruleJson;
        ruleJson["type"] = static_cast<int>(rule.type);
        ruleJson["name"] = rule.name;
        ruleJson["description"] = rule.description;
        ruleJson["parameters"] = rule.parameters;
        ruleJson["enabled"] = rule.enabled;
        rulesArray.append(ruleJson);
    }
    
    json["rules"] = rulesArray;
    json["version"] = "1.0";
    json["exportTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    return json;
}

// 从JSON导入规则
bool DataCleaningEngine::importRulesFromJson(const QVariantMap& json)
{
    if (!json.contains("rules") || !json["rules"].canConvert<QVariantList>()) {
        qWarning() << "Invalid JSON format: missing rules array";
        return false;
    }
    
    QVariantList rulesArray = json["rules"].toList();
    QVector<CleaningRule> newRules;
    
    for (const QVariant& ruleVar : rulesArray) {
        if (!ruleVar.canConvert<QVariantMap>()) {
            qWarning() << "Invalid rule format in JSON";
            continue;
        }
        
        QVariantMap ruleMap = ruleVar.toMap();
        if (!ruleMap.contains("type") || !ruleMap.contains("name")) {
            qWarning() << "Invalid rule: missing type or name";
            continue;
        }
        
        CleaningRule rule(static_cast<CleaningRuleType>(ruleMap["type"].toInt()),
                         ruleMap["name"].toString(),
                         ruleMap["description"].toString());
        
        if (ruleMap.contains("parameters") && ruleMap["parameters"].canConvert<QVariantMap>()) {
            rule.parameters = ruleMap["parameters"].toMap();
        }
        
        if (ruleMap.contains("enabled")) {
            rule.enabled = ruleMap["enabled"].toBool();
        }
        
        newRules.append(rule);
    }
    
    {
        QMutexLocker locker(&m_mutex);
        m_rules = newRules;
    }
    
    emit rulesUpdated();
    qDebug() << "Imported" << newRules.size() << "rules from JSON";
    
    return true;
}

// 验证规则参数
bool DataCleaningEngine::validateRuleParameters(const CleaningRule& rule) const
{
    switch (rule.type) {
    case RULE_TIME_RANGE:
        return rule.parameters.contains("startDate") && rule.parameters.contains("endDate");
    case RULE_PRICE_FILTER:
        return rule.parameters.contains("minPrice") && rule.parameters.contains("maxPrice");
    case RULE_VOLUME_FILTER:
        return rule.parameters.contains("minVolume") && rule.parameters.contains("maxVolume");
    case RULE_COMPLETENESS_CHECK:
        return rule.parameters.contains("requiredFields");
    case RULE_OUTLIER_DETECTION:
        return rule.parameters.contains("method") && rule.parameters.contains("threshold");
    case RULE_DUPLICATE_REMOVAL:
        return rule.parameters.contains("keyFields");
    case RULE_FORMAT_VALIDATION:
        return rule.parameters.contains("dateFormat");
    case RULE_CUSTOM_FILTER:
        return true; // 自定义规则参数验证由规则本身处理
    default:
        qWarning() << "Unknown rule type:" << rule.type;
        return false;
    }
}

// 执行规则（使用内部上下文）
bool DataCleaningEngine::executeRule(const CleaningRule& rule, const QVariantMap& data, 
                                    QVariantMap& ruleContext)
{
    // 对于需要外部上下文的规则，使用内部上下文
    QVector<QString> seenKeys;
    return executeRule(rule, data, ruleContext, seenKeys);
}

// 执行规则（使用外部上下文和重复键列表）
bool DataCleaningEngine::executeRule(const CleaningRule& rule, const QVariantMap& data,
                                    QVariantMap& cleaningContext, QVector<QString>& seenKeys)
{
    switch (rule.type) {
    case RULE_TIME_RANGE:
        return applyTimeRangeFilter(data, rule.parameters);
    case RULE_PRICE_FILTER:
        return applyPriceFilter(data, rule.parameters);
    case RULE_VOLUME_FILTER:
        return applyVolumeFilter(data, rule.parameters);
    case RULE_COMPLETENESS_CHECK:
        return applyCompletenessCheck(data, rule.parameters);
    case RULE_OUTLIER_DETECTION:
        return applyOutlierDetection(data, rule.parameters, cleaningContext);
    case RULE_DUPLICATE_REMOVAL:
        return applyDuplicateRemoval(data, rule.parameters, seenKeys);
    case RULE_FORMAT_VALIDATION:
        return applyFormatValidation(data, rule.parameters);
    case RULE_CUSTOM_FILTER:
        return applyCustomFilter(data, rule.parameters);
    default:
        qWarning() << "Unknown rule type:" << rule.type;
        return false;
    }
}

// 应用时间范围过滤
bool DataCleaningEngine::applyTimeRangeFilter(const QVariantMap& data, const QVariantMap& params)
{
    if (!data.contains("date")) {
        return false;
    }
    
    QString dateStr = data["date"].toString();
    QDate date = QDate::fromString(dateStr, "yyyy-MM-dd");
    if (!date.isValid()) {
        return false;
    }
    
    QDate startDate = QDate::fromString(params["startDate"].toString(), "yyyy-MM-dd");
    QDate endDate = QDate::fromString(params["endDate"].toString(), "yyyy-MM-dd");
    
    if (!startDate.isValid() || !endDate.isValid()) {
        return false;
    }
    
    return date >= startDate && date <= endDate;
}

// 应用价格过滤
bool DataCleaningEngine::applyPriceFilter(const QVariantMap& data, const QVariantMap& params)
{
    QStringList priceFields = {"open", "high", "low", "close"};
    double minPrice = params["minPrice"].toDouble();
    double maxPrice = params["maxPrice"].toDouble();
    
    for (const QString& field : priceFields) {
        if (!data.contains(field)) {
            return false;
        }
        
        double price = data[field].toDouble();
        if (price < minPrice || price > maxPrice) {
            return false;
        }
    }
    
    return true;
}

// 应用成交量过滤
bool DataCleaningEngine::applyVolumeFilter(const QVariantMap& data, const QVariantMap& params)
{
    if (!data.contains("volume")) {
        return false;
    }
    
    double volume = data["volume"].toDouble();
    double minVolume = params["minVolume"].toDouble();
    double maxVolume = params["maxVolume"].toDouble();
    
    return volume >= minVolume && volume <= maxVolume;
}

// 应用完整性检查
bool DataCleaningEngine::applyCompletenessCheck(const QVariantMap& data, const QVariantMap& params)
{
    if (!params.contains("requiredFields")) {
        return true; // 如果没有指定必要字段，则通过检查
    }
    
    QStringList requiredFields = params["requiredFields"].toStringList();
    for (const QString& field : requiredFields) {
        if (!data.contains(field) || data[field].isNull()) {
            return false;
        }
    }
    
    return true;
}

// 应用异常值检测（使用外部上下文）
bool DataCleaningEngine::applyOutlierDetection(const QVariantMap& data, const QVariantMap& params, 
                                              QVariantMap& cleaningContext)
{
    QString method = params["method"].toString();
    double threshold = params["threshold"].toDouble();
    
    if (method == "iqr") {
        // 使用IQR方法检测异常值
        // 这里需要实现IQR算法
        // 暂时返回true，表示通过检查
        return true;
    }
    
    // 默认返回true
    return true;
}

// 应用重复数据删除
bool DataCleaningEngine::applyDuplicateRemoval(const QVariantMap& data, const QVariantMap& params, 
                                              QVector<QString>& seenKeys)
{
    if (!params.contains("keyFields")) {
        return true; // 如果没有指定关键字段，则通过检查
    }
    
    QStringList keyFields = params["keyFields"].toStringList();
    QString key;
    for (const QString& field : keyFields) {
        if (data.contains(field)) {
            key += data[field].toString() + "_";
        }
    }
    
    if (key.isEmpty()) {
        return false; // 无法生成唯一键
    }
    
    if (seenKeys.contains(key)) {
        return false; // 重复数据
    }
    
    seenKeys.append(key);
    return true;
}

// 应用格式验证
bool DataCleaningEngine::applyFormatValidation(const QVariantMap& data, const QVariantMap& params)
{
    // 验证日期格式
    if (data.contains("date")) {
        QString dateFormat = params["dateFormat"].toString();
        QString dateStr = data["date"].toString();
        QDate date = QDate::fromString(dateStr, dateFormat);
        if (!date.isValid()) {
            return false;
        }
    }
    
    // 验证数值格式
    QStringList numericFields = {"open", "high", "low", "close", "volume"};
    for (const QString& field : numericFields) {
        if (data.contains(field)) {
            bool ok;
            data[field].toDouble(&ok);
            if (!ok) {
                return false;
            }
        }
    }
    
    return true;
}

// 应用自定义过滤
bool DataCleaningEngine::applyCustomFilter(const QVariantMap& data, const QVariantMap& params)
{
    // 自定义过滤逻辑
    // 这里可以根据params中的配置执行自定义过滤
    // 暂时返回true，表示通过检查
    return true;
}

// 更新清洗统计
void DataCleaningEngine::updateCleaningStats(const CleaningRule& rule, bool passed)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_lastStats.ruleStats.contains(rule.name)) {
        m_lastStats.ruleStats[rule.name] = QVariantMap{
            {"total", 0},
            {"passed", 0},
            {"failed", 0}
        };
    }
    
    QVariantMap ruleStat = m_lastStats.ruleStats[rule.name].toMap();
    ruleStat["total"] = ruleStat["total"].toInt() + 1;
    
    if (passed) {
        ruleStat["passed"] = ruleStat["passed"].toInt() + 1;
    } else {
        ruleStat["failed"] = ruleStat["failed"].toInt() + 1;
    }
    
    m_lastStats.ruleStats[rule.name] = ruleStat;
}

// 重置清洗统计
void DataCleaningEngine::resetCleaningStats()
{
    QMutexLocker locker(&m_mutex);
    m_lastStats = CleaningStats();
}
