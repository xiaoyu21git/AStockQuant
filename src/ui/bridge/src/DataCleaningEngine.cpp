#include "DataCleaningEngine.h"

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

QVector<QVariantList> DataCleaningEngine::batchCleanData(const QVector<QVariantList>& dataList,
                                                        const QVector<CleaningRule>& rules)
{
    QVector<QVariantList> results;
    results.reserve(dataList.size());
    
    int batchIndex = 0;
    for (const QVariantList& data : dataList) {
        batchIndex++;
        QString message = QString("正在处理批次 %1/%2").arg(batchIndex).arg(dataList.size());
        emit cleaningProgress(0, message);
        
        QVariantList cleaned = cleanData(data, rules);
        results.append(cleaned);
    }
    
    return results;
}

DataCleaningEngine::CleaningStats DataCleaningEngine::getLastCleaningStats() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastStats;
}

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
    
    // 2. 价格过滤
    CleaningRule priceFilter(RULE_PRICE_FILTER, "价格过滤", "过滤异常价格数据");
    priceFilter.parameters["minPrice"] = 0.01;
    priceFilter.parameters["maxPrice"] = 10000.0;
    priceFilter.parameters["checkOpen"] = true;
    priceFilter.parameters["checkHigh"] = true;
    priceFilter.parameters["checkLow"] = true;
    priceFilter.parameters["checkClose"] = true;
    rules.append(priceFilter);
    
    // 3. 成交量过滤
    CleaningRule volumeFilter(RULE_VOLUME_FILTER, "成交量过滤", "过滤异常成交量数据");
    volumeFilter.parameters["minVolume"] = 0;
    volumeFilter.parameters["maxVolume"] = 1000000000; // 10亿
    rules.append(volumeFilter);
    
    // 4. 完整性检查
    CleaningRule completenessCheck(RULE_COMPLETENESS_CHECK, "完整性检查", "检查数据字段完整性");
    completenessCheck.parameters["requiredFields"] = QStringList{"symbol", "date", "open", "high", "low", "close", "volume"};
    rules.append(completenessCheck);
    
    // 5. 异常值检测
    CleaningRule outlierDetection(RULE_OUTLIER_DETECTION, "异常值检测", "检测并过滤异常值");
    outlierDetection.parameters["priceDeviation"] = 3.0; // 3倍标准差
    outlierDetection.parameters["volumeDeviation"] = 5.0; // 5倍标准差
    rules.append(outlierDetection);
    
    // 6. 重复数据删除
    CleaningRule duplicateRemoval(RULE_DUPLICATE_REMOVAL, "重复数据删除", "删除重复的数据记录");
    duplicateRemoval.parameters["keyFields"] = QStringList{"symbol", "date"};
    rules.append(duplicateRemoval);
    
    // 7. 格式验证 - 使用更灵活的日期格式验证
    CleaningRule formatValidation(RULE_FORMAT_VALIDATION, "格式验证", "验证数据格式正确性");
    formatValidation.parameters["symbolPattern"] = "^[0-9]{6}\\.[A-Z]{2}$";
    // 支持多种日期格式的正则表达式
    formatValidation.parameters["datePattern"] = "^(\\d{4}[-./]\\d{2}[-./]\\d{2}|\\d{2}[-./]\\d{2}[-./]\\d{4})$";
    rules.append(formatValidation);
    
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningEngine::createTechnicalAnalysisRuleSet()
{
    QVector<CleaningRule> rules = createDefaultRuleSet();
    
    // 添加技术分析专用规则
    CleaningRule priceConsistency(RULE_CUSTOM_FILTER, "价格一致性检查", "检查价格数据逻辑一致性");
    priceConsistency.parameters["checkHighLow"] = true; // 最高价 >= 最低价
    priceConsistency.parameters["checkOpenCloseRange"] = true; // 开盘价和收盘价在最高最低价范围内
    rules.append(priceConsistency);
    
    CleaningRule volumePriceRelation(RULE_CUSTOM_FILTER, "量价关系检查", "检查成交量和价格的关系");
    volumePriceRelation.parameters["minVolumePriceRatio"] = 0.000001; // 最小量价比
    volumePriceRelation.parameters["maxVolumePriceRatio"] = 100.0; // 最大量价比
    rules.append(volumePriceRelation);
    
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningEngine::createFundamentalAnalysisRuleSet()
{
    QVector<CleaningRule> rules = createDefaultRuleSet();
    
    // 添加基本面分析专用规则
    CleaningRule financialDataValidation(RULE_CUSTOM_FILTER, "财务数据验证", "验证财务数据的合理性");
    financialDataValidation.parameters["minPE"] = 0.0; // 最小市盈率
    financialDataValidation.parameters["maxPE"] = 1000.0; // 最大市盈率
    financialDataValidation.parameters["minPB"] = 0.0; // 最小市净率
    financialDataValidation.parameters["maxPB"] = 100.0; // 最大市净率
    rules.append(financialDataValidation);
    
    return rules;
}

bool DataCleaningEngine::validateDataFormat(const QVariantMap& data) const
{
    // 基本格式检查
    if (data.isEmpty()) {
        return false;
    }
    
    // 检查必要字段是否存在
    if (!data.contains("symbol") || !data.contains("date")) {
        return false;
    }
    
    // 检查字段类型
    if (!data["symbol"].canConvert<QString>() || !data["date"].canConvert<QString>()) {
        return false;
    }
    
    return true;
}

bool DataCleaningEngine::applyDuplicateRemoval(const QVariantMap& data, const QVariantMap& params, 
                                              QVector<QString>& seenKeys)
{
    if (!params.contains("keyFields")) {
        return true;
    }
    
    QStringList keyFields = params["keyFields"].toStringList();
    QString key;
    
    // 构建唯一键
    for (const QString& field : keyFields) {
        if (data.contains(field)) {
            key += data[field].toString() + "|";
        }
    }
    
    if (key.isEmpty()) {
        return true; // 如果没有键字段，跳过此规则
    }
    
    // 检查是否已存在
    if (seenKeys.contains(key)) {
        return false; // 重复数据，过滤掉
    }
    
    seenKeys.append(key);
    return true;
}

bool DataCleaningEngine::applyFormatValidation(const QVariantMap& data, const QVariantMap& params)
{
    // 验证股票代码格式
    if (params.contains("symbolPattern") && data.contains("symbol")) {
        QRegularExpression symbolRegex(params["symbolPattern"].toString());
        QString symbol = data["symbol"].toString();
        if (!symbolRegex.match(symbol).hasMatch()) {
            return false;
        }
    }
    
    // 验证日期格式
    if (params.contains("datePattern") && data.contains("date")) {
        QRegularExpression dateRegex(params["datePattern"].toString());
        QString date = data["date"].toString();
        if (!dateRegex.match(date).hasMatch()) {
            return false;
        }
    }
    
    return true;
}

bool DataCleaningEngine::applyCustomFilter(const QVariantMap& data, const QVariantMap& params)
{
    // 价格一致性检查
    if (params.value("checkHighLow", false).toBool()) {
        if (data.contains("high") && data.contains("low")) {
            double high = data["high"].toDouble();
            double low = data["low"].toDouble();
            if (high < low) {
                return false;
            }
        }
    }
    
    // 开盘收盘价范围检查
    if (params.value("checkOpenCloseRange", false).toBool()) {
        if (data.contains("open") && data.contains("close") && 
            data.contains("high") && data.contains("low")) {
            double open = data["open"].toDouble();
            double close = data["close"].toDouble();
            double high = data["high"].toDouble();
            double low = data["low"].toDouble();
            
            if (open < low || open > high || close < low || close > high) {
                return false;
            }
        }
    }
    
    // 量价比检查
    if (params.contains("minVolumePriceRatio") || params.contains("maxVolumePriceRatio")) {
        if (data.contains("volume") && data.contains("close")) {
            double volume = data["volume"].toDouble();
            double close = data["close"].toDouble();
            double ratio = (close > 0) ? volume / close : 0;
            
            double minRatio = params.value("minVolumePriceRatio", 0.0).toDouble();
            double maxRatio = params.value("maxVolumePriceRatio", std::numeric_limits<double>::max()).toDouble();
            
            if (ratio < minRatio || ratio > maxRatio) {
                return false;
            }
        }
    }
    
    // 财务数据验证
    if (data.contains("pe_ratio")) {
        double pe = data["pe_ratio"].toDouble();
        double minPE = params.value("minPE", 0.0).toDouble();
        double maxPE = params.value("maxPE", std::numeric_limits<double>::max()).toDouble();
        
        if (pe < minPE || pe > maxPE) {
            return false;
        }
    }
    
    if (data.contains("pb_ratio")) {
        double pb = data["pb_ratio"].toDouble();
        double minPB = params.value("minPB", 0.0).toDouble();
        double maxPB = params.value("maxPB", std::numeric_limits<double>::max()).toDouble();
        
        if (pb < minPB || pb > maxPB) {
            return false;
        }
    }
    
    return true;
}

bool DataCleaningEngine::executeRule(const CleaningRule& rule, const QVariantMap& data, 
                                    QVariantMap& ruleContext)
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
            return applyOutlierDetection(data, rule.parameters);
        case RULE_DUPLICATE_REMOVAL:
            return applyDuplicateRemoval(data, rule.parameters, m_seenKeys);
        case RULE_FORMAT_VALIDATION:
            return applyFormatValidation(data, rule.parameters);
        case RULE_CUSTOM_FILTER:
            return applyCustomFilter(data, rule.parameters);
        default:
            qWarning() << "Unknown rule type:" << static_cast<int>(rule.type);
            return true;
    }
}

// 重载版本，用于无锁清洗操作
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
            qWarning() << "Unknown rule type:" << static_cast<int>(rule.type);
            return true;
    }
}

void DataCleaningEngine::updateCleaningStats(const CleaningRule& rule, bool passed)
{
    QString ruleKey = rule.name;
    if (!m_lastStats.ruleStats.contains(ruleKey)) {
        QVariantMap ruleStat;
        ruleStat["total"] = 0;
        ruleStat["passed"] = 0;
        ruleStat["failed"] = 0;
        m_lastStats.ruleStats[ruleKey] = ruleStat;
    }
    
    QVariantMap ruleStat = m_lastStats.ruleStats[ruleKey].toMap();
    ruleStat["total"] = ruleStat["total"].toInt() + 1;
    if (passed) {
        ruleStat["passed"] = ruleStat["passed"].toInt() + 1;
    } else {
        ruleStat["failed"] = ruleStat["failed"].toInt() + 1;
    }
    m_lastStats.ruleStats[ruleKey] = ruleStat;
}

void DataCleaningEngine::resetCleaningStats()
{
    m_lastStats = CleaningStats();
    m_lastStats.startTime = QDateTime::currentDateTime();
}

bool DataCleaningEngine::validateRuleParameters(const CleaningRule& rule) const
{
    // 基本参数验证
    if (rule.name.isEmpty()) {
        qWarning() << "Rule name is empty";
        return false;
    }
    
    // 根据规则类型验证特定参数
    switch (rule.type) {
        case RULE_TIME_RANGE:
            if (!rule.parameters.contains("startDate") || !rule.parameters.contains("endDate")) {
                qWarning() << "Time range rule missing startDate or endDate";
                return false;
            }
            break;
        case RULE_PRICE_FILTER:
            // 价格过滤规则可以有默认值，不强制要求参数存在
            // applyPriceFilter函数会使用默认值：minPrice=0.01, maxPrice=10000.0
            // 但仍然检查参数格式是否正确（如果存在）
            if (rule.parameters.contains("minPrice") && !rule.parameters["minPrice"].canConvert<double>()) {
                qWarning() << "Price filter rule minPrice is not a valid number";
                return false;
            }
            if (rule.parameters.contains("maxPrice") && !rule.parameters["maxPrice"].canConvert<double>()) {
                qWarning() << "Price filter rule maxPrice is not a valid number";
                return false;
            }
            // 也兼容QML中的"min"和"max"参数名
            if (rule.parameters.contains("min") && !rule.parameters["min"].canConvert<double>()) {
                qWarning() << "Price filter rule min is not a valid number";
                return false;
            }
            if (rule.parameters.contains("max") && !rule.parameters["max"].canConvert<double>()) {
                qWarning() << "Price filter rule max is not a valid number";
                return false;
            }
            break;
        case RULE_VOLUME_FILTER:
            if (!rule.parameters.contains("minVolume") || !rule.parameters.contains("maxVolume")) {
                qWarning() << "Volume filter rule missing minVolume or maxVolume";
                return false;
            }
            break;
        case RULE_COMPLETENESS_CHECK:
            if (!rule.parameters.contains("requiredFields")) {
                qWarning() << "Completeness check rule missing requiredFields";
                return false;
            }
            break;
        default:
            // 其他规则类型不需要特定参数验证
            break;
    }
    
    return true;
}

QVariantMap DataCleaningEngine::exportRulesToJson() const
{
    QMutexLocker locker(&m_mutex);
    
    QVariantMap json;
    QVariantList rulesArray;
    
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
    json["exportTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    json["version"] = "1.0";
    
    return json;
}

bool DataCleaningEngine::importRulesFromJson(const QVariantMap& json)
{
    QMutexLocker locker(&m_mutex);
    
    if (!json.contains("rules") || !json["rules"].canConvert<QVariantList>()) {
        qWarning() << "Invalid rules JSON format";
        return false;
    }
    
    QVariantList rulesArray = json["rules"].toList();
    QVector<CleaningRule> newRules;
    
    for (const QVariant& ruleVar : rulesArray) {
        if (!ruleVar.canConvert<QVariantMap>()) {
            continue;
        }
        
        QVariantMap ruleMap = ruleVar.toMap();
        if (!ruleMap.contains("type") || !ruleMap.contains("name")) {
            continue;
        }
        
        CleaningRule rule(
            static_cast<CleaningRuleType>(ruleMap["type"].toInt()),
            ruleMap["name"].toString(),
            ruleMap["description"].toString()
        );
        
        if (ruleMap.contains("parameters")) {
            if (ruleMap["parameters"].canConvert<QVariantMap>()) {
                rule.parameters = ruleMap["parameters"].toMap();
            }
        }
        
        if (ruleMap.contains("enabled")) {
            rule.enabled = ruleMap["enabled"].toBool();
        }
        
        newRules.append(rule);
    }
    
    if (!newRules.isEmpty()) {
        m_rules = newRules;
        emit rulesUpdated();
        qDebug() << "Imported" << newRules.size() << "rules from JSON";
        return true;
    }
    
    return false;
}

// 私有方法实现
bool DataCleaningEngine::applyTimeRangeFilter(const QVariantMap& data, const QVariantMap& params)
{
    if (!data.contains("date") || !params.contains("startDate") || !params.contains("endDate")) {
        return true; // 如果缺少必要参数，跳过此规则
    }
    
    QString dateStr = data["date"].toString();
    QString startDateStr = params["startDate"].toString();
    QString endDateStr = params["endDate"].toString();
    
    // 支持多种日期格式解析，与数据源的日期格式保持一致
    auto parseDate = [](const QString& dateStr) -> QDate {
        // 尝试多种常见的日期格式
        QList<QString> formats = {
            "yyyy-MM-dd",      // ISO 标准格式（默认）
            "yyyy/MM/dd",      // 斜杠分隔格式
            "dd-MM-yyyy",      // 日-月-年格式
            "dd/MM/yyyy",      // 日/月/年格式
            "yyyy.MM.dd",      // 点分隔格式
            "MM-dd-yyyy",      // 月-日-年格式（美式）
            "MM/dd/yyyy"       // 月/日/年格式（美式）
        };
        
        for (const QString& format : formats) {
            QDate date = QDate::fromString(dateStr, format);
            if (date.isValid()) {
                return date;
            }
        }
        
        // 如果所有格式都失败，返回无效日期
        return QDate();
    };
    
    QDate date = parseDate(dateStr);
    QDate startDate = parseDate(startDateStr);
    QDate endDate = parseDate(endDateStr);
    
    if (!date.isValid() || !startDate.isValid() || !endDate.isValid()) {
        qWarning() << "Invalid date format in time range filter:"
                   << "date=" << dateStr << "start=" << startDateStr << "end=" << endDateStr
                   << "（支持的格式: yyyy-MM-dd, yyyy/MM/dd, dd-MM-yyyy, dd/MM/yyyy, yyyy.MM.dd, MM-dd-yyyy, MM/dd/yyyy）";
        return true; // 如果日期格式无效，跳过此规则
    }
    
    return date >= startDate && date <= endDate;
}

bool DataCleaningEngine::applyPriceFilter(const QVariantMap& data, const QVariantMap& params)
{
    // 兼容两种参数名：minPrice/maxPrice 和 min/max
    double minPrice = 0.01;
    double maxPrice = 10000.0;
    
    // 尝试获取minPrice参数，如果不存在则尝试min参数
    if (params.contains("minPrice")) {
        minPrice = params["minPrice"].toDouble();
    } else if (params.contains("min")) {
        minPrice = params["min"].toDouble();
    }
    
    // 尝试获取maxPrice参数，如果不存在则尝试max参数
    if (params.contains("maxPrice")) {
        maxPrice = params["maxPrice"].toDouble();
    } else if (params.contains("max")) {
        maxPrice = params["max"].toDouble();
    }
    
    // 检查开盘价
    if (params.value("checkOpen", true).toBool() && data.contains("open")) {
        double open = data["open"].toDouble();
        if (open < minPrice || open > maxPrice) {
            return false;
        }
    }
    
    // 检查最高价
    if (params.value("checkHigh", true).toBool() && data.contains("high")) {
        double high = data["high"].toDouble();
        if (high < minPrice || high > maxPrice) {
            return false;
        }
    }
    
    // 检查最低价
    if (params.value("checkLow", true).toBool() && data.contains("low")) {
        double low = data["low"].toDouble();
        if (low < minPrice || low > maxPrice) {
            return false;
        }
    }
    
    // 检查收盘价
    if (params.value("checkClose", true).toBool() && data.contains("close")) {
        double close = data["close"].toDouble();
        if (close < minPrice || close > maxPrice) {
            return false;
        }
    }
    
    return true;
}

bool DataCleaningEngine::applyVolumeFilter(const QVariantMap& data, const QVariantMap& params)
{
    if (!data.contains("volume")) {
        return true; // 如果没有成交量字段，跳过此规则
    }
    
    double volume = data["volume"].toDouble();
    double minVolume = params.value("minVolume", 0.0).toDouble();
    double maxVolume = params.value("maxVolume", 1000000000.0).toDouble();
    
    return volume >= minVolume && volume <= maxVolume;
}

bool DataCleaningEngine::applyCompletenessCheck(const QVariantMap& data, const QVariantMap& params)
{
    if (!params.contains("requiredFields")) {
        return true;
    }
    
    QStringList requiredFields = params["requiredFields"].toStringList();
    for (const QString& field : requiredFields) {
        if (!data.contains(field) || data[field].isNull()) {
            return false;
        }
    }
    
    return true;
}

bool DataCleaningEngine::applyOutlierDetection(const QVariantMap& data, const QVariantMap& params)
{
    return applyOutlierDetection(data, params, m_cleaningContext);
}

bool DataCleaningEngine::applyOutlierDetection(const QVariantMap& data, const QVariantMap& params, 
                                              QVariantMap& cleaningContext)
{
    // 简单的异常值检测实现
    // 在实际应用中，这里应该使用更复杂的统计方法
    
    // 检查价格异常
    if (data.contains("close")) {
        double close = data["close"].toDouble();
        double prevClose = cleaningContext.value("prevClose", close).toDouble();
        
        // 防止除以零
        if (std::abs(prevClose) < std::numeric_limits<double>::epsilon()) {
            prevClose = (std::abs(close) < std::numeric_limits<double>::epsilon()) ? 1.0 : close;
        }
        
        // 计算价格变化率
        double priceChange = std::abs((close - prevClose) / prevClose);
        double priceDeviation = params.value("priceDeviation", 3.0).toDouble();
        
        // 如果价格变化超过阈值，可能是异常值
        if (priceChange > priceDeviation * 0.1) { // 简化处理
            return false;
        }
        
        // 更新上下文
        cleaningContext["prevClose"] = close;
    }
    
    // 检查成交量异常
    if (data.contains("volume")) {
        double volume = data["volume"].toDouble();
        double prevVolume = cleaningContext.value("prevVolume", volume).toDouble();
        
        // 计算成交量变化率
        double volumeChange = std::abs((volume - prevVolume) / (prevVolume > 0 ? prevVolume : 1.0));
        double volumeDeviation = params.value("volumeDeviation", 5.0).toDouble();
        
        // 如果成交量变化超过阈值，可能是异常值
        if (volumeChange > volumeDeviation * 0.1) { // 简化处理
            return false;
        }
        
        // 更新上下文
        cleaningContext["prevVolume"] = volume;
    }
    
    return true;
}
