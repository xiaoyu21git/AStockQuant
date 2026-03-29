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

}

DataCleaningRuleRegistry::DataCleaningRuleRegistry()
{
    m_descriptors = {
        {
            "timeRange",
            "时间范围过滤",
            "按日期区间过滤数据",
            false,
            &DataCleaningRuleRegistry::buildTimeRangeRules,
        },
        {
            "outlierFilter",
            "异常值检测",
            "检测并过滤价格和成交量异常值",
            false,
            &DataCleaningRuleRegistry::buildOutlierRules,
        },
        {
            "missingValue",
            "完整性检查",
            "校验关键字段完整性",
            false,
            &DataCleaningRuleRegistry::buildMissingValueRules,
        },
        {
            "dataCleaning",
            "基础数据清洗",
            "执行格式、价格和成交量清洗",
            false,
            &DataCleaningRuleRegistry::buildDataCleaningRules,
        },
        {
            "duplicateRemoval",
            "重复数据删除",
            "删除同 symbol/date 的重复记录",
            true,
            &DataCleaningRuleRegistry::buildDuplicateRules,
        },
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
        if (!rawRules.contains(descriptor.id) && !descriptor.defaultEnabled) {
            continue;
        }
        const QVariant rawValue = rawRules.contains(descriptor.id)
            ? rawRules.value(descriptor.id)
            : QVariant(descriptor.defaultEnabled);
        rules += descriptor.builder(rawValue);
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
        const QVariantMap ruleMap = rawValue.toMap();
        return ruleMap.value("enabled", defaultEnabled).toBool();
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

    const QString startDate = ruleMap.value("startDate", ruleMap.value("start")).toString().trimmed();
    const QString endDate = ruleMap.value("endDate", ruleMap.value("end")).toString().trimmed();
    if (startDate.isEmpty() || endDate.isEmpty()) {
        return rules;
    }

    DataCleaningEngine::CleaningRule rule(
        DataCleaningEngine::RULE_TIME_RANGE,
        "时间范围过滤",
        QString("过滤时间范围: %1 至 %2").arg(startDate, endDate)
    );
    rule.parameters["startDate"] = startDate;
    rule.parameters["endDate"] = endDate;
    rule.enabled = true;
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

    DataCleaningEngine::CleaningRule rule(
        DataCleaningEngine::RULE_OUTLIER_DETECTION,
        "异常值检测",
        "检测并过滤价格和成交量的异常值"
    );
    rule.parameters["method"] = ruleMap.value("method", "zscore");
    rule.parameters["threshold"] = ruleMap.value("threshold", 3.0);
    rule.parameters["priceDeviation"] = ruleMap.value("priceDeviation", 3.0);
    rule.parameters["volumeDeviation"] = ruleMap.value("volumeDeviation", 5.0);
    rule.enabled = true;
    rules.append(rule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildMissingValueRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, false)) {
        return rules;
    }

    DataCleaningEngine::CleaningRule rule(
        DataCleaningEngine::RULE_COMPLETENESS_CHECK,
        "完整性检查",
        "检查数据字段完整性，过滤缺失值"
    );
    QStringList requiredFields = toStringList(ruleMap.value("requiredFields"));
    if (requiredFields.isEmpty()) {
        requiredFields = {"symbol", "date", "open", "high", "low", "close", "volume"};
    }
    rule.parameters["requiredFields"] = requiredFields;
    rule.enabled = true;
    rules.append(rule);
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildDataCleaningRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, false)) {
        return rules;
    }

    const QVariantMap formatParams = ruleMap.value("formatValidation").toMap();
    DataCleaningEngine::CleaningRule formatRule(
        DataCleaningEngine::RULE_FORMAT_VALIDATION,
        "格式验证",
        "验证数据格式正确性"
    );
    formatRule.parameters["dateFormat"] = formatParams.value("dateFormat", "auto");
    formatRule.parameters["symbolPattern"] = formatParams.value("symbolPattern", "^[0-9]{6}\\.[A-Z]{2}$");
    formatRule.parameters["datePattern"] = formatParams.value("datePattern", "^(\\d{4}[-./]\\d{2}[-./]\\d{2}|\\d{2}[-./]\\d{2}[-./]\\d{4})$");
    formatRule.enabled = true;
    rules.append(formatRule);

    const QVariantMap priceParams = ruleMap.value("priceFilter").toMap();
    DataCleaningEngine::CleaningRule priceRule(
        DataCleaningEngine::RULE_PRICE_FILTER,
        "价格过滤",
        "过滤异常价格数据"
    );
    priceRule.parameters["minPrice"] = priceParams.value("minPrice", priceParams.value("min", 0.01));
    priceRule.parameters["maxPrice"] = priceParams.value("maxPrice", priceParams.value("max", 10000.0));
    priceRule.parameters["checkOpen"] = priceParams.value("checkOpen", true);
    priceRule.parameters["checkHigh"] = priceParams.value("checkHigh", true);
    priceRule.parameters["checkLow"] = priceParams.value("checkLow", true);
    priceRule.parameters["checkClose"] = priceParams.value("checkClose", true);
    priceRule.enabled = true;
    rules.append(priceRule);

    const QVariantMap volumeParams = ruleMap.value("volumeFilter").toMap();
    DataCleaningEngine::CleaningRule volumeRule(
        DataCleaningEngine::RULE_VOLUME_FILTER,
        "成交量过滤",
        "过滤异常成交量数据"
    );
    volumeRule.parameters["minVolume"] = volumeParams.value("minVolume", volumeParams.value("min", 0));
    volumeRule.parameters["maxVolume"] = volumeParams.value("maxVolume", volumeParams.value("max", 1000000000));
    volumeRule.enabled = true;
    rules.append(volumeRule);

    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningRuleRegistry::buildDuplicateRules(const QVariant& rawValue)
{
    QVector<DataCleaningEngine::CleaningRule> rules;
    const QVariantMap ruleMap = toRuleMap(rawValue);
    if (!isRuleEnabled(ruleMap, true)) {
        return rules;
    }

    DataCleaningEngine::CleaningRule rule(
        DataCleaningEngine::RULE_DUPLICATE_REMOVAL,
        "重复数据删除",
        "删除重复的数据记录"
    );
    QStringList keyFields = toStringList(ruleMap.value("keyFields"));
    if (keyFields.isEmpty()) {
        keyFields = {"symbol", "date"};
    }
    rule.parameters["keyFields"] = keyFields;
    rule.enabled = true;
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
        ruleMap.value("name", QStringLiteral("自定义规则")).toString(),
        ruleMap.value("description").toString()
    );
    rule.parameters = ruleMap.value("parameters").toMap();
    rule.enabled = ruleMap.value("enabled", true).toBool();
    rules->append(rule);
    return true;
}