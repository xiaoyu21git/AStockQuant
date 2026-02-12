#include "DataCleaningEngine.h"

#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
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
    QMutexLocker locker(&m_mutex);
    
    // 重置清洗统计
    resetCleaningStats();
    m_lastStats.startTime = QDateTime::currentDateTime();
    m_lastStats.totalRecords = data.size();
    
    // 使用传入的规则或默认规则
    QVector<CleaningRule> rulesToUse = rules.isEmpty() ? m_rules : rules;
    
    // 过滤启用的规则
    QVector<CleaningRule> enabledRules;
    for (const auto& rule : rulesToUse) {
        if (rule.enabled && validateRuleParameters(rule)) {
            enabledRules.append(rule);
        }
    }
    
    if (enabledRules.isEmpty()) {
        qWarning() << "No enabled rules to apply";
        m_lastStats.cleanedRecords = data.size();
        m_lastStats.endTime = QDateTime::currentDateTime();
        m_lastStats.durationMs = m_lastStats.startTime.msecsTo(m_lastStats.endTime);
        return data; // 返回原始数据
    }
    
    qDebug() << "Starting data cleaning with" << enabledRules.size() << "enabled rules";
    emit cleaningProgress(0, "开始数据清洗...");
    
    QVariantList cleanedData;
    m_seenKeys.clear();
    m_cleaningContext.clear();
    
    int processed = 0;
    const int total = data.size();
    
    for (const QVariant& item : data) {
        QVariantMap record = item.toMap();
        bool passedAllRules = true;
        
        // 验证数据格式
        if (!validateDataFormat(record)) {
            updateCleaningStats(CleaningRule(RULE_FORMAT_VALIDATION, "格式验证"), false);
            continue;
        }
        
        // 应用每条规则
        for (const CleaningRule& rule : enabledRules) {
            QVariantMap ruleContext;
            if (!executeRule(rule, record, ruleContext)) {
                passedAllRules = false;
                updateCleaningStats(rule, false);
                break;
            }
            updateCleaningStats(rule, true);
        }
        
        if (passedAllRules) {
            cleanedData.append(record);
        }
        
        processed++;
        if (processed % 100 == 0 || processed == total) {
            int progress = static_cast<int>((processed * 100.0) / total);
            QString message = QString("正在清洗数据... %1/%2 (%3%)")
                                .arg(processed).arg(total).arg(progress);
            emit cleaningProgress(progress, message);
        }
    }
    
    m_lastStats.cleanedRecords = cleanedData.size();
    m_lastStats.removedRecords = total - cleanedData.size();
    m_lastStats.endTime = QDateTime::currentDateTime();
    m_lastStats.durationMs = m_lastStats.startTime.msecsTo(m_lastStats.endTime);
    
    qDebug() << "Data cleaning completed:"
             << "original:" << total
             << "cleaned:" << cleanedData.size()
             << "removed:" << m_lastStats.removedRecords
             << "duration:" << m_lastStats.durationMs << "ms";
    
    emit cleaningProgress(100, "数据清洗完成");
    emit cleaningCompleted(m_lastStats);
    
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
    
    // 1. 时间范围过滤
    CleaningRule timeRange(RULE_TIME_RANGE, "时间范围过滤", "过滤指定时间范围外的数据");
    timeRange.parameters["startDate"] = "2020-01-01";
    timeRange.parameters["endDate"] = QDateTime::currentDateTime().toString("yyyy-MM-dd");
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
    
    // 7. 格式验证
    CleaningRule formatValidation(RULE_FORMAT_VALIDATION, "格式验证", "验证数据格式正确性");
    formatValidation.parameters["symbolPattern"] = "^[0-9]{6}\\.[A-Z]{2}$";
    formatValidation.parameters["datePattern"] = "^\\d{4}-\\d{2}-\\d{2}$";
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
            if (!rule.parameters.contains("minPrice") || !rule.parameters.contains("maxPrice")) {
                qWarning() << "Price filter rule missing minPrice or maxPrice";
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
            rule.parameters = ruleMap["parameters"].toMap();
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
    
    // 使用QDate进行正确的日期比较
    QDate date = QDate::fromString(dateStr, "yyyy-MM-dd");
    QDate startDate = QDate::fromString(startDateStr, "yyyy-MM-dd");
    QDate endDate = QDate::fromString(endDateStr, "yyyy-MM-dd");
    
    if (!date.isValid() || !startDate.isValid() || !endDate.isValid()) {
        qWarning() << "Invalid date format in time range filter:"
                   << "date=" << dateStr << "start=" << startDateStr << "end=" << endDateStr;
        return true; // 如果日期格式无效，跳过此规则
    }
    
    return date >= startDate && date <= endDate;
}

bool DataCleaningEngine::applyPriceFilter(const QVariantMap& data, const QVariantMap& params)
{
    double minPrice = params.value("minPrice", 0.01).toDouble();
    double maxPrice = params.value("maxPrice", 10000.0).toDouble();
    
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
    // 简单的异常值检测实现
    // 在实际应用中，这里应该使用更复杂的统计方法
    
    // 检查价格异常
    if (data.contains("close")) {
        double close = data["close"].toDouble();
        double prevClose = m_cleaningContext.value("prevClose", close).toDouble();
        
        // 计算价格变化率
        double priceChange = std::abs((close - prevClose) / prevClose);
        double priceDeviation = params.value("priceDeviation", 3.0).toDouble();
        
        // 如果价格变化超过阈值，可能是异常值
        if (priceChange > priceDeviation * 0.1) { // 简化处理
            return false;
        }
        
        // 更新上下文
        m_cleaningContext["prevClose"] = close;
    }
    
    // 检查成交量异常
    if (data.contains("volume")) {
        double volume = data["volume"].toDouble();
        double prevVolume = m_cleaningContext.value("prevVolume", volume).toDouble();
        
        // 计算成交量变化率
        double volumeChange = std::abs((volume - prevVolume) / (prevVolume > 0 ? prevVolume : 1.0));
        double volumeDeviation = params.value("volumeDeviation", 5.0).toDouble();
        
        // 如果成交量变化超过阈值，可能是异常值
        if (volumeChange > volumeDeviation * 0.1) { // 简化处理
            return false;
        }
        
        // 更新上下文
        m_cleaningContext["prevVolume"] = volume;
    }
    
    return true;
}
