#include "DataCleaningEngine.h"
#include "DataCleaningPersistence.h"
#include "DataFetchFieldContractUtils.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QUuid>
#include <QtConcurrent>

template <typename Tag = void>
class CleaningFieldSchemaAdapter final
{
public:
    static QString canonicalFieldKey(const QString& field)
    {
        const QString normalized = field.trimmed().toLower();
        if (normalized.isEmpty()) {
            return {};
        }

        if (normalized == QStringLiteral("date")
            || normalized == QStringLiteral("trade_date")
            || normalized == QStringLiteral("tradeDate")
            || normalized == QStringLiteral("date_str")) {
            return QString(factor::bridge::CommonFields::TRADE_DATE);
        }

        if (normalized == QStringLiteral("symbol")
            || normalized == QStringLiteral("code")
            || normalized == QStringLiteral("stock_code")) {
            return QString(factor::bridge::CommonFields::SYMBOL);
        }

        if (normalized == QStringLiteral("industry")
            || normalized == QStringLiteral("industry_code")) {
            return QString(factor::bridge::SymbolInfoFieldKeys::INDUSTRY_CODE);
        }

        if (normalized == QStringLiteral("amount")
            || normalized == QStringLiteral("turnover_amount")) {
            return QString(factor::bridge::MarketBarFieldKeys::TURNOVER);
        }

        if (normalized == QStringLiteral("pre_adjust_factor") || normalized == QStringLiteral("pre_adj_factor")) {
            return QString(factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR);
        }
        if (normalized == QStringLiteral("post_adjust_factor") || normalized == QStringLiteral("post_adj_factor") || normalized == QStringLiteral("hfq_factor") || normalized == QStringLiteral("adjust_factor")) {
            return QString(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR);
        }

        if (normalized == QStringLiteral("is_suspended")
            || normalized == QStringLiteral("limit_up")
            || normalized == QStringLiteral("limit_down")
            || normalized == QStringLiteral("suspension_days")
            || normalized == QStringLiteral("valuation_sanitized")
            || normalized == QStringLiteral("survivor_bias_checked")
            || normalized == QStringLiteral("forward_filled")
            || normalized == QStringLiteral("missing_value_filled")
            || normalized == QStringLiteral("backward_filled")
            || normalized == QStringLiteral("interpolated")
            || normalized == QStringLiteral("winsorized")
            || normalized == QStringLiteral("standardized")
            || normalized == QStringLiteral("neutralized")
            || normalized == QStringLiteral("industry_neutralized")
            || normalized == QStringLiteral("market_neutralized")
            || normalized == QStringLiteral("cleaning_tags")
            || normalized == QStringLiteral("valuation_invalid_fields")
            || normalized == QStringLiteral("data_quality_score")
            || normalized == QStringLiteral("processing_timestamp")
            || normalized == QStringLiteral("data_source")
            || normalized == QStringLiteral("data_type")
            || normalized == QStringLiteral("time_stamp")) {
            return normalized;
        }

        if (normalized == QStringLiteral("open")
            || normalized == QStringLiteral("high")
            || normalized == QStringLiteral("low")
            || normalized == QStringLiteral("close")
            || normalized == QStringLiteral("pre_close")
            || normalized == QStringLiteral("volume")
            || normalized == QStringLiteral("turnover")
            || normalized == QStringLiteral("turnover_rate")
            || normalized == QStringLiteral("change_amt")
            || normalized == QStringLiteral("change_pct")
            || normalized == QStringLiteral("amplitude")
            || normalized == QStringLiteral("market_cap")
            || normalized == QStringLiteral("circulating_market_cap")
            || normalized == QStringLiteral("pe_ratio")
            || normalized == QStringLiteral("pb_ratio")
            || normalized == QStringLiteral("dividend_yield")) {
            return normalized;
        }

        if (normalized == QStringLiteral("report_date")
            || normalized == QStringLiteral("disclosure_date")
            || normalized == QStringLiteral("eps")
            || normalized == QStringLiteral("bps")
            || normalized == QStringLiteral("roe")
            || normalized == QStringLiteral("roa")
            || normalized == QStringLiteral("profit_margin")
            || normalized == QStringLiteral("gross_margin")
            || normalized == QStringLiteral("operating_margin")
            || normalized == QStringLiteral("net_profit")
            || normalized == QStringLiteral("total_revenue")
            || normalized == QStringLiteral("total_assets")
            || normalized == QStringLiteral("total_liabilities")
            || normalized == QStringLiteral("equity")
            || normalized == QStringLiteral("debt_to_equity")
            || normalized == QStringLiteral("current_ratio")
            || normalized == QStringLiteral("quick_ratio")
            || normalized == QStringLiteral("operating_cash_flow")
            || normalized == QStringLiteral("investing_cash_flow")
            || normalized == QStringLiteral("financing_cash_flow")
            || normalized == QStringLiteral("payout_ratio")
            || normalized == QStringLiteral("dividend_stability")) {
            return normalized;
        }

        return normalized;
    }

    static QStringList aliasedKeysForField(const QString& field)
    {
        const QString canonical = canonicalFieldKey(field);
        if (canonical == QString(factor::bridge::CommonFields::TRADE_DATE)) {
            return {QStringLiteral("trade_date"), QStringLiteral("date"), QStringLiteral("tradeDate"), QStringLiteral("date_str"), QStringLiteral("dateStr")};
        }
        if (canonical == QString(factor::bridge::CommonFields::SYMBOL)) {
            return {QStringLiteral("symbol"), QStringLiteral("code"), QStringLiteral("stock_code")};
        }
        if (canonical == QString(factor::bridge::SymbolInfoFieldKeys::INDUSTRY_CODE)) {
            return {QStringLiteral("industry_code"), QStringLiteral("industry"), QStringLiteral("sw_industry_1"), QStringLiteral("sw_industry_2"), QStringLiteral("citics_industry_1"), QStringLiteral("gics_sector")};
        }
        if (canonical == QString(factor::bridge::MarketBarFieldKeys::TURNOVER)) {
            return {QStringLiteral("turnover"), QStringLiteral("amount"), QStringLiteral("turnover_amount")};
        }
        if (canonical == QString(factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR)) {
            return {QStringLiteral("pre_adjust_factor"), QStringLiteral("pre_adj_factor")};
        }
        if (canonical == QString(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR)) {
            return {QStringLiteral("post_adjust_factor"), QStringLiteral("post_adj_factor"), QStringLiteral("hfq_factor"), QStringLiteral("adjust_factor"), QStringLiteral("adj_factor")};
        }
        if (canonical == QStringLiteral("data_source")
            || canonical == QStringLiteral("data_type")
            || canonical == QStringLiteral("time_stamp")) {
            return {canonical};
        }

        return {canonical};
    }

    static QStringList tradeDateAliases() { return aliasedKeysForField(QStringLiteral("trade_date")); }
    static QStringList symbolAliases() { return aliasedKeysForField(QStringLiteral("symbol")); }
    static QStringList openAliases() { return aliasedKeysForField(QStringLiteral("open")); }
    static QStringList highAliases() { return aliasedKeysForField(QStringLiteral("high")); }
    static QStringList lowAliases() { return aliasedKeysForField(QStringLiteral("low")); }
    static QStringList closeAliases() { return aliasedKeysForField(QStringLiteral("close")); }
    static QStringList volumeAliases() { return aliasedKeysForField(QStringLiteral("volume")); }
    static QStringList preCloseAliases() { return aliasedKeysForField(QStringLiteral("pre_close")); }
    static QStringList turnoverAliases() { return aliasedKeysForField(QStringLiteral("turnover")); }
    static QStringList changeAmtAliases() { return aliasedKeysForField(QStringLiteral("change_amt")); }
    static QStringList changePctAliases() { return aliasedKeysForField(QStringLiteral("change_pct")); }
    static QStringList amplitudeAliases() { return aliasedKeysForField(QStringLiteral("amplitude")); }
    static QStringList turnoverRateAliases() { return aliasedKeysForField(QStringLiteral("turnover_rate")); }
    static QStringList marketCapAliases() { return aliasedKeysForField(QStringLiteral("market_cap")); }
    static QStringList circulatingMarketCapAliases() { return aliasedKeysForField(QStringLiteral("circulating_market_cap")); }
    static QStringList industryAliases() { return aliasedKeysForField(QStringLiteral("industry_code")); }

    static QStringList coreKeyFields()
    {
        return {
            QString(factor::bridge::CommonFields::SYMBOL),
            QString(factor::bridge::CommonFields::TRADE_DATE),
            QString(factor::bridge::MarketBarFieldKeys::OPEN),
            QString(factor::bridge::MarketBarFieldKeys::HIGH),
            QString(factor::bridge::MarketBarFieldKeys::LOW),
            QString(factor::bridge::MarketBarFieldKeys::CLOSE),
            QString(factor::bridge::MarketBarFieldKeys::PRE_CLOSE),
            QString(factor::bridge::MarketBarFieldKeys::VOLUME),
            QString(factor::bridge::MarketBarFieldKeys::TURNOVER),
            QString(factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR),
            QString(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR),
            QString(factor::bridge::SymbolInfoFieldKeys::INDUSTRY_CODE)
        };
    }

    static QStringList priceFields()
    {
        return {
            QString(factor::bridge::MarketBarFieldKeys::OPEN),
            QString(factor::bridge::MarketBarFieldKeys::HIGH),
            QString(factor::bridge::MarketBarFieldKeys::LOW),
            QString(factor::bridge::MarketBarFieldKeys::CLOSE),
            QString(factor::bridge::MarketBarFieldKeys::PRE_CLOSE),
            QString(factor::bridge::MarketBarFieldKeys::VOLUME),
            QString(factor::bridge::MarketBarFieldKeys::TURNOVER),
            QString(factor::bridge::MarketBarFieldKeys::TURNOVER_RATE),
            QString(factor::bridge::MarketBarFieldKeys::CHANGE_AMT),
            QString(factor::bridge::MarketBarFieldKeys::CHANGE_PCT),
            QString(factor::bridge::MarketBarFieldKeys::AMPLITUDE),
            QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP),
            QString(factor::bridge::MarketBarFieldKeys::CIRCULATING_MARKET_CAP),
            QString(factor::bridge::MarketBarFieldKeys::PE_RATIO),
            QString(factor::bridge::MarketBarFieldKeys::PB_RATIO),
            QString(factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR),
            QString(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR)
        };
    }

    static QStringList financialFields()
    {
        return factor::bridge::FinancialFieldKeys::cleaningDefaults().orderedValues();
    }

    static QStringList defaultMissingValueFields()
    {
        return priceFields();
    }

    static QStringList defaultFactorFields()
    {
        return {QStringLiteral("factor_value"), QStringLiteral("factor"), QStringLiteral("value"), QStringLiteral("score")};
    }
};

using DefaultCleaningFieldSchema = CleaningFieldSchemaAdapter<>;
namespace {

bool hasAliasedField(const QVariantMap& data, const QStringList& keys);
bool extractAliasedNumericValue(const QVariantMap& data, const QStringList& keys, double* outValue);
bool extractAliasedBoolValue(const QVariantMap& data, const QStringList& keys, bool* outValue);
QString resolveAliasedField(const QVariantMap& data, const QStringList& keys);
QStringList toStringList(const QVariant& value);
QDate parseFlexibleDate(const QString& text);
QString canonicalFieldKey(const QString& field);
QStringList aliasedKeysForField(const QString& field);
const QStringList& tradeDateAliases();
const QStringList& symbolAliases();
const QStringList& openAliases();
const QStringList& highAliases();
const QStringList& lowAliases();
const QStringList& closeAliases();
const QStringList& volumeAliases();
const QStringList& preCloseAliases();
const QStringList& turnoverAliases();
const QStringList& changeAmtAliases();
const QStringList& changePctAliases();
const QStringList& amplitudeAliases();
const QStringList& turnoverRateAliases();
const QStringList& marketCapAliases();
const QStringList& circulatingMarketCapAliases();
const QStringList& industryAliases();

bool hasAliasedField(const QVariantMap& data, const QStringList& keys)
{
    for (const QString& key : keys) {
        if (data.contains(key)) {
            return true;
        }
    }
    return false;
}

bool extractAliasedNumericValue(const QVariantMap& data, const QStringList& keys, double* outValue)
{
    for (const QString& key : keys) {
        const auto it = data.find(key);
        if (it == data.end()) {
            continue;
        }

        bool ok = false;
        const double value = it.value().toDouble(&ok);
        if (!ok) {
            continue;
        }

        if (outValue != nullptr) {
            *outValue = value;
        }
        return true;
    }
    return false;
}

bool extractAliasedBoolValue(const QVariantMap& data, const QStringList& keys, bool* outValue)
{
    for (const QString& key : keys) {
        const auto it = data.find(key);
        if (it == data.end()) {
            continue;
        }

        const QString text = it.value().toString().trimmed().toLower();
        if (text == QStringLiteral("1") || text == QStringLiteral("true") || text == QStringLiteral("yes") || text == QStringLiteral("y")) {
            if (outValue != nullptr) {
                *outValue = true;
            }
            return true;
        }
        if (text == QStringLiteral("0") || text == QStringLiteral("false") || text == QStringLiteral("no") || text == QStringLiteral("n")) {
            if (outValue != nullptr) {
                *outValue = false;
            }
            return true;
        }
    }

    return false;
}

QString resolveAliasedField(const QVariantMap& data, const QStringList& keys)
{
    for (const QString& key : keys) {
        const auto it = data.find(key);
        if (it != data.end()) {
            return it.value().toString();
        }
    }
    return {};
}

QStringList toStringList(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }

    if (value.canConvert<QStringList>()) {
        return value.toStringList();
    }

    if (value.canConvert<QVariantList>()) {
        QStringList result;
        const QVariantList list = value.toList();
        result.reserve(list.size());
        for (const QVariant& item : list) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty()) {
                result.append(text);
            }
        }
        return result;
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return {};
    }
    if (text.contains(',')) {
        return text.split(',', Qt::SkipEmptyParts);
    }
    return {text};
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
    return DefaultCleaningFieldSchema::canonicalFieldKey(field);
}

QStringList aliasedKeysForField(const QString& field)
{
    return DefaultCleaningFieldSchema::aliasedKeysForField(field);
}

const QStringList& tradeDateAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::tradeDateAliases();
    return aliases;
}

const QStringList& symbolAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::symbolAliases();
    return aliases;
}

const QStringList& openAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::openAliases();
    return aliases;
}

const QStringList& highAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::highAliases();
    return aliases;
}

const QStringList& lowAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::lowAliases();
    return aliases;
}

const QStringList& closeAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::closeAliases();
    return aliases;
}

const QStringList& volumeAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::volumeAliases();
    return aliases;
}

const QStringList& preCloseAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::preCloseAliases();
    return aliases;
}

const QStringList& turnoverAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::turnoverAliases();
    return aliases;
}

const QStringList& changeAmtAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::changeAmtAliases();
    return aliases;
}

const QStringList& changePctAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::changePctAliases();
    return aliases;
}

const QStringList& amplitudeAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::amplitudeAliases();
    return aliases;
}

const QStringList& turnoverRateAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::turnoverRateAliases();
    return aliases;
}

const QStringList& marketCapAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::marketCapAliases();
    return aliases;
}

const QStringList& circulatingMarketCapAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::circulatingMarketCapAliases();
    return aliases;
}

const QStringList& industryAliases()
{
    static const QStringList aliases = DefaultCleaningFieldSchema::industryAliases();
    return aliases;
}

QDate resolveTradeDate(const QVariantMap& data)
{
    return parseFlexibleDate(resolveAliasedField(data, tradeDateAliases()));
}

QDate resolveAliasedDate(const QVariantMap& data, const QStringList& keys)
{
    return parseFlexibleDate(resolveAliasedField(data, keys));
}

QString resolveCurrentStockLabel(const QVariantMap& data)
{
    const QString symbol = resolveAliasedField(data, symbolAliases());
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

void removeNonCanonicalAliases(QVariantMap& data, const QString& field)
{
    const QString canonical = canonicalFieldKey(field);
    for (const QString& alias : aliasedKeysForField(canonical)) {
        if (canonicalFieldKey(alias) == canonical && alias != canonical) {
            data.remove(alias);
        }
    }
}

void normalizeCoreCanonicalFields(QVariantMap& data)
{
    const QString normalizedTradeDate = normalizeDateString(data);
    if (!normalizedTradeDate.isEmpty()) {
        setCanonicalStringField(data, QString(factor::bridge::CommonFields::TRADE_DATE), normalizedTradeDate);
    }
    removeNonCanonicalAliases(data, QString(factor::bridge::CommonFields::TRADE_DATE));

    const QString symbol = resolveAliasedField(data, aliasedKeysForField(QString(factor::bridge::CommonFields::SYMBOL))).trimmed();
    if (!symbol.isEmpty()) {
        setCanonicalStringField(data, QString(factor::bridge::CommonFields::SYMBOL), symbol);
    }
    removeNonCanonicalAliases(data, QString(factor::bridge::CommonFields::SYMBOL));

    const QString industryCode = resolveAliasedField(data, aliasedKeysForField(QString(factor::bridge::SymbolInfoFieldKeys::INDUSTRY_CODE))).trimmed();
    if (!industryCode.isEmpty()) {
        setCanonicalStringField(data, QString(factor::bridge::SymbolInfoFieldKeys::INDUSTRY_CODE), industryCode);
    }
    removeNonCanonicalAliases(data, QString(factor::bridge::SymbolInfoFieldKeys::INDUSTRY_CODE));

    const QStringList numericFields = DefaultCleaningFieldSchema::priceFields();
    for (const QString& fieldName : numericFields) {
        double value = 0.0;
        if (extractAliasedNumericValue(data, aliasedKeysForField(fieldName), &value)) {
            setCanonicalNumericField(data, fieldName, value);
        }
        removeNonCanonicalAliases(data, fieldName);
    }
}

void removeNonContractOutputFields(QVariantMap& data)
{
    static const QStringList legacyFields = {
        QStringLiteral("adjusted_price_applied"),
        QStringLiteral("adj_factor"),  // 旧版单一复权因子，已拆分为pre_/post_adjust_factor
        QStringLiteral("hfq_factor"),
        QStringLiteral("adjust_factor"),
        QStringLiteral("turnover_amount"),
        QStringLiteral("amount"),
        QStringLiteral("date"),
        QStringLiteral("tradeDate"),
        QStringLiteral("industry")
    };

    for (const QString& field : legacyFields) {
        data.remove(field);
    }
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

    sanitizeField(QString(factor::bridge::MarketBarFieldKeys::PE_RATIO), false);
    sanitizeField(QString(factor::bridge::MarketBarFieldKeys::PB_RATIO), false);
    sanitizeField(QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP), true);
    sanitizeField(QString(factor::bridge::MarketBarFieldKeys::CIRCULATING_MARKET_CAP), true);

    double marketCap = 0.0;
    double circulatingMarketCap = 0.0;
    if (extractAliasedNumericValue(data, marketCapAliases(), &marketCap)
        && extractAliasedNumericValue(data, circulatingMarketCapAliases(), &circulatingMarketCap)
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

const QStringList& defaultMissingFillFields()
{
    static const QStringList fields = factor::bridge::MarketBarFieldKeys::missingFillDefaults().orderedValues();
    return fields;
}

const QStringList& defaultFactorFields()
{
    static const QStringList fields = {"factor_value", "factor", "value", "score"};
    return fields;
}

const QStringList& defaultFinancialFields()
{
    static const QStringList fields = factor::bridge::FinancialFieldKeys::cleaningDefaults().orderedValues();
    return fields;
}

const QStringList& priceFields()
{
    static const QStringList fields = factor::bridge::MarketBarFieldKeys::priceCore().orderedValues();
    return fields;
}

bool hasAnyAliasedField(const QVariantMap& data, const QStringList& fields);

QString normalizedRecordType(const QVariantMap& data)
{
    return resolveAliasedField(data, {"dataType", "data_type", "dataSourceType", "sourceType", "type"}).trimmed().toLower();
}

bool hasPriceStyleFields(const QVariantMap& data)
{
    static const QStringList auxiliaryFields = {
        QString(factor::bridge::MarketBarFieldKeys::PRE_CLOSE),
        QString(factor::bridge::MarketBarFieldKeys::VOLUME),
        QString(factor::bridge::MarketBarFieldKeys::TURNOVER),
        QString(factor::bridge::MarketBarFieldKeys::CHANGE_PCT),
        QString(factor::bridge::MarketBarFieldKeys::CHANGE_AMT),
        QString(factor::bridge::MarketBarFieldKeys::AMPLITUDE),
        QString(factor::bridge::MarketBarFieldKeys::TURNOVER_RATE),
        QString(factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR),
        QString(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR)
    };
    return hasAnyAliasedField(data, priceFields())
        || hasAnyAliasedField(data, auxiliaryFields);
}

bool isPriceOnlyField(const QString& field)
{
    static const QStringList priceOnlyFields = factor::bridge::MarketBarFieldKeys::priceOnly().orderedValues();
    return priceOnlyFields.contains(canonicalFieldKey(field));
}

bool hasAnyAliasedField(const QVariantMap& data, const QStringList& fields)
{
    for (const QString& field : fields) {
        if (hasAliasedField(data, aliasedKeysForField(field))) {
            return true;
        }
    }
    return false;
}

bool isFinancialRecord(const QVariantMap& data)
{
    if (hasAnyAliasedField(data, defaultFinancialFields())) {
        return true;
    }
    if (hasPriceStyleFields(data)) {
        return false;
    }
    static const QStringList reportFields = {QString(factor::bridge::FinancialFieldKeys::REPORT_DATE)};
    return hasAliasedField(data, reportFields);
}

QString recordKind(const QVariantMap& data)
{
    const QString dataType = normalizedRecordType(data);
    if (!dataType.isEmpty()) {
        return dataType;
    }

    if (isFinancialRecord(data)) {
        return QStringLiteral("financial");
    }

    if (hasPriceStyleFields(data)) {
        return QStringLiteral("kline");
    }

    return QStringLiteral("other");
}

bool hasAnyConfiguredField(const QVariantMap& data, const QStringList& fields)
{
    for (const QString& field : fields) {
        if (hasAliasedField(data, aliasedKeysForField(field))) {
            return true;
        }
    }
    return false;
}

bool isSecurityLikeRecord(const QVariantMap& data)
{
    const QString kind = recordKind(data);
    return kind == QStringLiteral("kline") || kind == QStringLiteral("financial");
}

bool shouldApplyRuleToRecord(const DataCleaningEngine::CleaningRule& rule, const QVariantMap& data)
{
    const bool financialRecord = isFinancialRecord(data);
    const bool priceRecord = hasPriceStyleFields(data);

    switch (rule.type) {
    case DataCleaningEngine::RULE_DUPLICATE_REMOVAL:
    case DataCleaningEngine::RULE_TIME_RANGE:
    case DataCleaningEngine::RULE_FORMAT_VALIDATION:
    case DataCleaningEngine::RULE_COMPLETENESS_CHECK:
    case DataCleaningEngine::RULE_CUSTOM_FILTER:
        return true;
    case DataCleaningEngine::RULE_REPORT_DATE_ALIGNMENT:
        return financialRecord || hasAliasedField(data, {QString(factor::bridge::FinancialFieldKeys::REPORT_DATE)});
    case DataCleaningEngine::RULE_SURVIVOR_BIAS:
    case DataCleaningEngine::RULE_NEW_STOCK_FILTER:
    case DataCleaningEngine::RULE_ST_FILTER:
        return isSecurityLikeRecord(data);
    case DataCleaningEngine::RULE_SUSPENSION_FILL:
    case DataCleaningEngine::RULE_ADJUSTED_PRICE:
    case DataCleaningEngine::RULE_PRICE_FILTER:
    case DataCleaningEngine::RULE_VOLUME_FILTER:
    case DataCleaningEngine::RULE_LIMIT_MOVE_TAG:
    case DataCleaningEngine::RULE_INDEX_MEMBERSHIP_ALIGNMENT:
    case DataCleaningEngine::RULE_CONTINUOUS_SUSPENSION_FILTER:
    case DataCleaningEngine::RULE_OUTLIER_DETECTION:
    case DataCleaningEngine::RULE_MARKET_CAP_FILTER:
        return priceRecord && !financialRecord;
    case DataCleaningEngine::RULE_MISSING_VALUE_FILL: {
        const QStringList fields = toStringList(rule.parameters.value("fields", defaultMissingFillFields()));
        return hasAnyConfiguredField(data, fields);
    }
    case DataCleaningEngine::RULE_WINSORIZATION:
    case DataCleaningEngine::RULE_STANDARDIZATION:
    case DataCleaningEngine::RULE_NEUTRALIZATION: {
        const QStringList fields = toStringList(rule.parameters.value("fields", defaultFactorFields()));
        return hasAnyConfiguredField(data, fields);
    }
    default:
        return true;
    }
}

QString recordDedupScope(const QVariantMap& data)
{
    const QString kind = recordKind(data);
    if (kind == QStringLiteral("financial")) {
        const QString reportType = resolveAliasedField(data, {"report_type"});
        if (!reportType.isEmpty()) {
            return QStringLiteral("financial:%1").arg(reportType);
        }

        return QStringLiteral("financial");
    }

    return kind;
}

QString recordCrossSectionalScope(const QVariantMap& data)
{
    const QString kind = recordKind(data);
    const QString date = normalizeDateString(data);
    if (date.isEmpty()) {
        return kind;
    }
    return kind + QStringLiteral("|") + date;
}

bool resolveRecordIdentity(const QVariantMap& data, QString* symbol, QDate* tradeDate)
{
    if (symbol) {
        *symbol = resolveAliasedField(data, symbolAliases());
        if (symbol->isEmpty()) {
            return false;
        }
    } else if (resolveAliasedField(data, symbolAliases()).isEmpty()) {
        return false;
    }

    QDate resolvedTradeDate = resolveTradeDate(data);
    if (!resolvedTradeDate.isValid() && isFinancialRecord(data)) {
        resolvedTradeDate = resolveAliasedDate(data, {QString(factor::bridge::FinancialFieldKeys::REPORT_DATE)});
    }
    if (tradeDate) {
        *tradeDate = resolvedTradeDate;
    }
    return resolvedTradeDate.isValid();
}

bool extractAnyNumericValue(const QVariantMap& data, const QStringList& fields, double* outValue, QString* outField = nullptr)
{
    for (const QString& field : fields) {
        double value = 0.0;
        if (!extractAliasedNumericValue(data, aliasedKeysForField(field), &value)) {
            continue;
        }

        if (outValue != nullptr) {
            *outValue = value;
        }
        if (outField != nullptr) {
            *outField = canonicalFieldKey(field);
        }
        return true;
    }
    return false;
}

bool isMissingValue(const QVariantMap& data, const QString& field)
{
    for (const QString& alias : aliasedKeysForField(field)) {
        const auto it = data.constFind(alias);
        if (it == data.constEnd()) {
            continue;
        }

        const QVariant value = it.value();
        if (!value.isValid() || value.isNull()) {
            return true;
        }
        if (value.type() == QVariant::String && value.toString().trimmed().isEmpty()) {
            return true;
        }
        if (value.canConvert<double>()) {
            bool ok = false;
            const double numericValue = value.toDouble(&ok);
            return !ok || !std::isfinite(numericValue);
        }
        return false;
    }
    return true;
}

bool isSuspendedRecord(const QVariantMap& data)
{
    bool suspended = false;
    if (extractAliasedBoolValue(data, {QStringLiteral("is_suspended"), QStringLiteral("suspended"), QStringLiteral("isSuspended")}, &suspended) && suspended) {
        return true;
    }

    const QString status = resolveAliasedField(data, {QStringLiteral("trade_status"), QStringLiteral("status"), QStringLiteral("trading_status")}).toUpper();
    if (status == QStringLiteral("SUSPENDED") || status == QStringLiteral("HALT") || status == QStringLiteral("停牌")) {
        return true;
    }

    double volume = 0.0;
    return extractAliasedNumericValue(data, volumeAliases(), &volume) && volume <= 0.0;
}

struct PreparedRecord {
    QVariantMap data;
    int originalIndex{0};
    QDate tradeDate;
    QString symbol;
    QString currentStock;
};

struct PreparedRecordCandidate {
    bool valid{false};
    PreparedRecord prepared;
};

PreparedRecordCandidate buildPreparedRecordCandidate(const QVariant& item, int index)
{
    PreparedRecordCandidate candidate;
    if (!item.canConvert<QVariantMap>()) {
        return candidate;
    }

    QVariantMap record = item.toMap();
    const QString currentStock = resolveCurrentStockLabel(record);
    QString symbol;
    QDate tradeDate;
    if (!resolveRecordIdentity(record, &symbol, &tradeDate)) {
        return candidate;
    }

    candidate.valid = true;
    candidate.prepared.data = std::move(record);
    candidate.prepared.originalIndex = index;
    candidate.prepared.tradeDate = tradeDate;
    candidate.prepared.symbol = symbol;
    candidate.prepared.currentStock = currentStock;
    return candidate;
}
}

DataCleaningEngine::DataCleaningEngine(QObject *parent)
    : QObject(parent)
    , m_rules(createDefaultRuleSet())
{
    qDebug() << "DataCleaningEngine initialized with" << m_rules.size() << "default rules";
}

DataCleaningEngine::~DataCleaningEngine() = default;

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
    QVariantMap localRuleStats;

    auto updateLocalRuleStats = [&localRuleStats](const DataCleaningEngine::CleaningRule& rule,
                                                  int totalEvaluated,
                                                  int passedCount) {
        QVariantMap ruleStat = localRuleStats.value(rule.name).toMap();
        ruleStat["total"] = ruleStat.value("total").toInt() + totalEvaluated;
        ruleStat["passed"] = ruleStat.value("passed").toInt() + passedCount;
        ruleStat["failed"] = ruleStat.value("failed").toInt() + (totalEvaluated - passedCount);
        ruleStat["level"] = static_cast<int>(rule.level);
        ruleStat["mode"] = static_cast<int>(rule.mode);
        localRuleStats[rule.name] = ruleStat;
    };

    auto emitProgressDetail = [this](int progress,
                                     const QString& message,
                                     const QString& currentStock,
                                     int keptRecords,
                                     int removedRecords) {
        emit cleaningProgress(progress, message);
        emit cleaningProgressDetail(progress, message, currentStock, keptRecords, removedRecords);
    };

    const QStringList& symbolKeyAliases = symbolAliases();

    emitProgressDetail(0,
                       QString("开始数据清洗，共%1条记录").arg(total),
                       QString(),
                       0,
                       0);

    QVector<PreparedRecord> preparedRecords;
    preparedRecords.reserve(total);
    QVector<PreparedRecordCandidate> preparedCandidates(total);

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

    const int idealThreadCount = QThread::idealThreadCount();
    const bool useParallelPreparation = total >= 1024 && idealThreadCount > 1;
    if (useParallelPreparation) {
        QVector<int> indices(total);
        std::iota(indices.begin(), indices.end(), 0);
        QtConcurrent::blockingMap(indices, [&data, &preparedCandidates](int index) {
            preparedCandidates[index] = buildPreparedRecordCandidate(data[index], index);
        });
    }

    for (int index = 0; index < data.size(); ++index) {
        PreparedRecordCandidate candidate;
        if (useParallelPreparation) {
            candidate = preparedCandidates[index];
        } else {
            candidate = buildPreparedRecordCandidate(data[index], index);
        }

        processedRecords++;
        if (candidate.valid) {
            preparedRecords.append(candidate.prepared);
            validProcessed++;
            updatePreparationProgress(candidate.prepared.currentStock);
            continue;
        }

        updatePreparationProgress(QString());
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

    DataCleaningEngineRuntimeContext context;
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
        normalizeCoreCanonicalFields(record);
        bool keep = true;
        for (const CleaningRule& rule : rowRules) {
            if (!shouldApplyRuleToRecord(rule, record)) {
                continue;
            }
            const bool passed = executeRule(rule, record, context);
            updateLocalRuleStats(rule, 1, passed ? 1 : 0);
            if (!passed) {
                keep = false;
                break;
            }
        }
        if (keep) {
            removeNonContractOutputFields(record);
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
                               prepared.currentStock,
                               keptCount,
                               removedCount);
        }
    }

    if (!crossSectionalRules.isEmpty() && !cleanedData.isEmpty()) {
        QMap<QString, QVariantList> recordsByScope;
        for (const QVariant& item : cleanedData) {
            const QVariantMap record = item.toMap();
            recordsByScope[recordCrossSectionalScope(record)].append(record);
        }

        int crossTotalSteps = 0;
        for (const CleaningRule& rule : crossSectionalRules) {
            Q_UNUSED(rule)
            crossTotalSteps += recordsByScope.size();
        }
        int crossProcessed = 0;
        int crossKeptCount = cleanedData.size();
        const int crossScopeCount = recordsByScope.size();
        const bool useParallelCrossSectional = crossScopeCount >= 2 && QThread::idealThreadCount() > 1;

        for (const CleaningRule& rule : crossSectionalRules) {
            QVector<QString> scopeKeys;
            scopeKeys.reserve(crossScopeCount);

            QVector<QVariantList> scopedRecords;
            scopedRecords.reserve(crossScopeCount);

            for (auto it = recordsByScope.cbegin(); it != recordsByScope.cend(); ++it) {
                scopeKeys.append(it.key());
                scopedRecords.append(it.value());
            }

            if (useParallelCrossSectional) {
                QVector<int> scopeIndices(scopeKeys.size());
                std::iota(scopeIndices.begin(), scopeIndices.end(), 0);
                QtConcurrent::blockingMap(scopeIndices, [this, &rule, &scopedRecords](int index) {
                    DataCleaningEngineRuntimeContext localContext;
                    this->executeCrossSectionalRule(rule, scopedRecords[index], localContext);
                });
            } else {
                for (int index = 0; index < scopedRecords.size(); ++index) {
                    executeCrossSectionalRule(rule, scopedRecords[index], context);
                }
            }

            for (int index = 0; index < scopeKeys.size(); ++index) {
                const QString& scopeKey = scopeKeys[index];
                const int before = recordsByScope.value(scopeKey).size();
                const int after = scopedRecords[index].size();
                recordsByScope[scopeKey] = scopedRecords[index];
                updateLocalRuleStats(rule, static_cast<int>(before), static_cast<int>((std::min)(before, after)));
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
                                   scopeKey,
                                   crossKeptCount,
                                   removedCount);
            }
        }

        cleanedData.clear();
        for (auto it = recordsByScope.begin(); it != recordsByScope.end(); ++it) {
            std::sort(it.value().begin(), it.value().end(), [&symbolKeyAliases](const QVariant& lhsVar, const QVariant& rhsVar) {
                const QVariantMap lhs = lhsVar.toMap();
                const QVariantMap rhs = rhsVar.toMap();
                const QString lhsSymbol = resolveAliasedField(lhs, symbolKeyAliases);
                const QString rhsSymbol = resolveAliasedField(rhs, symbolKeyAliases);
                return lhsSymbol < rhsSymbol;
            });
            for (const QVariant& item : it.value()) {
                QVariantMap record = item.toMap();
                removeNonContractOutputFields(record);
                cleanedData.append(record);
            }
        }
    }

    stats.cleanedRecords = cleanedData.size();
    stats.removedRecords = validProcessed - cleanedData.size();
    stats.endTime = QDateTime::currentDateTime();
    stats.durationMs = stats.startTime.msecsTo(stats.endTime);
    stats.ruleStats = localRuleStats;

    {
        QMutexLocker locker(&m_mutex);
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
    const QString symbolField = QString(factor::bridge::CommonFields::SYMBOL);
    const QString tradeDateField = QString(factor::bridge::CommonFields::TRADE_DATE);

    CleaningRule duplicateRemoval(RULE_DUPLICATE_REMOVAL, "duplicateRemoval", "删除重复 symbol/trade_date 记录");
    duplicateRemoval.name = "重复数据删除";
    duplicateRemoval.level = RULE_LEVEL_MANDATORY;
    duplicateRemoval.mode = RULE_MODE_SINGLE_POINT;
    duplicateRemoval.executionOrder = 10;
    duplicateRemoval.parameters["keyFields"] = QStringList{symbolField, tradeDateField};
    rules.append(duplicateRemoval);

    CleaningRule reportAlignment(RULE_REPORT_DATE_ALIGNMENT, "reportDateAlignment", "使用公布日作为生效日期");
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
    completeness.parameters["requiredFields"] = QStringList{symbolField, tradeDateField};
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

    CleaningRule standardization(RULE_STANDARDIZATION, "standardization", "对字段做Z-Score标准化");
    standardization.name = "标准化";
    standardization.level = RULE_LEVEL_RECOMMENDED;
    standardization.mode = RULE_MODE_CROSS_SECTIONAL;
    standardization.executionOrder = 215;
    standardization.parameters["fields"] = defaultFactorFields();
    rules.append(standardization);

    CleaningRule neutralization(RULE_NEUTRALIZATION, "neutralization", "按行业和市值进行中性化");
    neutralization.name = "中性化";
    neutralization.level = RULE_LEVEL_RECOMMENDED;
    neutralization.mode = RULE_MODE_CROSS_SECTIONAL;
    neutralization.executionOrder = 220;
    neutralization.parameters["fields"] = defaultFactorFields();
    rules.append(neutralization);

    CleaningRule indexAlignment(RULE_INDEX_MEMBERSHIP_ALIGNMENT, "indexAlignment", "指数调仓日按滞后一天生效");
    indexAlignment.name = "指数调整对齐";
    indexAlignment.level = RULE_LEVEL_OPTIONAL;
    indexAlignment.mode = RULE_MODE_TEMPORAL;
    indexAlignment.executionOrder = 230;
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
    QVector<CleaningRule> rules;
    const QString symbolField = QString(factor::bridge::CommonFields::SYMBOL);
    const QString tradeDateField = QString(factor::bridge::CommonFields::TRADE_DATE);
    const QString reportDateField = QString(factor::bridge::FinancialFieldKeys::REPORT_DATE);
    const QString disclosureDateField = QString(factor::bridge::FinancialFieldKeys::DISCLOSURE_DATE);

    CleaningRule duplicateRemoval(RULE_DUPLICATE_REMOVAL, "duplicateRemoval", "删除重复 symbol/trade_date 记录");
    duplicateRemoval.name = "重复数据删除";
    duplicateRemoval.level = RULE_LEVEL_MANDATORY;
    duplicateRemoval.mode = RULE_MODE_SINGLE_POINT;
    duplicateRemoval.executionOrder = 10;
    duplicateRemoval.parameters["keyFields"] = QStringList{symbolField, tradeDateField};
    rules.append(duplicateRemoval);

    CleaningRule reportAlignment(RULE_REPORT_DATE_ALIGNMENT, "reportDateAlignment", "使用公布日作为生效日期");
    reportAlignment.name = "财报日期对齐";
    reportAlignment.level = RULE_LEVEL_MANDATORY;
    reportAlignment.mode = RULE_MODE_TEMPORAL;
    reportAlignment.executionOrder = 20;
    reportAlignment.parameters["reportDateField"] = reportDateField;
    reportAlignment.parameters["disclosureDateField"] = disclosureDateField;
    rules.append(reportAlignment);

    CleaningRule missingValueFill(RULE_MISSING_VALUE_FILL, "missingValueFill", "优先按时序向前填充财务字段");
    missingValueFill.name = "缺失值处理";
    missingValueFill.level = RULE_LEVEL_RECOMMENDED;
    missingValueFill.mode = RULE_MODE_TEMPORAL;
    missingValueFill.executionOrder = 50;
    missingValueFill.parameters["fields"] = defaultFinancialFields();
    missingValueFill.parameters["maxLookbackDays"] = 5;
    rules.append(missingValueFill);

    CleaningRule winsorization(RULE_WINSORIZATION, "winsorization", "对财务字段做缩尾处理");
    winsorization.name = "异常值缩尾";
    winsorization.level = RULE_LEVEL_RECOMMENDED;
    winsorization.mode = RULE_MODE_CROSS_SECTIONAL;
    winsorization.executionOrder = 210;
    winsorization.parameters["fields"] = defaultFinancialFields();
    winsorization.parameters["lowerQuantile"] = 0.01;
    winsorization.parameters["upperQuantile"] = 0.99;
    rules.append(winsorization);

    CleaningRule standardization(RULE_STANDARDIZATION, "standardization", "对财务字段做Z-Score标准化");
    standardization.name = "标准化";
    standardization.level = RULE_LEVEL_RECOMMENDED;
    standardization.mode = RULE_MODE_CROSS_SECTIONAL;
    standardization.executionOrder = 215;
    standardization.parameters["fields"] = defaultFinancialFields();
    rules.append(standardization);

    CleaningRule neutralization(RULE_NEUTRALIZATION, "neutralization", "按行业和市值进行中性化");
    neutralization.name = "中性化";
    neutralization.level = RULE_LEVEL_RECOMMENDED;
    neutralization.mode = RULE_MODE_CROSS_SECTIONAL;
    neutralization.executionOrder = 220;
    neutralization.parameters["fields"] = defaultFinancialFields();
    rules.append(neutralization);

    return rules;
}

bool DataCleaningEngine::validateDataFormat(const QVariantMap& data) const
{
    return resolveRecordIdentity(data, nullptr, nullptr);
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

bool DataCleaningEngine::executeRule(const CleaningRule& rule, QVariantMap& data, DataCleaningEngineRuntimeContext& context)
{
    sanitizeValuationFields(data);
    const QString tradeDateField = QString(factor::bridge::CommonFields::TRADE_DATE);
    const QString symbolField = QString(factor::bridge::CommonFields::SYMBOL);
    const QString preAdjField = QString(factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR);
    const QString postAdjField = QString(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR);
    const QString reportDateField = QString(factor::bridge::FinancialFieldKeys::REPORT_DATE);
    const QString disclosureDateField = QString(factor::bridge::FinancialFieldKeys::DISCLOSURE_DATE);

    auto canonicalTradeDate = [&data]() {
        return QDate::fromString(data.value(QString(factor::bridge::CommonFields::TRADE_DATE)).toString(), "yyyy-MM-dd");
    };

    auto symbolState = [&context, &data]() -> DataCleaningEngineRuntimeContext::SymbolState& {
        return context.symbols[data.value(QString(factor::bridge::CommonFields::SYMBOL)).toString()];
    };

    switch (rule.type) {
    case RULE_TIME_RANGE: {
        const QDate tradeDate = canonicalTradeDate();
        const QDate startDate = QDate::fromString(rule.parameters.value("startDate").toString(), "yyyy-MM-dd");
        const QDate endDate = QDate::fromString(rule.parameters.value("endDate").toString(), "yyyy-MM-dd");
        return tradeDate.isValid() && startDate.isValid() && endDate.isValid() && tradeDate >= startDate && tradeDate <= endDate;
    }
    case RULE_FORMAT_VALIDATION: {
        const QString dateFormat = rule.parameters.value("dateFormat", "auto").toString();
        const QString rawDate = resolveAliasedField(data, tradeDateAliases());
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
        QStringList requiredFields = toStringList(rule.parameters.value("requiredFields"));
        const bool priceRecord = hasPriceStyleFields(data);
        const bool financialRecord = isFinancialRecord(data);

        for (int i = requiredFields.size() - 1; i >= 0; --i) {
            const QString canonical = canonicalFieldKey(requiredFields.at(i));
            if (canonical == QString(factor::bridge::CommonFields::SYMBOL)
                || canonical == QString(factor::bridge::CommonFields::TRADE_DATE)) {
                continue;
            }
            if ((financialRecord || !priceRecord) && isPriceOnlyField(canonical)) {
                requiredFields.removeAt(i);
            }
        }

        if (requiredFields.isEmpty()) {
            return true;
        }

        for (const QString& field : requiredFields) {
            if (!hasAliasedField(data, aliasedKeysForField(field))) {
                return false;
            }
        }
        return true;
    }
    case RULE_DUPLICATE_REMOVAL: {
        const QStringList keyFields = toStringList(rule.parameters.value("keyFields"));
        QString key = recordDedupScope(data) + '|';
        for (const QString& field : keyFields) {
            const QString resolvedValue = canonicalFieldKey(field) == tradeDateField
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
        const QDate tradeDate = canonicalTradeDate();
        const QDate delistDate = resolveAliasedDate(data, {"delist_date", "delisted_date", "退市日期"});
        if (delistDate.isValid() && tradeDate > delistDate) {
            return false;
        }
        setRuleTag(data, "survivor_bias_checked", true);
        return true;
    }
    case RULE_REPORT_DATE_ALIGNMENT: {
        const bool hasReportDate = hasAliasedField(data, {reportDateField});
        if (!hasReportDate) {
            return true;
        }
        const QDate reportDate = resolveAliasedDate(data, {reportDateField});
        if (!reportDate.isValid()) {
            return true;
        }
        const QDate disclosureDate = resolveAliasedDate(data, {disclosureDateField});
        const QString disclosureDateText = disclosureDate.isValid()
            ? disclosureDate.toString("yyyy-MM-dd")
            : reportDate.toString("yyyy-MM-dd");
        setCanonicalStringField(data, tradeDateField, disclosureDateText);
        setCanonicalStringField(data, disclosureDateField, disclosureDateText);
        setRuleTag(data, "report_date_aligned", true);
        return true;
    }
    case RULE_SUSPENSION_FILL: {
        DataCleaningEngineRuntimeContext::SymbolState& state = symbolState();
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
        DataCleaningEngineRuntimeContext::SymbolState& state = symbolState();
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
        // 前复权因子 (pre_adjust_factor) 将历史价格向前调整，使价格序列可比
        double preAdjFactor = 0.0;
        const bool hasPreAdjFactor = extractAliasedNumericValue(data, {preAdjField}, &preAdjFactor) && preAdjFactor > 0.0;
        // 后复权因子 (post_adjust_factor) 将历史价格向后调整，使价格序列可比
        double postAdjFactor = 0.0;
        const bool hasPostAdjFactor = extractAliasedNumericValue(data, {postAdjField}, &postAdjFactor) && postAdjFactor > 0.0;
        
        if (hasPreAdjFactor) {
            setCanonicalNumericField(data, preAdjField, preAdjFactor);
            removeNonCanonicalAliases(data, preAdjField);
        }
        if (hasPostAdjFactor) {
            setCanonicalNumericField(data, postAdjField, postAdjFactor);
            removeNonCanonicalAliases(data, postAdjField);
        }
        
        // 默认使用后复权（post_adjust_factor）计算调整价格
        const double effectiveFactor = hasPostAdjFactor ? postAdjFactor : (hasPreAdjFactor ? preAdjFactor : 1.0);
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
            if ((hasPostAdjFactor || hasPreAdjFactor) && extractAliasedNumericValue(data, aliasedKeysForField(field), &rawValue)) {
                setCanonicalNumericField(data, field, rawValue * effectiveFactor);
                adjustedApplied = true;
            }
        }
        if (adjustedApplied) {
            setRuleTag(data, "adjusted_price_applied", true);
        }
        return true;
    }
    case RULE_NEW_STOCK_FILTER: {
        DataCleaningEngineRuntimeContext::SymbolState& state = symbolState();
        const QDate tradeDate = canonicalTradeDate();
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
        if (!hasPriceStyleFields(data)) {
            return true;
        }

        const double minPrice = rule.parameters.value("minPrice", 0.01).toDouble();
        const double maxPrice = rule.parameters.value("maxPrice", 10000.0).toDouble();
        const bool allowZeroWhenSuspended = rule.parameters.value("allowZeroWhenSuspended", true).toBool() && data.value("is_suspended").toBool();
        const bool requirePositiveTurnoverWhenTraded = rule.parameters.value("requirePositiveTurnoverWhenTraded", true).toBool();
        const bool requireConsistentDerivedFields = rule.parameters.value("requireConsistentDerivedFields", true).toBool();

        double open = 0.0;
        double high = 0.0;
        double low = 0.0;
        double close = 0.0;
        if (!extractAliasedNumericValue(data, openAliases(), &open)
            || !extractAliasedNumericValue(data, highAliases(), &high)
            || !extractAliasedNumericValue(data, lowAliases(), &low)
            || !extractAliasedNumericValue(data, closeAliases(), &close)) {
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
        if (extractAliasedNumericValue(data, volumeAliases(), &volume) && volume <= 0.0 && !allowZeroWhenSuspended) {
            return false;
        }

        double preClose = 0.0;
        const bool hasPreClose = extractAliasedNumericValue(data, preCloseAliases(), &preClose);
        if (hasPreClose && preClose <= 0.0) {
            return false;
        }

        double turnoverAmount = 0.0;
        const bool hasTurnoverAmount = extractAliasedNumericValue(data, turnoverAliases(), &turnoverAmount);
        if (hasTurnoverAmount && turnoverAmount < 0.0) {
            return false;
        }
        if (requirePositiveTurnoverWhenTraded && close > 0.0 && !allowZeroWhenSuspended) {
            if (hasTurnoverAmount && turnoverAmount <= 0.0) {
                return false;
            }
            if (extractAliasedNumericValue(data, volumeAliases(), &volume) && volume <= 0.0) {
                return false;
            }
        }

        if (requireConsistentDerivedFields && hasPreClose && preClose > 0.0) {
            const double closeDiff = close - preClose;
            double changeAmt = 0.0;
            if (std::abs(closeDiff) > 1e-8
                && extractAliasedNumericValue(data, changeAmtAliases(), &changeAmt)
                && std::abs(changeAmt) <= 1e-8) {
                return false;
            }

            double changePct = 0.0;
            if (std::abs(closeDiff) > 1e-8
                && extractAliasedNumericValue(data, changePctAliases(), &changePct)
                && std::abs(normalizePercentValue(changePct)) <= 1e-4) {
                return false;
            }

            double amplitude = 0.0;
            if (std::abs(high - low) > 1e-8
                && extractAliasedNumericValue(data, amplitudeAliases(), &amplitude)
                && std::abs(normalizePercentValue(amplitude)) <= 1e-4) {
                return false;
            }

            double turnoverRate = 0.0;
            if (extractAliasedNumericValue(data, turnoverRateAliases(), &turnoverRate)) {
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
        if (!hasPriceStyleFields(data)) {
            return true;
        }

        double volume = 0.0;
        if (!extractAliasedNumericValue(data, volumeAliases(), &volume)) {
            return true;
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
        if (!extractAliasedNumericValue(data, changePctAliases(), &changePct)) {
            double prevClose = 0.0;
            double close = 0.0;
            if (extractAliasedNumericValue(data, {QString(factor::bridge::MarketBarFieldKeys::PRE_CLOSE)}, &prevClose)
                && extractAliasedNumericValue(data, closeAliases(), &close)
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
        DataCleaningEngineRuntimeContext::SymbolState& state = symbolState();
        const int maxSuspensionDays = rule.parameters.value("maxSuspensionDays", 10).toInt();
        const int suspensionDays = data.value("suspension_days", state.consecutiveSuspensions).toInt();
        return suspensionDays <= maxSuspensionDays;
    }
    case RULE_INDEX_MEMBERSHIP_ALIGNMENT: {
        const QDate tradeDate = canonicalTradeDate();
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
        if (!hasPriceStyleFields(data)) {
            return true;
        }

        double open = 0.0;
        double close = 0.0;
        if (!extractAliasedNumericValue(data, openAliases(), &open)
            || !extractAliasedNumericValue(data, closeAliases(), &close)
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

bool DataCleaningEngine::executeCrossSectionalRule(const CleaningRule& rule, QVariantList& records, DataCleaningEngineRuntimeContext& context)
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
            const QVariantMap record = item.toMap();
            if (isFinancialRecord(record) || !hasPriceStyleFields(record)) {
                continue;
            }

            double marketCap = 0.0;
            if (extractAnyNumericValue(record, marketCapAliases(), &marketCap) && marketCap > 0.0) {
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
            if (isFinancialRecord(record) || !hasPriceStyleFields(record)) {
                filtered.append(record);
                continue;
            }

            double marketCap = 0.0;
            if (!extractAnyNumericValue(record, marketCapAliases(), &marketCap) || marketCap > cutoff) {
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
                extractAnyNumericValue(record, marketCapAliases(), &marketCap);
                samples.append({i, value, marketCap, resolveAliasedField(record, industryAliases())});
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