#include "DataCleaningRuleRegistry.h"

#include <QSet>

namespace {

QStringList toStringList(const QVariant& value)
{
    if (value.canConvert<QStringList>()) {
        return value.toStringList();
    }

    QStringList result;
    const QVariantList list = value.toList();
    for (const QVariant& item : list) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty()) {
            result.append(text);
        }
    }
    return result;
}

DataCleaningEngine::CleaningRule makeRule(DataCleaningEngine::CleaningRuleType type,
                                         const QString& id,
                                         const QString& name,
                                         const QString& description,
                                         DataCleaningEngine::CleaningRuleLevel level,
                                         DataCleaningEngine::CleaningRuleMode mode,
                                         int executionOrder)
{
    DataCleaningEngine::CleaningRule rule(type, id, description);
    rule.name = name;
    rule.level = level;
    rule.mode = mode;
    rule.executionOrder = executionOrder;
    rule.enabled = true;
    return rule;
}

}

DataCleaningRuleRegistry::DataCleaningRuleRegistry()
{
    m_descriptors = {
        {"duplicateRemoval", "重复数据删除", "删除同一交易日重复的 symbol/date 记录", true, &DataCleaningRuleRegistry::buildDuplicateRules},
        {"reportDateAlignment", "财报日期对齐", "使用披露日而不是报告期作为生效日期", true, &DataCleaningRuleRegistry::buildReportDateAlignmentRules},
        {"survivorBias", "生存者偏差处理", "保留退市股票退市前的历史记录", true, &DataCleaningRuleRegistry::buildSurvivorBiasRules},
        {"suspensionFill", "停牌填充", "停牌期间按时序向前填充价格，超阈值剔除", true, &DataCleaningRuleRegistry::buildSuspensionFillRules},
        {"missingValueFill", "缺失值处理", "优先按时序向前填充关键字段", true, &DataCleaningRuleRegistry::buildMissingValueRules},
        {"adjustedPrice", "复权处理", "统一使用后复权价格", true, &DataCleaningRuleRegistry::buildAdjustedPriceRules},
        {"newStockFilter", "新股过滤", "过滤上市前 60 个交易日内的新股", true, &DataCleaningRuleRegistry::buildNewStockFilterRules},
        {"stFilter", "ST状态剔除", "剔除 ST 和 *ST 股票", true, &DataCleaningRuleRegistry::buildStFilterRules},
        {"timeRange", "时间范围过滤", "按日期范围过滤数据", false, &DataCleaningRuleRegistry::buildTimeRangeRules},
        {"formatValidation", "格式验证", "验证日期和关键数值字段格式", true, &DataCleaningRuleRegistry::buildDataCleaningRules},
        {"priceValidity", "价格有效性", "检查价格链和价格范围", true, &DataCleaningRuleRegistry::buildPriceValidityRules},
        {"limitMoveTag", "涨跌停标记", "生成涨跌停和买卖限制标签", true, &DataCleaningRuleRegistry::buildLimitMoveRules},
        {"marketCapFilter", "市值过滤", "剔除市值尾部股票", true, &DataCleaningRuleRegistry::buildMarketCapFilterRules},
        {"winsorization", "异常值缩尾", "按分位数缩尾因子值", true, &DataCleaningRuleRegistry::buildWinsorizationRules},
        {"indexAlignment", "指数调整对齐", "成分股调整日滞后一天生效", false, &DataCleaningRuleRegistry::buildIndexAlignmentRules},
        {"continuousSuspensionFilter", "连续停牌剔除", "连续停牌超过阈值直接剔除", false, &DataCleaningRuleRegistry::buildContinuousSuspensionRules},
        {"outlierFilter", "单点异常检测", "对日内跳变异常做基础过滤", false, &DataCleaningRuleRegistry::buildOutlierRules},
        {"dataCleaning", "基础数据清洗", "兼容旧配置的基础清洗规则集合", false, &DataCleaningRuleRegistry::buildDataCleaningRules}
    };
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildRules(const QVariantMap& rawRules) const
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    QSet<QString> appendedIds;

    const QVariantList ruleList = rawRules.value("rules").toList();
    for (const QVariant& item : ruleList) {
        const QVariantMap ruleMap = item.toMap();
        const QString id = ruleMap.value("id").toString().trimmed();
        if (id.isEmpty()) {
            appendDirectEngineRule(ruleMap, &rules);
            continue;
        }

        for (const RuleDescriptor& descriptor : m_descriptors) {
            if (descriptor.id != id) {
                continue;
            }
            rules += descriptor.builder(ruleMap);
            appendedIds.insert(id);
            break;
        }
    }

    for (const RuleDescriptor& descriptor : m_descriptors) {
        if (appendedIds.contains(descriptor.id)) {
            continue;
        }

        if (!rawRules.contains(descriptor.id)) {
            if (!descriptor.defaultEnabled) {
                continue;
            }
            rules += descriptor.builder(QVariant(descriptor.defaultEnabled));
            continue;
        }

        rules += descriptor.builder(rawRules.value(descriptor.id));
    }

    return rules;
}

QVariantList DataCleaningRuleRegistry::availableRules() const
{
    QVariantList result;
    for (const RuleDescriptor& descriptor : m_descriptors) {
        QVariantMap ruleInfo;
        ruleInfo["id"] = descriptor.id;
        ruleInfo["name"] = descriptor.displayName;
        ruleInfo["description"] = descriptor.description;
        ruleInfo["defaultEnabled"] = descriptor.defaultEnabled;
        result.append(ruleInfo);
    }
    return result;
}

bool DataCleaningRuleRegistry::isRuleEnabled(const QVariant& rawValue, bool defaultEnabled)
{
    if (!rawValue.isValid() || rawValue.isNull()) {
        return defaultEnabled;
    }
    if (rawValue.canConvert<QVariantMap>()) {
        return rawValue.toMap().value("enabled", defaultEnabled).toBool();
    }
    return rawValue.toBool();
}

QVariantMap DataCleaningRuleRegistry::toRuleMap(const QVariant& rawValue)
{
    if (rawValue.canConvert<QVariantMap>()) {
        return rawValue.toMap();
    }
    QVariantMap ruleMap;
    if (rawValue.isValid()) {
        ruleMap["enabled"] = rawValue.toBool();
    }
    return ruleMap;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildTimeRangeRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, false)) {
        return rules;
    }

    DataCleaningEngine::CleaningRule rule = makeRule(
        DataCleaningEngine::RULE_TIME_RANGE,
        "timeRange",
        "时间范围过滤",
        "按日期区间过滤数据",
        DataCleaningEngine::RULE_LEVEL_OPTIONAL,
        DataCleaningEngine::RULE_MODE_SINGLE_POINT,
        90);
    rule.parameters["startDate"] = ruleMap.value("startDate", ruleMap.value("start")).toString();
    rule.parameters["endDate"] = ruleMap.value("endDate", ruleMap.value("end")).toString();
    rules.append(rule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildOutlierRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, false)) {
        return rules;
    }

    DataCleaningEngine::CleaningRule rule = makeRule(
        DataCleaningEngine::RULE_OUTLIER_DETECTION,
        "outlierFilter",
        "单点异常检测",
        "对日内跳变异常做基础过滤",
        DataCleaningEngine::RULE_LEVEL_RECOMMENDED,
        DataCleaningEngine::RULE_MODE_SINGLE_POINT,
        135);
    rule.parameters["threshold"] = ruleMap.value("threshold", 0.3);
    rules.append(rule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildMissingValueRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, true)) {
        return rules;
    }

    DataCleaningEngine::CleaningRule rule = makeRule(
        DataCleaningEngine::RULE_MISSING_VALUE_FILL,
        "missingValueFill",
        "缺失值处理",
        "优先按时序向前填充关键字段",
        DataCleaningEngine::RULE_LEVEL_RECOMMENDED,
        DataCleaningEngine::RULE_MODE_TEMPORAL,
        50);
    QStringList fields = toStringList(ruleMap.value("fields"));
    if (fields.isEmpty()) {
        fields = {"open", "high", "low", "close", "turnover_rate", "market_cap", "circulating_market_cap"};
    }
    rule.parameters["fields"] = fields;
    rule.parameters["maxLookbackDays"] = ruleMap.value("maxLookbackDays", 5);
    rules.append(rule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildDataCleaningRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, true)) {
        return rules;
    }

    DataCleaningEngine::CleaningRule formatRule = makeRule(
        DataCleaningEngine::RULE_FORMAT_VALIDATION,
        "formatValidation",
        "格式验证",
        "验证日期与关键数值字段格式",
        DataCleaningEngine::RULE_LEVEL_MANDATORY,
        DataCleaningEngine::RULE_MODE_SINGLE_POINT,
        100);
    formatRule.parameters["dateFormat"] = ruleMap.value("dateFormat", "auto");
    rules.append(formatRule);

    DataCleaningEngine::CleaningRule completenessRule = makeRule(
        DataCleaningEngine::RULE_COMPLETENESS_CHECK,
        "completeness",
        "完整性检查",
        "检查关键字段是否完整",
        DataCleaningEngine::RULE_LEVEL_MANDATORY,
        DataCleaningEngine::RULE_MODE_SINGLE_POINT,
        110);
    completenessRule.parameters["requiredFields"] = ruleMap.value("requiredFields", QStringList{"symbol", "date", "open", "high", "low", "close"});
    rules.append(completenessRule);

    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildDuplicateRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, true)) {
        return rules;
    }

    DataCleaningEngine::CleaningRule rule = makeRule(
        DataCleaningEngine::RULE_DUPLICATE_REMOVAL,
        "duplicateRemoval",
        "重复数据删除",
        "删除同一交易日重复的 symbol/date 记录",
        DataCleaningEngine::RULE_LEVEL_MANDATORY,
        DataCleaningEngine::RULE_MODE_SINGLE_POINT,
        10);
    QStringList keyFields = toStringList(ruleMap.value("keyFields"));
    if (keyFields.isEmpty()) {
        keyFields = {"symbol", "date"};
    }
    rule.parameters["keyFields"] = keyFields;
    rules.append(rule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildSurvivorBiasRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    if (!isRuleEnabled(rawValue, true)) {
        return rules;
    }
    rules.append(makeRule(DataCleaningEngine::RULE_SURVIVOR_BIAS, "survivorBias", "生存者偏差处理", "保留退市股票退市前的历史记录", DataCleaningEngine::RULE_LEVEL_MANDATORY, DataCleaningEngine::RULE_MODE_TEMPORAL, 30));
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildReportDateAlignmentRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    if (!isRuleEnabled(rawValue, true)) {
        return rules;
    }
    rules.append(makeRule(DataCleaningEngine::RULE_REPORT_DATE_ALIGNMENT, "reportDateAlignment", "财报日期对齐", "使用披露日而不是报告期作为生效日期", DataCleaningEngine::RULE_LEVEL_MANDATORY, DataCleaningEngine::RULE_MODE_TEMPORAL, 20));
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildPriceValidityRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, true)) {
        return rules;
    }
    DataCleaningEngine::CleaningRule rule = makeRule(DataCleaningEngine::RULE_PRICE_FILTER, "priceValidity", "价格有效性", "检查价格链和价格范围", DataCleaningEngine::RULE_LEVEL_MANDATORY, DataCleaningEngine::RULE_MODE_SINGLE_POINT, 120);
    rule.parameters["minPrice"] = ruleMap.value("minPrice", ruleMap.value("min", 0.01));
    rule.parameters["maxPrice"] = ruleMap.value("maxPrice", ruleMap.value("max", 10000.0));
    rule.parameters["enforceChain"] = ruleMap.value("enforceChain", true);
    rule.parameters["allowZeroWhenSuspended"] = ruleMap.value("allowZeroWhenSuspended", true);
    rules.append(rule);

    DataCleaningEngine::CleaningRule volumeRule = makeRule(DataCleaningEngine::RULE_VOLUME_FILTER, "volumeFilter", "成交量过滤", "检查成交量范围与异常零量情形", DataCleaningEngine::RULE_LEVEL_MANDATORY, DataCleaningEngine::RULE_MODE_SINGLE_POINT, 130);
    volumeRule.parameters["minVolume"] = ruleMap.value("minVolume", 0);
    volumeRule.parameters["maxVolume"] = ruleMap.value("maxVolume", 1000000000);
    volumeRule.parameters["allowZeroWhenSuspended"] = ruleMap.value("allowZeroWhenSuspended", true);
    rules.append(volumeRule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildAdjustedPriceRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, true)) {
        return rules;
    }
    DataCleaningEngine::CleaningRule rule = makeRule(DataCleaningEngine::RULE_ADJUSTED_PRICE, "adjustedPrice", "复权处理", "统一使用后复权价格", DataCleaningEngine::RULE_LEVEL_MANDATORY, DataCleaningEngine::RULE_MODE_SINGLE_POINT, 60);
    rule.parameters["preferAdjustedFields"] = ruleMap.value("preferAdjustedFields", true);
    rule.parameters["applyFactorFallback"] = ruleMap.value("applyFactorFallback", true);
    rules.append(rule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildNewStockFilterRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, true)) {
        return rules;
    }
    DataCleaningEngine::CleaningRule rule = makeRule(DataCleaningEngine::RULE_NEW_STOCK_FILTER, "newStockFilter", "新股过滤", "过滤上市后前 N 个交易日的新股", DataCleaningEngine::RULE_LEVEL_MANDATORY, DataCleaningEngine::RULE_MODE_TEMPORAL, 70);
    rule.parameters["minTradeDays"] = ruleMap.value("minTradeDays", 60);
    rules.append(rule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildStFilterRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    if (!isRuleEnabled(rawValue, true)) {
        return rules;
    }
    rules.append(makeRule(DataCleaningEngine::RULE_ST_FILTER, "stFilter", "ST状态剔除", "剔除 ST 和 *ST 股票", DataCleaningEngine::RULE_LEVEL_MANDATORY, DataCleaningEngine::RULE_MODE_SINGLE_POINT, 80));
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildLimitMoveRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, true)) {
        return rules;
    }
    DataCleaningEngine::CleaningRule rule = makeRule(DataCleaningEngine::RULE_LIMIT_MOVE_TAG, "limitMoveTag", "涨跌停标记", "生成涨跌停和买卖限制标签", DataCleaningEngine::RULE_LEVEL_RECOMMENDED, DataCleaningEngine::RULE_MODE_TAG_GENERATION, 140);
    rule.parameters["upThreshold"] = ruleMap.value("upThreshold", 9.5);
    rule.parameters["downThreshold"] = ruleMap.value("downThreshold", -9.5);
    rules.append(rule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildSuspensionFillRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, true)) {
        return rules;
    }
    DataCleaningEngine::CleaningRule rule = makeRule(DataCleaningEngine::RULE_SUSPENSION_FILL, "suspensionFill", "停牌填充", "停牌期间按时序向前填充价格，超阈值剔除", DataCleaningEngine::RULE_LEVEL_RECOMMENDED, DataCleaningEngine::RULE_MODE_TEMPORAL, 40);
    QStringList fillFields = toStringList(ruleMap.value("fillFields"));
    if (fillFields.isEmpty()) {
        fillFields = {"open", "high", "low", "close"};
    }
    rule.parameters["fillFields"] = fillFields;
    rule.parameters["maxForwardFillDays"] = ruleMap.value("maxForwardFillDays", 10);
    rule.parameters["dropAfterMaxDays"] = ruleMap.value("dropAfterMaxDays", true);
    rules.append(rule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildWinsorizationRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, true)) {
        return rules;
    }
    DataCleaningEngine::CleaningRule rule = makeRule(DataCleaningEngine::RULE_WINSORIZATION, "winsorization", "异常值缩尾", "按分位数缩尾因子值", DataCleaningEngine::RULE_LEVEL_RECOMMENDED, DataCleaningEngine::RULE_MODE_CROSS_SECTIONAL, 210);
    QStringList fields = toStringList(ruleMap.value("fields"));
    if (fields.isEmpty()) {
        fields = {"factor_value", "factor", "value", "score"};
    }
    rule.parameters["fields"] = fields;
    rule.parameters["lowerQuantile"] = ruleMap.value("lowerQuantile", 0.01);
    rule.parameters["upperQuantile"] = ruleMap.value("upperQuantile", 0.99);
    rules.append(rule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildMarketCapFilterRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, true)) {
        return rules;
    }
    DataCleaningEngine::CleaningRule rule = makeRule(DataCleaningEngine::RULE_MARKET_CAP_FILTER, "marketCapFilter", "市值过滤", "剔除市值尾部股票", DataCleaningEngine::RULE_LEVEL_RECOMMENDED, DataCleaningEngine::RULE_MODE_CROSS_SECTIONAL, 200);
    rule.parameters["lowerTail"] = ruleMap.value("lowerTail", 0.05);
    rules.append(rule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildIndexAlignmentRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, false)) {
        return rules;
    }
    DataCleaningEngine::CleaningRule rule = makeRule(DataCleaningEngine::RULE_INDEX_MEMBERSHIP_ALIGNMENT, "indexAlignment", "指数调整对齐", "成分股调整日滞后一天生效", DataCleaningEngine::RULE_LEVEL_OPTIONAL, DataCleaningEngine::RULE_MODE_TEMPORAL, 220);
    rule.parameters["lagDays"] = ruleMap.value("lagDays", 1);
    rules.append(rule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildContinuousSuspensionRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, false)) {
        return rules;
    }
    DataCleaningEngine::CleaningRule rule = makeRule(DataCleaningEngine::RULE_CONTINUOUS_SUSPENSION_FILTER, "continuousSuspensionFilter", "连续停牌剔除", "连续停牌超过阈值直接剔除", DataCleaningEngine::RULE_LEVEL_OPTIONAL, DataCleaningEngine::RULE_MODE_TEMPORAL, 150);
    rule.parameters["maxSuspensionDays"] = ruleMap.value("maxSuspensionDays", 10);
    rules.append(rule);
    return rules;
}

bool DataCleaningRuleRegistry::appendDirectEngineRule(const QVariantMap& ruleMap, QVector<DataCleaningEngine::CleaningRule>* rules)
{
    if (rules == nullptr || !ruleMap.contains("type")) {
        return false;
    }

    DataCleaningEngine::CleaningRule rule(
        static_cast<DataCleaningEngine::CleaningRuleType>(ruleMap.value("type").toInt()),
        ruleMap.value("id", ruleMap.value("name", QStringLiteral("customRule"))).toString(),
        ruleMap.value("description").toString());
    rule.name = ruleMap.value("name", QStringLiteral("自定义规则")).toString();
    rule.parameters = ruleMap.value("parameters").toMap();
    rule.enabled = ruleMap.value("enabled", true).toBool();
    rule.level = static_cast<DataCleaningEngine::CleaningRuleLevel>(ruleMap.value("level", DataCleaningEngine::RULE_LEVEL_OPTIONAL).toInt());
    rule.mode = static_cast<DataCleaningEngine::CleaningRuleMode>(ruleMap.value("mode", DataCleaningEngine::RULE_MODE_SINGLE_POINT).toInt());
    rule.executionOrder = ruleMap.value("executionOrder", 0).toInt();
    rules->append(rule);
    return true;
}