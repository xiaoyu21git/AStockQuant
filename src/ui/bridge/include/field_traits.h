#pragma once

#include <QString>
#include <QStringList>
#include <QSet>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace factor::bridge {

template <std::size_t N>
struct ConstString {
    std::array<char, N> data{};

    constexpr ConstString(const char (&str)[N])
    {
        for (std::size_t i = 0; i < N; ++i) {
            data[i] = str[i];
        }
    }

    constexpr std::string_view view() const { return std::string_view(data.data(), N - 1); }
    constexpr const char* c_str() const { return data.data(); }
    operator QString() const { return QString::fromUtf8(data.data(), static_cast<int>(N - 1)); }

    bool operator==(const ConstString& other) const { return view() == other.view(); }
    bool operator==(std::string_view other) const { return view() == other; }
};

#define DEFINE_FIELD(Name, Str) inline constexpr const char Name[] = Str

namespace CommonFields {
    DEFINE_FIELD(SYMBOL, "symbol");
    DEFINE_FIELD(TRADE_DATE, "trade_date");
    DEFINE_FIELD(DATA_SOURCE, "data_source");
    DEFINE_FIELD(DATA_TYPE, "data_type");
    DEFINE_FIELD(TIME_STAMP, "time_stamp");

    // symbol_info 通用字段
    DEFINE_FIELD(NAME, "name");
    DEFINE_FIELD(EXCHANGE, "exchange");
    DEFINE_FIELD(ASSET_CLASS, "asset_class");
    DEFINE_FIELD(STATUS, "status");
    DEFINE_FIELD(LIST_DATE, "list_date");
    DEFINE_FIELD(DELIST_DATE, "delist_date");
    DEFINE_FIELD(INDUSTRY, "industry_code");
}

namespace CoreRequiredFields {
    namespace Quote {
        DEFINE_FIELD(OPEN, "open");
        DEFINE_FIELD(HIGH, "high");
        DEFINE_FIELD(LOW, "low");
        DEFINE_FIELD(CLOSE, "close");
        DEFINE_FIELD(PRE_CLOSE, "pre_close");
        DEFINE_FIELD(VOLUME, "volume");
        DEFINE_FIELD(TURNOVER, "turnover");
        DEFINE_FIELD(PRE_ADJ_FACTOR, "pre_adjust_factor");
        DEFINE_FIELD(POST_ADJ_FACTOR, "post_adjust_factor");
    }

    namespace TradingState {
        DEFINE_FIELD(IS_SUSPENDED, "is_suspended");
        DEFINE_FIELD(LIMIT_UP, "limit_up");
        DEFINE_FIELD(LIMIT_DOWN, "limit_down");
        DEFINE_FIELD(SUSPENSION_DAYS, "suspension_days");
    }

    namespace MarketCap {
        DEFINE_FIELD(TOTAL_MARKET_CAP, "total_market_cap");
        DEFINE_FIELD(CIRCULATING_MARKET_CAP, "circulating_market_cap");
        DEFINE_FIELD(FREE_FLOAT_RATIO, "free_float_ratio");
    }

    namespace FinancialCore {
        DEFINE_FIELD(REPORT_DATE, "report_date");
        DEFINE_FIELD(DISCLOSURE_DATE, "disclosure_date");
        DEFINE_FIELD(REPORT_TYPE, "report_type");
        DEFINE_FIELD(EPS, "eps");
        DEFINE_FIELD(BPS, "bps");
        DEFINE_FIELD(ROE, "roe");
        DEFINE_FIELD(NET_PROFIT, "net_profit");
        DEFINE_FIELD(TOTAL_REVENUE, "total_revenue");
        DEFINE_FIELD(TOTAL_ASSETS, "total_assets");
        DEFINE_FIELD(TOTAL_LIABILITIES, "total_liabilities");
        DEFINE_FIELD(EQUITY, "equity");
        DEFINE_FIELD(OPERATING_CASH_FLOW, "operating_cash_flow");
    }

    namespace Industry {
        DEFINE_FIELD(SW_INDUSTRY_1, "sw_industry_1");
        DEFINE_FIELD(SW_INDUSTRY_2, "sw_industry_2");
        DEFINE_FIELD(CITICS_INDUSTRY_1, "citics_industry_1");
        DEFINE_FIELD(GICS_SECTOR, "gics_sector");
    }
}

namespace OptionalFields {
    namespace Valuation {
        DEFINE_FIELD(PE_TTM, "pe_ttm");
        DEFINE_FIELD(PB_LF, "pb_lf");
        DEFINE_FIELD(PS_TTM, "ps_ttm");
        DEFINE_FIELD(PCF_TTM, "pcf_ttm");
        DEFINE_FIELD(EV_EBITDA, "ev_ebitda");
    }

    namespace Financial {
        DEFINE_FIELD(GROSS_MARGIN, "gross_margin");
        DEFINE_FIELD(OPERATING_MARGIN, "operating_margin");
        DEFINE_FIELD(NET_MARGIN, "net_margin");
        DEFINE_FIELD(ROIC, "roic");
        DEFINE_FIELD(DEBT_TO_EQUITY, "debt_to_equity");
        DEFINE_FIELD(CURRENT_RATIO, "current_ratio");
        DEFINE_FIELD(QUICK_RATIO, "quick_ratio");
        DEFINE_FIELD(ASSET_TURNOVER, "asset_turnover");
        DEFINE_FIELD(FREE_CASH_FLOW, "free_cash_flow");
        DEFINE_FIELD(DIVIDEND_YIELD, "dividend_yield");
        DEFINE_FIELD(PAYOUT_RATIO, "payout_ratio");
    }

    namespace Daily {
        DEFINE_FIELD(CHANGE_PCT, "change_pct");
        DEFINE_FIELD(AMPLITUDE, "amplitude");
        DEFINE_FIELD(TURNOVER_RATE, "turnover_rate");
        DEFINE_FIELD(VWAP, "vwap");
    }

    namespace HighFreq {
        DEFINE_FIELD(OPEN_MINUTE, "open_minute");
        DEFINE_FIELD(HIGH_MINUTE, "high_minute");
        DEFINE_FIELD(LOW_MINUTE, "low_minute");
        DEFINE_FIELD(CLOSE_MINUTE, "close_minute");
        DEFINE_FIELD(VOLUME_MINUTE, "volume_minute");

        DEFINE_FIELD(CLOSE_WEEK, "close_week");
        DEFINE_FIELD(CHANGE_PCT_WEEK, "change_pct_week");

        DEFINE_FIELD(CLOSE_MONTH, "close_month");
        DEFINE_FIELD(CHANGE_PCT_MONTH, "change_pct_month");
    }

    namespace Sentiment {
        DEFINE_FIELD(NEWS_SENTIMENT, "news_sentiment");
        DEFINE_FIELD(NEWS_POSITIVE_COUNT, "news_positive_count");
        DEFINE_FIELD(NEWS_NEGATIVE_COUNT, "news_negative_count");
        DEFINE_FIELD(NEWS_TOTAL_COUNT, "news_total_count");
        DEFINE_FIELD(SOCIAL_SENTIMENT, "social_sentiment");
        DEFINE_FIELD(INVESTOR_SENTIMENT, "investor_sentiment");
        DEFINE_FIELD(SEARCH_HOT_INDEX, "search_hot_index");
    }

    namespace Alternative {
        DEFINE_FIELD(POPULARITY_SCORE, "popularity_score");
        DEFINE_FIELD(COMMENT_COUNT, "comment_count");
        DEFINE_FIELD(BAIDU_INDEX, "baidu_index");
        DEFINE_FIELD(FOLLOWER_COUNT, "follower_count");
    }

    namespace Index {
        DEFINE_FIELD(INDEX_SYMBOL, "index_symbol");
        DEFINE_FIELD(INDEX_NAME, "index_name");
        DEFINE_FIELD(INDEX_OPEN, "index_open");
        DEFINE_FIELD(INDEX_HIGH, "index_high");
        DEFINE_FIELD(INDEX_LOW, "index_low");
        DEFINE_FIELD(INDEX_CLOSE, "index_close");
        DEFINE_FIELD(INDEX_VOLUME, "index_volume");
        DEFINE_FIELD(INDEX_TURNOVER, "index_turnover");
        DEFINE_FIELD(INDEX_PE, "index_pe");
        DEFINE_FIELD(INDEX_PB, "index_pb");
    }

    namespace Derivatives {
        DEFINE_FIELD(FUTURES_CLOSE, "futures_close");
        DEFINE_FIELD(FUTURES_VOLUME, "futures_volume");
        DEFINE_FIELD(OPEN_INTEREST, "open_interest");
        DEFINE_FIELD(BASIS, "basis");
        DEFINE_FIELD(BASIS_RATE, "basis_rate");
    }
}

namespace QualityFields {
    DEFINE_FIELD(CAN_BUY, "can_buy");
    DEFINE_FIELD(CAN_SELL, "can_sell");

    DEFINE_FIELD(SURVIVOR_BIAS_CHECKED, "survivor_bias_checked");
    DEFINE_FIELD(VALUATION_SANITIZED, "valuation_sanitized");
    DEFINE_FIELD(FORWARD_FILLED, "forward_filled");
    DEFINE_FIELD(MISSING_VALUE_FILLED, "missing_value_filled");
    DEFINE_FIELD(BACKWARD_FILLED, "backward_filled");
    DEFINE_FIELD(INTERPOLATED, "interpolated");

    DEFINE_FIELD(WINSORIZED, "winsorized");
    DEFINE_FIELD(STANDARDIZED, "standardized");
    DEFINE_FIELD(NEUTRALIZED, "neutralized");
    DEFINE_FIELD(INDUSTRY_NEUTRALIZED, "industry_neutralized");
    DEFINE_FIELD(MARKET_NEUTRALIZED, "market_neutralized");

    DEFINE_FIELD(CLEANING_TAGS, "cleaning_tags");
    DEFINE_FIELD(VALUATION_INVALID_FIELDS, "valuation_invalid_fields");
    DEFINE_FIELD(DATA_QUALITY_SCORE, "data_quality_score");
    DEFINE_FIELD(PROCESSING_TIMESTAMP, "processing_timestamp");
}

enum class DataFrequency : std::uint8_t {
    Tick,
    Minute1,
    Minute5,
    Minute15,
    Minute30,
    Minute60,
    Daily,
    Weekly,
    Monthly,
    Quarterly,
    Yearly
};

enum class DataType : std::uint8_t {
    Unknown,
    Quote,
    Financial,
    Industry,
    Sentiment,
    Alternative,
    Index,
    Derivatives,
    Quality
};

template <std::size_t N>
struct FieldSetHelpers {
    static constexpr bool contains(const std::array<std::string_view, N>& names, std::string_view name)
    {
        for (const auto& candidate : names) {
            if (candidate == name) {
                return true;
            }
        }
        return false;
    }

    static QStringList toStringList(const std::array<std::string_view, N>& names)
    {
        QStringList result;
        result.reserve(static_cast<qsizetype>(N));
        for (const auto& name : names) {
            result.push_back(QString::fromUtf8(name.data(), static_cast<int>(name.size())));
        }
        return result;
    }

    static QSet<QString> toQSet(const std::array<std::string_view, N>& names)
    {
        QSet<QString> result;
        result.reserve(static_cast<qsizetype>(N));
        for (const auto& name : names) {
            result.insert(QString::fromUtf8(name.data(), static_cast<int>(name.size())));
        }
        return result;
    }
};

struct CoreRequiredSet {
    static constexpr std::array<std::string_view, 21> names{
        CommonFields::SYMBOL,
        CommonFields::TRADE_DATE,
        CoreRequiredFields::Quote::OPEN,
        CoreRequiredFields::Quote::HIGH,
        CoreRequiredFields::Quote::LOW,
        CoreRequiredFields::Quote::CLOSE,
        CoreRequiredFields::Quote::PRE_CLOSE,
        CoreRequiredFields::Quote::VOLUME,
        CoreRequiredFields::Quote::TURNOVER,
        CoreRequiredFields::Quote::PRE_ADJ_FACTOR,
        CoreRequiredFields::Quote::POST_ADJ_FACTOR,
        CoreRequiredFields::TradingState::IS_SUSPENDED,
        CoreRequiredFields::TradingState::LIMIT_UP,
        CoreRequiredFields::TradingState::LIMIT_DOWN,
        CoreRequiredFields::MarketCap::TOTAL_MARKET_CAP,
        CoreRequiredFields::MarketCap::CIRCULATING_MARKET_CAP,
        CoreRequiredFields::FinancialCore::EPS,
        CoreRequiredFields::FinancialCore::BPS,
        CoreRequiredFields::FinancialCore::REPORT_DATE,
        CoreRequiredFields::FinancialCore::DISCLOSURE_DATE,
        CoreRequiredFields::Industry::SW_INDUSTRY_1};

    static constexpr std::size_t size() { return names.size(); }
    static constexpr bool contains(std::string_view name) { return FieldSetHelpers<size()>::contains(names, name); }
    static QStringList toStringList() { return FieldSetHelpers<size()>::toStringList(names); }
    static QSet<QString> toQSet() { return FieldSetHelpers<size()>::toQSet(names); }
};

struct DailyQuoteSet {
    static constexpr std::array<std::string_view, 18> names{
        CommonFields::SYMBOL,
        CommonFields::TRADE_DATE,
        CoreRequiredFields::Quote::OPEN,
        CoreRequiredFields::Quote::HIGH,
        CoreRequiredFields::Quote::LOW,
        CoreRequiredFields::Quote::CLOSE,
        CoreRequiredFields::Quote::PRE_CLOSE,
        CoreRequiredFields::Quote::VOLUME,
        CoreRequiredFields::Quote::TURNOVER,
        CoreRequiredFields::Quote::PRE_ADJ_FACTOR,
        CoreRequiredFields::Quote::POST_ADJ_FACTOR,
        OptionalFields::Daily::CHANGE_PCT,
        OptionalFields::Daily::AMPLITUDE,
        OptionalFields::Daily::TURNOVER_RATE,
        CoreRequiredFields::MarketCap::TOTAL_MARKET_CAP,
        CoreRequiredFields::MarketCap::CIRCULATING_MARKET_CAP,
        OptionalFields::Valuation::PE_TTM,
        OptionalFields::Valuation::PB_LF};

    static constexpr std::size_t size() { return names.size(); }
    static constexpr bool contains(std::string_view name) { return FieldSetHelpers<size()>::contains(names, name); }
    static QStringList toStringList() { return FieldSetHelpers<size()>::toStringList(names); }
    static QSet<QString> toQSet() { return FieldSetHelpers<size()>::toQSet(names); }
};

struct FinancialSet {
    static constexpr std::array<std::string_view, 19> names{
        CommonFields::SYMBOL,
        CoreRequiredFields::FinancialCore::REPORT_DATE,
        CoreRequiredFields::FinancialCore::DISCLOSURE_DATE,
        CoreRequiredFields::FinancialCore::REPORT_TYPE,
        CoreRequiredFields::FinancialCore::EPS,
        CoreRequiredFields::FinancialCore::BPS,
        CoreRequiredFields::FinancialCore::ROE,
        CoreRequiredFields::FinancialCore::NET_PROFIT,
        CoreRequiredFields::FinancialCore::TOTAL_REVENUE,
        CoreRequiredFields::FinancialCore::TOTAL_ASSETS,
        CoreRequiredFields::FinancialCore::TOTAL_LIABILITIES,
        CoreRequiredFields::FinancialCore::EQUITY,
        CoreRequiredFields::FinancialCore::OPERATING_CASH_FLOW,
        OptionalFields::Financial::GROSS_MARGIN,
        OptionalFields::Financial::OPERATING_MARGIN,
        OptionalFields::Financial::DEBT_TO_EQUITY,
        OptionalFields::Financial::CURRENT_RATIO,
        OptionalFields::Financial::FREE_CASH_FLOW,
        OptionalFields::Financial::DIVIDEND_YIELD};

    static constexpr std::size_t size() { return names.size(); }
    static constexpr bool contains(std::string_view name) { return FieldSetHelpers<size()>::contains(names, name); }
    static QStringList toStringList() { return FieldSetHelpers<size()>::toStringList(names); }
    static QSet<QString> toQSet() { return FieldSetHelpers<size()>::toQSet(names); }
};

struct TradingConstraintSet {
    static constexpr std::array<std::string_view, 5> names{
        CoreRequiredFields::TradingState::IS_SUSPENDED,
        CoreRequiredFields::TradingState::LIMIT_UP,
        CoreRequiredFields::TradingState::LIMIT_DOWN,
        QualityFields::CAN_BUY,
        QualityFields::CAN_SELL};

    static constexpr std::size_t size() { return names.size(); }
    static constexpr bool contains(std::string_view name) { return FieldSetHelpers<size()>::contains(names, name); }
    static QStringList toStringList() { return FieldSetHelpers<size()>::toStringList(names); }
    static QSet<QString> toQSet() { return FieldSetHelpers<size()>::toQSet(names); }
};

template <typename T>
struct FieldAccessor {
    QString name;

    explicit FieldAccessor(const QString& n)
        : name(n)
    {
    }

    std::optional<T> get(const QVariantMap& row) const
    {
        const auto it = row.find(name);
        if (it == row.end() || !it.value().isValid() || it.value().isNull()) {
            return std::nullopt;
        }
        if constexpr (std::is_same_v<T, double>) {
            bool ok = false;
            double val = it.value().toDouble(&ok);
            if (!ok || !std::isfinite(val)) return std::nullopt;
            return val;
        } else if constexpr (std::is_same_v<T, QString>) {
            QString val = it.value().toString().trimmed();
            if (val.isEmpty()) return std::nullopt;
            return val;
        } else if constexpr (std::is_same_v<T, bool>) {
            if (it.value().type() == QVariant::Bool) return it.value().toBool();
            QString text = it.value().toString().trimmed().toLower();
            if (text == "1" || text == "true" || text == "yes" || text == "y") return true;
            if (text == "0" || text == "false" || text == "no" || text == "n") return false;
            return std::nullopt;
        } else {
            return it.value().template value<T>();
        }
    }

    void set(QVariantMap& row, T value) const {
        row[name] = QVariant::fromValue(value);
    }

    bool has(const QVariantMap& row) const { return row.contains(name); }

    bool hasValue(const QVariantMap& row) const { return get(row).has_value(); }

    void clear(QVariantMap& row) const { row.remove(name); }
};

namespace Accessors {
    inline const FieldAccessor<QString> Symbol{CommonFields::SYMBOL};
    inline const FieldAccessor<QString> TradeDate{CommonFields::TRADE_DATE};
    inline const FieldAccessor<QString> DataSource{CommonFields::DATA_SOURCE};
    inline const FieldAccessor<QString> DataTypeStr{CommonFields::DATA_TYPE};

    // symbol_info 通用字段
    inline const FieldAccessor<QString> Name{CommonFields::NAME};
    inline const FieldAccessor<QString> Exchange{CommonFields::EXCHANGE};
    inline const FieldAccessor<QString> AssetClass{CommonFields::ASSET_CLASS};
    inline const FieldAccessor<QString> StatusVal{CommonFields::STATUS};
    inline const FieldAccessor<QString> ListDate{CommonFields::LIST_DATE};
    inline const FieldAccessor<QString> DelistDate{CommonFields::DELIST_DATE};
    inline const FieldAccessor<QString> Industry{CommonFields::INDUSTRY};

    inline const FieldAccessor<double> Open{CoreRequiredFields::Quote::OPEN};
    inline const FieldAccessor<double> High{CoreRequiredFields::Quote::HIGH};
    inline const FieldAccessor<double> Low{CoreRequiredFields::Quote::LOW};
    inline const FieldAccessor<double> Close{CoreRequiredFields::Quote::CLOSE};
    inline const FieldAccessor<double> PreClose{CoreRequiredFields::Quote::PRE_CLOSE};
    inline const FieldAccessor<double> Volume{CoreRequiredFields::Quote::VOLUME};
    inline const FieldAccessor<double> Turnover{CoreRequiredFields::Quote::TURNOVER};
    inline const FieldAccessor<double> PreAdjFactor{CoreRequiredFields::Quote::PRE_ADJ_FACTOR};
    inline const FieldAccessor<double> PostAdjFactor{CoreRequiredFields::Quote::POST_ADJ_FACTOR};

    inline const FieldAccessor<bool> IsSuspended{CoreRequiredFields::TradingState::IS_SUSPENDED};
    inline const FieldAccessor<bool> LimitUp{CoreRequiredFields::TradingState::LIMIT_UP};
    inline const FieldAccessor<bool> LimitDown{CoreRequiredFields::TradingState::LIMIT_DOWN};

    inline const FieldAccessor<double> TotalMarketCap{CoreRequiredFields::MarketCap::TOTAL_MARKET_CAP};
    inline const FieldAccessor<double> CirculatingMarketCap{CoreRequiredFields::MarketCap::CIRCULATING_MARKET_CAP};

    inline const FieldAccessor<double> EPS{CoreRequiredFields::FinancialCore::EPS};
    inline const FieldAccessor<double> BPS{CoreRequiredFields::FinancialCore::BPS};
    inline const FieldAccessor<double> ROE{CoreRequiredFields::FinancialCore::ROE};
    inline const FieldAccessor<QString> ReportDate{CoreRequiredFields::FinancialCore::REPORT_DATE};
    inline const FieldAccessor<QString> DisclosureDate{CoreRequiredFields::FinancialCore::DISCLOSURE_DATE};

    inline const FieldAccessor<double> PETTM{OptionalFields::Valuation::PE_TTM};
    inline const FieldAccessor<double> PBLF{OptionalFields::Valuation::PB_LF};
    inline const FieldAccessor<double> ChangePct{OptionalFields::Daily::CHANGE_PCT};

    inline const FieldAccessor<QString> SWIndustry1{CoreRequiredFields::Industry::SW_INDUSTRY_1};

    inline const FieldAccessor<bool> CanBuy{QualityFields::CAN_BUY};
    inline const FieldAccessor<bool> CanSell{QualityFields::CAN_SELL};
    inline const FieldAccessor<bool> SurvivorBiasChecked{QualityFields::SURVIVOR_BIAS_CHECKED};
    inline const FieldAccessor<bool> ForwardFilled{QualityFields::FORWARD_FILLED};
    inline const FieldAccessor<bool> MissingValueFilled{QualityFields::MISSING_VALUE_FILLED};
}

namespace BacktestValidator {
    inline bool validateCoreFields(const QVariantMap& row, QStringList* missing = nullptr)
    {
        QStringList missingFields;
        for (const auto& name : CoreRequiredSet::names) {
            const QString field = QString::fromUtf8(name.data(), static_cast<int>(name.size()));
            if (!row.contains(field)) {
                missingFields << field;
            }
        }

        if (missing != nullptr) {
            *missing = missingFields;
        }
        return missingFields.isEmpty();
    }

        inline bool isTradable(const QVariantMap& row, bool isBuy = true)
    {
        auto suspended = Accessors::IsSuspended.get(row);
        if (suspended.value_or(false)) return false;

        auto limitUp = Accessors::LimitUp.get(row);
        auto limitDown = Accessors::LimitDown.get(row);
        auto canBuy = Accessors::CanBuy.get(row);
        auto canSell = Accessors::CanSell.get(row);

        if (isBuy && limitUp.value_or(false)) return false;
        if (!isBuy && limitDown.value_or(false)) return false;
        if (isBuy && !canBuy.value_or(true)) return false;
        if (!isBuy && !canSell.value_or(true)) return false;

        return true;
    }

    inline bool isDataClean(const QVariantMap& row)
    {
        return Accessors::SurvivorBiasChecked.get(row).value_or(false)
            && row.contains(QString::fromUtf8(QualityFields::VALUATION_SANITIZED));
    }
}

inline DataFrequency inferFrequency(const QVariantMap& row)
{
    if (row.contains(QString::fromUtf8(OptionalFields::HighFreq::CLOSE_MINUTE))) {
        return DataFrequency::Minute1;
    }
    if (row.contains(QString::fromUtf8(OptionalFields::HighFreq::CLOSE_WEEK))) {
        return DataFrequency::Weekly;
    }
    if (row.contains(QString::fromUtf8(OptionalFields::HighFreq::CLOSE_MONTH))) {
        return DataFrequency::Monthly;
    }
    if (row.contains(QString::fromUtf8(CoreRequiredFields::Quote::CLOSE))) {
        return DataFrequency::Daily;
    }
    return DataFrequency::Daily;
}

inline DataType inferDataType(const QVariantMap& row)
{
    if (row.contains(QString::fromUtf8(CoreRequiredFields::Quote::CLOSE))) {
        return DataType::Quote;
    }
    if (row.contains(QString::fromUtf8(CoreRequiredFields::FinancialCore::EPS))) {
        return DataType::Financial;
    }
    if (row.contains(QString::fromUtf8(CoreRequiredFields::Industry::SW_INDUSTRY_1))) {
        return DataType::Industry;
    }
    if (row.contains(QString::fromUtf8(OptionalFields::Sentiment::NEWS_SENTIMENT))) {
        return DataType::Sentiment;
    }
    if (row.contains(QString::fromUtf8(OptionalFields::Alternative::BAIDU_INDEX))) {
        return DataType::Alternative;
    }
    if (row.contains(QString::fromUtf8(OptionalFields::Index::INDEX_CLOSE))) {
        return DataType::Index;
    }
    return DataType::Unknown;
}

#undef DEFINE_FIELD

}  // namespace factor::bridge
