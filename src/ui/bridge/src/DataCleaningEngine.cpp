#include "DataCleaningEngine.h"
#include "DataCleaningPersistence.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

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

QString resolveAliasedField(const QVariantMap& data, const QStringList& keys)
{
    for (const QString& key : keys) {
        auto it = data.constFind(key);
        if (it == data.constEnd() || !it.value().isValid() || it.value().isNull()) {
            continue;
        }

        const QString text = it.value().toString().trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }

    return {};
}

bool hasAliasedField(const QVariantMap& data, const QStringList& keys)
{
    for (const QString& key : keys) {
        auto it = data.constFind(key);
        if (it == data.constEnd() || !it.value().isValid() || it.value().isNull()) {
            continue;
        }
        if (!it.value().toString().trimmed().isEmpty()) {
            return true;
        }
    }
    return false;
}

bool extractAliasedNumericValue(const QVariantMap& data, const QStringList& keys, double* outValue)
{
    for (const QString& key : keys) {
        auto it = data.constFind(key);
        if (it == data.constEnd() || !it.value().isValid() || it.value().isNull()) {
            continue;
        }

        bool ok = false;
        const double value = it.value().toDouble(&ok);
        if (ok && std::isfinite(value)) {
            if (outValue) {
                *outValue = value;
            }
            return true;
        }
    }

    return false;
}

bool extractAliasedBoolValue(const QVariantMap& data, const QStringList& keys, bool* outValue)
{
    for (const QString& key : keys) {
        auto it = data.constFind(key);
        if (it == data.constEnd() || !it.value().isValid() || it.value().isNull()) {
            continue;
        }

        if (it.value().type() == QVariant::Bool) {
            if (outValue) {
                *outValue = it.value().toBool();
            }
            return true;
        }

        const QString text = it.value().toString().trimmed().toLower();
        if (text == "1" || text == "true" || text == "yes" || text == "y") {
            if (outValue) {
                *outValue = true;
            }
            return true;
        }
        if (text == "0" || text == "false" || text == "no" || text == "n") {
            if (outValue) {
                *outValue = false;
            }
            return true;
        }
    }

    return false;
}

QDate parseFlexibleDate(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QStringList dateTimeFormats = {
        "yyyy-MM-dd HH:mm:ss",
        "yyyy/MM/dd HH:mm:ss",
        "yyyy.MM.dd HH:mm:ss",
        "yyyy-MM-ddTHH:mm:ss",
        "yyyy-MM-ddTHH:mm:ss.zzz"
    };

    for (const QString& format : dateTimeFormats) {
        const QDateTime dateTime = QDateTime::fromString(trimmed, format);
        if (dateTime.isValid()) {
            return dateTime.date();
        }
    }

    const QDateTime isoDateTime = QDateTime::fromString(trimmed, Qt::ISODate);
    if (isoDateTime.isValid()) {
        return isoDateTime.date();
    }

    const QDateTime isoDateTimeMs = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
    if (isoDateTimeMs.isValid()) {
        return isoDateTimeMs.date();
    }

    const QStringList dateFormats = {
        "yyyy-MM-dd",
        "yyyy/MM/dd",
        "yyyy.MM.dd",
        "yyyyMMdd",
        "dd/MM/yyyy",
        "dd-MM-yyyy"
    };

    for (const QString& format : dateFormats) {
        const QDate date = QDate::fromString(trimmed, format);
        if (date.isValid()) {
            return date;
        }
    }

    return {};
}

QString canonicalFieldKey(const QString& field)
{
    const QString normalized = field.trimmed().toLower();
    if (normalized == "trade_date" || normalized == "bar_time" || normalized == "report_date"
        || normalized == "publish_time" || normalized == "created_at" || normalized == "ann_date"
        || normalized == "announcement_date" || normalized == "disclosure_date") {
        return "date";
    }
    if (normalized == "code" || normalized == "stock_code" || normalized == "ts_code") {
        return "symbol";
    }
    if (normalized == "vol") {
        return "volume";
    }
    if (normalized == "preclose" || normalized == "prevclose") {
        return "pre_close";
    }
    if (normalized == "amount" || normalized == "turnover") {
        return "turnover_amount";
    }
    if (normalized == "turnrate" || normalized == "turn_rate") {
        return "turnover_rate";
    }
    if (normalized == "change" || normalized == "chg") {
        return "change_amt";
    }
    if (normalized == "pct_chg" || normalized == "pct_change") {
        return "change_pct";
    }
    if (normalized == "swing") {
        return "amplitude";
    }
    if (normalized == "pe" || normalized == "pe_ttm" || normalized == "市盈率") {
        return "pe_ratio";
    }
    if (normalized == "pb" || normalized == "市净率") {
        return "pb_ratio";
    }
    if (normalized == "total_mv") {
        return "market_cap";
    }
    if (normalized == "circ_mv") {
        return "circulating_market_cap";
    }
    return normalized;
}

QStringList aliasedKeysForField(const QString& field)
{
    const QString canonical = canonicalFieldKey(field);
    if (canonical == "date") {
        return {"date", "trade_date", "bar_time", "report_date", "publish_time", "created_at", "ann_date", "announcement_date", "disclosure_date"};
    }
    if (canonical == "symbol") {
        return {"symbol", "code", "stock_code", "ts_code"};
    }
    if (canonical == "volume") {
        return {"volume", "vol"};
    }
    if (canonical == "pre_close") {
        return {"pre_close", "prev_close", "preclose", "昨收", "昨收价"};
    }
    if (canonical == "turnover_amount") {
        return {"turnover_amount", "turnover", "amount"};
    }
    if (canonical == "turnover_rate") {
        return {"turnover_rate", "turn_rate", "turnrate", "换手率"};
    }
    if (canonical == "change_amt") {
        return {"change_amt", "change", "chg", "涨跌额"};
    }
    if (canonical == "change_pct") {
        return {"change_pct", "pct_chg", "pct_change", "changepercent"};
    }
    if (canonical == "amplitude") {
        return {"amplitude", "swing", "振幅"};
    }
    if (canonical == "pe_ratio") {
        return {"pe_ratio", "pe", "pe_ttm", "市盈率"};
    }
    if (canonical == "pb_ratio") {
        return {"pb_ratio", "pb", "市净率"};
    }
    if (canonical == "market_cap") {
        return {"market_cap", "total_mv", "total_market_cap", "总市值"};
    }
    if (canonical == "circulating_market_cap") {
        return {"circulating_market_cap", "circ_mv", "float_market_cap", "流通市值"};
    }
    if (canonical == "industry") {
        return {"industry", "industry_name", "sw_industry", "申万行业"};
    }
    return {canonical};
}

QDate resolveTradeDate(const QVariantMap& data)
{
    return parseFlexibleDate(resolveAliasedField(data, aliasedKeysForField("date")));
}

QDate resolveAliasedDate(const QVariantMap& data, const QStringList& keys)
{
    return parseFlexibleDate(resolveAliasedField(data, keys));
}

QString resolveCurrentStockLabel(const QVariantMap& data)
{
    const QString symbol = resolveAliasedField(data, aliasedKeysForField("symbol"));
    const QString name = resolveAliasedField(data, {"name", "stock_name", "stockName", "sec_name", "Name", "名称"});

    if (!name.isEmpty() && !symbol.isEmpty() && name != symbol) {
        return QString("%1 (%2)").arg(name, symbol);
    }
    if (!name.isEmpty()) {
        return name;
    }
    return symbol;
}

QString normalizeDateString(const QVariantMap& data)
{
    const QDate date = resolveTradeDate(data);
    return date.isValid() ? date.toString("yyyy-MM-dd") : QString();
}

void setCanonicalNumericField(QVariantMap& data, const QString& field, double value)
{
    data[canonicalFieldKey(field)] = value;
}

void setCanonicalStringField(QVariantMap& data, const QString& field, const QString& value)
{
    data[canonicalFieldKey(field)] = value;
}

void clearCanonicalField(QVariantMap& data, const QString& field)
{
    data[canonicalFieldKey(field)] = QVariant();
}

void setRuleTag(QVariantMap& data, const QString& key, const QVariant& value)
{
    QVariantMap tags = data.value("cleaning_tags").toMap();
    tags[key] = value;
    data["cleaning_tags"] = tags;
    data[key] = value;
}

void sanitizeValuationFields(QVariantMap& data)
{
    QStringList invalidFields;

    auto sanitizeField = [&](const QString& field, bool strictlyPositive) {
        double value = 0.0;
        if (!extractAliasedNumericValue(data, aliasedKeysForField(field), &value)) {
            return;
        }
        const bool invalid = !std::isfinite(value) || (strictlyPositive ? value <= 0.0 : std::abs(value) <= 1e-8);
        if (!invalid) {
            setCanonicalNumericField(data, field, value);
            return;
        }

        clearCanonicalField(data, field);
        invalidFields.append(canonicalFieldKey(field));
    };

    sanitizeField("pe_ratio", false);
    sanitizeField("pb_ratio", false);
    sanitizeField("market_cap", true);
    sanitizeField("circulating_market_cap", true);

    double marketCap = 0.0;
    double circulatingMarketCap = 0.0;
    if (extractAliasedNumericValue(data, aliasedKeysForField("market_cap"), &marketCap)
        && extractAliasedNumericValue(data, aliasedKeysForField("circulating_market_cap"), &circulatingMarketCap)
        && std::isfinite(marketCap) && std::isfinite(circulatingMarketCap)
        && marketCap > 0.0 && circulatingMarketCap > 0.0 && marketCap < circulatingMarketCap) {
        clearCanonicalField(data, "market_cap");
        if (!invalidFields.contains("market_cap")) {
            invalidFields.append("market_cap");
        }
    }

    if (!invalidFields.isEmpty()) {
        setRuleTag(data, "valuation_sanitized", true);
        data["valuation_invalid_fields"] = invalidFields;
    }
}

double normalizePercentValue(double value)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::abs(value) <= 1.0 ? value * 100.0 : value;
}

double quantile(std::vector<double> values, double q)
{
    if (values.empty()) {
        return 0.0;
    }

    q = std::clamp(q, 0.0, 1.0);
    std::sort(values.begin(), values.end());
    const double position = q * static_cast<double>(values.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(position));
    const size_t upper = static_cast<size_t>(std::ceil(position));
    if (lower == upper) {
        return values[lower];
    }
    const double weight = position - static_cast<double>(lower);
    return values[lower] * (1.0 - weight) + values[upper] * weight;
}

double calculateMean(const std::vector<double>& values)
{
    if (values.empty()) {
        return 0.0;
    }
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / static_cast<double>(values.size());
}

double calculateStdDev(const std::vector<double>& values, double mean)
{
    if (values.size() < 2) {
        return 0.0;
    }
    double sumSquares = 0.0;
    for (double value : values) {
        const double diff = value - mean;
        sumSquares += diff * diff;
    }
    return std::sqrt(sumSquares / static_cast<double>(values.size() - 1));
}

QStringList defaultMissingFillFields()
{
    return {"open", "high", "low", "close", "turnover_rate", "market_cap", "circulating_market_cap"};
}

QStringList defaultFactorFields()
{
    return {"factor_value", "factor", "value", "score"};
}

QStringList priceFields()
{
    return {"open", "high", "low", "close"};
}

bool extractAnyNumericValue(const QVariantMap& data, const QStringList& fields, double* outValue, QString* outField = nullptr)
{
    for (const QString& field : fields) {
        double value = 0.0;
        if (!extractAliasedNumericValue(data, aliasedKeysForField(field), &value)) {
            continue;
        }
        if (outValue) {
            *outValue = value;
        }
        if (outField) {
            *outField = canonicalFieldKey(field);
        }
        return true;
    }
    return false;
}

bool isMissingValue(const QVariantMap& data, const QString& field)
{
    for (const QString& alias : aliasedKeysForField(field)) {
        auto it = data.constFind(alias);
        if (it == data.constEnd()) {
            continue;
        }
        if (!it.value().isValid() || it.value().isNull()) {
            return true;
        }
        if (it.value().type() == QVariant::String && it.value().toString().trimmed().isEmpty()) {
            return true;
        }
        if (it.value().canConvert<double>()) {
            bool ok = false;
            const double value = it.value().toDouble(&ok);
            return !ok || !std::isfinite(value);
        }
        return false;
    }
    return true;
}

bool isSuspendedRecord(const QVariantMap& data)
{
    bool suspended = false;
    if (extractAliasedBoolValue(data, {"is_suspended", "suspended", "isSuspended"}, &suspended) && suspended) {
        return true;
    }

    const QString status = resolveAliasedField(data, {"trade_status", "status", "trading_status"}).toUpper();
    if (status == "SUSPENDED" || status == "HALT" || status == "停牌") {
        return true;
    }

    double volume = 0.0;
    return extractAliasedNumericValue(data, aliasedKeysForField("volume"), &volume) && volume <= 0.0;
}

struct PreparedRecord {
    QVariantMap data;
    int originalIndex{0};
    QDate tradeDate;
    QString symbol;
};

} // namespace

struct DataCleaningEngine::RuntimeContext {
    struct SymbolState {
        int tradeDaysSeen{0};
        int consecutiveSuspensions{0};
        QDate lastTradeDate;
        QVariantMap lastValidValues;
    };

    QSet<QString> seenKeys;
    QHash<QString, SymbolState> symbols;
};

DataCleaningEngine::DataCleaningEngine(QObject *parent)
    : QObject(parent)
{
    m_rules = createDefaultRuleSet();
    qDebug() << "DataCleaningEngine initialized with" << m_rules.size() << "default rules";
}

DataCleaningEngine::~DataCleaningEngine()
{
    m_seenKeys.clear();
    m_cleaningContext.clear();
}

void DataCleaningEngine::addRule(const CleaningRule& rule)
{
    QMutexLocker locker(&m_mutex);

    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].name == rule.name || m_rules[i].id == rule.id) {
            m_rules[i] = rule;
            emit rulesUpdated();
            return;
        }
    }

    m_rules.append(rule);
    emit rulesUpdated();
}

void DataCleaningEngine::removeRule(const QString& ruleName)
{
    QMutexLocker locker(&m_mutex);

    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].name == ruleName || m_rules[i].id == ruleName) {
            m_rules.removeAt(i);
            emit rulesUpdated();
            return;
        }
    }
}

void DataCleaningEngine::setRuleEnabled(const QString& ruleName, bool enabled)
{
    QMutexLocker locker(&m_mutex);

    for (CleaningRule& rule : m_rules) {
        if (rule.name == ruleName || rule.id == ruleName) {
            rule.enabled = enabled;
            emit rulesUpdated();
            return;
        }
    }
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningEngine::getRules() const
{
    QMutexLocker locker(&m_mutex);
    return m_rules;
}

QVariantList DataCleaningEngine::cleanData(const QVariantList& data,
                                          const QVector<CleaningRule>& rules)
{
    {
        QMutexLocker locker(&m_mutex);
        m_seenKeys.clear();
        m_cleaningContext.clear();
        m_lastStats = CleaningStats();
    }

    const int total = data.size();
    if (total == 0) {
        qWarning() << "No data to clean";
        return {};
    }

    QVector<CleaningRule> rulesToUse = rules.isEmpty() ? getRules() : rules;
    QVector<CleaningRule> enabledRules;
    enabledRules.reserve(rulesToUse.size());
    for (const CleaningRule& rule : rulesToUse) {
        if (rule.enabled && validateRuleParameters(rule)) {
            enabledRules.append(rule);
        }
    }

    std::sort(enabledRules.begin(), enabledRules.end(), [](const CleaningRule& lhs, const CleaningRule& rhs) {
        if (lhs.executionOrder != rhs.executionOrder) {
            return lhs.executionOrder < rhs.executionOrder;
        }
        return lhs.type < rhs.type;
    });

    QVector<CleaningRule> rowRules;
    QVector<CleaningRule> crossSectionalRules;
    for (const CleaningRule& rule : enabledRules) {
        if (isCrossSectionalRule(rule)) {
            crossSectionalRules.append(rule);
        } else {
            rowRules.append(rule);
        }
    }

    CleaningStats stats;
    stats.totalRecords = total;
    stats.startTime = QDateTime::currentDateTime();

    auto emitProgressDetail = [this](int progress,
                                     const QString& message,
                                     const QString& currentStock,
                                     int keptRecords,
                                     int removedRecords) {
        emit cleaningProgress(progress, message);
        emit cleaningProgressDetail(progress, message, currentStock, keptRecords, removedRecords);
    };

    emitProgressDetail(0,
                       QString("开始数据清洗，共%1条记录").arg(total),
                       QString(),
                       0,
                       0);

    QVector<PreparedRecord> preparedRecords;
    preparedRecords.reserve(total);

    int processedRecords = 0;
    int validProcessed = 0;
    int lastProgress = -1;

    auto updatePreparationProgress = [&](const QString& currentStock) {
        if (total <= 0) {
            return;
        }
        int currentProgress = static_cast<int>((processedRecords * 25.0) / total);
        if (processedRecords == total) {
            currentProgress = 25;
        }
        if (currentProgress == lastProgress && processedRecords != total && processedRecords % 500 != 0) {
            return;
        }
        lastProgress = currentProgress;

        const QString message = QString("准备清洗: %1/%2 (%3%) - 可清洗: %4 - 暂存移除: %5")
                                    .arg(processedRecords)
                                    .arg(total)
                                    .arg(currentProgress)
                                    .arg(validProcessed)
                                    .arg(processedRecords - validProcessed);
        emitProgressDetail(currentProgress,
                           message,
                           currentStock,
                           validProcessed,
                           processedRecords - validProcessed);
    };

    for (int index = 0; index < data.size(); ++index) {
        processedRecords++;

        if (!data[index].canConvert<QVariantMap>()) {
            updatePreparationProgress(QString());
            continue;
        }

        QVariantMap record = data[index].toMap();
        const QString currentStock = resolveCurrentStockLabel(record);
        if (!validateDataFormat(record)) {
            updatePreparationProgress(currentStock);
            continue;
        }

        PreparedRecord prepared;
        prepared.data = record;
        prepared.originalIndex = index;
        prepared.tradeDate = resolveTradeDate(record);
        prepared.symbol = resolveAliasedField(record, aliasedKeysForField("symbol"));
        preparedRecords.append(prepared);
        validProcessed++;
        updatePreparationProgress(currentStock);
    }

    std::sort(preparedRecords.begin(), preparedRecords.end(), [](const PreparedRecord& lhs, const PreparedRecord& rhs) {
        if (lhs.tradeDate != rhs.tradeDate) {
            return lhs.tradeDate < rhs.tradeDate;
        }
        if (lhs.symbol != rhs.symbol) {
            return lhs.symbol < rhs.symbol;
        }
        return lhs.originalIndex < rhs.originalIndex;
    });

    RuntimeContext context;
    QVariantList cleanedData;
    cleanedData.reserve(preparedRecords.size());
    emitProgressDetail(25,
                       QString("开始逐条规则清洗，共%1条可清洗记录").arg(preparedRecords.size()),
                       QString(),
                       0,
                       total - validProcessed);

    int rowPhaseProgress = -1;
    int rowPhaseProcessed = 0;

    for (const PreparedRecord& prepared : preparedRecords) {
        QVariantMap record = prepared.data;
        bool keep = true;
        for (const CleaningRule& rule : rowRules) {
            const bool passed = executeRule(rule, record, context);
            updateCleaningStats(rule, 1, passed ? 1 : 0);
            if (!passed) {
                keep = false;
                break;
            }
        }
        if (keep) {
            cleanedData.append(record);
        }

        rowPhaseProcessed++;
        const int currentProgress = preparedRecords.isEmpty()
            ? 80
            : 25 + static_cast<int>((rowPhaseProcessed * 55.0) / preparedRecords.size());
        if (currentProgress != rowPhaseProgress || rowPhaseProcessed == preparedRecords.size() || rowPhaseProcessed % 500 == 0) {
            rowPhaseProgress = currentProgress;
            const int keptCount = cleanedData.size();
            const int removedCount = validProcessed - keptCount;
            emitProgressDetail(currentProgress,
                               QString("逐条清洗: %1/%2 (%3%) - 暂存保留: %4 - 暂存移除: %5")
                                   .arg(rowPhaseProcessed)
                                   .arg(preparedRecords.size())
                                   .arg(currentProgress)
                                   .arg(keptCount)
                                   .arg(removedCount),
                               resolveCurrentStockLabel(prepared.data),
                               keptCount,
                               removedCount);
        }
    }

    if (!crossSectionalRules.isEmpty() && !cleanedData.isEmpty()) {
        QMap<QString, QVariantList> recordsByDate;
        for (const QVariant& item : cleanedData) {
            const QVariantMap record = item.toMap();
            recordsByDate[normalizeDateString(record)].append(record);
        }

        int crossTotalSteps = 0;
        for (const CleaningRule& rule : crossSectionalRules) {
            Q_UNUSED(rule)
            crossTotalSteps += recordsByDate.size();
        }
        int crossProcessed = 0;
        int crossKeptCount = cleanedData.size();

        for (const CleaningRule& rule : crossSectionalRules) {
            for (auto it = recordsByDate.begin(); it != recordsByDate.end(); ++it) {
                const int before = it.value().size();
                executeCrossSectionalRule(rule, it.value(), context);
                const int after = it.value().size();
                updateCleaningStats(rule, static_cast<int>(before), static_cast<int>((std::min)(before, after)));
                crossProcessed++;
                crossKeptCount -= (before - after);
                const int currentProgress = crossTotalSteps <= 0
                    ? 95
                    : 80 + static_cast<int>((crossProcessed * 15.0) / crossTotalSteps);
                const int removedCount = validProcessed - crossKeptCount;
                emitProgressDetail(currentProgress,
                                   QString("截面清洗: %1/%2 (%3%) - 暂存保留: %4 - 暂存移除: %5")
                                       .arg(crossProcessed)
                                       .arg(crossTotalSteps)
                                       .arg(currentProgress)
                                       .arg(crossKeptCount)
                                       .arg(removedCount),
                                   it.key(),
                                   crossKeptCount,
                                   removedCount);
            }
        }

        cleanedData.clear();
        for (auto it = recordsByDate.begin(); it != recordsByDate.end(); ++it) {
            std::sort(it.value().begin(), it.value().end(), [](const QVariant& lhsVar, const QVariant& rhsVar) {
                const QVariantMap lhs = lhsVar.toMap();
                const QVariantMap rhs = rhsVar.toMap();
                const QString lhsSymbol = resolveAliasedField(lhs, aliasedKeysForField("symbol"));
                const QString rhsSymbol = resolveAliasedField(rhs, aliasedKeysForField("symbol"));
                return lhsSymbol < rhsSymbol;
            });
            for (const QVariant& item : it.value()) {
                cleanedData.append(item);
            }
        }
    }

    stats.cleanedRecords = cleanedData.size();
    stats.removedRecords = validProcessed - cleanedData.size();
    stats.endTime = QDateTime::currentDateTime();
    stats.durationMs = stats.startTime.msecsTo(stats.endTime);

    {
        QMutexLocker locker(&m_mutex);
        stats.ruleStats = m_lastStats.ruleStats;
        m_lastStats = stats;
    }

    const int skipped = total - validProcessed;
    const QString finalMessage = QString("数据清洗完成: 共%1条，有效%2条，跳过%3条，保留%4条，移除%5条")
                                     .arg(total)
                                     .arg(validProcessed)
                                     .arg(skipped)
                                     .arg(cleanedData.size())
                                     .arg(stats.removedRecords);
    emitProgressDetail(100,
                       finalMessage,
                       QString(),
                       cleanedData.size(),
                       stats.removedRecords);
    emit cleaningCompleted(stats);

    return cleanedData;
}

QVariantList DataCleaningEngine::cleanDataWithPersistence(const QVariantList& data,
                                                         const QVector<CleaningRule>& rules,
                                                         bool autoSave)
{
    QVariantList cleanedData = cleanData(data, rules);
    if (autoSave && !cleanedData.isEmpty()) {
        saveCleaningResult(cleanedData);
    }
    return cleanedData;
}

bool DataCleaningEngine::saveCleaningResult(const QVariantList& cleanedData)
{
    try {
        ui::bridge::DataCleaningPersistence persistence;
        const QString taskId = QUuid::createUuid().toString();

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

        const bool success = persistence.saveCleaningResult(taskId, cleanedData, stats);
        if (success) {
            emit dataSaved(taskId);
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

QVariantList DataCleaningEngine::loadCleanedData(const QString& taskId)
{
    try {
        ui::bridge::DataCleaningPersistence persistence;
        const QVariantList loadedData = persistence.loadCleanedData(taskId);
        if (!loadedData.isEmpty()) {
            emit dataLoaded(taskId, loadedData);
        }
        return loadedData;
    } catch (const std::exception& e) {
        qCritical() << "加载清洗结果时发生异常:" << e.what();
        return {};
    } catch (...) {
        qCritical() << "加载清洗结果时发生未知异常";
        return {};
    }
}

double DataCleaningEngine::calculateQualityScore(const CleaningStats& stats)
{
    if (stats.totalRecords <= 0) {
        return 0.0;
    }
    const double cleaningRate = static_cast<double>(stats.cleanedRecords) / static_cast<double>(stats.totalRecords);
    const double removalRate = static_cast<double>(stats.removedRecords) / static_cast<double>(stats.totalRecords);
    return std::clamp(cleaningRate * 100.0 - removalRate * 50.0, 0.0, 100.0);
}

QVector<QVariantList> DataCleaningEngine::batchCleanData(const QVector<QVariantList>& dataList,
                                                        const QVector<CleaningRule>& rules)
{
    QVector<QVariantList> results;
    results.reserve(dataList.size());

    for (int i = 0; i < dataList.size(); ++i) {
        emit cleaningProgress(static_cast<int>((i * 100.0) / (dataList.isEmpty() ? 1 : dataList.size())),
                             QString("批量清洗中: %1/%2").arg(i + 1).arg(dataList.size()));
        results.append(cleanData(dataList[i], rules));
    }

    emit cleaningProgress(100, "批量清洗完成");
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

    CleaningRule duplicateRemoval(RULE_DUPLICATE_REMOVAL, "duplicateRemoval", "删除重复 symbol/date 记录");
    duplicateRemoval.name = "重复数据删除";
    duplicateRemoval.level = RULE_LEVEL_MANDATORY;
    duplicateRemoval.mode = RULE_MODE_SINGLE_POINT;
    duplicateRemoval.executionOrder = 10;
    duplicateRemoval.parameters["keyFields"] = QStringList{"symbol", "date"};
    rules.append(duplicateRemoval);

    CleaningRule reportAlignment(RULE_REPORT_DATE_ALIGNMENT, "reportDateAlignment", "使用披露日而不是报告期作为生效日期");
    reportAlignment.name = "财报日期对齐";
    reportAlignment.level = RULE_LEVEL_MANDATORY;
    reportAlignment.mode = RULE_MODE_TEMPORAL;
    reportAlignment.executionOrder = 20;
    rules.append(reportAlignment);

    CleaningRule survivorBias(RULE_SURVIVOR_BIAS, "survivorBias", "保留退市股票退市前的历史数据，剔除退市后的无效记录");
    survivorBias.name = "生存者偏差处理";
    survivorBias.level = RULE_LEVEL_MANDATORY;
    survivorBias.mode = RULE_MODE_TEMPORAL;
    survivorBias.executionOrder = 30;
    rules.append(survivorBias);

    CleaningRule suspensionFill(RULE_SUSPENSION_FILL, "suspensionFill", "停牌期间向前填充价格并跟踪连续停牌天数");
    suspensionFill.name = "停牌填充";
    suspensionFill.level = RULE_LEVEL_RECOMMENDED;
    suspensionFill.mode = RULE_MODE_TEMPORAL;
    suspensionFill.executionOrder = 40;
    suspensionFill.parameters["fillFields"] = priceFields();
    suspensionFill.parameters["maxForwardFillDays"] = 10;
    suspensionFill.parameters["dropAfterMaxDays"] = true;
    rules.append(suspensionFill);

    CleaningRule missingValueFill(RULE_MISSING_VALUE_FILL, "missingValueFill", "缺失值优先按时序向前填充");
    missingValueFill.name = "缺失值处理";
    missingValueFill.level = RULE_LEVEL_RECOMMENDED;
    missingValueFill.mode = RULE_MODE_TEMPORAL;
    missingValueFill.executionOrder = 50;
    missingValueFill.parameters["fields"] = defaultMissingFillFields();
    missingValueFill.parameters["maxLookbackDays"] = 5;
    rules.append(missingValueFill);

    CleaningRule adjustedPrice(RULE_ADJUSTED_PRICE, "adjustedPrice", "统一使用后复权价格");
    adjustedPrice.name = "复权处理";
    adjustedPrice.level = RULE_LEVEL_MANDATORY;
    adjustedPrice.mode = RULE_MODE_SINGLE_POINT;
    adjustedPrice.executionOrder = 60;
    adjustedPrice.parameters["preferAdjustedFields"] = true;
    adjustedPrice.parameters["applyFactorFallback"] = true;
    rules.append(adjustedPrice);

    CleaningRule newStockFilter(RULE_NEW_STOCK_FILTER, "newStockFilter", "过滤上市后前 N 个交易日的新股");
    newStockFilter.name = "新股过滤";
    newStockFilter.level = RULE_LEVEL_MANDATORY;
    newStockFilter.mode = RULE_MODE_TEMPORAL;
    newStockFilter.executionOrder = 70;
    newStockFilter.parameters["minTradeDays"] = 60;
    rules.append(newStockFilter);

    CleaningRule stFilter(RULE_ST_FILTER, "stFilter", "剔除 ST 和 *ST 股票");
    stFilter.name = "ST状态剔除";
    stFilter.level = RULE_LEVEL_MANDATORY;
    stFilter.mode = RULE_MODE_SINGLE_POINT;
    stFilter.executionOrder = 80;
    rules.append(stFilter);

    CleaningRule timeRange(RULE_TIME_RANGE, "timeRange", "过滤指定时间范围外的数据");
    timeRange.name = "时间范围过滤";
    timeRange.level = RULE_LEVEL_OPTIONAL;
    timeRange.mode = RULE_MODE_SINGLE_POINT;
    timeRange.executionOrder = 90;
    timeRange.parameters["startDate"] = QDateTime::currentDateTime().addDays(-365).date().toString("yyyy-MM-dd");
    timeRange.parameters["endDate"] = QDateTime::currentDateTime().date().toString("yyyy-MM-dd");
    timeRange.enabled = false;
    rules.append(timeRange);

    CleaningRule formatValidation(RULE_FORMAT_VALIDATION, "formatValidation", "验证日期与关键数值字段格式");
    formatValidation.name = "格式验证";
    formatValidation.level = RULE_LEVEL_MANDATORY;
    formatValidation.mode = RULE_MODE_SINGLE_POINT;
    formatValidation.executionOrder = 100;
    formatValidation.parameters["dateFormat"] = "auto";
    rules.append(formatValidation);

    CleaningRule completeness(RULE_COMPLETENESS_CHECK, "completeness", "检查关键字段是否完整");
    completeness.name = "完整性检查";
    completeness.level = RULE_LEVEL_MANDATORY;
    completeness.mode = RULE_MODE_SINGLE_POINT;
    completeness.executionOrder = 110;
    completeness.parameters["requiredFields"] = QStringList{"symbol", "date", "open", "high", "low", "close"};
    rules.append(completeness);

    CleaningRule priceValidity(RULE_PRICE_FILTER, "priceValidity", "检查价格链和价格有效性");
    priceValidity.name = "价格有效性";
    priceValidity.level = RULE_LEVEL_MANDATORY;
    priceValidity.mode = RULE_MODE_SINGLE_POINT;
    priceValidity.executionOrder = 120;
    priceValidity.parameters["minPrice"] = 0.01;
    priceValidity.parameters["maxPrice"] = 10000.0;
    priceValidity.parameters["enforceChain"] = true;
    priceValidity.parameters["allowZeroWhenSuspended"] = true;
    priceValidity.parameters["requirePositiveTurnoverWhenTraded"] = true;
    priceValidity.parameters["requireConsistentDerivedFields"] = true;
    rules.append(priceValidity);

    CleaningRule volumeFilter(RULE_VOLUME_FILTER, "volumeFilter", "检查成交量范围与异常零量情形");
    volumeFilter.name = "成交量过滤";
    volumeFilter.level = RULE_LEVEL_MANDATORY;
    volumeFilter.mode = RULE_MODE_SINGLE_POINT;
    volumeFilter.executionOrder = 130;
    volumeFilter.parameters["minVolume"] = 0;
    volumeFilter.parameters["maxVolume"] = 1000000000;
    volumeFilter.parameters["allowZeroWhenSuspended"] = true;
    rules.append(volumeFilter);

    CleaningRule limitMoveTag(RULE_LIMIT_MOVE_TAG, "limitMoveTag", "标记涨跌停和交易限制");
    limitMoveTag.name = "涨跌停标记";
    limitMoveTag.level = RULE_LEVEL_RECOMMENDED;
    limitMoveTag.mode = RULE_MODE_TAG_GENERATION;
    limitMoveTag.executionOrder = 140;
    limitMoveTag.parameters["upThreshold"] = 9.5;
    limitMoveTag.parameters["downThreshold"] = -9.5;
    rules.append(limitMoveTag);

    CleaningRule continuousSuspension(RULE_CONTINUOUS_SUSPENSION_FILTER, "continuousSuspensionFilter", "连续停牌过久的股票剔除");
    continuousSuspension.name = "连续停牌剔除";
    continuousSuspension.level = RULE_LEVEL_OPTIONAL;
    continuousSuspension.mode = RULE_MODE_TEMPORAL;
    continuousSuspension.executionOrder = 150;
    continuousSuspension.parameters["maxSuspensionDays"] = 10;
    continuousSuspension.enabled = false;
    rules.append(continuousSuspension);

    CleaningRule marketCapFilter(RULE_MARKET_CAP_FILTER, "marketCapFilter", "剔除市值尾部股票");
    marketCapFilter.name = "市值过滤";
    marketCapFilter.level = RULE_LEVEL_RECOMMENDED;
    marketCapFilter.mode = RULE_MODE_CROSS_SECTIONAL;
    marketCapFilter.executionOrder = 200;
    marketCapFilter.parameters["lowerTail"] = 0.05;
    rules.append(marketCapFilter);

    CleaningRule winsorization(RULE_WINSORIZATION, "winsorization", "对因子分布做缩尾处理");
    winsorization.name = "异常值缩尾";
    winsorization.level = RULE_LEVEL_RECOMMENDED;
    winsorization.mode = RULE_MODE_CROSS_SECTIONAL;
    winsorization.executionOrder = 210;
    winsorization.parameters["fields"] = defaultFactorFields();
    winsorization.parameters["lowerQuantile"] = 0.01;
    winsorization.parameters["upperQuantile"] = 0.99;
    rules.append(winsorization);

    CleaningRule indexAlignment(RULE_INDEX_MEMBERSHIP_ALIGNMENT, "indexAlignment", "指数调仓日按滞后一天生效");
    indexAlignment.name = "指数调整对齐";
    indexAlignment.level = RULE_LEVEL_OPTIONAL;
    indexAlignment.mode = RULE_MODE_TEMPORAL;
    indexAlignment.executionOrder = 220;
    indexAlignment.parameters["lagDays"] = 1;
    indexAlignment.enabled = false;
    rules.append(indexAlignment);

    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningEngine::createTechnicalAnalysisRuleSet()
{
    QVector<CleaningRule> rules = createDefaultRuleSet();
    for (CleaningRule& rule : rules) {
        if (rule.type == RULE_STANDARDIZATION || rule.type == RULE_WINSORIZATION) {
            rule.enabled = true;
        }
    }
    return rules;
}

QVector<DataCleaningEngine::CleaningRule> DataCleaningEngine::createFundamentalAnalysisRuleSet()
{
    QVector<CleaningRule> rules = createDefaultRuleSet();
    for (CleaningRule& rule : rules) {
        if (rule.type == RULE_REPORT_DATE_ALIGNMENT || rule.type == RULE_MARKET_CAP_FILTER) {
            rule.enabled = true;
        }
    }
    return rules;
}

bool DataCleaningEngine::validateDataFormat(const QVariantMap& data) const
{
    const QString symbol = resolveAliasedField(data, aliasedKeysForField("symbol"));
    if (symbol.isEmpty()) {
        return false;
    }

    const QDate date = resolveTradeDate(data);
    return date.isValid();
}

QVariantMap DataCleaningEngine::exportRulesToJson() const
{
    QVariantMap json;
    QVariantList rulesArray;

    QMutexLocker locker(&m_mutex);
    for (const CleaningRule& rule : m_rules) {
        QVariantMap ruleJson;
        ruleJson["type"] = static_cast<int>(rule.type);
        ruleJson["id"] = rule.id;
        ruleJson["name"] = rule.name;
        ruleJson["description"] = rule.description;
        ruleJson["parameters"] = rule.parameters;
        ruleJson["enabled"] = rule.enabled;
        ruleJson["level"] = static_cast<int>(rule.level);
        ruleJson["mode"] = static_cast<int>(rule.mode);
        ruleJson["executionOrder"] = rule.executionOrder;
        rulesArray.append(ruleJson);
    }

    json["rules"] = rulesArray;
    json["version"] = "2.0";
    json["exportTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    return json;
}

bool DataCleaningEngine::importRulesFromJson(const QVariantMap& json)
{
    if (!json.contains("rules") || !json.value("rules").canConvert<QVariantList>()) {
        return false;
    }

    QVector<CleaningRule> newRules;
    const QVariantList rulesArray = json.value("rules").toList();
    for (const QVariant& ruleVar : rulesArray) {
        const QVariantMap ruleMap = ruleVar.toMap();
        if (!ruleMap.contains("type") || !ruleMap.contains("name")) {
            continue;
        }

        CleaningRule rule(static_cast<CleaningRuleType>(ruleMap.value("type").toInt()),
                         ruleMap.value("id", ruleMap.value("name")).toString(),
                         ruleMap.value("description").toString());
        rule.name = ruleMap.value("name").toString();
        rule.parameters = ruleMap.value("parameters").toMap();
        rule.enabled = ruleMap.value("enabled", true).toBool();
        rule.level = static_cast<CleaningRuleLevel>(ruleMap.value("level", RULE_LEVEL_OPTIONAL).toInt());
        rule.mode = static_cast<CleaningRuleMode>(ruleMap.value("mode", RULE_MODE_SINGLE_POINT).toInt());
        rule.executionOrder = ruleMap.value("executionOrder", 0).toInt();
        newRules.append(rule);
    }

    {
        QMutexLocker locker(&m_mutex);
        m_rules = newRules;
    }

    emit rulesUpdated();
    return true;
}

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
        return rule.parameters.contains("threshold") || rule.parameters.contains("priceDeviation");
    case RULE_DUPLICATE_REMOVAL:
        return rule.parameters.contains("keyFields");
    case RULE_FORMAT_VALIDATION:
        return rule.parameters.contains("dateFormat");
    case RULE_NEW_STOCK_FILTER:
        return rule.parameters.contains("minTradeDays");
    case RULE_SUSPENSION_FILL:
        return rule.parameters.contains("maxForwardFillDays");
    case RULE_WINSORIZATION:
        return rule.parameters.contains("lowerQuantile") && rule.parameters.contains("upperQuantile");
    case RULE_MARKET_CAP_FILTER:
        return rule.parameters.contains("lowerTail");
    case RULE_STANDARDIZATION:
    case RULE_NEUTRALIZATION:
    case RULE_SURVIVOR_BIAS:
    case RULE_REPORT_DATE_ALIGNMENT:
    case RULE_ADJUSTED_PRICE:
    case RULE_ST_FILTER:
    case RULE_LIMIT_MOVE_TAG:
    case RULE_MISSING_VALUE_FILL:
    case RULE_INDEX_MEMBERSHIP_ALIGNMENT:
    case RULE_CONTINUOUS_SUSPENSION_FILTER:
    case RULE_CUSTOM_FILTER:
        return true;
    default:
        return false;
    }
}

bool DataCleaningEngine::isCrossSectionalRule(const CleaningRule& rule) const
{
    return rule.mode == RULE_MODE_CROSS_SECTIONAL;
}

bool DataCleaningEngine::executeRule(const CleaningRule& rule, QVariantMap& data, RuntimeContext& context)
{
    sanitizeValuationFields(data);

    const QString symbol = resolveAliasedField(data, aliasedKeysForField("symbol"));
    RuntimeContext::SymbolState& state = context.symbols[symbol];
    const QDate tradeDate = resolveTradeDate(data);
    if (tradeDate.isValid() && state.lastTradeDate != tradeDate) {
        state.lastTradeDate = tradeDate;
    }

    switch (rule.type) {
    case RULE_TIME_RANGE: {
        const QDate startDate = QDate::fromString(rule.parameters.value("startDate").toString(), "yyyy-MM-dd");
        const QDate endDate = QDate::fromString(rule.parameters.value("endDate").toString(), "yyyy-MM-dd");
        return tradeDate.isValid() && startDate.isValid() && endDate.isValid() && tradeDate >= startDate && tradeDate <= endDate;
    }
    case RULE_FORMAT_VALIDATION: {
        const QString dateFormat = rule.parameters.value("dateFormat", "auto").toString();
        const QString rawDate = resolveAliasedField(data, aliasedKeysForField("date"));
        if (rawDate.isEmpty()) {
            return false;
        }
        const QDate parsedDate = dateFormat == "auto" ? parseFlexibleDate(rawDate) : QDate::fromString(rawDate, dateFormat);
        if (!parsedDate.isValid()) {
            return false;
        }
        for (const QString& field : priceFields()) {
            double value = 0.0;
            if (hasAliasedField(data, aliasedKeysForField(field)) && !extractAliasedNumericValue(data, aliasedKeysForField(field), &value)) {
                return false;
            }
        }
        return true;
    }
    case RULE_COMPLETENESS_CHECK: {
        const QStringList requiredFields = toStringList(rule.parameters.value("requiredFields"));
        for (const QString& field : requiredFields) {
            if (!hasAliasedField(data, aliasedKeysForField(field))) {
                return false;
            }
        }
        return true;
    }
    case RULE_DUPLICATE_REMOVAL: {
        const QStringList keyFields = toStringList(rule.parameters.value("keyFields"));
        QString key;
        for (const QString& field : keyFields) {
            const QString resolvedValue = canonicalFieldKey(field) == "date"
                ? normalizeDateString(data)
                : resolveAliasedField(data, aliasedKeysForField(field));
            if (resolvedValue.isEmpty()) {
                continue;
            }
            key += resolvedValue + "|";
        }
        if (key.isEmpty() || context.seenKeys.contains(key)) {
            return false;
        }
        context.seenKeys.insert(key);
        return true;
    }
    case RULE_SURVIVOR_BIAS: {
        const QDate delistDate = resolveAliasedDate(data, {"delist_date", "delisted_date", "退市日期"});
        if (delistDate.isValid() && tradeDate > delistDate) {
            return false;
        }
        setRuleTag(data, "survivor_bias_checked", true);
        return true;
    }
    case RULE_REPORT_DATE_ALIGNMENT: {
        const bool hasReportDate = hasAliasedField(data, {"report_date", "report_period", "财报期"});
        if (!hasReportDate) {
            return true;
        }
        const QDate disclosureDate = resolveAliasedDate(data, {"disclosure_date", "announcement_date", "ann_date", "publish_date", "披露日期"});
        if (!disclosureDate.isValid()) {
            return false;
        }
        setCanonicalStringField(data, "date", disclosureDate.toString("yyyy-MM-dd"));
        setCanonicalStringField(data, "effective_disclosure_date", disclosureDate.toString("yyyy-MM-dd"));
        setRuleTag(data, "report_date_aligned", true);
        return true;
    }
    case RULE_SUSPENSION_FILL: {
        const bool suspended = isSuspendedRecord(data);
        if (suspended) {
            state.consecutiveSuspensions++;
            setRuleTag(data, "is_suspended", true);
            setRuleTag(data, "suspension_days", state.consecutiveSuspensions);
            const int maxForwardFillDays = rule.parameters.value("maxForwardFillDays", 10).toInt();
            if (state.consecutiveSuspensions > maxForwardFillDays && rule.parameters.value("dropAfterMaxDays", true).toBool()) {
                return false;
            }

            if (state.lastValidValues.isEmpty()) {
                return rule.parameters.value("keepWithoutHistory", false).toBool();
            }

            const QStringList fillFields = toStringList(rule.parameters.value("fillFields", priceFields()));
            for (const QString& field : fillFields) {
                const QString canonical = canonicalFieldKey(field);
                if (!state.lastValidValues.contains(canonical)) {
                    continue;
                }
                setCanonicalNumericField(data, canonical, state.lastValidValues.value(canonical).toDouble());
            }
            setRuleTag(data, "forward_filled", true);
            return true;
        }

        state.consecutiveSuspensions = 0;
        setRuleTag(data, "is_suspended", false);
        for (const QString& field : priceFields()) {
            double value = 0.0;
            if (extractAliasedNumericValue(data, aliasedKeysForField(field), &value) && value > 0.0) {
                state.lastValidValues[canonicalFieldKey(field)] = value;
            }
        }
        return true;
    }
    case RULE_MISSING_VALUE_FILL: {
        const QStringList fields = toStringList(rule.parameters.value("fields", defaultMissingFillFields()));
        bool anyFilled = false;
        for (const QString& field : fields) {
            if (!isMissingValue(data, field)) {
                double value = 0.0;
                if (extractAliasedNumericValue(data, aliasedKeysForField(field), &value)) {
                    state.lastValidValues[canonicalFieldKey(field)] = value;
                }
                continue;
            }

            const QString canonical = canonicalFieldKey(field);
            if (!state.lastValidValues.contains(canonical)) {
                continue;
            }
            setCanonicalNumericField(data, canonical, state.lastValidValues.value(canonical).toDouble());
            anyFilled = true;
        }
        if (anyFilled) {
            setRuleTag(data, "missing_value_filled", true);
        }
        return true;
    }
    case RULE_ADJUSTED_PRICE: {
        bool adjustedApplied = false;
        double adjFactor = 0.0;
        const bool hasAdjFactor = extractAliasedNumericValue(data, {"adj_factor", "hfq_factor", "post_adjust_factor"}, &adjFactor) && adjFactor > 0.0;
        for (const QString& field : priceFields()) {
            double adjustedValue = 0.0;
            const QStringList adjustedAliases = {
                QString("hfq_%1").arg(field),
                QString("%1_hfq").arg(field),
                QString("adj_%1").arg(field),
                QString("post_adj_%1").arg(field)
            };
            if (extractAliasedNumericValue(data, adjustedAliases, &adjustedValue)) {
                setCanonicalNumericField(data, field, adjustedValue);
                adjustedApplied = true;
                continue;
            }

            double rawValue = 0.0;
            if (hasAdjFactor && extractAliasedNumericValue(data, aliasedKeysForField(field), &rawValue)) {
                setCanonicalNumericField(data, field, rawValue * adjFactor);
                adjustedApplied = true;
            }
        }
        if (adjustedApplied) {
            setRuleTag(data, "adjusted_price_applied", true);
        }
        return true;
    }
    case RULE_NEW_STOCK_FILTER: {
        const int minTradeDays = rule.parameters.value("minTradeDays", 60).toInt();
        double listedDays = 0.0;
        if (extractAliasedNumericValue(data, {"listed_days", "trading_days_since_listing"}, &listedDays)) {
            return listedDays >= static_cast<double>(minTradeDays);
        }

        const QDate listDate = resolveAliasedDate(data, {"list_date", "ipo_date", "listing_date", "上市日期"});
        if (listDate.isValid() && tradeDate < listDate) {
            return false;
        }
        state.tradeDaysSeen++;
        return state.tradeDaysSeen > minTradeDays;
    }
    case RULE_ST_FILTER: {
        bool isSt = false;
        if (extractAliasedBoolValue(data, {"is_st", "st", "risk_warning"}, &isSt) && isSt) {
            return false;
        }
        const QString name = resolveAliasedField(data, {"name", "stock_name", "sec_name", "名称"}).toUpper();
        if (name.startsWith("ST") || name.startsWith("*ST")) {
            return false;
        }
        const QString status = resolveAliasedField(data, {"status", "stock_status"}).toUpper();
        return status != "ST" && status != "*ST";
    }
    case RULE_PRICE_FILTER: {
        const double minPrice = rule.parameters.value("minPrice", 0.01).toDouble();
        const double maxPrice = rule.parameters.value("maxPrice", 10000.0).toDouble();
        const bool allowZeroWhenSuspended = rule.parameters.value("allowZeroWhenSuspended", true).toBool() && data.value("is_suspended").toBool();
        const bool requirePositiveTurnoverWhenTraded = rule.parameters.value("requirePositiveTurnoverWhenTraded", true).toBool();
        const bool requireConsistentDerivedFields = rule.parameters.value("requireConsistentDerivedFields", true).toBool();

        double open = 0.0;
        double high = 0.0;
        double low = 0.0;
        double close = 0.0;
        if (!extractAliasedNumericValue(data, aliasedKeysForField("open"), &open)
            || !extractAliasedNumericValue(data, aliasedKeysForField("high"), &high)
            || !extractAliasedNumericValue(data, aliasedKeysForField("low"), &low)
            || !extractAliasedNumericValue(data, aliasedKeysForField("close"), &close)) {
            return false;
        }

        for (double value : {open, high, low, close}) {
            if (!std::isfinite(value) || value < minPrice || value > maxPrice) {
                return false;
            }
        }

        if (rule.parameters.value("enforceChain", true).toBool()) {
            const double maxPriceInBar = std::max({open, high, low, close});
            const double minPriceInBar = std::min({open, high, low, close});
            if (std::abs(high - maxPriceInBar) > 1e-9 || std::abs(low - minPriceInBar) > 1e-9) {
                return false;
            }
        }

        double volume = 0.0;
        if (extractAliasedNumericValue(data, aliasedKeysForField("volume"), &volume) && volume <= 0.0 && !allowZeroWhenSuspended) {
            return false;
        }

        double preClose = 0.0;
        const bool hasPreClose = extractAliasedNumericValue(data, aliasedKeysForField("pre_close"), &preClose);
        if (hasPreClose && preClose <= 0.0) {
            return false;
        }

        double turnoverAmount = 0.0;
        const bool hasTurnoverAmount = extractAliasedNumericValue(data, aliasedKeysForField("turnover_amount"), &turnoverAmount);
        if (hasTurnoverAmount && turnoverAmount < 0.0) {
            return false;
        }
        if (requirePositiveTurnoverWhenTraded && close > 0.0 && !allowZeroWhenSuspended) {
            if (hasTurnoverAmount && turnoverAmount <= 0.0) {
                return false;
            }
            if (extractAliasedNumericValue(data, aliasedKeysForField("volume"), &volume) && volume <= 0.0) {
                return false;
            }
        }

        if (requireConsistentDerivedFields && hasPreClose && preClose > 0.0) {
            const double closeDiff = close - preClose;
            double changeAmt = 0.0;
            if (std::abs(closeDiff) > 1e-8
                && extractAliasedNumericValue(data, aliasedKeysForField("change_amt"), &changeAmt)
                && std::abs(changeAmt) <= 1e-8) {
                return false;
            }

            double changePct = 0.0;
            if (std::abs(closeDiff) > 1e-8
                && extractAliasedNumericValue(data, aliasedKeysForField("change_pct"), &changePct)
                && std::abs(normalizePercentValue(changePct)) <= 1e-4) {
                return false;
            }

            double amplitude = 0.0;
            if (std::abs(high - low) > 1e-8
                && extractAliasedNumericValue(data, aliasedKeysForField("amplitude"), &amplitude)
                && std::abs(normalizePercentValue(amplitude)) <= 1e-4) {
                return false;
            }

            double turnoverRate = 0.0;
            if (extractAliasedNumericValue(data, aliasedKeysForField("turnover_rate"), &turnoverRate)) {
                if (turnoverRate < 0.0) {
                    return false;
                }
                if (hasTurnoverAmount && turnoverAmount > 0.0 && std::abs(normalizePercentValue(turnoverRate)) <= 1e-4) {
                    return false;
                }
            }
        }
        return true;
    }
    case RULE_VOLUME_FILTER: {
        double volume = 0.0;
        if (!extractAliasedNumericValue(data, aliasedKeysForField("volume"), &volume)) {
            return false;
        }
        const bool allowZeroWhenSuspended = rule.parameters.value("allowZeroWhenSuspended", true).toBool() && data.value("is_suspended").toBool();
        if (volume <= 0.0 && allowZeroWhenSuspended) {
            return true;
        }
        const double minVolume = rule.parameters.value("minVolume", 0).toDouble();
        const double maxVolume = rule.parameters.value("maxVolume", 1000000000).toDouble();
        return volume >= minVolume && volume <= maxVolume;
    }
    case RULE_LIMIT_MOVE_TAG: {
        double changePct = 0.0;
        if (!extractAliasedNumericValue(data, aliasedKeysForField("change_pct"), &changePct)) {
            double prevClose = 0.0;
            double close = 0.0;
            if (extractAliasedNumericValue(data, {"prev_close", "pre_close"}, &prevClose)
                && extractAliasedNumericValue(data, aliasedKeysForField("close"), &close)
                && prevClose > 0.0) {
                changePct = (close - prevClose) * 100.0 / prevClose;
            }
        }
        changePct = normalizePercentValue(changePct);
        const double upThreshold = rule.parameters.value("upThreshold", 9.5).toDouble();
        const double downThreshold = rule.parameters.value("downThreshold", -9.5).toDouble();
        setRuleTag(data, "limit_up", changePct >= upThreshold);
        setRuleTag(data, "limit_down", changePct <= downThreshold);
        setRuleTag(data, "can_buy", changePct < upThreshold);
        setRuleTag(data, "can_sell", changePct > downThreshold);
        return true;
    }
    case RULE_CONTINUOUS_SUSPENSION_FILTER: {
        const int maxSuspensionDays = rule.parameters.value("maxSuspensionDays", 10).toInt();
        const int suspensionDays = data.value("suspension_days", state.consecutiveSuspensions).toInt();
        return suspensionDays <= maxSuspensionDays;
    }
    case RULE_INDEX_MEMBERSHIP_ALIGNMENT: {
        const int lagDays = rule.parameters.value("lagDays", 1).toInt();
        const QDate indexInDate = resolveAliasedDate(data, {"index_in_date", "constituent_in_date", "effective_date", "adjustment_date"});
        const QDate indexOutDate = resolveAliasedDate(data, {"index_out_date", "constituent_out_date"});
        if (indexInDate.isValid() && tradeDate < indexInDate.addDays(lagDays)) {
            return false;
        }
        if (indexOutDate.isValid() && tradeDate >= indexOutDate.addDays(lagDays)) {
            return false;
        }
        return true;
    }
    case RULE_OUTLIER_DETECTION: {
        double open = 0.0;
        double close = 0.0;
        if (!extractAliasedNumericValue(data, aliasedKeysForField("open"), &open)
            || !extractAliasedNumericValue(data, aliasedKeysForField("close"), &close)
            || open <= 0.0) {
            return true;
        }
        const double threshold = rule.parameters.value("threshold", 0.3).toDouble();
        const double intradayMove = std::abs((close - open) / open);
        return intradayMove <= threshold;
    }
    case RULE_CUSTOM_FILTER:
        return true;
    case RULE_WINSORIZATION:
    case RULE_MARKET_CAP_FILTER:
    case RULE_NEUTRALIZATION:
    case RULE_STANDARDIZATION:
        return true;
    default:
        return true;
    }
}

bool DataCleaningEngine::executeCrossSectionalRule(const CleaningRule& rule, QVariantList& records, RuntimeContext& context)
{
    (void)context;
    if (records.isEmpty()) {
        return true;
    }

    switch (rule.type) {
    case RULE_MARKET_CAP_FILTER: {
        const double lowerTail = rule.parameters.value("lowerTail", 0.05).toDouble();
        std::vector<double> caps;
        caps.reserve(static_cast<size_t>(records.size()));
        for (const QVariant& item : records) {
            double marketCap = 0.0;
            const QVariantMap record = item.toMap();
            if (extractAnyNumericValue(record, {"market_cap", "circulating_market_cap"}, &marketCap) && marketCap > 0.0) {
                caps.push_back(marketCap);
            }
        }
        if (caps.size() < 3) {
            return true;
        }
        const double cutoff = quantile(caps, lowerTail);
        QVariantList filtered;
        filtered.reserve(records.size());
        for (const QVariant& item : records) {
            QVariantMap record = item.toMap();
            double marketCap = 0.0;
            if (!extractAnyNumericValue(record, {"market_cap", "circulating_market_cap"}, &marketCap) || marketCap > cutoff) {
                filtered.append(record);
            }
        }
        records = filtered;
        return true;
    }
    case RULE_WINSORIZATION: {
        const QStringList fields = toStringList(rule.parameters.value("fields", defaultFactorFields()));
        const double lowerQuantile = rule.parameters.value("lowerQuantile", 0.01).toDouble();
        const double upperQuantile = rule.parameters.value("upperQuantile", 0.99).toDouble();
        for (const QString& field : fields) {
            std::vector<double> values;
            QVector<int> indices;
            for (int i = 0; i < records.size(); ++i) {
                double value = 0.0;
                if (extractAliasedNumericValue(records[i].toMap(), aliasedKeysForField(field), &value)) {
                    values.push_back(value);
                    indices.append(i);
                }
            }
            if (values.size() < 3) {
                continue;
            }
            const double lower = quantile(values, lowerQuantile);
            const double upper = quantile(values, upperQuantile);
            for (int i = 0; i < indices.size(); ++i) {
                QVariantMap record = records[indices[i]].toMap();
                double value = 0.0;
                if (!extractAliasedNumericValue(record, aliasedKeysForField(field), &value)) {
                    continue;
                }
                setCanonicalNumericField(record, field, std::clamp(value, lower, upper));
                setRuleTag(record, "winsorized", true);
                records[indices[i]] = record;
            }
        }
        return true;
    }
    case RULE_STANDARDIZATION: {
        const QStringList fields = toStringList(rule.parameters.value("fields", defaultFactorFields()));
        for (const QString& field : fields) {
            std::vector<double> values;
            QVector<int> indices;
            for (int i = 0; i < records.size(); ++i) {
                double value = 0.0;
                if (extractAliasedNumericValue(records[i].toMap(), aliasedKeysForField(field), &value)) {
                    values.push_back(value);
                    indices.append(i);
                }
            }
            if (values.size() < 2) {
                continue;
            }
            const double mean = calculateMean(values);
            const double stddev = calculateStdDev(values, mean);
            if (stddev <= 0.0) {
                continue;
            }
            for (int i = 0; i < indices.size(); ++i) {
                QVariantMap record = records[indices[i]].toMap();
                const double normalized = (values[static_cast<size_t>(i)] - mean) / stddev;
                setCanonicalNumericField(record, field, normalized);
                setRuleTag(record, "standardized", true);
                records[indices[i]] = record;
            }
        }
        return true;
    }
    case RULE_NEUTRALIZATION: {
        const QStringList fields = toStringList(rule.parameters.value("fields", defaultFactorFields()));
        for (const QString& field : fields) {
            struct Sample {
                int index;
                double value;
                double marketCap;
                QString industry;
            };
            QVector<Sample> samples;
            for (int i = 0; i < records.size(); ++i) {
                const QVariantMap record = records[i].toMap();
                double value = 0.0;
                if (!extractAliasedNumericValue(record, aliasedKeysForField(field), &value)) {
                    continue;
                }
                double marketCap = 0.0;
                extractAnyNumericValue(record, {"market_cap", "circulating_market_cap"}, &marketCap);
                samples.append({i, value, marketCap, resolveAliasedField(record, aliasedKeysForField("industry"))});
            }
            if (samples.size() < 3) {
                continue;
            }

            QHash<QString, QVector<double>> industryBuckets;
            for (const Sample& sample : samples) {
                if (!sample.industry.isEmpty()) {
                    industryBuckets[sample.industry].append(sample.value);
                }
            }

            std::vector<double> residuals;
            residuals.reserve(static_cast<size_t>(samples.size()));
            std::vector<double> logCaps;
            logCaps.reserve(static_cast<size_t>(samples.size()));
            for (const Sample& sample : samples) {
                double residual = sample.value;
                if (!sample.industry.isEmpty() && industryBuckets.contains(sample.industry)) {
                    const QVector<double>& bucket = industryBuckets[sample.industry];
                    const double mean = std::accumulate(bucket.begin(), bucket.end(), 0.0) / static_cast<double>(bucket.size());
                    residual -= mean;
                }
                residuals.push_back(residual);
                logCaps.push_back(std::log(std::max(sample.marketCap, 1.0)));
            }

            const double xMean = calculateMean(logCaps);
            const double yMean = calculateMean(residuals);
            double numerator = 0.0;
            double denominator = 0.0;
            for (size_t i = 0; i < residuals.size(); ++i) {
                numerator += (logCaps[i] - xMean) * (residuals[i] - yMean);
                denominator += (logCaps[i] - xMean) * (logCaps[i] - xMean);
            }
            const double slope = denominator > 0.0 ? numerator / denominator : 0.0;

            for (int i = 0; i < samples.size(); ++i) {
                QVariantMap record = records[samples[i].index].toMap();
                const double neutralized = residuals[static_cast<size_t>(i)] - slope * (logCaps[static_cast<size_t>(i)] - xMean);
                setCanonicalNumericField(record, field, neutralized);
                setRuleTag(record, "neutralized", true);
                records[samples[i].index] = record;
            }
        }
        return true;
    }
    default:
        return true;
    }
}

void DataCleaningEngine::updateCleaningStats(const CleaningRule& rule, int totalEvaluated, int passedCount)
{
    QMutexLocker locker(&m_mutex);

    QVariantMap ruleStat = m_lastStats.ruleStats.value(rule.name).toMap();
    ruleStat["total"] = ruleStat.value("total").toInt() + totalEvaluated;
    ruleStat["passed"] = ruleStat.value("passed").toInt() + passedCount;
    ruleStat["failed"] = ruleStat.value("failed").toInt() + (totalEvaluated - passedCount);
    ruleStat["level"] = static_cast<int>(rule.level);
    ruleStat["mode"] = static_cast<int>(rule.mode);
    m_lastStats.ruleStats[rule.name] = ruleStat;
}

void DataCleaningEngine::resetCleaningStats()
{
    QMutexLocker locker(&m_mutex);
    m_lastStats = CleaningStats();
}