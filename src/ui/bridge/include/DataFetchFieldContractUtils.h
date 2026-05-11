#pragma once

#include <QString>
#include <QStringList>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include <initializer_list>

#include "field_traits.h"

namespace factor::bridge {

class FieldKey {
public:
    constexpr explicit FieldKey(const char* name)
        : m_name(name)
    {
    }

    operator QString() const
    {
        return QString::fromUtf8(m_name);
    }

    QString toQString() const
    {
        return QString::fromUtf8(m_name);
    }

    const char* c_str() const
    {
        return m_name;
    }

private:
    const char* m_name;
};

class FieldKeySet {
public:
    using const_iterator = QStringList::const_iterator;

    FieldKeySet() = default;

    FieldKeySet(std::initializer_list<FieldKey> fields)
    {
        for (const FieldKey& field : fields) {
            insert(field);
        }
    }

    bool contains(const QString& field) const
    {
        return m_lookup.contains(field);
    }

    bool contains(const FieldKey& field) const
    {
        return contains(field.toQString());
    }

    bool isEmpty() const
    {
        return m_ordered.isEmpty();
    }

    void unite(const FieldKeySet& other)
    {
        for (const QString& field : other.m_ordered) {
            insert(field);
        }
    }

    const QStringList& orderedValues() const
    {
        return m_ordered;
    }

    QSet<QString> toQStringSet() const
    {
        return m_lookup;
    }

    operator QSet<QString>() const
    {
        return toQStringSet();
    }

    const_iterator begin() const
    {
        return m_ordered.cbegin();
    }

    const_iterator end() const
    {
        return m_ordered.cend();
    }

private:
    void insert(const FieldKey& field)
    {
        insert(field.toQString());
    }

    void insert(const QString& field)
    {
        if (field.isEmpty() || m_lookup.contains(field)) {
            return;
        }
        m_lookup.insert(field);
        m_ordered.append(field);
    }

    QStringList m_ordered;
    QSet<QString> m_lookup;
};

enum class CleanedDataFieldGroup {
    MarketBar,
    Financial,
    SymbolInfo,
    News,
    Policy,
    Alternative,
    Derivatives,
    IndexList,
    IndexConstituents,
    Unknown
};

class CommonFieldKeys {
public:
    inline static const FieldKey SYMBOL{"symbol"};
    inline static const FieldKey TRADE_DATE{"trade_date"};
    inline static const FieldKey DATA_SOURCE{"data_source"};

    static const FieldKeySet& all()
    {
        static const FieldKeySet fields{
            SYMBOL,
            TRADE_DATE,
            DATA_SOURCE
        };
        return fields;
    }
};

class ContextualMetadataFieldKeys {
public:
    inline static const FieldKey NAME{"name"};
    inline static const FieldKey INDUSTRY_CODE{"industry_code"};
    inline static const FieldKey EXCHANGE{"exchange"};
    inline static const FieldKey ASSET_CLASS{"asset_class"};
    inline static const FieldKey STATUS{"status"};
    inline static const FieldKey LIST_DATE{"list_date"};
    inline static const FieldKey INDEX_SYMBOL{"index_symbol"};
    inline static const FieldKey INDEX_NAME{"index_name"};
    inline static const FieldKey INDEX_SNAPSHOT_DATE{"index_snapshot_date"};
    inline static const FieldKey INDEX_IN_DATE{"index_in_date"};
    inline static const FieldKey INDEX_OUT_DATE{"index_out_date"};
    inline static const FieldKey WEIGHT{"weight"};
    inline static const FieldKey START_DATE{"start_date"};
    inline static const FieldKey END_DATE{"end_date"};

    static const FieldKeySet& all()
    {
        static const FieldKeySet fields{
            NAME,
            INDUSTRY_CODE,
            EXCHANGE,
            ASSET_CLASS,
            STATUS,
            LIST_DATE,
            INDEX_SYMBOL,
            INDEX_NAME,
            INDEX_SNAPSHOT_DATE,
            INDEX_IN_DATE,
            INDEX_OUT_DATE,
            WEIGHT,
            START_DATE,
            END_DATE
        };
        return fields;
    }
};

class MarketBarFieldKeys {
public:
    inline static const FieldKey OPEN{"open"};
    inline static const FieldKey HIGH{"high"};
    inline static const FieldKey LOW{"low"};
    inline static const FieldKey CLOSE{"close"};
    inline static const FieldKey PRE_CLOSE{"pre_close"};
    inline static const FieldKey VOLUME{"volume"};
    inline static const FieldKey TURNOVER{"turnover"};
    inline static const FieldKey CHANGE_AMT{"change_amt"};
    inline static const FieldKey CHANGE_PCT{"change_pct"};
    inline static const FieldKey AMPLITUDE{"amplitude"};
    inline static const FieldKey TURNOVER_RATE{"turnover_rate"};
        /// 前复权累计因子（adj_factor_fwd_acc），后复权累计因子（adj_factor_bwd_acc）
    inline static const FieldKey PRE_ADJ_FACTOR{"pre_adjust_factor"};
    inline static const FieldKey POST_ADJ_FACTOR{"post_adjust_factor"};
    inline static const FieldKey MARKET_CAP{"market_cap"};
    inline static const FieldKey CIRCULATING_MARKET_CAP{"circulating_market_cap"};
    inline static const FieldKey PE_RATIO{"pe_ratio"};
    inline static const FieldKey PB_RATIO{"pb_ratio"};
    inline static const FieldKey DIVIDEND_YIELD{"dividend_yield"};
    inline static const FieldKey INDUSTRY_CODE{"industry_code"};

    static const FieldKeySet& all()
    {
        static const FieldKeySet fields{
            OPEN,
            HIGH,
            LOW,
            CLOSE,
            PRE_CLOSE,
            VOLUME,
            TURNOVER,
            CHANGE_AMT,
            CHANGE_PCT,
            AMPLITUDE,
            TURNOVER_RATE,
            PRE_ADJ_FACTOR,
            POST_ADJ_FACTOR,
            MARKET_CAP,
            CIRCULATING_MARKET_CAP,
            PE_RATIO,
            PB_RATIO,
            DIVIDEND_YIELD,
            INDUSTRY_CODE
        };
        return fields;
    }

    static const FieldKeySet& backtestReady()
    {
        static const FieldKeySet fields{
            CommonFieldKeys::SYMBOL,
            CommonFieldKeys::TRADE_DATE,
            OPEN,
            HIGH,
            LOW,
            CLOSE,
            PRE_CLOSE,
            VOLUME,
            TURNOVER,
            CHANGE_PCT,
            CHANGE_AMT,
            AMPLITUDE,
            TURNOVER_RATE,
            PE_RATIO,
            PB_RATIO,
            MARKET_CAP,
            CIRCULATING_MARKET_CAP,
            PRE_ADJ_FACTOR,
            POST_ADJ_FACTOR,
            CommonFieldKeys::DATA_SOURCE
        };
        return fields;
    }

    static const FieldKeySet& missingFillDefaults()
    {
        static const FieldKeySet fields{
            OPEN,
            HIGH,
            LOW,
            CLOSE,
            TURNOVER_RATE,
            MARKET_CAP,
            CIRCULATING_MARKET_CAP
        };
        return fields;
    }

    static const FieldKeySet& priceCore()
    {
        static const FieldKeySet fields{
            OPEN,
            HIGH,
            LOW,
            CLOSE
        };
        return fields;
    }

    static const FieldKeySet& priceOnly()
    {
        static const FieldKeySet fields{
            OPEN,
            HIGH,
            LOW,
            CLOSE,
            PRE_CLOSE,
            VOLUME,
            TURNOVER,
            CHANGE_PCT,
            CHANGE_AMT,
            AMPLITUDE,
            TURNOVER_RATE,
            PRE_ADJ_FACTOR,
            POST_ADJ_FACTOR
        };
        return fields;
    }

    /// 返回两个独立复权因子字段名
    static const QStringList& adjustFactorFields()
    {
        static const QStringList fields{
            QStringLiteral("pre_adjust_factor"),
            QStringLiteral("post_adjust_factor")
        };
        return fields;
    }

    /// 根据 adjustPriceType 配置解析实际使用的复权因子字段
    static QString resolveAdjustField(const QString& adjustPriceType)
    {
        const QString type = adjustPriceType.trimmed().toLower();
        if (type == QStringLiteral("pre_adjust_factor") || type == QStringLiteral("pre")) {
            return QStringLiteral("pre_adjust_factor");
        }
        return QStringLiteral("post_adjust_factor");
    }
};

class FinancialFieldKeys {
public:
    inline static const FieldKey REPORT_DATE{"report_date"};
    inline static const FieldKey DISCLOSURE_DATE{"disclosure_date"};
    inline static const FieldKey BPS{"bps"};
    inline static const FieldKey TOTAL_ASSETS{"total_assets"};
    inline static const FieldKey NET_PROFIT{"net_profit"};
    inline static const FieldKey EQUITY{"equity"};
    inline static const FieldKey ROE{"roe"};
    inline static const FieldKey ROA{"roa"};
    inline static const FieldKey PROFIT_MARGIN{"profit_margin"};
    inline static const FieldKey GROSS_MARGIN{"gross_margin"};
    inline static const FieldKey OPERATING_MARGIN{"operating_margin"};
    inline static const FieldKey EPS{"eps"};
    inline static const FieldKey TOTAL_REVENUE{"total_revenue"};
    inline static const FieldKey TOTAL_LIABILITIES{"total_liabilities"};
    inline static const FieldKey DEBT_TO_EQUITY{"debt_to_equity"};
    inline static const FieldKey CURRENT_RATIO{"current_ratio"};
    inline static const FieldKey QUICK_RATIO{"quick_ratio"};
    inline static const FieldKey OPERATING_CASH_FLOW{"operating_cash_flow"};
    inline static const FieldKey INVESTING_CASH_FLOW{"investing_cash_flow"};
    inline static const FieldKey FINANCING_CASH_FLOW{"financing_cash_flow"};
    inline static const FieldKey DIVIDEND_YIELD{"dividend_yield"};
    inline static const FieldKey PAYOUT_RATIO{"payout_ratio"};
    inline static const FieldKey DIVIDEND_STABILITY{"dividend_stability"};

    static const FieldKeySet& all()
    {
        static const FieldKeySet fields{
            REPORT_DATE,
            DISCLOSURE_DATE,
            BPS,
            TOTAL_ASSETS,
            NET_PROFIT,
            EQUITY,
            ROE,
            ROA,
            PROFIT_MARGIN,
            GROSS_MARGIN,
            OPERATING_MARGIN,
            EPS,
            TOTAL_REVENUE,
            TOTAL_LIABILITIES,
            DEBT_TO_EQUITY,
            CURRENT_RATIO,
            QUICK_RATIO,
            OPERATING_CASH_FLOW,
            INVESTING_CASH_FLOW,
            FINANCING_CASH_FLOW,
            DIVIDEND_YIELD,
            PAYOUT_RATIO,
            DIVIDEND_STABILITY
        };
        return fields;
    }

    static const FieldKeySet& cleaningDefaults()
    {
        static const FieldKeySet fields{
            EPS,
            BPS,
            ROE,
            ROA,
            PROFIT_MARGIN,
            GROSS_MARGIN,
            OPERATING_MARGIN,
            NET_PROFIT,
            TOTAL_REVENUE,
            TOTAL_ASSETS,
            TOTAL_LIABILITIES,
            EQUITY,
            DEBT_TO_EQUITY,
            CURRENT_RATIO,
            QUICK_RATIO,
            OPERATING_CASH_FLOW,
            INVESTING_CASH_FLOW,
            FINANCING_CASH_FLOW,
            DIVIDEND_YIELD,
            PAYOUT_RATIO,
            DIVIDEND_STABILITY
        };
        return fields;
    }
};

class SymbolInfoFieldKeys {
public:
    inline static const FieldKey INDUSTRY_CODE{"industry_code"};
    inline static const FieldKey EXCHANGE{"exchange"};
    inline static const FieldKey ASSET_CLASS{"asset_class"};
    inline static const FieldKey STATUS{"status"};
    inline static const FieldKey LIST_DATE{"list_date"};
    inline static const FieldKey NAME{"name"};

    static const FieldKeySet& all()
    {
        static const FieldKeySet fields{
            INDUSTRY_CODE,
            EXCHANGE,
            ASSET_CLASS,
            STATUS,
            LIST_DATE,
            NAME
        };
        return fields;
    }
};

class NewsFieldKeys {
public:
    inline static const FieldKey SENTIMENT_SCORE{"sentiment_score"};
    inline static const FieldKey SOCIAL_SENTIMENT{"social_sentiment"};
    inline static const FieldKey INVESTOR_SENTIMENT{"investor_sentiment"};
    inline static const FieldKey MARKET_SENTIMENT{"market_sentiment"};
    inline static const FieldKey SECTOR_SENTIMENT{"sector_sentiment"};
    inline static const FieldKey THEME_SENTIMENT{"theme_sentiment"};
    inline static const FieldKey NEWS_COUNT{"news_count"};

    static const FieldKeySet& all()
    {
        static const FieldKeySet fields{
            SENTIMENT_SCORE,
            SOCIAL_SENTIMENT,
            INVESTOR_SENTIMENT,
            MARKET_SENTIMENT,
            SECTOR_SENTIMENT,
            THEME_SENTIMENT,
            NEWS_COUNT
        };
        return fields;
    }
};

class PolicyFieldKeys {
public:
    inline static const FieldKey POLICY_SCORE{"policy_score"};
    inline static const FieldKey POLICY_STRENGTH{"policy_strength"};
    inline static const FieldKey POLICY_COUNT{"policy_count"};

    static const FieldKeySet& all()
    {
        static const FieldKeySet fields{
            POLICY_SCORE,
            POLICY_STRENGTH,
            POLICY_COUNT
        };
        return fields;
    }
};

class AlternativeFieldKeys {
public:
    inline static const FieldKey HOT_RANK{"hot_rank"};
    inline static const FieldKey POPULARITY_SCORE{"popularity_score"};
    inline static const FieldKey COMMENT_COUNT{"comment_count"};
    inline static const FieldKey COMMENT_SENTIMENT{"comment_sentiment"};

    static const FieldKeySet& all()
    {
        static const FieldKeySet fields{
            HOT_RANK,
            POPULARITY_SCORE,
            COMMENT_COUNT,
            COMMENT_SENTIMENT
        };
        return fields;
    }
};

class DerivativesFieldKeys {
public:
    inline static const FieldKey FUTURES_CLOSE{"futures_close"};
    inline static const FieldKey FUTURES_VOLUME{"futures_volume"};
    inline static const FieldKey OPEN_INTEREST{"open_interest"};
    inline static const FieldKey BASIS{"basis"};
    inline static const FieldKey BASIS_RATE{"basis_rate"};

    static const FieldKeySet& all()
    {
        static const FieldKeySet fields{
            FUTURES_CLOSE,
            FUTURES_VOLUME,
            OPEN_INTEREST,
            BASIS,
            BASIS_RATE
        };
        return fields;
    }
};

class IndexListFieldKeys {
public:
    static const FieldKeySet& all()
    {
        static const FieldKeySet fields{};
        return fields;
    }
};

inline const FieldKeySet& cleanedDataCommonFields()
{
    return CommonFieldKeys::all();
}

inline const FieldKeySet& contextualMetadataFields()
{
    return ContextualMetadataFieldKeys::all();
}

inline const FieldKeySet& marketBarFields()
{
    return MarketBarFieldKeys::all();
}

inline const FieldKeySet& marketBarBacktestReadyFields()
{
    return MarketBarFieldKeys::backtestReady();
}

inline const FieldKeySet& financialFields()
{
    return FinancialFieldKeys::all();
}

inline const FieldKeySet& symbolInfoFields()
{
    return SymbolInfoFieldKeys::all();
}

inline const FieldKeySet& newsFields()
{
    return NewsFieldKeys::all();
}

inline const FieldKeySet& policyFields()
{
    return PolicyFieldKeys::all();
}

inline const FieldKeySet& alternativeFields()
{
    return AlternativeFieldKeys::all();
}

inline const FieldKeySet& derivativesFields()
{
    return DerivativesFieldKeys::all();
}

inline const FieldKeySet& indexListFields()
{
    return IndexListFieldKeys::all();
}

inline QString selectedDataTypeTagPrefix()
{
    return QStringLiteral("selected_data_type_");
}

inline QString normalizeSelectedDataType(const QString& rawType)
{
    const QString dataType = rawType.trimmed().toLower();
    if (dataType.isEmpty()) {
        return {};
    }

    if (dataType == QStringLiteral("market_bar")
        || dataType == QStringLiteral("kline_daily")
        || dataType == QStringLiteral("kline_weekly")
        || dataType == QStringLiteral("kline_monthly")
        || dataType == QStringLiteral("minute_data")
        || dataType == QStringLiteral("realtime")
        || dataType == QStringLiteral("historical")) {
        return QStringLiteral("market_bar");
    }

    if (dataType == QStringLiteral("financial")) {
        return QStringLiteral("financial");
    }
    if (dataType == QStringLiteral("symbol_info")) {
        return QStringLiteral("symbol_info");
    }
    if (dataType == QStringLiteral("news")) {
        return QStringLiteral("news");
    }
    if (dataType == QStringLiteral("policy")) {
        return QStringLiteral("policy");
    }
    if (dataType == QStringLiteral("alternative")) {
        return QStringLiteral("alternative");
    }
    if (dataType == QStringLiteral("derivatives")) {
        return QStringLiteral("derivatives");
    }
    if (dataType == QStringLiteral("index_list")) {
        return QStringLiteral("index_list");
    }
    if (dataType == QStringLiteral("index_constituents")) {
        return QStringLiteral("index_constituents");
    }
    return {};
}

inline CleanedDataFieldGroup cleanedDataFieldGroupForType(const QString& normalizedType)
{
    if (normalizedType == QStringLiteral("market_bar")) {
        return CleanedDataFieldGroup::MarketBar;
    }
    if (normalizedType == QStringLiteral("financial")) {
        return CleanedDataFieldGroup::Financial;
    }
    if (normalizedType == QStringLiteral("symbol_info")) {
        return CleanedDataFieldGroup::SymbolInfo;
    }
    if (normalizedType == QStringLiteral("news")) {
        return CleanedDataFieldGroup::News;
    }
    if (normalizedType == QStringLiteral("policy")) {
        return CleanedDataFieldGroup::Policy;
    }
    if (normalizedType == QStringLiteral("alternative")) {
        return CleanedDataFieldGroup::Alternative;
    }
    if (normalizedType == QStringLiteral("derivatives")) {
        return CleanedDataFieldGroup::Derivatives;
    }
    if (normalizedType == QStringLiteral("index_list")) {
        return CleanedDataFieldGroup::IndexList;
    }
    if (normalizedType == QStringLiteral("index_constituents")) {
        return CleanedDataFieldGroup::IndexConstituents;
    }
    return CleanedDataFieldGroup::Unknown;
}

inline QSet<QString> fieldsForSelectedDataType(const QString& normalizedType)
{
    switch (cleanedDataFieldGroupForType(normalizedType)) {
    case CleanedDataFieldGroup::MarketBar:
        return marketBarFields();
    case CleanedDataFieldGroup::Financial:
        return financialFields();
    case CleanedDataFieldGroup::SymbolInfo:
        return symbolInfoFields();
    case CleanedDataFieldGroup::News:
        return newsFields();
    case CleanedDataFieldGroup::Policy:
        return policyFields();
    case CleanedDataFieldGroup::Alternative:
        return alternativeFields();
    case CleanedDataFieldGroup::Derivatives:
        return derivativesFields();
    case CleanedDataFieldGroup::IndexList:
        return indexListFields();
    case CleanedDataFieldGroup::IndexConstituents:
        return contextualMetadataFields();
    case CleanedDataFieldGroup::Unknown:
        return {};
    }
    return {};
}

inline QStringList orderedFieldsForSelectedDataType(const QString& normalizedType)
{
    switch (cleanedDataFieldGroupForType(normalizedType)) {
    case CleanedDataFieldGroup::MarketBar:
        return marketBarFields().orderedValues();
    case CleanedDataFieldGroup::Financial:
        return financialFields().orderedValues();
    case CleanedDataFieldGroup::SymbolInfo:
        return symbolInfoFields().orderedValues();
    case CleanedDataFieldGroup::News:
        return newsFields().orderedValues();
    case CleanedDataFieldGroup::Policy:
        return policyFields().orderedValues();
    case CleanedDataFieldGroup::Alternative:
        return alternativeFields().orderedValues();
    case CleanedDataFieldGroup::Derivatives:
        return derivativesFields().orderedValues();
    case CleanedDataFieldGroup::IndexList:
        return indexListFields().orderedValues();
    case CleanedDataFieldGroup::IndexConstituents:
        return contextualMetadataFields().orderedValues();
    case CleanedDataFieldGroup::Unknown:
        return {};
    }
    return {};
}

inline QStringList sortedStringList(const QSet<QString>& values)
{
    QStringList result = values.values();
    result.sort();
    return result;
}

inline QSet<QString> resolveSelectedDataTypeSet(const QStringList& selectedDataTypes)
{
    QSet<QString> resolved;
    for (const QString& rawType : selectedDataTypes) {
        const QString normalizedType = normalizeSelectedDataType(rawType);
        if (!normalizedType.isEmpty()) {
            resolved.insert(normalizedType);
        }
    }
    return resolved;
}

inline bool rowContainsAnyField(const QVariantMap& row,
                                const QSet<QString>& fields)
{
    for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
        if (fields.contains(it.key().trimmed())) {
            return true;
        }
    }
    return false;
}

inline QSet<QString> inferSelectedDataTypeSetFromRowStructure(const QVariantMap& row)
{
    QSet<QString> inferred;

    static const QSet<QString> marketBarInferenceFields = [] {
        QSet<QString> fields = MarketBarFieldKeys::priceCore().toQStringSet();
        fields.insert(MarketBarFieldKeys::PRE_CLOSE.toQString());
        fields.insert(MarketBarFieldKeys::VOLUME.toQString());
        fields.insert(MarketBarFieldKeys::TURNOVER.toQString());
        fields.insert(MarketBarFieldKeys::CHANGE_AMT.toQString());
        fields.insert(MarketBarFieldKeys::CHANGE_PCT.toQString());
        fields.insert(MarketBarFieldKeys::AMPLITUDE.toQString());
        fields.insert(MarketBarFieldKeys::TURNOVER_RATE.toQString());
        fields.insert(MarketBarFieldKeys::PRE_ADJ_FACTOR.toQString());
        fields.insert(MarketBarFieldKeys::POST_ADJ_FACTOR.toQString());
        fields.insert(MarketBarFieldKeys::MARKET_CAP.toQString());
        fields.insert(MarketBarFieldKeys::CIRCULATING_MARKET_CAP.toQString());
        fields.insert(MarketBarFieldKeys::PE_RATIO.toQString());
        fields.insert(MarketBarFieldKeys::PB_RATIO.toQString());
        return fields;
    }();

    static const QSet<QString> financialInferenceFields = [] {
        QSet<QString> fields = FinancialFieldKeys::cleaningDefaults().toQStringSet();
        fields.insert(FinancialFieldKeys::REPORT_DATE.toQString());
        fields.insert(FinancialFieldKeys::DISCLOSURE_DATE.toQString());
        fields.insert(FinancialFieldKeys::TOTAL_ASSETS.toQString());
        fields.insert(FinancialFieldKeys::TOTAL_LIABILITIES.toQString());
        fields.insert(FinancialFieldKeys::NET_PROFIT.toQString());
        return fields;
    }();

    if (rowContainsAnyField(row, marketBarInferenceFields)) {
        inferred.insert(QStringLiteral("market_bar"));
    }
    if (rowContainsAnyField(row, financialInferenceFields)) {
        inferred.insert(QStringLiteral("financial"));
    }
    if (rowContainsAnyField(row, newsFields().toQStringSet())) {
        inferred.insert(QStringLiteral("news"));
    }
    if (rowContainsAnyField(row, policyFields().toQStringSet())) {
        inferred.insert(QStringLiteral("policy"));
    }
    if (rowContainsAnyField(row, alternativeFields().toQStringSet())) {
        inferred.insert(QStringLiteral("alternative"));
    }
    if (rowContainsAnyField(row, derivativesFields().toQStringSet())) {
        inferred.insert(QStringLiteral("derivatives"));
    }

    return inferred;
}

inline QSet<QString> inferSelectedDataTypeSetFromRows(const QVariantList& data)
{
    QSet<QString> inferred;
    for (const QVariant& item : data) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }

        const QVariantMap row = item.toMap();
        const QString rawType = row.value(QStringLiteral("dataType"),
                                          row.value(QStringLiteral("data_type"),
                                                    row.value(QStringLiteral("dataSourceType"), QString()))).toString();
        const QString normalizedType = normalizeSelectedDataType(rawType);
        if (!normalizedType.isEmpty()) {
            inferred.insert(normalizedType);
            continue;
        }

        inferred.unite(inferSelectedDataTypeSetFromRowStructure(row));
 
    }
    return inferred;
}

inline QStringList resolveSelectedDataTypes(const QStringList& selectedDataTypes,
                                            const QVariantList& data = {})
{
    QSet<QString> resolved = resolveSelectedDataTypeSet(selectedDataTypes);
    if (resolved.isEmpty()) {
        resolved = inferSelectedDataTypeSetFromRows(data);
    }
    return sortedStringList(resolved);
}

inline QStringList extractSelectedDataTypesFromTags(const QStringList& tags)
{
    QSet<QString> resolved;
    const QString prefix = selectedDataTypeTagPrefix();
    for (const QString& tag : tags) {
        if (!tag.startsWith(prefix)) {
            continue;
        }
        const QString normalizedType = normalizeSelectedDataType(tag.mid(prefix.size()));
        if (!normalizedType.isEmpty()) {
            resolved.insert(normalizedType);
        }
    }
    return sortedStringList(resolved);
}

inline QStringList buildSelectedDataTypeTags(const QStringList& selectedDataTypes,
                                             const QVariantList& data = {})
{
    QStringList tags;
    for (const QString& normalizedType : resolveSelectedDataTypes(selectedDataTypes, data)) {
        tags.append(selectedDataTypeTagPrefix() + normalizedType);
    }
    return tags;
}

inline QStringList contractAvailableFieldsForSelectedDataTypes(const QStringList& selectedDataTypes,
                                                               const QVariantList& data = {})
{
    QStringList fields;
    QSet<QString> seen;

    const auto appendFields = [&](const QStringList& additions) {
        for (const QString& field : additions) {
            const QString normalized = field.trimmed();
            if (normalized.isEmpty() || seen.contains(normalized)) {
                continue;
            }
            seen.insert(normalized);
            fields.append(normalized);
        }
    };

    appendFields(cleanedDataCommonFields().orderedValues());
    appendFields(contextualMetadataFields().orderedValues());

    for (const QString& normalizedType : resolveSelectedDataTypes(selectedDataTypes, data)) {
        appendFields(orderedFieldsForSelectedDataType(normalizedType));
    }

    return fields;
}

inline QSet<QString> allowedContractFieldsForSelectedDataTypes(const QStringList& selectedDataTypes,
                                                               const QVariantList& data = {})
{
    const QStringList contractFields = contractAvailableFieldsForSelectedDataTypes(selectedDataTypes, data);
    return QSet<QString>(contractFields.begin(), contractFields.end());
}

inline const QSet<QString>& cleanedDataAllowedTags()
{
    static const QSet<QString> tags{
        QStringLiteral("valuation_sanitized"),
        QStringLiteral("survivor_bias_checked"),
        QStringLiteral("report_date_aligned"),
        QStringLiteral("is_suspended"),
        QStringLiteral("suspension_days"),
        QStringLiteral("forward_filled"),
        QStringLiteral("missing_value_filled"),
        QStringLiteral("limit_up"),
        QStringLiteral("limit_down"),
        QStringLiteral("can_buy"),
        QStringLiteral("can_sell")
    };
    return tags;
}

inline bool isAllowedCleanedDataField(const QString& rawField,
                                      const QSet<QString>& allowedContractFields)
{
    const QString field = rawField.trimmed();
    if (field.isEmpty()) {
        return false;
    }
    return allowedContractFields.contains(field)
        || cleanedDataAllowedTags().contains(field);
}

inline QStringList collectContractAvailableFields(const QVariantList& data,
                                                 const QStringList& selectedDataTypes = {})
{
    const QSet<QString> allowedContractFields = allowedContractFieldsForSelectedDataTypes(selectedDataTypes, data);
    QSet<QString> fields;
    for (const QVariant& item : data) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }
        const QVariantMap row = item.toMap();
        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            const QString key = it.key().trimmed();
            if (isAllowedCleanedDataField(key, allowedContractFields)) {
                fields.insert(key);
            }
        }
    }

    return sortedStringList(fields);
}

}  // namespace factor::bridge