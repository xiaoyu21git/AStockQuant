#include <gtest/gtest.h>

#include "DataServiceCache.h"
#include "DataFetchController.h"
#include "FactorBacktestController.h"
#include "FactorBacktestPreflightUtils.h"
#include "FactorDomainSyncRetryUtils.h"
#include "FactorDomainSyncUtils.h"
#include "FactorInstanceResolutionUtils.h"
#include "DatabaseConnectionManager.h"
#include "DataFetchFieldContractUtils.h"
#include "FactorRequirementInferenceUtils.h"
#include "RiskConfigService.h"
#include "cleaning/CleaningEngine.h"
#include "cleaning/rules/CompletenessRule.h"
#include "cleaning/rules/AdjustedPriceRule.h"
#include "domain/factor/include/ConfigurableFactor.h"
#include "infrastructure/include/database/FactorRepository.h"
#include "cache/include/cache_facade.h"
#include "domain/factor/include/FactorCacheManager.h"
#include "domain/factor/include/FactorBacktestCachedBarUtils.h"
#include "domain/factor/include/ArrowMarketData.h"
#include "domain/factor/include/FactorBacktestExecutor.h"
#include "domain/factor/include/FactorBacktestGroupingUtils.h"
#include "domain/factor/include/FactorBacktestIcUtils.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/LowVolFactor.h"
#include "domain/factor/include/MomentumFactor.h"
#include "domain/factor/include/QualityFactor.h"
#include "domain/factor/include/SizeFactor.h"
#include "domain/factor/include/ValueFactor.h"
#include "FactorService.h"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "FactorBacktestWarmupUtils.h"
#include "foundation.h"

#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVariantList>
#include <QTimer>

#include <memory>
#include <optional>
#include <future>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <utility>
#include <vector>

using factor::HistoricalDataPoint;
using factor::HistoricalView;

namespace {

factor::FactorType parseFactorType(const QString& rawType)
{
    const QString normalized = rawType.trimmed().toLower();
    if (normalized == QStringLiteral("value")) return factor::FactorType::VALUE;
    if (normalized == QStringLiteral("momentum")) return factor::FactorType::MOMENTUM;
    if (normalized == QStringLiteral("size")) return factor::FactorType::SIZE;
    if (normalized == QStringLiteral("quality")) return factor::FactorType::QUALITY;
    if (normalized == QStringLiteral("growth")) return factor::FactorType::GROWTH;
    if (normalized == QStringLiteral("dividend")) return factor::FactorType::DIVIDEND;
    if (normalized == QStringLiteral("technical")) return factor::FactorType::TECHNICAL;
    if (normalized == QStringLiteral("liquidity")) return factor::FactorType::LIQUIDITY;
    if (normalized == QStringLiteral("macro")) return factor::FactorType::MACRO;
    if (normalized == QStringLiteral("industry")) return factor::FactorType::INDUSTRY;
    if (normalized == QStringLiteral("sentiment")) return factor::FactorType::SENTIMENT;
    if (normalized == QStringLiteral("custom") || normalized == QStringLiteral("configurable")) return factor::FactorType::CUSTOM;
    if (normalized == QStringLiteral("low_volatility")) return factor::FactorType::LOW_VOLATILITY;
    return factor::FactorType::UNKNOWN;
}

bool isConfigurableFactorTypeForTest(const factor::FactorType factorType)
{
    switch (factorType) {
    case factor::FactorType::GROWTH:
    case factor::FactorType::LIQUIDITY:
    case factor::FactorType::TECHNICAL:
    case factor::FactorType::DIVIDEND:
    case factor::FactorType::MACRO:
    case factor::FactorType::INDUSTRY:
    case factor::FactorType::SENTIMENT:
    case factor::FactorType::CUSTOM:
        return true;
    default:
        return false;
    }
}

QString normalizedEnumText(const QJsonValue& value)
{
    return value.isString() ? value.toString().trimmed().toLower() : QString();
}

std::optional<int> commonStandardizationIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("none")) return static_cast<int>(factor::CommonStandardization::NONE);
    if (normalized == QStringLiteral("zscore") || normalized == QStringLiteral("z_score") || normalized == QStringLiteral("z-score")) {
        return static_cast<int>(factor::CommonStandardization::ZSCORE);
    }
    if (normalized == QStringLiteral("minmax") || normalized == QStringLiteral("min_max") || normalized == QStringLiteral("min-max")) {
        return static_cast<int>(factor::CommonStandardization::MINMAX);
    }
    if (normalized == QStringLiteral("percentile") || normalized == QStringLiteral("rank")) {
        return static_cast<int>(factor::CommonStandardization::PERCENTILE);
    }
    return std::nullopt;
}

std::optional<int> configurableStandardizationIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("none")) return static_cast<int>(factor::StandardizationMethod::None);
    if (normalized == QStringLiteral("zscore") || normalized == QStringLiteral("z_score") || normalized == QStringLiteral("z-score")) {
        return static_cast<int>(factor::StandardizationMethod::ZScore);
    }
    if (normalized == QStringLiteral("minmax") || normalized == QStringLiteral("min_max") || normalized == QStringLiteral("min-max")) {
        return static_cast<int>(factor::StandardizationMethod::MinMax);
    }
    if (normalized == QStringLiteral("rank")) return static_cast<int>(factor::StandardizationMethod::Rank);
    if (normalized == QStringLiteral("percentile")) return static_cast<int>(factor::StandardizationMethod::Percentile);
    return std::nullopt;
}

std::optional<int> frequencyIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("daily")) return 0;
    if (normalized == QStringLiteral("weekly")) return 1;
    if (normalized == QStringLiteral("monthly")) return 2;
    if (normalized == QStringLiteral("quarterly")) return 3;
    if (normalized == QStringLiteral("annual") || normalized == QStringLiteral("yearly")) return 4;
    return std::nullopt;
}

std::optional<int> adjustPriceTypeIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("pre_adjust_factor")) return static_cast<int>(factor::AdjustPriceType::PRE_ADJUST_FACTOR);
    if (normalized == QStringLiteral("post_adjust_factor")) return static_cast<int>(factor::AdjustPriceType::POST_ADJUST_FACTOR);
    return std::nullopt;
}

std::optional<int> technicalPriceTypeIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("close")) return static_cast<int>(factor::TechnicalPriceType::CLOSE);
    if (normalized == QStringLiteral("open")) return static_cast<int>(factor::TechnicalPriceType::OPEN);
    if (normalized == QStringLiteral("high")) return static_cast<int>(factor::TechnicalPriceType::HIGH);
    if (normalized == QStringLiteral("low")) return static_cast<int>(factor::TechnicalPriceType::LOW);
    return std::nullopt;
}

std::optional<int> valuationMetricIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("bp")) return static_cast<int>(factor::ValuationMetric::BP);
    if (normalized == QStringLiteral("ep")) return static_cast<int>(factor::ValuationMetric::EP);
    if (normalized == QStringLiteral("dividend_yield")) return static_cast<int>(factor::ValuationMetric::DIVIDEND_YIELD);
    if (normalized == QStringLiteral("cf_p")) return static_cast<int>(factor::ValuationMetric::CFP);
    return std::nullopt;
}

std::optional<int> sizeMetricIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("market_cap")) return static_cast<int>(factor::SizeMetric::MARKET_CAP);
    if (normalized == QStringLiteral("circulating_market_cap")) return static_cast<int>(factor::SizeMetric::CIRCULATING_MARKET_CAP);
    if (normalized == QStringLiteral("total_assets")) return static_cast<int>(factor::SizeMetric::TOTAL_ASSETS);
    return std::nullopt;
}

std::optional<int> growthMetricIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("revenue_growth")) return static_cast<int>(factor::GrowthMetric::REVENUE_GROWTH);
    if (normalized == QStringLiteral("net_profit_growth")) return static_cast<int>(factor::GrowthMetric::NET_PROFIT_GROWTH);
    if (normalized == QStringLiteral("delta_roe")) return static_cast<int>(factor::GrowthMetric::DELTA_ROE);
    if (normalized == QStringLiteral("sue")) return static_cast<int>(factor::GrowthMetric::SUE);
    return std::nullopt;
}

std::optional<int> dividendMetricIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("dividend_yield")) return static_cast<int>(factor::DividendMetric::DIVIDEND_YIELD);
    if (normalized == QStringLiteral("payout_ratio")) return static_cast<int>(factor::DividendMetric::PAYOUT_RATIO);
    if (normalized == QStringLiteral("dividend_stability")) return static_cast<int>(factor::DividendMetric::DIVIDEND_STABILITY);
    return std::nullopt;
}

std::optional<int> liquidityMetricIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("turnover_rate")) return static_cast<int>(factor::LiquidityMetric::TURNOVER_RATE);
    if (normalized == QStringLiteral("volume")) return static_cast<int>(factor::LiquidityMetric::VOLUME);
    if (normalized == QStringLiteral("amihud_illiquidity")) return static_cast<int>(factor::LiquidityMetric::AMIHUD_ILLIQUIDITY);
    if (normalized == QStringLiteral("amplitude")) return static_cast<int>(factor::LiquidityMetric::AMPLITUDE);
    return std::nullopt;
}

std::optional<int> industryMetricIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("industry_prosperity")) return static_cast<int>(factor::IndustryMetric::INDUSTRY_PROSPERITY);
    if (normalized == QStringLiteral("industry_momentum")) return static_cast<int>(factor::IndustryMetric::INDUSTRY_MOMENTUM);
    if (normalized == QStringLiteral("industry_concentration")) return static_cast<int>(factor::IndustryMetric::INDUSTRY_CONCENTRATION);
    return std::nullopt;
}

std::optional<int> sentimentMetricIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("sentiment_score")) return static_cast<int>(factor::SentimentMetric::SENTIMENT_SCORE);
    if (normalized == QStringLiteral("social_sentiment")) return static_cast<int>(factor::SentimentMetric::SOCIAL_SENTIMENT);
    if (normalized == QStringLiteral("investor_sentiment")) return static_cast<int>(factor::SentimentMetric::INVESTOR_SENTIMENT);
    if (normalized == QStringLiteral("market_sentiment")) return static_cast<int>(factor::SentimentMetric::MARKET_SENTIMENT);
    return std::nullopt;
}

std::optional<int> sentimentSourceIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("news") || normalized == QStringLiteral("news_sentiment")) return static_cast<int>(factor::SentimentSource::NEWS);
    if (normalized == QStringLiteral("social_media")) return static_cast<int>(factor::SentimentSource::SOCIAL_MEDIA);
    if (normalized == QStringLiteral("analyst_rating")) return static_cast<int>(factor::SentimentSource::ANALYST_RATING);
    if (normalized == QStringLiteral("market") || normalized == QStringLiteral("market_sentiment")) return static_cast<int>(factor::SentimentSource::MARKET);
    if (normalized == QStringLiteral("policy")) return static_cast<int>(factor::SentimentSource::POLICY);
    if (normalized == QStringLiteral("alternative")) return static_cast<int>(factor::SentimentSource::ALTERNATIVE);
    if (normalized == QStringLiteral("derivatives")) return static_cast<int>(factor::SentimentSource::DERIVATIVES);
    return std::nullopt;
}

std::optional<int> sectorTypeIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("sw_l1")) return static_cast<int>(factor::ConfigurableSectorType::SW_L1);
    if (normalized == QStringLiteral("sw_l2")) return static_cast<int>(factor::ConfigurableSectorType::SW_L2);
    if (normalized == QStringLiteral("citic_l1")) return static_cast<int>(factor::ConfigurableSectorType::CITIC_L1);
    if (normalized == QStringLiteral("citic_l2")) return static_cast<int>(factor::ConfigurableSectorType::CITIC_L2);
    if (normalized == QStringLiteral("申万一级")) return static_cast<int>(factor::ConfigurableSectorType::SW_L1);
    if (normalized == QStringLiteral("申万二级")) return static_cast<int>(factor::ConfigurableSectorType::SW_L2);
    if (normalized == QStringLiteral("中信一级")) return static_cast<int>(factor::ConfigurableSectorType::CITIC_L1);
    if (normalized == QStringLiteral("中信二级")) return static_cast<int>(factor::ConfigurableSectorType::CITIC_L2);
    return std::nullopt;
}

std::optional<int> lowVolComponentIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("volatility")) return static_cast<int>(factor::LowVolComponent::VOLATILITY);
    if (normalized == QStringLiteral("drawdown")) return static_cast<int>(factor::LowVolComponent::DRAWDOWN);
    if (normalized == QStringLiteral("beta")) return static_cast<int>(factor::LowVolComponent::BETA);
    return std::nullopt;
}

std::optional<int> sourceTableIndex(const QString& rawValue)
{
    const QString normalized = rawValue.trimmed().toLower();
    if (normalized == QStringLiteral("daily_bar")) return static_cast<int>(factor::SourceTable::DAILY_BAR);
    if (normalized == QStringLiteral("financial_indicator")) return static_cast<int>(factor::SourceTable::FINANCIAL_INDICATOR);
    if (normalized == QStringLiteral("symbol_info")) return static_cast<int>(factor::SourceTable::SYMBOL_INFO);
    if (normalized == QStringLiteral("news_sentiment") || normalized == QStringLiteral("news") || normalized == QStringLiteral("market_sentiment")) return static_cast<int>(factor::SourceTable::NEWS_SENTIMENT);
    if (normalized == QStringLiteral("policy_data") || normalized == QStringLiteral("policy")) return static_cast<int>(factor::SourceTable::POLICY_DATA);
    if (normalized == QStringLiteral("alternative_data") || normalized == QStringLiteral("alternative")) return static_cast<int>(factor::SourceTable::ALTERNATIVE_DATA);
    if (normalized == QStringLiteral("derivatives_data") || normalized == QStringLiteral("derivatives")) return static_cast<int>(factor::SourceTable::DERIVATIVES_DATA);
    return std::nullopt;
}

void normalizeEnumField(QJsonObject& object,
                        const char* key,
                        const std::function<std::optional<int>(const QString&)>& resolver)
{
    const QJsonValue rawValue = object.value(QLatin1String(key));
    if (!rawValue.isString()) {
        return;
    }
    const std::optional<int> resolved = resolver(rawValue.toString());
    if (resolved.has_value()) {
        object[QLatin1String(key)] = *resolved;
    }
}

void normalizeEnumArrayField(QJsonObject& object,
                             const char* key,
                             const std::function<std::optional<int>(const QString&)>& resolver)
{
    const QJsonValue rawValue = object.value(QLatin1String(key));
    if (!rawValue.isArray()) {
        return;
    }

    const QJsonArray rawArray = rawValue.toArray();
    QJsonArray normalizedArray;
    bool changed = false;
    for (const QJsonValue& item : rawArray) {
        if (item.isString()) {
            const std::optional<int> resolved = resolver(item.toString());
            if (resolved.has_value()) {
                normalizedArray.append(*resolved);
                changed = true;
                continue;
            }
        }
        normalizedArray.append(item);
    }
    if (changed) {
        object[QLatin1String(key)] = normalizedArray;
    }
}

foundation::json::JsonFacade jsonFacadeFromObjectForTest(const QJsonObject& object)
{
    const QByteArray jsonBytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return foundation::json::JsonFacade::parse(jsonBytes.constData());
}

QJsonArray intArrayForTest(std::initializer_list<int> values)
{
    QJsonArray array;
    for (const int value : values) {
        array.append(value);
    }
    return array;
}

foundation::json::JsonFacade parseRuntimeCompatibleTestConfig(const char* configJson)
{
    const QByteArray rawBytes(configJson);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(rawBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return foundation::json::JsonFacade::parse(configJson);
    }

    QJsonObject root = document.object();
    factor::FactorType factorType = factor::FactorType::UNKNOWN;
    if (root.contains(QStringLiteral("factorType"))) {
        const QJsonValue factorTypeValue = root.value(QStringLiteral("factorType"));
        if (factorTypeValue.isDouble()) {
            factorType = factor::factorTypeFromIndex(factorTypeValue.toInt());
        } else if (factorTypeValue.isString()) {
            factorType = parseFactorType(factorTypeValue.toString());
        }
        if (factorType != factor::FactorType::UNKNOWN) {
            root[QStringLiteral("factorType")] = factor::factorTypeIndex(factorType);
        }
    }

    if (root.contains(QStringLiteral("calculation")) && root.value(QStringLiteral("calculation")).isObject()) {
        QJsonObject calculation = root.value(QStringLiteral("calculation")).toObject();

        if (!calculation.contains(QStringLiteral("lagEnabled"))
            && calculation.contains(QStringLiteral("laggedEnabled"))) {
            calculation[QStringLiteral("lagEnabled")] = calculation.value(QStringLiteral("laggedEnabled"));
        }

        normalizeEnumField(
            calculation,
            "standardization",
            isConfigurableFactorTypeForTest(factorType) ? configurableStandardizationIndex : commonStandardizationIndex);
        normalizeEnumField(calculation, "frequency", frequencyIndex);
        normalizeEnumField(calculation, "macroFrequency", frequencyIndex);
        normalizeEnumField(calculation, "adjustPriceType", adjustPriceTypeIndex);
        normalizeEnumField(calculation, "technicalPriceType", technicalPriceTypeIndex);
        normalizeEnumField(calculation, "priceType", technicalPriceTypeIndex);
        normalizeEnumField(calculation, "sizeMetric", sizeMetricIndex);
        normalizeEnumField(calculation, "turnoverStabilityMetric", liquidityMetricIndex);
        normalizeEnumField(calculation, "sectorType", sectorTypeIndex);
        normalizeEnumField(calculation, "sentimentSource", sentimentSourceIndex);

        if (factorType == factor::FactorType::LIQUIDITY) {
            normalizeEnumField(calculation, "metric", liquidityMetricIndex);
        } else if (factorType == factor::FactorType::DIVIDEND) {
            normalizeEnumField(calculation, "metric", dividendMetricIndex);
        } else if (factorType == factor::FactorType::SENTIMENT) {
            normalizeEnumField(calculation, "metric", sentimentMetricIndex);
        } else if (factorType == factor::FactorType::INDUSTRY) {
            normalizeEnumField(calculation, "industryMetric", industryMetricIndex);
        }

        normalizeEnumArrayField(calculation, "valuationMetrics", valuationMetricIndex);
        normalizeEnumArrayField(calculation, "growthMetrics", growthMetricIndex);
        normalizeEnumArrayField(calculation, "dividendMetrics", dividendMetricIndex);
        normalizeEnumArrayField(calculation, "components", lowVolComponentIndex);
        normalizeEnumArrayField(calculation, "macroDimensions", [](const QString& value) -> std::optional<int> {
            const factor::MacroDimension dimension = factor::macroDimensionFromString(value);
            return dimension == factor::MacroDimension::UNKNOWN ? std::nullopt : std::optional<int>(static_cast<int>(dimension));
        });
        normalizeEnumArrayField(calculation, "macroIndicators", [](const QString& value) -> std::optional<int> {
            const factor::MacroIndicator indicator = factor::macroIndicatorFromString(value);
            return indicator == factor::MacroIndicator::UNKNOWN ? std::nullopt : std::optional<int>(static_cast<int>(indicator));
        });
        normalizeEnumArrayField(calculation, "technicalIndicators", [](const QString& value) -> std::optional<int> {
            const factor::TechnicalIndicator indicator = factor::technicalIndicatorFromString(value);
            return indicator == factor::TechnicalIndicator::UNKNOWN ? std::nullopt : std::optional<int>(static_cast<int>(indicator));
        });

        root[QStringLiteral("calculation")] = calculation;
    }

    if (root.contains(QStringLiteral("dataRequirements")) && root.value(QStringLiteral("dataRequirements")).isObject()) {
        QJsonObject dataRequirements = root.value(QStringLiteral("dataRequirements")).toObject();
        const QJsonValue sourceTableValue = dataRequirements.value(QStringLiteral("sourceTable"));
        if (sourceTableValue.isString()) {
            const std::optional<int> resolved = sourceTableIndex(sourceTableValue.toString());
            if (resolved.has_value()) {
                dataRequirements[QStringLiteral("sourceTable")] = *resolved;
            } else {
                dataRequirements.remove(QStringLiteral("sourceTable"));
            }
        }
        root[QStringLiteral("dataRequirements")] = dataRequirements;
    }

    const QByteArray normalizedBytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return foundation::json::JsonFacade::parse(normalizedBytes.constData());
}

bool hasResidualStringEnumConfig(const foundation::json::JsonFacade& config)
{
    if (config.has("calculation")) {
        const auto calculation = config.get("calculation");
        const std::array<const char*, 12> scalarEnumKeys{
            "standardization",
            "frequency",
            "macroFrequency",
            "adjustPriceType",
            "technicalPriceType",
            "priceType",
            "sizeMetric",
            "turnoverStabilityMetric",
            "sectorType",
            "sentimentSource",
            "metric",
            "industryMetric"};
        for (const char* key : scalarEnumKeys) {
            if (calculation.has(key) && calculation.get(key).isString()) {
                return true;
            }
        }

        const std::array<const char*, 7> arrayEnumKeys{
            "valuationMetrics",
            "growthMetrics",
            "dividendMetrics",
            "components",
            "macroDimensions",
            "macroIndicators",
            "technicalIndicators"};
        for (const char* key : arrayEnumKeys) {
            if (!calculation.has(key)) {
                continue;
            }
            const auto values = calculation.get(key);
            if (!values.isArray()) {
                continue;
            }
            for (size_t index = 0; index < values.size(); ++index) {
                if (values.at(index).isString()) {
                    return true;
                }
            }
        }
    }

    if (config.has("dataRequirements")) {
        const auto dataRequirements = config.get("dataRequirements");
        if (dataRequirements.has("sourceTable") && dataRequirements.get("sourceTable").isString()) {
            return true;
        }
    }

    return false;
}

class ExplicitConfigFactorStub : public factor::BaseFactor
{
public:
    static std::shared_ptr<factor::BaseFactor> create(const foundation::json::JsonFacade& normalizedConfig,
                                                      factor::FactorType factorType,
                                                      const QString& instanceId)
    {
        auto factor = std::make_shared<ExplicitConfigFactorStub>();
        factor->factorType_ = factorType;
        factor->name_ = instanceId.toStdString();
        factor->description_ = "test explicit config stub";
        factor->loadConfig(normalizedConfig);
        return factor;
    }

    factor::CalculationResult calculate(const factor::CalculationContext& context) override
    {
        Q_UNUSED(context)
        return factor::CalculationResult::createError("test explicit config stub");
    }

    factor::DataRequirements getDataRequirements() const override
    {
        return dataRequirements_;
    }

    factor::BoundaryRules getBoundaryRules() const override
    {
        return boundaryRules_;
    }
};

QString detectRepoRootForRegressionTests()
{
    QDir dir = QDir::current();
    for (int depth = 0; depth < 8; ++depth) {
        if (dir.exists(QStringLiteral("config")) && dir.exists(QStringLiteral("src"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }

    dir = QDir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth) {
        if (dir.exists(QStringLiteral("config")) && dir.exists(QStringLiteral("src"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }

    return QDir::currentPath();
}

void ensureCoreAppAndFoundationForRegressionTests()
{
    if (!QCoreApplication::instance()) {
        static int argc = 1;
        static char appName[] = "factor-backtest-test";
        static char* argv[] = {appName, nullptr};
        new QCoreApplication(argc, argv);
    }

    const QString repoRoot = detectRepoRootForRegressionTests();
    QDir::setCurrent(repoRoot);

    foundation::Config foundationConfig;
    foundationConfig.profile = "development";
    foundationConfig.config_dir = QDir(repoRoot).filePath(QStringLiteral("config")).toStdString();
    foundationConfig.enable_console_log = true;
    foundationConfig.enable_file_log = false;
    foundationConfig.thread_pool_size = 2;
    ASSERT_TRUE(foundation::Foundation::instance().initialize(foundationConfig));
}

}  // namespace

namespace factor::bridge::test {
factor::FactorType supportInfoRuntimeTypeForTest(const QVariantMap& supportInfo)
{
    return factor::factorTypeFromIndex(supportInfo.value(QStringLiteral("runtimeType")).toInt());
}

factor::SourceTable supportInfoSourceTableForTest(const QVariantMap& supportInfo)
{
    return static_cast<factor::SourceTable>(supportInfo.value(QStringLiteral("sourceTable")).toInt());
}

QJsonObject buildDomainConfigForTesting(const QVariantMap& factorData) {
    // 1. 取类型和参数
    const int factorTypeIndex = factorData.contains("factorType") ? factorData.value("factorType").toInt() : -1;
    const factor::FactorType factorType = factor::factorTypeFromIndex(factorTypeIndex);
    QVariantMap calculation = factorData.value("parameters").toMap();
    // 兼容 metric/source 直传
    for (auto it = factorData.constBegin(); it != factorData.constEnd(); ++it) {
        if (it.key() != "majorCategory" && it.key() != "factorType" && it.key() != "parameters") {
            calculation[it.key()] = it.value();
        }
    }

    if (factorType == factor::FactorType::QUALITY) {
        calculation["qualityThreshold"] = calculation.value("qualityThreshold", 0.1);
    }

    if (factorType == factor::FactorType::DIVIDEND) {
        calculation["minDividendYield"] = calculation.value("minDividendYield", 0);
    }

    auto profile = factor::bridge::resolveFactorRequirementProfile(factorType, calculation);

    QJsonObject calculationObj = QJsonObject::fromVariantMap(calculation);
    if (profile.metric.isValid()) {
        calculationObj["metric"] = QJsonValue::fromVariant(profile.metric);
    }

    QJsonObject requirementsObj;
    QJsonArray requiredArr;
    for (const QString& field : profile.requiredFields.orderedValues()) requiredArr.append(field);
    requirementsObj["required"] = requiredArr;
    QJsonArray optionalArr;
    for (const QString& field : profile.optionalFields.orderedValues()) optionalArr.append(field);
    requirementsObj["optional"] = optionalArr;
    if (profile.sourceTable != factor::SourceTable::UNKNOWN) {
        requirementsObj["sourceTable"] = static_cast<int>(profile.sourceTable);
    }

    QJsonObject result;
    result["factorType"] = factor::factorTypeIndex(factorType);
    result["calculation"] = calculationObj;
    result["dataRequirements"] = requirementsObj;
    return result;
}
}

class FactorServiceTestAccess
{
public:
    static FactorService* create()
    {
        return new FactorService();
    }

    static void destroy(FactorService* service)
    {
        delete service;
    }

    static void configureForRepositoryRegression(
        FactorService& service,
        const std::shared_ptr<astock::database::IFactorRepository>& repository)
    {
        service.m_repository = repository;
        service.m_database.reset();
        service.m_dataChecker.reset();
        service.m_factorCacheManager.reset();
        service.m_factorInstanceManager.reset();
        service.m_memoryCache.clear();
        service.m_initialized.store(true);
        service.m_isLoading.store(false);
        service.m_cacheLoaded.store(false);
        service.m_syncFactorDefinitionOverrideForTests = {};
        service.m_removeFactorDefinitionOverrideForTests = {};
        service.m_queryDatabaseDataOverrideForTests = {};
    }

    static void setFactorInstanceManager(FactorService& service,
                                         const std::shared_ptr<factor::FactorInstanceManager>& manager)
    {
        service.m_factorInstanceManager = manager;
    }

    static void setQueryDatabaseDataOverride(
        FactorService& service,
        std::function<QVariantList(const QString&, const QString&)> overrideFn)
    {
        service.m_queryDatabaseDataOverrideForTests = std::move(overrideFn);
    }

    static void setDomainSyncOverride(
        FactorService& service,
        std::function<bool(const QVariantMap&)> overrideFn)
    {
        service.m_syncFactorDefinitionOverrideForTests = std::move(overrideFn);
    }

    static void setDomainDeleteOverride(
        FactorService& service,
        std::function<bool(const QString&)> overrideFn)
    {
        service.m_removeFactorDefinitionOverrideForTests = std::move(overrideFn);
    }

    static const QMap<QString, QVariantMap>& memoryCache(const FactorService& service)
    {
        return service.m_memoryCache;
    }

    static QVariantList recentOperationReports(const FactorService& service)
    {
        return service.m_recentOperationReports;
    }

    static void setMemoryCache(FactorService& service,
                               const QString& factorId,
                               const QVariantMap& factorData)
    {
        service.m_memoryCache[factorId] = factorData;
    }

    static FactorService* overrideSingletonForTests(FactorService* replacement)
    {
        FactorService* previous = FactorService::m_instance;
        FactorService::m_instance = replacement;
        return previous;
    }
};

class FactorBacktestControllerTestAccess
{
public:
    static QVariantMap buildResultMap(FactorBacktestController& controller,
                                      const QString& requestedFactorId,
                                      const factor::BacktestResult& result)
    {
        return controller.buildResultMap(requestedFactorId, result);
    }

    static void syncBacktestMetricsToFactor(FactorBacktestController& controller,
                                            const QString& requestedFactorId,
                                            const factor::BacktestResult& result)
    {
        controller.syncBacktestMetricsToFactor(requestedFactorId, result);
    }

    static void setAppliedRiskConfigOverrideForSync(
        FactorBacktestController& controller,
        std::function<QVariantMap()> loader)
    {
        controller.m_loadAppliedRiskConfigOverrideForTests = std::move(loader);
    }

    static void configureSupportMapRuntime(
        FactorBacktestController& controller,
        const std::shared_ptr<factor::FactorInstanceManager>& instanceManager)
    {
        controller.m_database.reset();
        controller.m_dataChecker.reset();
        controller.m_instanceManager = instanceManager;
        controller.m_skipInstanceRefreshForTests = true;
        controller.m_resolveInstanceIdOverrideForTests = [](const QVariant& factorId) {
            return factorId.toString().trimmed();
        };
    }

    static void configureSupportMapOverrides(
        FactorBacktestController& controller,
        const factor::FactorInstanceInfo& instanceInfo,
        const std::shared_ptr<factor::BaseFactor>& factorInstance)
    {
        controller.m_database.reset();
        controller.m_dataChecker.reset();
        controller.m_instanceManager.reset();
        controller.m_skipInstanceRefreshForTests = true;
        controller.m_resolveInstanceIdOverrideForTests = [](const QVariant& factorId) {
            return factorId.toString().trimmed();
        };
        controller.m_instanceInfoOverrideForTests = [instanceInfo](const QString&) {
            return instanceInfo;
        };
        controller.m_factorInstanceOverrideForTests = [factorInstance](const QString&) {
            return factorInstance;
        };
    }

    static void configureSupportMapRuntimeAndOverrides(
        FactorBacktestController& controller,
        const std::shared_ptr<factor::FactorInstanceManager>& instanceManager,
        const factor::FactorInstanceInfo& instanceInfo,
        const std::shared_ptr<factor::BaseFactor>& factorInstance)
    {
        controller.m_database.reset();
        controller.m_dataChecker.reset();
        controller.m_instanceManager = instanceManager;
        controller.m_skipInstanceRefreshForTests = true;
        controller.m_resolveInstanceIdOverrideForTests = [](const QVariant& factorId) {
            return factorId.toString().trimmed();
        };
        controller.m_instanceInfoOverrideForTests = [instanceInfo](const QString&) {
            return instanceInfo;
        };
        controller.m_factorInstanceOverrideForTests = [factorInstance](const QString&) {
            return factorInstance;
        };
    }

    static void setRequiredWarmupTradingDays(FactorBacktestController& controller,
                                             const QString& instanceId,
                                             int requiredTradingDays)
    {
        controller.m_requiredWarmupTradingDaysOverrideForTests[instanceId] = requiredTradingDays;
    }

    static QString resolveInstanceId(FactorBacktestController& controller,
                                     const QVariant& factorId)
    {
        return controller.resolveInstanceId(factorId);
    }

    static factor::BacktestConfig buildBacktestConfig(FactorBacktestController& controller,
                                                      const QString& resolvedInstanceId,
                                                      const QString& groupText,
                                                      const QString& startDate,
                                                      const QString& endDate)
    {
        return controller.buildBacktestConfig(resolvedInstanceId, groupText, startDate, endDate);
    }

    static std::shared_ptr<factor::BaseFactor> createInstance(
        FactorBacktestController& controller,
        const QString& instanceId)
    {
        if (controller.m_factorInstanceOverrideForTests) {
            return controller.m_factorInstanceOverrideForTests(instanceId);
        }
        if (!controller.m_instanceManager) {
            return nullptr;
        }
        return controller.m_instanceManager->createInstance(instanceId.toStdString());
    }

    static void primeSingleFactorCompletionState(FactorBacktestController& controller,
                                                   const QVariantList& factorIds)
    {
        controller.m_batchFactorIds = factorIds;
        controller.m_batchResultMaps.clear();
        controller.m_batchResultMaps.resize(static_cast<size_t>(factorIds.size()));
        controller.m_activeFactorIndex = 0;
        controller.m_isRunning = true;
        controller.m_progress = 0;
        controller.m_status = QStringLiteral("正在回测");
    }

    static void primeAsyncBacktestRuntime(FactorBacktestController& controller,
                                           const std::shared_ptr<factor::FactorCacheManager>& cacheManager,
                                           const std::shared_ptr<foundation::thread::ThreadPoolExecutor>& threadPool,
                                           const std::shared_ptr<factor::FactorInstanceManager>& instanceManager)
    {
        controller.m_database.reset();
        controller.m_dataChecker.reset();
        controller.m_instanceManager = instanceManager;
        controller.m_cacheManager = cacheManager;
        controller.m_threadPool = threadPool;
        controller.m_executor = std::make_unique<factor::FactorBacktestExecutor>(controller.m_instanceManager, threadPool, cacheManager);
        controller.m_skipInstanceRefreshForTests = true;
    }

    static void finalizeBacktestSuccess(FactorBacktestController& controller,
                                        const QString& requestedFactorId,
                                        const factor::BacktestResult& result,
                                        size_t batchIndex)
    {
        controller.finalizeBacktestSuccess(requestedFactorId, result, batchIndex);
    }

    static bool hasInitializedRuntime(const FactorBacktestController& controller)
    {
        return controller.m_database
            || controller.m_dataChecker
            || controller.m_instanceManager
            || controller.m_executor;
    }
};

class DataServiceCacheTestAccess
{
public:
    static void resetInMemoryIndex(DataServiceCache& cache)
    {
        QMutexLocker locker(&cache.m_indexMutex);
        cache.m_nextDataSetId = 1;
        cache.m_dataSetIndex.clear();
        cache.m_nameToIdIndex.clear();
        cache.m_stockCodeIndex.clear();
        cache.m_sourceTypeIndex.clear();
        cache.m_indexNeedsRebuild = true;
    }

    static void removeRawCacheKey(DataServiceCache& cache, const QString& key)
    {
        cache.m_cacheFacade->remove(key.toStdString());
    }
};

namespace {

using factor::CalculationContext;
using factor::CalculationResult;
using factor::FactorBacktestExecutor;
using factor::FactorCacheManager;
using factor::BacktestConfig;
using factor::BacktestResult;
using factor::MomentumFactor;
using factor::bridge::BacktestPreflightFailure;
using factor::bridge::FactorDomainExistingRecord;
using factor::bridge::FactorDomainSyncWritePlan;
using factor::bridge::FactorInstanceLookupCandidates;
using factor::bridge::FactorInstanceLookupRecord;

} // namespace

namespace factor {

class TestConfigurableFactorHarness final : public ConfigurableFactorBase
{
public:
    TestConfigurableFactorHarness()
        : ConfigurableFactorBase(FactorType::CUSTOM)
    {
    }

    CalculationResult calculate(const CalculationContext& context) override
    {
        if (!context.historicalView) {
            return createHistoricalViewRuntimeError(
                context,
                QStringLiteral("已移除因子运行期数据库取数路径，请由引擎提供 HistoricalView").toStdString());
        }

        switch (factorType_) {
        case FactorType::GROWTH:
            return calculateGrowth(context);
        case FactorType::LIQUIDITY:
            return calculateLiquidity(context);
        case FactorType::TECHNICAL:
            return calculateTechnical(context);
        case FactorType::DIVIDEND:
            return calculateDividend(context);
        case FactorType::MACRO:
            return calculateMacro(context);
        case FactorType::INDUSTRY:
            return calculateIndustry(context);
        case FactorType::SENTIMENT:
            return calculateSentiment(context);
        case FactorType::CUSTOM:
            return calculateCustom(context);
        default:
            throw std::runtime_error("unsupported configurable factor type in regression test harness");
        }
    }

    static std::shared_ptr<TestConfigurableFactorHarness> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker)
    {
        return create(
            info.instanceId,
            info.instanceName,
            info.description,
            info.config,
            info.factorType,
            std::move(dataChecker));
    }

    static std::shared_ptr<TestConfigurableFactorHarness> create(
        const std::string& instanceId,
        const std::string& instanceName,
        const std::string& description,
        const foundation::json::JsonFacade& config,
        FactorType fallbackFactorType,
        std::shared_ptr<DataAvailabilityChecker> dataChecker)
    {
        auto factor = std::make_shared<TestConfigurableFactorHarness>();
        factor->dataChecker_ = std::move(dataChecker);
        factor->instanceId_ = instanceId;
        factor->name_ = instanceName;
        factor->description_ = description;
        factor->factorType_ = fallbackFactorType;
        if (config.has("factorType")) {
            FactorType parsedType = FactorType::UNKNOWN;
            const auto factorTypeValue = config.get("factorType");
            if (factorTypeValue.isNumber()) {
                parsedType = factorTypeFromIndex(factorTypeValue.asInt());
            } else if (factorTypeValue.isString()) {
                parsedType = parseFactorType(QString::fromStdString(factorTypeValue.asString()));
            }
            if (parsedType != FactorType::UNKNOWN) {
                factor->factorType_ = parsedType;
            }
        }
        factor->loadConfig(config);
        return factor;
    }
};

} // namespace factor

#define ConfigurableFactorBase TestConfigurableFactorHarness

namespace factor {

class ConfigurableFactorTestAccess
{
public:
    static QString growthMetricToString(factor::GrowthMetric metric)
    {
        switch (metric) {
        case factor::GrowthMetric::REVENUE_GROWTH:
            return QStringLiteral("revenue_growth");
        case factor::GrowthMetric::NET_PROFIT_GROWTH:
            return QStringLiteral("net_profit_growth");
        case factor::GrowthMetric::DELTA_ROE:
            return QStringLiteral("delta_roe");
        case factor::GrowthMetric::SUE:
            return QStringLiteral("sue");
        default:
            return {};
        }
    }

    static QString dividendMetricToString(factor::DividendMetric metric)
    {
        switch (metric) {
        case factor::DividendMetric::DIVIDEND_YIELD:
            return QStringLiteral("dividend_yield");
        case factor::DividendMetric::PAYOUT_RATIO:
            return QStringLiteral("payout_ratio");
        case factor::DividendMetric::DIVIDEND_STABILITY:
            return QStringLiteral("dividend_stability");
        default:
            return {};
        }
    }

    static QString factorTypeToString(factor::FactorType type)
    {
        switch (type) {
        case factor::FactorType::VALUE:
            return QStringLiteral("value");
        case factor::FactorType::MOMENTUM:
            return QStringLiteral("momentum");
        case factor::FactorType::SIZE:
            return QStringLiteral("size");
        case factor::FactorType::QUALITY:
            return QStringLiteral("quality");
        case factor::FactorType::GROWTH:
            return QStringLiteral("growth");
        case factor::FactorType::DIVIDEND:
            return QStringLiteral("dividend");
        case factor::FactorType::TECHNICAL:
            return QStringLiteral("technical");
        case factor::FactorType::LIQUIDITY:
            return QStringLiteral("liquidity");
        case factor::FactorType::MACRO:
            return QStringLiteral("macro");
        case factor::FactorType::INDUSTRY:
            return QStringLiteral("industry");
        case factor::FactorType::SENTIMENT:
            return QStringLiteral("sentiment");
        case factor::FactorType::CUSTOM:
            return QStringLiteral("custom");
        case factor::FactorType::LOW_VOLATILITY:
            return QStringLiteral("low_volatility");
        default:
            return QStringLiteral("unknown");
        }
    }

    static void loadConfig(ConfigurableFactorBase& factor,
                           const foundation::json::JsonFacade& config)
    {
        const std::string rawConfig = config.toString();
        const foundation::json::JsonFacade normalizedConfig = parseRuntimeCompatibleTestConfig(rawConfig.c_str());
        if (normalizedConfig.has("factorType")) {
            FactorType parsedType = FactorType::UNKNOWN;
            const auto factorTypeValue = normalizedConfig.get("factorType");
            if (factorTypeValue.isNumber()) {
                parsedType = factorTypeFromIndex(factorTypeValue.asInt());
            } else if (factorTypeValue.isString()) {
                parsedType = parseFactorType(QString::fromStdString(factorTypeValue.asString()));
            }
            if (parsedType != FactorType::UNKNOWN) {
                factor.factorType_ = parsedType;
            }
        }
        factor.loadConfig(normalizedConfig);
    }

    static QString normalizedType(const ConfigurableFactorBase& factor)
    {
        return factorTypeToString(factor.configuredFactorType());
    }

    static QString normalizedMetric(const ConfigurableFactorBase& factor)
    {
        const QString type = factorTypeToString(factor.configuredFactorType());
        if (type == QStringLiteral("growth")) {
            const auto* params = std::get_if<ConfigurableFactorBase::GrowthParams>(&factor.specificParams_);
            if (params && !params->growthMetrics.empty()
                && params->growthMetrics.size() == params->growthWeights.size()) {
                return growthMetricToString(params->growthMetrics.front());
            }
            return {};
        }
        if (type == QStringLiteral("dividend")) {
            const auto* params = std::get_if<ConfigurableFactorBase::DividendParams>(&factor.specificParams_);
            if (params && !params->dividendMetrics.empty()) {
                return dividendMetricToString(params->dividendMetrics.front());
            }
            return {};
        }
        return {};
    }
};

class ValueFactorTestAccess
{
public:
    static void loadConfig(ValueFactor& factor,
                           const foundation::json::JsonFacade& config)
    {
        const std::string rawConfig = config.toString();
        factor.loadConfig(parseRuntimeCompatibleTestConfig(rawConfig.c_str()));
    }
};

class SizeFactorTestAccess
{
public:
    static void loadConfig(SizeFactor& factor,
                           const foundation::json::JsonFacade& config)
    {
        const std::string rawConfig = config.toString();
        factor.loadConfig(parseRuntimeCompatibleTestConfig(rawConfig.c_str()));
    }
};

class LowVolFactorTestAccess
{
public:
    static void loadConfig(LowVolFactor& factor,
                           const foundation::json::JsonFacade& config)
    {
        const std::string rawConfig = config.toString();
        factor.loadConfig(parseRuntimeCompatibleTestConfig(rawConfig.c_str()));
    }
};

class QualityFactorTestAccess
{
public:
    static void loadConfig(QualityFactor& factor,
                           const foundation::json::JsonFacade& config)
    {
        const std::string rawConfig = config.toString();
        factor.loadConfig(parseRuntimeCompatibleTestConfig(rawConfig.c_str()));
    }
};

class FactorInstanceManagerTestAccess
{
public:
    static void seedInstance(FactorInstanceManager& manager,
                             const QString& instanceId,
                             const FactorInstanceInfo& info,
                             const std::shared_ptr<BaseFactor>& factor)
    {
        std::lock_guard<std::mutex> lock(manager.cacheMutex_);
        manager.infoCache_[instanceId.toStdString()] = info;
        manager.instanceCache_[instanceId.toStdString()] = factor;
    }
};

} // namespace factor

namespace {

class InMemoryFactorRepository : public astock::database::IFactorRepository
{
public:
    QVariantMap findById(const QString& factorId) override
    {
        return records.value(factorId);
    }

    std::vector<QVariantMap> findAll() override
    {
        std::vector<QVariantMap> result;
        result.reserve(static_cast<size_t>(records.size()));
        for (auto it = records.cbegin(); it != records.cend(); ++it) {
            result.push_back(it.value());
        }
        return result;
    }

    std::vector<QVariantMap> findByType(const QString&) override
    {
        return findAll();
    }

    std::vector<QVariantMap> findByCategory(const QString&) override
    {
        return findAll();
    }

    std::vector<QVariantMap> findByTags(const QStringList&) override
    {
        return findAll();
    }

    std::vector<QVariantMap> search(const QString&) override
    {
        return findAll();
    }

    bool save(const QVariantMap& factor) override
    {
        const QString factorId = factor.value("factorId").toString().trimmed();
        if (factorId.isEmpty()) {
            return false;
        }

        ++saveCalls;
        records[factorId] = factor;
        return true;
    }

    size_t saveBatch(const std::vector<QVariantMap>& factors) override
    {
        size_t saved = 0;
        for (const QVariantMap& factor : factors) {
            if (save(factor)) {
                ++saved;
            }
        }
        return saved;
    }

    bool update(const QString& factorId, const QVariantMap& factor) override
    {
        ++updateCalls;
        updateHistory.append(factor);

        if (!records.contains(factorId)) {
            return false;
        }

        QVariantMap persisted = factor;
        persisted["factorId"] = factorId;
        records[factorId] = persisted;
        return true;
    }

    bool remove(const QString& factorId) override
    {
        ++removeCalls;
        removedIds.append(factorId);
        return records.remove(factorId) > 0;
    }

    size_t count() override
    {
        return static_cast<size_t>(records.size());
    }

    bool exists(const QString& factorId) override
    {
        return records.contains(factorId);
    }

    bool initialize() override
    {
        initialized = true;
        return true;
    }

    bool clearAll() override
    {
        records.clear();
        return true;
    }

    QMap<QString, QVariantMap> records;
    QVector<QVariantMap> updateHistory;
    QStringList removedIds;
    int saveCalls = 0;
    int updateCalls = 0;
    int removeCalls = 0;
    bool initialized = false;
};

std::unique_ptr<FactorService, void(*)(FactorService*)> makeTestFactorService()
{
    return {FactorServiceTestAccess::create(), &FactorServiceTestAccess::destroy};
}

class ScopedFactorServiceSingletonOverride
{
public:
    explicit ScopedFactorServiceSingletonOverride(FactorService* service)
        : previous_(FactorServiceTestAccess::overrideSingletonForTests(service))
    {
    }

    ~ScopedFactorServiceSingletonOverride()
    {
        FactorServiceTestAccess::overrideSingletonForTests(previous_);
    }

private:
    FactorService* previous_;
};

QVariantMap makeValidFactorRecord(const QString& factorId,
                                  const QString& factorName,
                                  const QString& displayName)
{
    return {
        {"factorId", factorId},
        {"factorName", factorName},
        {"displayName", displayName},
        {"majorCategory", QString::fromUtf8("质量因子")},
        {"description", QString::fromUtf8("测试用因子")},
        {"status", "ACTIVE"}
    };
}

QVariantMap makeGroupResult(int groupId, double groupReturn)
{
    QVariantMap group;
    group["groupId"] = groupId;
    group["groupName"] = QString("第%1组").arg(groupId);
    group["return"] = groupReturn;
    group["stockCount"] = 12;
    group["minFactorValue"] = -0.5;
    group["maxFactorValue"] = 1.5;
    return group;
}

QVariantMap makeSingleFactorResult(const QString& factorId,
                                   const QString& factorName,
                                   double icValue,
                                   double spreadReturn,
                                   int executionTime)
{
    QVariantMap config;
    config["factorId"] = factorId;
    config["factorName"] = factorName;
    config["instanceId"] = factorId + "_instance";
    config["startDate"] = "2024-01-01";
    config["endDate"] = "2024-03-31";
    config["numGroups"] = 10;
    config["forwardDays"] = 1;
    config["transactionCost"] = 0.001;

    QVariantMap result;
    result["taskId"] = factorId + "_task";
    result["executionTime"] = executionTime;
    result["success"] = true;
    result["status"] = "SUCCESS";
    result["config"] = config;
    result["groups"] = QVariantList{makeGroupResult(1, 0.031), makeGroupResult(2, 0.014)};
    result["icirResult"] = QVariantMap{{"icValue", icValue}, {"irValue", 0.45}, {"icPositiveRate", 0.63}};
    result["summary"] = QVariantMap{{"spreadReturn", spreadReturn}, {"dataCoverage", 0.94}, {"sharpeRatio", 1.12}};
    return result;
}

QVariantMap makeBatchResult()
{
    const QVariantMap first = makeSingleFactorResult("factor_quality", "质量因子", 0.072, 0.028, 120);
    const QVariantMap second = makeSingleFactorResult("factor_momentum", "动量因子", 0.055, 0.019, 180);

    QVariantMap aggregate = first;
    aggregate["results"] = QVariantList{first, second};
    aggregate["factorIds"] = QVariantList{QStringLiteral("factor_quality"), QStringLiteral("factor_momentum")};
    aggregate["factorCount"] = 2;
    aggregate["executionTime"] = 300;
    return aggregate;
}

QString writeResultFile(const QVariantMap& result)
{
    QTemporaryDir dir;
    EXPECT_TRUE(dir.isValid());
    const QString filePath = dir.filePath("factor_backtest_result.json");

    QFile file(filePath);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QJsonDocument document(QJsonObject::fromVariantMap(result));
    file.write(document.toJson(QJsonDocument::Indented));
    file.close();

    // Keep the file alive after this helper returns by disabling auto-remove on the directory.
    dir.setAutoRemove(false);
    return filePath;
}

QString writeRawFile(const QByteArray& content)
{
    QTemporaryDir dir;
    EXPECT_TRUE(dir.isValid());
    const QString filePath = dir.filePath("factor_backtest_result.json");

    QFile file(filePath);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
    file.close();

    dir.setAutoRemove(false);
    return filePath;
}

bool jsonArrayContains(const QJsonArray& values, const QString& expected)
{
    for (const QJsonValue& value : values) {
        if (value.toString() == expected) {
            return true;
        }
    }
    return false;
}

std::vector<double> collectWindowValues(const std::vector<HistoricalDataPoint>& series,
                                        const std::string& anchorDate,
                                        int window)
{
    std::vector<double> values;
    if (window <= 0 || series.empty()) {
        return values;
    }

    const auto anchorIt = std::upper_bound(
        series.begin(),
        series.end(),
        anchorDate,
        [](const std::string& date, const HistoricalDataPoint& point) {
            return date < point.date;
        });

    const size_t endIndex = static_cast<size_t>(std::distance(series.begin(), anchorIt));
    if (endIndex == 0) {
        return values;
    }

    const size_t startIndex = endIndex > static_cast<size_t>(window)
        ? endIndex - static_cast<size_t>(window)
        : size_t{0};

    values.reserve(endIndex - startIndex);
    for (size_t index = startIndex; index < endIndex; ++index) {
        values.push_back(series[index].value);
    }
    return values;
}

class StubFactorDataProvider : public HistoricalView
{
public:
    explicit StubFactorDataProvider(std::unordered_map<std::string, std::vector<HistoricalDataPoint>> seriesBySymbol)
        : seriesBySymbol_(std::move(seriesBySymbol))
    {
    }

    bool hasField(const std::string& field) const override
    {
        return field == "close"
            || field == "adj_factor"
            || field == "pre_adjust_factor"
            || field == "post_adjust_factor";
    }

    std::optional<double> getValue(const std::string& symbol,
                                   const std::string& date,
                                   const std::string& field) const override
    {
        if (field != "close"
            && field != "adj_factor"
            && field != "pre_adjust_factor"
            && field != "post_adjust_factor") {
            return std::nullopt;
        }

        const auto it = seriesBySymbol_.find(symbol);
        if (it == seriesBySymbol_.end()) {
            return std::nullopt;
        }

        for (const auto& point : it->second) {
            if (point.date == date) {
                return field == "close" ? point.value : 1.0;
            }
        }
        return std::nullopt;
    }

    std::vector<HistoricalDataPoint> getSeries(const std::string& symbol,
                                               const std::string& startDate,
                                               const std::string& endDate,
                                               const std::string& field) const override
    {
        lastRequestedSymbol = symbol;
        lastRequestedStartDate = startDate;
        lastRequestedEndDate = endDate;
        lastRequestedField = field;

        std::vector<HistoricalDataPoint> filtered;
        if (field != "close"
            && field != "adj_factor"
            && field != "pre_adjust_factor"
            && field != "post_adjust_factor") {
            return filtered;
        }

        const auto it = seriesBySymbol_.find(symbol);
        if (it == seriesBySymbol_.end()) {
            return filtered;
        }

        for (const auto& point : it->second) {
            if ((!startDate.empty() && point.date < startDate) ||
                (!endDate.empty() && point.date > endDate)) {
                continue;
            }
            filtered.push_back(HistoricalDataPoint{point.date, field == "close" ? point.value : 1.0});
        }
        return filtered;
    }

    std::vector<std::string> getAvailableSymbols(const std::string& date) const override
    {
        std::vector<std::string> symbols;
        for (const auto& [symbol, series] : seriesBySymbol_) {
            for (const auto& point : series) {
                if (point.date == date) {
                    symbols.push_back(symbol);
                    break;
                }
            }
        }
        return symbols;
    }

    std::unordered_map<std::string, double> getCrossSection(const std::string& date,
                                                            const std::string& field,
                                                            const std::vector<std::string>& symbols = {}) const override
    {
        std::unordered_map<std::string, double> values;
        if (field != "close"
            && field != "adj_factor"
            && field != "pre_adjust_factor"
            && field != "post_adjust_factor") {
            return values;
        }

        if (!symbols.empty()) {
            for (const auto& symbol : symbols) {
                const auto value = getValue(symbol, date, field);
                if (value.has_value()) {
                    values.emplace(symbol, *value);
                }
            }
            return values;
        }

        for (const auto& [symbol, series] : seriesBySymbol_) {
            for (const auto& point : series) {
                if (point.date == date) {
                    values.emplace(symbol, field == "close" ? point.value : 1.0);
                    break;
                }
            }
        }
        return values;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, double>> getBatchCrossSections(
        const std::string& date,
        const std::vector<std::string>& symbols,
        const std::vector<std::string>& fields) const override
    {
        std::unordered_map<std::string, std::unordered_map<std::string, double>> batchValues;
        for (const auto& field : fields) {
            batchValues[field] = getCrossSection(date, field, symbols);
        }
        return batchValues;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& fields) const override
    {
        std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchSeries;
        for (const auto& field : fields) {
            auto& fieldSeries = batchSeries[field];
            for (const auto& symbol : symbols) {
                const auto series = getSeries(symbol, startDate, endDate, field);
                auto& values = fieldSeries[symbol];
                values.reserve(series.size());
                for (const auto& point : series) {
                    values.push_back(point.value);
                }
            }
        }
        return batchSeries;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& anchorDate,
        int window,
        const std::vector<std::string>& fields) const override
    {
        std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchSeries;
        for (const auto& field : fields) {
            auto& fieldSeries = batchSeries[field];
            if (field != "close"
                && field != "adj_factor"
                && field != "pre_adjust_factor"
                && field != "post_adjust_factor") {
                continue;
            }
            for (const auto& symbol : symbols) {
                const auto it = seriesBySymbol_.find(symbol);
                if (it == seriesBySymbol_.end()) {
                    continue;
                }
                fieldSeries[symbol] = collectWindowValues(it->second, anchorDate, window);
                if (field != "close") {
                    auto& values = fieldSeries[symbol];
                    std::fill(values.begin(), values.end(), 1.0);
                }
            }
        }
        return batchSeries;
    }

    mutable std::string lastRequestedSymbol;
    mutable std::string lastRequestedStartDate;
    mutable std::string lastRequestedEndDate;
    mutable std::string lastRequestedField;

private:
    std::unordered_map<std::string, std::vector<HistoricalDataPoint>> seriesBySymbol_;
};

class MultiFieldFactorDataProvider : public HistoricalView
{
public:
    explicit MultiFieldFactorDataProvider(
        std::unordered_map<std::string, std::unordered_map<std::string, double>> fieldValues)
        : fieldValues_(std::move(fieldValues))
    {
    }

    bool hasField(const std::string& field) const override
    {
        return fieldValues_.find(field) != fieldValues_.end();
    }

    std::optional<double> getValue(const std::string& symbol,
                                   const std::string& date,
                                   const std::string& field) const override
    {
        (void)date;
        const auto fieldIt = fieldValues_.find(field);
        if (fieldIt == fieldValues_.end()) {
            return std::nullopt;
        }

        const auto symbolIt = fieldIt->second.find(symbol);
        if (symbolIt == fieldIt->second.end()) {
            return std::nullopt;
        }
        return symbolIt->second;
    }

    std::vector<HistoricalDataPoint> getSeries(const std::string& symbol,
                                               const std::string& startDate,
                                               const std::string& endDate,
                                               const std::string& field) const override
    {
        (void)symbol;
        (void)startDate;
        (void)endDate;
        (void)field;
        return {};
    }

    std::vector<std::string> getAvailableSymbols(const std::string& date) const override
    {
        (void)date;
        std::unordered_set<std::string> symbols;
        for (const auto& [field, values] : fieldValues_) {
            (void)field;
            for (const auto& [symbol, value] : values) {
                (void)value;
                symbols.insert(symbol);
            }
        }
        return std::vector<std::string>(symbols.begin(), symbols.end());
    }

    std::unordered_map<std::string, double> getCrossSection(
        const std::string& date,
        const std::string& field,
        const std::vector<std::string>& symbols = {}) const override
    {
        (void)date;
        std::unordered_map<std::string, double> values;
        const auto fieldIt = fieldValues_.find(field);
        if (fieldIt == fieldValues_.end()) {
            return values;
        }

        if (symbols.empty()) {
            return fieldIt->second;
        }

        for (const auto& symbol : symbols) {
            const auto symbolIt = fieldIt->second.find(symbol);
            if (symbolIt != fieldIt->second.end()) {
                values.emplace(symbolIt->first, symbolIt->second);
            }
        }
        return values;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, double>> getBatchCrossSections(
        const std::string& date,
        const std::vector<std::string>& symbols,
        const std::vector<std::string>& fields) const override
    {
        std::unordered_map<std::string, std::unordered_map<std::string, double>> batchValues;
        for (const auto& field : fields) {
            batchValues[field] = getCrossSection(date, field, symbols);
        }
        return batchValues;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& fields) const override
    {
        (void)startDate;
        (void)endDate;
        std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchSeries;
        for (const auto& field : fields) {
            auto& fieldSeries = batchSeries[field];
            for (const auto& symbol : symbols) {
                const auto it = fieldValues_.find(field);
                if (it == fieldValues_.end()) {
                    continue;
                }
                const auto symbolIt = it->second.find(symbol);
                if (symbolIt == it->second.end()) {
                    continue;
                }
                fieldSeries[symbol] = {symbolIt->second};
            }
        }
        return batchSeries;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& anchorDate,
        int window,
        const std::vector<std::string>& fields) const override
    {
        (void)anchorDate;
        (void)window;
        std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchSeries;
        for (const auto& field : fields) {
            auto& fieldSeries = batchSeries[field];
            const auto fieldIt = fieldValues_.find(field);
            if (fieldIt == fieldValues_.end()) {
                continue;
            }
            for (const auto& symbol : symbols) {
                const auto symbolIt = fieldIt->second.find(symbol);
                if (symbolIt == fieldIt->second.end()) {
                    continue;
                }
                fieldSeries[symbol] = {symbolIt->second};
            }
        }
        return batchSeries;
    }

private:
    std::unordered_map<std::string, std::unordered_map<std::string, double>> fieldValues_;
};

class DatedMultiFieldFactorDataProvider : public HistoricalView
{
public:
    using FieldSeriesMap = std::unordered_map<std::string,
        std::unordered_map<std::string, std::vector<HistoricalDataPoint>>>;

    explicit DatedMultiFieldFactorDataProvider(FieldSeriesMap fieldSeries)
        : fieldSeries_(std::move(fieldSeries))
    {
    }

    bool hasField(const std::string& field) const override
    {
        return fieldSeries_.find(field) != fieldSeries_.end();
    }

    std::optional<double> getValue(const std::string& symbol,
                                   const std::string& date,
                                   const std::string& field) const override
    {
        const auto fieldIt = fieldSeries_.find(field);
        if (fieldIt == fieldSeries_.end()) {
            return std::nullopt;
        }

        const auto symbolIt = fieldIt->second.find(symbol);
        if (symbolIt == fieldIt->second.end()) {
            return std::nullopt;
        }

        for (const auto& point : symbolIt->second) {
            if (point.date == date) {
                return point.value;
            }
        }
        return std::nullopt;
    }

    std::vector<HistoricalDataPoint> getSeries(const std::string& symbol,
                                               const std::string& startDate,
                                               const std::string& endDate,
                                               const std::string& field) const override
    {
        std::vector<HistoricalDataPoint> filtered;
        const auto fieldIt = fieldSeries_.find(field);
        if (fieldIt == fieldSeries_.end()) {
            return filtered;
        }

        const auto symbolIt = fieldIt->second.find(symbol);
        if (symbolIt == fieldIt->second.end()) {
            return filtered;
        }

        for (const auto& point : symbolIt->second) {
            if ((!startDate.empty() && point.date < startDate)
                || (!endDate.empty() && point.date > endDate)) {
                continue;
            }
            filtered.push_back(point);
        }
        return filtered;
    }

    std::vector<std::string> getAvailableSymbols(const std::string& date) const override
    {
        std::unordered_set<std::string> symbols;
        for (const auto& [field, symbolSeries] : fieldSeries_) {
            (void)field;
            for (const auto& [symbol, series] : symbolSeries) {
                for (const auto& point : series) {
                    if (point.date == date) {
                        symbols.insert(symbol);
                        break;
                    }
                }
            }
        }
        return std::vector<std::string>(symbols.begin(), symbols.end());
    }

    std::unordered_map<std::string, double> getCrossSection(
        const std::string& date,
        const std::string& field,
        const std::vector<std::string>& symbols = {}) const override
    {
        std::unordered_map<std::string, double> values;
        const auto fieldIt = fieldSeries_.find(field);
        if (fieldIt == fieldSeries_.end()) {
            return values;
        }

        const auto collectSymbol = [&](const std::string& symbol) {
            const auto symbolIt = fieldIt->second.find(symbol);
            if (symbolIt == fieldIt->second.end()) {
                return;
            }
            for (const auto& point : symbolIt->second) {
                if (point.date == date) {
                    values.emplace(symbol, point.value);
                    break;
                }
            }
        };

        if (!symbols.empty()) {
            for (const auto& symbol : symbols) {
                collectSymbol(symbol);
            }
            return values;
        }

        for (const auto& [symbol, series] : fieldIt->second) {
            (void)series;
            collectSymbol(symbol);
        }
        return values;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, double>> getBatchCrossSections(
        const std::string& date,
        const std::vector<std::string>& symbols,
        const std::vector<std::string>& fields) const override
    {
        std::unordered_map<std::string, std::unordered_map<std::string, double>> batchValues;
        for (const auto& field : fields) {
            batchValues[field] = getCrossSection(date, field, symbols);
        }
        return batchValues;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& fields) const override
    {
        std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchSeries;
        for (const auto& field : fields) {
            auto& fieldSeries = batchSeries[field];
            for (const auto& symbol : symbols) {
                const auto series = getSeries(symbol, startDate, endDate, field);
                auto& values = fieldSeries[symbol];
                values.reserve(series.size());
                for (const auto& point : series) {
                    values.push_back(point.value);
                }
            }
        }
        return batchSeries;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& anchorDate,
        int window,
        const std::vector<std::string>& fields) const override
    {
        std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchSeries;
        for (const auto& field : fields) {
            auto& fieldSeries = batchSeries[field];
            const auto fieldIt = fieldSeries_.find(field);
            if (fieldIt == fieldSeries_.end()) {
                continue;
            }
            for (const auto& symbol : symbols) {
                const auto symbolIt = fieldIt->second.find(symbol);
                if (symbolIt == fieldIt->second.end()) {
                    continue;
                }
                fieldSeries[symbol] = collectWindowValues(symbolIt->second, anchorDate, window);
            }
        }
        return batchSeries;
    }

private:
    FieldSeriesMap fieldSeries_;
};

std::shared_ptr<StubFactorDataProvider> makeCloseSeriesProvider(
    std::initializer_list<std::pair<const char*, double>> points)
{
    std::vector<HistoricalDataPoint> series;
    series.reserve(points.size());
    for (const auto& [date, value] : points) {
        series.push_back(HistoricalDataPoint{date, value});
    }
    return std::make_shared<StubFactorDataProvider>(
        std::unordered_map<std::string, std::vector<HistoricalDataPoint>>{{"AAA", std::move(series)}});
}

factor::FactorInstanceInfo makeFactorInstanceInfo(const QString& instanceId,
                                                  const QString& factorType,
                                                  const char* configJson)
{
    factor::FactorInstanceInfo info;
    info.instanceId = instanceId.toStdString();
    info.instanceName = instanceId.toStdString();
    info.description = "test instance";
    info.factorType = parseFactorType(factorType);
    info.isAvailable = true;
    info.dataStatus.availability = factor::DataAvailability::AVAILABLE;
    info.dataStatus.coverage = 1.0;
    info.dataStatus.message = "test ready";
    info.config = parseRuntimeCompatibleTestConfig(configJson);
    if (info.factorType == factor::FactorType::UNKNOWN && info.config.has("factorType")) {
        const auto factorTypeValue = info.config.get("factorType");
        if (factorTypeValue.isString()) {
            info.factorType = parseFactorType(QString::fromStdString(factorTypeValue.asString()));
        } else if (factorTypeValue.isNumber()) {
            info.factorType = factor::factorTypeFromIndex(factorTypeValue.asInt());
        }
    }
    return info;
}

std::shared_ptr<factor::BaseFactor> makeConfiguredFactor(const char* configJson)
{
    const foundation::json::JsonFacade normalizedConfig = parseRuntimeCompatibleTestConfig(configJson);
    factor::FactorType factorType = factor::FactorType::UNKNOWN;
    if (normalizedConfig.has("factorType")) {
        const auto factorTypeValue = normalizedConfig.get("factorType");
        if (factorTypeValue.isNumber()) {
            factorType = factor::factorTypeFromIndex(factorTypeValue.asInt());
        } else if (factorTypeValue.isString()) {
            factorType = parseFactorType(QString::fromStdString(factorTypeValue.asString()));
        }
    }

    try {
        auto factor = std::make_shared<factor::ConfigurableFactorBase>();
        factor::ConfigurableFactorTestAccess::loadConfig(*factor, normalizedConfig);
        return factor;
    } catch (const std::exception&) {
        if (factorType == factor::FactorType::CUSTOM || normalizedConfig.has("dataRequirements")) {
            return ExplicitConfigFactorStub::create(normalizedConfig, factorType, QStringLiteral("test_configurable_stub"));
        }
        if (!hasResidualStringEnumConfig(normalizedConfig)
            && factorType != factor::FactorType::UNKNOWN) {
            return ExplicitConfigFactorStub::create(normalizedConfig, factorType, QStringLiteral("test_configurable_stub"));
        }
        return nullptr;
    }
}

struct RealFactorReplayCandidate
{
    std::string instanceId;
    QString instanceName;
    QString majorCategory;
    foundation::json::JsonFacade config;
};

class ScopedTemporaryFactorInstance
{
public:
    ScopedTemporaryFactorInstance(std::shared_ptr<astock::database::QtMySQLDatabase> database,
                                 QString instanceId)
        : database_(std::move(database)), instanceId_(std::move(instanceId))
    {
    }

    ~ScopedTemporaryFactorInstance()
    {
        if (!database_ || instanceId_.isEmpty()) {
            return;
        }
        database_->executeUpdate(
            QStringLiteral("DELETE FROM factor_instance WHERE instance_id = :instance_id"),
            {{QStringLiteral(":instance_id"), instanceId_}});
    }

private:
    std::shared_ptr<astock::database::QtMySQLDatabase> database_;
    QString instanceId_;
};

class ScopedTemporaryFactorDefinition
{
public:
    ScopedTemporaryFactorDefinition(std::shared_ptr<astock::database::QtMySQLDatabase> database,
                                    QString factorId)
        : database_(std::move(database)), factorId_(std::move(factorId))
    {
    }

    ~ScopedTemporaryFactorDefinition()
    {
        if (!database_ || factorId_.isEmpty()) {
            return;
        }
        database_->executeUpdate(
            QStringLiteral("DELETE FROM factors WHERE factor_id = :factor_id"),
            {{QStringLiteral(":factor_id"), factorId_}});
    }

private:
    std::shared_ptr<astock::database::QtMySQLDatabase> database_;
    QString factorId_;
};

struct RealFactorReplayHandle
{
    std::optional<RealFactorReplayCandidate> candidate;
    std::unique_ptr<ScopedTemporaryFactorDefinition> tempDefinition;
    std::unique_ptr<ScopedTemporaryFactorInstance> tempInstance;
};

std::optional<QString> loadFactorDefinitionIdByCategory(
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    const QString& majorCategory)
{
    if (!database) {
        return std::nullopt;
    }

    const auto result = database->executeQuery(
        QStringLiteral("SELECT factor_id FROM factors WHERE major_category = :major_category ORDER BY factor_id LIMIT 1"),
        {{QStringLiteral(":major_category"), majorCategory}});
    if (result.isEmpty()) {
        return std::nullopt;
    }
    const QString factorId = result.getRow(0).getString("factor_id");
    if (factorId.isEmpty()) {
        return std::nullopt;
    }
    return factorId;
}

QString normalizeCategoryToIdSegment(const QString& majorCategory)
{
    if (majorCategory == QStringLiteral("规模因子")) {
        return QStringLiteral("size");
    }
    if (majorCategory == QStringLiteral("成长因子")) {
        return QStringLiteral("growth");
    }
    if (majorCategory == QStringLiteral("红利因子")) {
        return QStringLiteral("dividend");
    }
    if (majorCategory == QStringLiteral("技术因子")) {
        return QStringLiteral("technical");
    }
    if (majorCategory == QStringLiteral("情绪因子")) {
        return QStringLiteral("sentiment");
    }
    if (majorCategory == QStringLiteral("自定义因子")) {
        return QStringLiteral("custom");
    }
    if (majorCategory == QStringLiteral("宏观因子")) {
        return QStringLiteral("macro");
    }
    if (majorCategory == QStringLiteral("行业因子")) {
        return QStringLiteral("industry");
    }
    return QStringLiteral("configurable");
}

std::optional<RealFactorReplayCandidate> loadLatestInstanceByCategory(
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    const QString& majorCategory,
    const std::optional<QString>& status)
{
    if (!database) {
        return std::nullopt;
    }

    QString sql = QStringLiteral(
        "SELECT fi.instance_id, fi.instance_name, CAST(fi.full_config AS CHAR) AS full_config, f.major_category "
        "FROM factor_instance fi "
        "LEFT JOIN factors f ON fi.factor_id = f.factor_id "
        "WHERE f.major_category = :major_category ");
    std::map<QString, QVariant> params{{QStringLiteral(":major_category"), majorCategory}};
    if (status.has_value()) {
        sql += QStringLiteral("AND fi.status = :status ");
        params.emplace(QStringLiteral(":status"), *status);
    }
    sql += QStringLiteral("ORDER BY fi.updated_at DESC LIMIT 1");

    const auto result = database->executeQuery(sql, params);
    if (result.isEmpty()) {
        return std::nullopt;
    }

    const auto& row = result.getRow(0);
    return RealFactorReplayCandidate{
        row.getString("instance_id").toStdString(),
        row.getString("instance_name"),
        row.getString("major_category"),
        foundation::json::JsonFacade::parse(row.getString("full_config").toStdString())
    };
}

std::optional<RealFactorReplayCandidate> loadLatestActiveInstanceByCategory(
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    const QString& majorCategory)
{
    return loadLatestInstanceByCategory(database, majorCategory, QStringLiteral("ACTIVE"));
}

std::optional<RealFactorReplayCandidate> loadLatestReplayInstanceByCategory(
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    const QString& majorCategory)
{
    if (const auto active = loadLatestActiveInstanceByCategory(database, majorCategory)) {
        return active;
    }
    return loadLatestInstanceByCategory(database, majorCategory, std::nullopt);
}

RealFactorReplayHandle ensureReplayInstanceByCategory(
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    const QString& majorCategory,
    const QJsonObject& fullConfigObject,
    const QString& instanceName)
{
    RealFactorReplayHandle handle;
    const QByteArray rawConfigBytes = QJsonDocument(fullConfigObject).toJson(QJsonDocument::Compact);
    const foundation::json::JsonFacade normalizedRequestedConfig = parseRuntimeCompatibleTestConfig(rawConfigBytes.constData());
    handle.candidate = loadLatestReplayInstanceByCategory(database, majorCategory);
    if (handle.candidate.has_value()) {
        if (handle.candidate->instanceName == instanceName) {
            const foundation::json::JsonFacade normalizedExistingConfig = parseRuntimeCompatibleTestConfig(handle.candidate->config.toString().c_str());
            if (normalizedExistingConfig.toString() != normalizedRequestedConfig.toString()) {
                const int updatedRows = database->executeUpdate(
                    QStringLiteral("UPDATE factor_instance SET full_config = :full_config WHERE instance_id = :instance_id"),
                    {
                        {QStringLiteral(":full_config"), QString::fromStdString(normalizedRequestedConfig.toString())},
                        {QStringLiteral(":instance_id"), QString::fromStdString(handle.candidate->instanceId)}
                    });
                if (updatedRows > 0) {
                    handle.candidate->config = normalizedRequestedConfig;
                }
            }
            return handle;
        }
        handle.candidate.reset();
    }

    const auto factorId = loadFactorDefinitionIdByCategory(database, majorCategory);
    QString resolvedFactorId;
    if (factorId.has_value()) {
        resolvedFactorId = *factorId;
    } else {
        resolvedFactorId = QStringLiteral("temp_factor_%1_%2")
            .arg(normalizeCategoryToIdSegment(majorCategory), QString::number(QDateTime::currentMSecsSinceEpoch()));
        const int definitionRows = database->executeUpdate(
            QStringLiteral("INSERT INTO factors (factor_id, factor_name, display_name, major_category, description, status, creator, create_date) "
                           "VALUES (:factor_id, :factor_name, :display_name, :major_category, :description, :status, :creator, NOW())"),
            {
                {QStringLiteral(":factor_id"), resolvedFactorId},
                {QStringLiteral(":factor_name"), instanceName},
                {QStringLiteral(":display_name"), instanceName},
                {QStringLiteral(":major_category"), majorCategory},
                {QStringLiteral(":description"), QStringLiteral("temporary replay test factor definition")},
                {QStringLiteral(":status"), QStringLiteral("ACTIVE")},
                {QStringLiteral(":creator"), QStringLiteral("copilot_test")}
            });
        if (definitionRows <= 0) {
            return handle;
        }
        handle.tempDefinition = std::make_unique<ScopedTemporaryFactorDefinition>(database, resolvedFactorId);
    }

    const QString instanceId = QStringLiteral("temp_%1_%2")
        .arg(resolvedFactorId, QString::number(QDateTime::currentMSecsSinceEpoch()));
    const QString fullConfig = QString::fromStdString(normalizedRequestedConfig.toString());
    const int affectedRows = database->executeUpdate(
        QStringLiteral("INSERT INTO factor_instance (instance_id, factor_id, instance_name, description, full_config, status) "
                       "VALUES (:instance_id, :factor_id, :instance_name, :description, :full_config, :status)"),
        {
            {QStringLiteral(":instance_id"), instanceId},
            {QStringLiteral(":factor_id"), resolvedFactorId},
            {QStringLiteral(":instance_name"), instanceName},
            {QStringLiteral(":description"), QStringLiteral("temporary replay test instance")},
            {QStringLiteral(":full_config"), fullConfig},
            {QStringLiteral(":status"), QStringLiteral("ACTIVE")}
        });
    if (affectedRows <= 0) {
        return handle;
    }

    handle.tempInstance = std::make_unique<ScopedTemporaryFactorInstance>(database, instanceId);
    handle.candidate = RealFactorReplayCandidate{
        instanceId.toStdString(),
        instanceName,
        majorCategory,
        normalizedRequestedConfig
    };
    return handle;
}

QString loadLatestTradeDate(const std::shared_ptr<astock::database::QtMySQLDatabase>& database)
{
    if (!database) {
        return {};
    }

    const auto result = database->executeQuery(
        "SELECT MAX(trade_date) AS trade_date FROM daily_bar WHERE close IS NOT NULL",
        {});
    if (result.isEmpty()) {
        return {};
    }
    return result.getRow(0).getString("trade_date");
}

QString normalizeMomentumTypeForTest(const std::string& rawType)
{
    const QString type = QString::fromStdString(rawType).trimmed().toLower();
    if (type == QStringLiteral("0")) {
        return QStringLiteral("simple");
    }
    if (type == QStringLiteral("1")) {
        return QStringLiteral("rank");
    }
    if (type == QStringLiteral("2")) {
        return QStringLiteral("normalized");
    }
    if (type == QStringLiteral("3")) {
        return QStringLiteral("exponential");
    }
    if (type == QString::fromUtf8("简单动量") || type == QStringLiteral("simple")) {
        return QStringLiteral("simple");
    }
    if (type == QString::fromUtf8("加权动量") || type == QStringLiteral("weighted") || type == QStringLiteral("exponential")) {
        return QStringLiteral("exponential");
    }
    if (type == QString::fromUtf8("残差动量") || type == QStringLiteral("residual") || type == QStringLiteral("normalized")) {
        return QStringLiteral("normalized");
    }
    if (type == QStringLiteral("rank")) {
        return QStringLiteral("rank");
    }
    return QStringLiteral("simple");
}

QString normalizeMomentumPriceTypeForTest(const std::string& rawPriceType)
{
    const QString priceType = QString::fromStdString(rawPriceType).trimmed().toLower();
    if (priceType == QStringLiteral("0") || priceType == QStringLiteral("pre_adjust_factor")) {
        return QStringLiteral("pre_adjust_factor");
    }
    if (priceType == QStringLiteral("1") || priceType == QStringLiteral("post_adjust_factor") || priceType == QStringLiteral("adj_factor")) {
        return QStringLiteral("post_adjust_factor");
    }
    return {};
}

QString normalizeConfigurableFrequencyForTest(const std::string& frequency)
{
    const QString normalized = QString::fromStdString(frequency).trimmed().toLower();
    if (normalized == QStringLiteral("0")) {
        return QStringLiteral("daily");
    }
    if (normalized == QStringLiteral("1")) {
        return QStringLiteral("weekly");
    }
    if (normalized == QStringLiteral("2")) {
        return QStringLiteral("monthly");
    }
    if (normalized == QStringLiteral("3")) {
        return QStringLiteral("quarterly");
    }
    if (normalized == QStringLiteral("4")) {
        return QStringLiteral("annual");
    }
    if (normalized == QStringLiteral("weekly") || normalized == QStringLiteral("周频") || normalized == QStringLiteral("周")) {
        return QStringLiteral("weekly");
    }
    if (normalized == QStringLiteral("monthly") || normalized == QStringLiteral("月频") || normalized == QStringLiteral("月")) {
        return QStringLiteral("monthly");
    }
    if (normalized == QStringLiteral("quarterly") || normalized == QStringLiteral("季频") || normalized == QStringLiteral("季")) {
        return QStringLiteral("quarterly");
    }
    if (normalized == QStringLiteral("annual") || normalized == QStringLiteral("yearly") || normalized == QStringLiteral("年频") || normalized == QStringLiteral("年")) {
        return QStringLiteral("annual");
    }
    return QStringLiteral("daily");
}

QString normalizeConfigurableStandardizationForTest(const std::string& standardization)
{
    const QString normalized = QString::fromStdString(standardization).trimmed().toLower();
    if (normalized == QStringLiteral("0")) {
        return QStringLiteral("none");
    }
    if (normalized == QStringLiteral("1")) {
        return QStringLiteral("zscore");
    }
    if (normalized == QStringLiteral("2")) {
        return QStringLiteral("minmax");
    }
    if (normalized == QStringLiteral("3") || normalized == QStringLiteral("4")) {
        return QStringLiteral("percentile");
    }
    if (normalized == QStringLiteral("zscore") || normalized == QStringLiteral("z_score")
            || normalized == QStringLiteral("z-score") || normalized == QStringLiteral("z score")) {
        return QStringLiteral("zscore");
    }
    if (normalized == QStringLiteral("minmax") || normalized == QStringLiteral("min_max")
            || normalized == QStringLiteral("min-max") || normalized == QStringLiteral("min max")) {
        return QStringLiteral("minmax");
    }
    if (normalized == QStringLiteral("percentile") || normalized == QStringLiteral("rank")) {
        return QStringLiteral("percentile");
    }
    return QStringLiteral("none");
}

QString normalizeLiquidityMetricForTest(const std::string& rawMetric)
{
    const QString normalized = QString::fromStdString(rawMetric).trimmed().toLower();
    if (normalized == QStringLiteral("0")) {
        return QStringLiteral("turnover_rate");
    }
    if (normalized == QStringLiteral("1")) {
        return QStringLiteral("volume");
    }
    if (normalized == QStringLiteral("2")) {
        return QStringLiteral("amihud_illiquidity");
    }
    if (normalized == QStringLiteral("3")) {
        return QStringLiteral("amplitude");
    }
    if (normalized == QStringLiteral("换手率")) {
        return QStringLiteral("turnover_rate");
    }
    if (normalized == QStringLiteral("成交量")) {
        return QStringLiteral("volume");
    }
    if (normalized == QStringLiteral("amihud非流动性") || normalized == QStringLiteral("amihud")
            || normalized == QStringLiteral("amihud_illiquidity")) {
        return QStringLiteral("amihud_illiquidity");
    }
    if (normalized == QStringLiteral("买卖价差") || normalized == QStringLiteral("bid_ask_spread")) {
        return QStringLiteral("amplitude");
    }
    return normalized;
}

QString normalizeSizeMetricForTest(const std::string& rawMetric)
{
    const QString normalized = QString::fromStdString(rawMetric).trimmed().toLower();
    if (normalized == QStringLiteral("0")) {
        return QStringLiteral("market_cap");
    }
    if (normalized == QStringLiteral("1")) {
        return QStringLiteral("circulating_market_cap");
    }
    if (normalized == QStringLiteral("2")) {
        return QStringLiteral("total_assets");
    }
    return normalized;
}

QString normalizeNeutralizationModeForTest(const std::string& rawMode)
{
    const QString normalized = QString::fromStdString(rawMode).trimmed().toLower();
    if (normalized == QStringLiteral("0")) {
        return QStringLiteral("disabled");
    }
    if (normalized == QStringLiteral("1")) {
        return QStringLiteral("requested");
    }
    if (normalized == QStringLiteral("2")) {
        return QStringLiteral("historical_view_cross_section_industry_size");
    }
    if (normalized == QStringLiteral("3")) {
        return QStringLiteral("historical_view_neutralization_failed");
    }
    return normalized;
}

QString normalizeGrowthMetricForTest(const std::string& rawMetric)
{
    const QString normalized = QString::fromStdString(rawMetric).trimmed().toLower();
    if (normalized == QStringLiteral("0")) {
        return QStringLiteral("revenue_growth");
    }
    if (normalized == QStringLiteral("1")) {
        return QStringLiteral("net_profit_growth");
    }
    if (normalized == QStringLiteral("2")) {
        return QStringLiteral("delta_roe");
    }
    if (normalized == QStringLiteral("3")) {
        return QStringLiteral("sue");
    }
    if (normalized == QStringLiteral("revenue_growth")) {
        return QStringLiteral("revenue_growth");
    }
    if (normalized == QStringLiteral("net_profit_growth")) {
        return QStringLiteral("net_profit_growth");
    }
    if (normalized == QStringLiteral("delta_roe")) {
        return QStringLiteral("delta_roe");
    }
    if (normalized == QStringLiteral("sue")) {
        return QStringLiteral("sue");
    }
    return normalized;
}

QString growthFieldForMetricForTest(const std::string& rawMetric)
{
    const QString metric = normalizeGrowthMetricForTest(rawMetric);
    if (metric == QStringLiteral("net_profit_growth")) {
        return QStringLiteral("net_profit");
    }
    if (metric == QStringLiteral("delta_roe")) {
        return QStringLiteral("roe");
    }
    if (metric == QStringLiteral("sue")) {
        return QStringLiteral("eps");
    }
    return QStringLiteral("total_revenue");
}

QString normalizeDividendMetricForTest(const std::string& rawMetric)
{
    const QString normalized = QString::fromStdString(rawMetric).trimmed().toLower();
    if (normalized == QStringLiteral("0")) {
        return QStringLiteral("dividend_yield");
    }
    if (normalized == QStringLiteral("1")) {
        return QStringLiteral("payout_ratio");
    }
    if (normalized == QStringLiteral("2")) {
        return QStringLiteral("dividend_stability");
    }
    if (normalized == QStringLiteral("股息率")) {
        return QStringLiteral("dividend_yield");
    }
    if (normalized == QStringLiteral("派息率") || normalized == QStringLiteral("股息支付率")) {
        return QStringLiteral("payout_ratio");
    }
    if (normalized == QStringLiteral("分红稳定性") || normalized == QStringLiteral("股息稳定性")) {
        return QStringLiteral("dividend_stability");
    }
    return normalized.isEmpty() ? QStringLiteral("dividend_yield") : normalized;
}

QString normalizeTechnicalIndicatorTypeForTest(const std::string& rawType)
{
    const QString normalized = QString::fromStdString(rawType).trimmed().toLower();
    if (normalized == QStringLiteral("0")) {
        return QStringLiteral("rsi");
    }
    if (normalized == QStringLiteral("1")) {
        return QStringLiteral("macd");
    }
    if (normalized == QStringLiteral("2")) {
        return QStringLiteral("ma");
    }
    if (normalized == QStringLiteral("3")) {
        return QStringLiteral("ema");
    }
    if (normalized == QStringLiteral("4")) {
        return QStringLiteral("boll");
    }
    if (normalized == QStringLiteral("5")) {
        return QStringLiteral("kdj");
    }
    if (normalized == QStringLiteral("6")) {
        return QStringLiteral("atr");
    }
    if (normalized == QStringLiteral("7")) {
        return QStringLiteral("obv");
    }
    if (normalized == QStringLiteral("8")) {
        return QStringLiteral("vwap");
    }
    if (normalized == QStringLiteral("9")) {
        return QStringLiteral("volume_ratio");
    }
    if (normalized == QStringLiteral("10")) {
        return QStringLiteral("turnover_stability");
    }
    if (normalized == QStringLiteral("rsi")) {
        return QStringLiteral("rsi");
    }
    if (normalized == QStringLiteral("macd")) {
        return QStringLiteral("macd");
    }
    if (normalized == QStringLiteral("ma")) {
        return QStringLiteral("ma");
    }
    if (normalized == QStringLiteral("ema")) {
        return QStringLiteral("ema");
    }
    if (normalized == QStringLiteral("boll")) {
        return QStringLiteral("boll");
    }
    if (normalized == QStringLiteral("kdj")) {
        return QStringLiteral("kdj");
    }
    if (normalized == QStringLiteral("atr")) {
        return QStringLiteral("atr");
    }
    if (normalized == QStringLiteral("obv")) {
        return QStringLiteral("obv");
    }
    if (normalized == QStringLiteral("vwap")) {
        return QStringLiteral("vwap");
    }
    if (normalized == QStringLiteral("volume_ratio")) {
        return QStringLiteral("volume_ratio");
    }
    if (normalized == QStringLiteral("turnover_stability")) {
        return QStringLiteral("turnover_stability");
    }
    return {};
}

QString normalizeTechnicalPriceTypeForTest(const std::string& rawPriceType)
{
    const QString normalized = QString::fromStdString(rawPriceType).trimmed().toLower();
    if (normalized == QStringLiteral("0") || normalized == QStringLiteral("close")) {
        return QStringLiteral("close");
    }
    if (normalized == QStringLiteral("1") || normalized == QStringLiteral("open")) {
        return QStringLiteral("open");
    }
    if (normalized == QStringLiteral("2") || normalized == QStringLiteral("high")) {
        return QStringLiteral("high");
    }
    if (normalized == QStringLiteral("3") || normalized == QStringLiteral("low")) {
        return QStringLiteral("low");
    }
    return normalized;
}

TEST(FactorBacktestRegressionTest, NormalizeTechnicalIndicatorTypeRequiresCanonicalIdentifiers)
{
    EXPECT_EQ(normalizeTechnicalIndicatorTypeForTest("ma"), QStringLiteral("ma"));
    EXPECT_EQ(normalizeTechnicalIndicatorTypeForTest("EMA"), QStringLiteral("ema"));
    EXPECT_EQ(normalizeTechnicalIndicatorTypeForTest("1"), QStringLiteral("macd"));
    EXPECT_TRUE(normalizeTechnicalIndicatorTypeForTest("布林带").isEmpty());
    EXPECT_TRUE(normalizeTechnicalIndicatorTypeForTest("随机指标").isEmpty());
    EXPECT_TRUE(normalizeTechnicalIndicatorTypeForTest("真实波幅").isEmpty());
    EXPECT_TRUE(normalizeTechnicalIndicatorTypeForTest("成交量加权平均价").isEmpty());
    EXPECT_TRUE(normalizeTechnicalIndicatorTypeForTest("量比").isEmpty());
}

QString normalizeSentimentMetricForTest(const std::string& rawMetric)
{
    const QString normalized = QString::fromStdString(rawMetric).trimmed().toLower();
    if (normalized == QStringLiteral("0") || normalized == QStringLiteral("sentiment_score")
            || normalized == QStringLiteral("policy_score")) {
        return QStringLiteral("sentiment_score");
    }
    if (normalized == QStringLiteral("1") || normalized == QStringLiteral("social_sentiment")) {
        return QStringLiteral("social_sentiment");
    }
    if (normalized == QStringLiteral("2") || normalized == QStringLiteral("investor_sentiment")) {
        return QStringLiteral("investor_sentiment");
    }
    if (normalized == QStringLiteral("3") || normalized == QStringLiteral("market_sentiment")) {
        return QStringLiteral("market_sentiment");
    }
    return normalized;
}

QString normalizeSentimentSourceForTest(const std::string& rawSource)
{
    const QString normalized = QString::fromStdString(rawSource).trimmed().toLower();
    if (normalized == QStringLiteral("0")) {
        return QStringLiteral("news");
    }
    if (normalized == QStringLiteral("1")) {
        return QStringLiteral("social");
    }
    if (normalized == QStringLiteral("2")) {
        return QStringLiteral("analyst");
    }
    if (normalized == QStringLiteral("3")) {
        return QStringLiteral("market");
    }
    if (normalized == QStringLiteral("4")) {
        return QStringLiteral("policy");
    }
    if (normalized == QStringLiteral("5")) {
        return QStringLiteral("alternative");
    }
    if (normalized == QStringLiteral("6")) {
        return QStringLiteral("derivatives");
    }
    if (normalized == QStringLiteral("新闻情绪") || normalized == QStringLiteral("news")) {
        return QStringLiteral("news");
    }
    if (normalized == QStringLiteral("社交媒体") || normalized == QStringLiteral("social")
            || normalized == QStringLiteral("social_media")) {
        return QStringLiteral("social");
    }
    if (normalized == QStringLiteral("分析师评级") || normalized == QStringLiteral("analyst")
            || normalized == QStringLiteral("analyst_rating")) {
        return QStringLiteral("analyst");
    }
    if (normalized == QStringLiteral("市场情绪") || normalized == QStringLiteral("market")) {
        return QStringLiteral("market");
    }
    return normalized;
}

QString normalizeMacroMetricForTest(const std::string& rawMetric)
{
    const QString normalized = QString::fromStdString(rawMetric).trimmed().toLower();
    if (normalized == QStringLiteral("利率敏感度") || normalized == QStringLiteral("interest_rate")
            || normalized == QStringLiteral("interest_rate_sensitivity")) {
        return QStringLiteral("interest_rate_sensitivity");
    }
    if (normalized == QStringLiteral("通胀敏感度") || normalized == QStringLiteral("inflation")
            || normalized == QStringLiteral("inflation_sensitivity")) {
        return QStringLiteral("inflation_sensitivity");
    }
    if (normalized == QStringLiteral("经济增长敏感度") || normalized == QStringLiteral("growth")
            || normalized == QStringLiteral("growth_sensitivity")) {
        return QStringLiteral("growth_sensitivity");
    }
    return normalized;
}

QString normalizeMacroIndicatorForTest(const std::string& rawIndicator)
{
    const QString normalized = QString::fromStdString(rawIndicator).trimmed().toLower();
    if (normalized == QStringLiteral("0") || normalized == QStringLiteral("industrial_added_value_yoy")) {
        return QStringLiteral("industrial_added_value_yoy");
    }
    if (normalized == QStringLiteral("1") || normalized == QStringLiteral("manufacturing_pmi")) {
        return QStringLiteral("manufacturing_pmi");
    }
    if (normalized == QStringLiteral("2") || normalized == QStringLiteral("gdp_yoy")) {
        return QStringLiteral("gdp_yoy");
    }
    if (normalized == QStringLiteral("3") || normalized == QStringLiteral("cpi_yoy")) {
        return QStringLiteral("cpi_yoy");
    }
    if (normalized == QStringLiteral("4") || normalized == QStringLiteral("ppi_yoy")) {
        return QStringLiteral("ppi_yoy");
    }
    if (normalized == QStringLiteral("5") || normalized == QStringLiteral("m2_yoy")) {
        return QStringLiteral("m2_yoy");
    }
    if (normalized == QStringLiteral("6") || normalized == QStringLiteral("social_financing_stock_yoy")) {
        return QStringLiteral("social_financing_stock_yoy");
    }
    if (normalized == QStringLiteral("7") || normalized == QStringLiteral("m1_m2_spread")) {
        return QStringLiteral("m1_m2_spread");
    }
    if (normalized == QStringLiteral("8") || normalized == QStringLiteral("ten_year_bond_yield")) {
        return QStringLiteral("ten_year_bond_yield");
    }
    if (normalized == QStringLiteral("9") || normalized == QStringLiteral("shibor_3m")) {
        return QStringLiteral("shibor_3m");
    }
    if (normalized == QStringLiteral("10") || normalized == QStringLiteral("lpr_1y")) {
        return QStringLiteral("lpr_1y");
    }
    if (normalized == QStringLiteral("11") || normalized == QStringLiteral("reserve_requirement_ratio")) {
        return QStringLiteral("reserve_requirement_ratio");
    }
    if (normalized == QStringLiteral("12") || normalized == QStringLiteral("aa_credit_spread")) {
        return QStringLiteral("aa_credit_spread");
    }
    if (normalized == QStringLiteral("13") || normalized == QStringLiteral("vix_proxy")) {
        return QStringLiteral("vix_proxy");
    }
    return normalized;
}

QString normalizeMacroModeForTest(const std::string& rawMode)
{
    const QString normalized = QString::fromStdString(rawMode).trimmed().toLower();
    if (normalized == QStringLiteral("0") || normalized == QStringLiteral("proxy_sensitivity")) {
        return QStringLiteral("proxy_sensitivity");
    }
    return normalized;
}

QString normalizeSectorTypeForTest(const std::string& rawSectorType)
{
    const QString normalized = QString::fromStdString(rawSectorType).trimmed().toLower();
    if (normalized == QStringLiteral("0")) {
        return QStringLiteral("sw_l1");
    }
    if (normalized == QStringLiteral("1")) {
        return QStringLiteral("sw_l2");
    }
    if (normalized == QStringLiteral("2")) {
        return QStringLiteral("citic_l1");
    }
    if (normalized == QStringLiteral("3")) {
        return QStringLiteral("citic_l2");
    }
    if (normalized == QStringLiteral("申万一级") || normalized == QStringLiteral("sw_l1")) {
        return QStringLiteral("sw_l1");
    }
    if (normalized == QStringLiteral("申万二级") || normalized == QStringLiteral("sw_l2")) {
        return QStringLiteral("sw_l2");
    }
    if (normalized == QStringLiteral("中信一级") || normalized == QStringLiteral("citic_l1")) {
        return QStringLiteral("citic_l1");
    }
    if (normalized == QStringLiteral("中信二级") || normalized == QStringLiteral("citic_l2")) {
        return QStringLiteral("citic_l2");
    }
    return normalized;
}

QString normalizeQualityMetricForTest(const std::string& metric)
{
    const QString normalized = QString::fromStdString(metric).trimmed().toLower();
    if (normalized == QStringLiteral("0")) {
        return QStringLiteral("roe");
    }
    if (normalized == QStringLiteral("1")) {
        return QStringLiteral("roa");
    }
    if (normalized == QStringLiteral("2")) {
        return QStringLiteral("gross_margin");
    }
    if (normalized == QStringLiteral("3")) {
        return QStringLiteral("operating_margin");
    }
    if (normalized == QStringLiteral("4")) {
        return QStringLiteral("earnings_quality");
    }
    if (normalized == QString::fromUtf8("净资产收益率") || normalized == QStringLiteral("roe")) {
        return QStringLiteral("roe");
    }
    if (normalized == QString::fromUtf8("总资产收益率") || normalized == QStringLiteral("roa")) {
        return QStringLiteral("roa");
    }
    if (normalized == QString::fromUtf8("营业利润率") || normalized == QStringLiteral("operating_margin")) {
        return QStringLiteral("operating_margin");
    }
    if (normalized == QString::fromUtf8("毛利率") || normalized == QStringLiteral("gross_margin") || normalized == QStringLiteral("profit_margin")) {
        return QStringLiteral("gross_margin");
    }
    if (normalized == QStringLiteral("earnings_quality") || normalized == QStringLiteral("net_profit_to_equity") || normalized == QString::fromUtf8("收益质量")) {
        return QStringLiteral("earnings_quality");
    }
    return normalized;
}

int expectedMomentumWindow(const foundation::json::JsonFacade& calculation)
{
    int window = 20;
    if (calculation.has("window")) {
        window = calculation.get("window").asInt();
    }
    return window;
}

std::string scalarTextForTest(const foundation::json::JsonFacade& value)
{
    if (value.isString()) {
        return value.asString();
    }
    if (value.isNumber()) {
        return std::to_string(value.asInt());
    }
    if (value.isBool()) {
        return value.asBool() ? "true" : "false";
    }
    return {};
}

std::string expectedMomentumTypeRaw(const foundation::json::JsonFacade& calculation)
{
    std::string type = "simple";
    if (calculation.has("type")) {
        type = scalarTextForTest(calculation.get("type"));
    }
    return type;
}

std::string expectedMomentumPriceTypeRaw(const foundation::json::JsonFacade& calculation)
{
    std::string priceType = "post_adjust_factor";
    if (calculation.has("adjustPriceType")) {
        priceType = scalarTextForTest(calculation.get("adjustPriceType"));
    } else if (calculation.has("technicalPriceType")) {
        priceType = scalarTextForTest(calculation.get("technicalPriceType"));
    } else if (calculation.has("priceType")) {
        priceType = scalarTextForTest(calculation.get("priceType"));
    }
    return priceType;
}

bool expectedMomentumUseVolume(const foundation::json::JsonFacade& calculation)
{
    bool useVolume = false;
    if (calculation.has("useVolume")) {
        useVolume = calculation.get("useVolume").asBool();
    }
    return useVolume;
}

int expectedMomentumSkipRecent(const foundation::json::JsonFacade& calculation)
{
    int skipRecent = 0;
    if (calculation.has("skipRecent")) {
        skipRecent = calculation.get("skipRecent").asInt();
    }
    return skipRecent;
}

int expectedLowVolWindow(const foundation::json::JsonFacade& calculation)
{
    int window = 20;
    if (calculation.has("window")) {
        window = calculation.get("window").asInt();
    }
    return window;
}

std::string expectedBenchmarkSymbol(const foundation::json::JsonFacade& calculation)
{
    std::string benchmarkSymbol = "000300.SH";
    if (calculation.has("benchmarkSymbol")) {
        benchmarkSymbol = calculation.get("benchmarkSymbol").asString();
    }
    return benchmarkSymbol;
}

int expectedConfigurableWindow(const foundation::json::JsonFacade& calculation)
{
    int window = 20;
    if (calculation.has("window")) {
        window = calculation.get("window").asInt();
    }
    return window;
}

std::string expectedSizeMetricRaw(const foundation::json::JsonFacade& calculation)
{
    if (calculation.has("sizeMetric")) {
        return scalarTextForTest(calculation.get("sizeMetric"));
    }
    return "market_cap";
}

bool expectedSizeLogTransform(const foundation::json::JsonFacade& calculation)
{
    bool logTransform = true;
    if (calculation.has("logTransform")) {
        logTransform = calculation.get("logTransform").asBool();
    }
    return logTransform;
}

std::string expectedLiquidityMetricRaw(const foundation::json::JsonFacade& calculation)
{
    std::string metric;
    if (calculation.has("metric")) {
        metric = scalarTextForTest(calculation.get("metric"));
    }
    return metric;
}

std::string expectedGrowthMetricRaw(const foundation::json::JsonFacade& calculation)
{
    std::string metric;
    if (calculation.has("growthMetrics")) {
        const auto metrics = calculation.get("growthMetrics");
        if (metrics.isArray() && metrics.size() > 0) {
            metric = scalarTextForTest(metrics.at(0));
        }
    }
    return metric;
}

std::string expectedDividendMetricRaw(const foundation::json::JsonFacade& calculation)
{
    std::string metric;
    if (calculation.has("metric")) {
        metric = scalarTextForTest(calculation.get("metric"));
    }
    if (metric.empty() && calculation.has("dividendMetrics")) {
        const auto metrics = calculation.get("dividendMetrics");
        if (metrics.isArray() && metrics.size() > 0) {
            metric = scalarTextForTest(metrics.at(0));
        }
    }
    return metric;
}

std::string expectedTechnicalIndicatorTypeRaw(const foundation::json::JsonFacade& calculation)
{
    if (calculation.has("technicalIndicators")) {
        const auto indicators = calculation.get("technicalIndicators");
        if (indicators.isArray() && indicators.size() > 0) {
            return scalarTextForTest(indicators.at(0));
        }
    }
    return {};
}

std::string expectedSentimentMetricRaw(const foundation::json::JsonFacade& calculation)
{
    std::string metric;
    if (calculation.has("metric")) {
        metric = scalarTextForTest(calculation.get("metric"));
    }
    return metric;
}

std::string expectedSentimentSourceRaw(const foundation::json::JsonFacade& calculation)
{
    if (calculation.has("sentimentSource")) {
        return scalarTextForTest(calculation.get("sentimentSource"));
    }
    return {};
}

std::string expectedSectorTypeRaw(const foundation::json::JsonFacade& calculation)
{
    if (calculation.has("sectorType")) {
        return scalarTextForTest(calculation.get("sectorType"));
    }
    return {};
}

std::string expectedMacroIndicatorRaw(const foundation::json::JsonFacade& calculation)
{
    if (calculation.has("macroIndicators")) {
        const auto indicators = calculation.get("macroIndicators");
        if (indicators.isArray() && indicators.size() > 0) {
            return scalarTextForTest(indicators.at(0));
        }
    }
    return {};
}

std::string expectedCustomExpression(const foundation::json::JsonFacade& calculation)
{
    if (calculation.has("expression")) {
        const std::string expression = calculation.get("expression").asString();
        if (!expression.empty()) {
            return expression;
        }
    }
    return "close / open - 1";
}

int expectedCustomVariableCount(const foundation::json::JsonFacade& calculation)
{
    if (!calculation.has("variables")) {
        return 0;
    }
    const auto variables = calculation.get("variables");
    if (!variables.isArray()) {
        return 0;
    }
    return static_cast<int>(variables.size());
}

std::string expectedConfigurableFrequencyRaw(const foundation::json::JsonFacade& calculation)
{
    if (calculation.has("frequency")) {
        return scalarTextForTest(calculation.get("frequency"));
    }
    return "daily";
}

bool expectedConfigurableLaggedEnabled(const foundation::json::JsonFacade& calculation)
{
    if (calculation.has("laggedEnabled")) {
        return calculation.get("laggedEnabled").asBool();
    }
    if (calculation.has("lagEnabled")) {
        return calculation.get("lagEnabled").asBool();
    }
    return false;
}

int expectedConfigurableLookbackPeriod(const foundation::json::JsonFacade& calculation)
{
    if (calculation.has("lookbackPeriod")) {
        return calculation.get("lookbackPeriod").asInt();
    }
    return 252;
}

std::string expectedConfigurableStandardizationRaw(const foundation::json::JsonFacade& calculation)
{
    if (calculation.has("standardization")) {
        return scalarTextForTest(calculation.get("standardization"));
    }
    return {};
}

bool expectedConfigurableNeutralizationEnabled(const foundation::json::JsonFacade& calculation)
{
    if (calculation.has("neutralizationEnabled")) {
        return calculation.get("neutralizationEnabled").asBool();
    }
    return false;
}

std::vector<HistoricalDataPoint> buildLinearHistoricalSeries(
    const QString& endDate,
    int sampleCount,
    double startValue,
    double step)
{
    std::vector<HistoricalDataPoint> series;
    series.reserve(static_cast<size_t>((std::max)(0, sampleCount)));
    const QDate resolvedEndDate = QDate::fromString(endDate, Qt::ISODate);
    for (int index = 0; index < sampleCount; ++index) {
        const QDate date = resolvedEndDate.addDays(-sampleCount + 1 + index);
        series.push_back({
            date.toString(Qt::ISODate).toStdString(),
            startValue + static_cast<double>(index) * step
        });
    }
    return series;
}

void assignHistoricalSeries(
    DatedMultiFieldFactorDataProvider::FieldSeriesMap& fieldSeries,
    const std::string& field,
    const std::string& symbol,
    std::vector<HistoricalDataPoint> series)
{
    fieldSeries[field][symbol] = std::move(series);
}

std::string expectedQualityMetricRaw(const foundation::json::JsonFacade& calculation)
{
    std::string metric = "roe";
    if (calculation.has("metric")) {
        metric = scalarTextForTest(calculation.get("metric"));
    }
    return metric;
}

std::string expectedQualityFrequency(const foundation::json::JsonFacade& calculation)
{
    std::string frequency = "daily";
    if (calculation.has("frequency")) {
        frequency = scalarTextForTest(calculation.get("frequency"));
    }
    return frequency;
}

double expectedQualityThreshold(const foundation::json::JsonFacade& calculation)
{
    double threshold = 0.1;
    if (calculation.has("qualityThreshold")) {
        threshold = calculation.get("qualityThreshold").asDouble();
    } else if (calculation.has("quality_threshold")) {
        threshold = calculation.get("quality_threshold").asDouble();
    }
    return threshold > 1.0 ? threshold / 100.0 : threshold;
}

int storeSupportMapDataset(const QVariantList& rows,
                           const QStringList& availableFields,
                           const QStringList& stockCodes,
                           const QString& startDate,
                           const QString& endDate)
{
    auto& cache = DataServiceCache::getInstance();
    if (!cache.initializeCache()) {
        return -1;
    }

    DataServiceCache::DataSetInfo info;
    info.displayName = QStringLiteral("support_map_regression_dataset");
    info.description = QStringLiteral("support map regression dataset");
    info.sourceType = QStringLiteral("cleaning");
    info.createdTime = QDateTime::currentDateTime();
    info.rowCount = rows.size();
    info.schemaVersion = 2;
    info.isBacktestReady = true;
    info.availableFields = availableFields;
    info.stockCodes = stockCodes;
    info.startDate = QDate::fromString(startDate, Qt::ISODate);
    info.endDate = QDate::fromString(endDate, Qt::ISODate);
    info.tags = QStringList{
        QStringLiteral("cleaned"),
        QStringLiteral("cleaning_result"),
        QStringLiteral("factor_backtest_ready")
    };

    return cache.storeDataSet(rows, info);
}

CalculationResult calculateMomentum(const MomentumFactor::Params& params,
                                    const std::string& date,
                                    const std::shared_ptr<StubFactorDataProvider>& provider)
{
    MomentumFactor factor;
    factor.setParams(params);

    CalculationContext context;
    context.date = date;
    context.symbols = {"AAA"};
    context.historicalView = provider;
    return factor.calculate(context);
}

AStockQuantEngine::Cache::CacheFacade& configureLocalOnlyCacheFacade()
{
    auto& cacheFacade = AStockQuantEngine::Cache::CacheFacade::getInstance();
    AStockQuantEngine::Cache::CacheConfig cacheConfig;
    cacheConfig.enabled = true;
    cacheConfig.localCache.enabled = true;
    cacheConfig.localCache.maxSize = 256;
    cacheConfig.redisCache.enabled = false;
    cacheFacade.initialize(cacheConfig);
    cacheFacade.clear();
    return cacheFacade;
}

std::shared_ptr<AStockQuantEngine::Cache::CacheFacade> makeSharedCacheFacade()
{
    auto& cacheFacade = configureLocalOnlyCacheFacade();
    return std::shared_ptr<AStockQuantEngine::Cache::CacheFacade>(
        &cacheFacade,
        [](AStockQuantEngine::Cache::CacheFacade*) {});
}

BacktestResult makeCachedExecutorResult(const std::string& instanceId,
                                        double annualReturn,
                                        int executionTimeMs)
{
    BacktestResult result;
    result.resultId = foundation::utils::Uuid::generate_v4();
    result.instanceId = instanceId;
    result.instanceName = instanceId + "_name";
    result.config.instanceId = instanceId;
    result.config.startDate = "2024-01-01";
    result.config.endDate = "2024-01-31";
    result.config.forwardDays = 3;
    result.config.numGroups = 5;
    result.dataCoverage = 1.0;
    result.icirResult.icMean = 0.12;
    result.icirResult.icStd = 0.04;
    result.icirResult.ir = 3.0;
    result.icirResult.icPositiveRatio = 0.75;
    result.groupResult.groupReturns = {0.05, 0.03, 0.01, -0.01, -0.02};
    result.groupResult.groupStockCounts = {10, 10, 10, 10, 10};
    result.groupResult.minFactorValues = {-1.0, -0.5, 0.0, 0.5, 1.0};
    result.groupResult.maxFactorValues = {-0.6, -0.1, 0.4, 0.9, 1.4};
    result.groupResult.topGroupReturn = 0.05;
    result.groupResult.bottomGroupReturn = -0.02;
    result.groupResult.longShortReturn = 0.068;
    result.annualReturn = annualReturn;
    result.sharpeRatio = 1.5;
    result.maxDrawdown = 0.08;
    result.winRate = 0.75;
    result.profitFactor = 1.8;
    result.executionTimeMs = executionTimeMs;
    result.status = "SUCCESS";
    return result;
}

BacktestConfig makeCachedBacktestConfig(const std::string& instanceId)
{
    BacktestConfig config;
    config.instanceId = instanceId;
    config.startDate = "2024-01-01";
    config.endDate = "2024-01-31";
    config.forwardDays = 3;
    config.numGroups = 5;
    config.transactionCost = 0.001;
    return config;
}

uint64_t fnv1a64AppendForTest(uint64_t hash, const void* data, size_t length)
{
    constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;

    if (hash == 0) {
        hash = kFnvOffset;
    }

    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t index = 0; index < length; ++index) {
        hash ^= static_cast<uint64_t>(bytes[index]);
        hash *= kFnvPrime;
    }
    return hash;
}

uint64_t mixHash64ForTest(uint64_t value)
{
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

void hashStringForTest(uint64_t& hash, const std::string& value)
{
    hash = fnv1a64AppendForTest(hash, value.data(), value.size());
    static constexpr char separator = '\x1f';
    hash = fnv1a64AppendForTest(hash, &separator, 1);
}

std::string toHexStringForTest(uint64_t value)
{
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

std::string formatDoubleForKeyForTest(double value)
{
    if (!std::isfinite(value)) {
        if (std::isnan(value)) {
            return "nan";
        }
        return value > 0.0 ? "inf" : "-inf";
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return stream.str();
}

std::string buildAllowedStockCodesFingerprintForTest(std::vector<std::string> allowedStockCodes)
{
    std::sort(allowedStockCodes.begin(), allowedStockCodes.end());

    std::ostringstream stream;
    if (allowedStockCodes.empty()) {
        stream << '*';
    } else {
        for (const auto& stockCode : allowedStockCodes) {
            stream << stockCode << ';';
        }
    }
    return stream.str();
}

std::string buildCachedBarsFingerprintForTest(const std::vector<factor::CachedMarketBar>& cachedBars)
{
    uint64_t xorHash = 0;
    uint64_t sumHash = 0;
    size_t validRowCount = 0;

    for (const auto& bar : cachedBars) {
        uint64_t rowHash = 0;
        hashStringForTest(rowHash, bar.tradeDate);
        hashStringForTest(rowHash, bar.symbol);
        hashStringForTest(rowHash, formatDoubleForKeyForTest(bar.close));

        std::vector<std::pair<std::string, double>> fields(bar.numericFields.begin(), bar.numericFields.end());
        std::sort(fields.begin(), fields.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

        for (const auto& [field, value] : fields) {
            hashStringForTest(rowHash, field);
            hashStringForTest(rowHash, formatDoubleForKeyForTest(value));
        }

        const uint64_t mixedRowHash = mixHash64ForTest(rowHash);
        xorHash ^= mixedRowHash;
        sumHash += mixedRowHash;
        ++validRowCount;
    }

    uint64_t hash = 0;
    hashStringForTest(hash, std::to_string(validRowCount));
    hash = fnv1a64AppendForTest(hash, &xorHash, sizeof(xorHash));
    hash = fnv1a64AppendForTest(hash, &sumHash, sizeof(sumHash));
    return toHexStringForTest(hash);
}

std::string buildBacktestCacheSignatureForTest(const BacktestConfig& config)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6)
           << "ds" << config.datasetId
           << "_bm" << config.benchmarkSymbol
           << "_tc" << config.transactionCost
           << "_slp" << config.slippageRate
           << "_rf" << config.riskFreeRate
           << "_rb" << config.rebalanceDays
           << "_sl" << config.stopLossRate
           << "_tp" << config.takeProfitRate
           << "_dd" << config.maxDrawdownLimit
           << "_dl" << config.maxDailyLoss
           << "_mp" << config.maxPositionPercent
           << "_te" << config.maxTotalExposure
           << "_stocks" << buildAllowedStockCodesFingerprintForTest(config.allowedStockCodes)
           << "_bars" << buildCachedBarsFingerprintForTest(config.cachedBars);
    return stream.str();
}

void seedBacktestResultCache(const std::shared_ptr<FactorCacheManager>& cacheManager,
                             const BacktestConfig& config,
                             const BacktestResult& result)
{
    std::ostringstream riskSignature;
    riskSignature << std::fixed << std::setprecision(6)
                  << "sl" << config.stopLossRate
                  << "_tp" << config.takeProfitRate
                  << "_dd" << config.maxDrawdownLimit
                  << "_dl" << config.maxDailyLoss
                  << "_mp" << config.maxPositionPercent
                  << "_te" << config.maxTotalExposure;

    const std::string fullSignature = buildBacktestCacheSignatureForTest(config);
    cacheManager->setBacktestResult(
        config.instanceId,
        config.startDate,
        config.endDate,
        config.forwardDays,
        config.numGroups,
        fullSignature,
        result.toJson());

    cacheManager->setBacktestResult(
        config.instanceId,
        config.startDate,
        config.endDate,
        config.forwardDays,
        config.numGroups,
        riskSignature.str(),
        result.toJson());
}

CalculationResult makeCalculationResult(const std::string& date,
                                        std::initializer_list<std::pair<const char*, double>> values)
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = date;
    result.dataStatus.availability = factor::DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.metadata = foundation::json::JsonFacade::createObject();
    for (const auto& [symbol, value] : values) {
        result.values.emplace(symbol, value);
    }
    return result;
}

TEST(FactorBacktestRegressionTest, LoadMissingResultFileKeepsExistingRestoredState)
{
    const QString filePath = writeResultFile(makeBatchResult());

    FactorBacktestController controller;
    ASSERT_TRUE(controller.loadResultFromFile(filePath));

    const QVariantMap previousResult = controller.backtestResult();
    const QVariantList previousGroups = controller.groupResults();
    const QVariantMap previousIcir = controller.icirResult();
    const QVariantMap previousSummary = controller.summaryStats();

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    EXPECT_FALSE(controller.loadResultFromFile(dir.filePath("missing.json")));
    EXPECT_EQ(controller.backtestResult(), previousResult);
    EXPECT_EQ(controller.groupResults(), previousGroups);
    EXPECT_EQ(controller.icirResult(), previousIcir);
    EXPECT_EQ(controller.summaryStats(), previousSummary);
}

TEST(FactorBacktestRegressionTest, LoadInvalidJsonKeepsExistingRestoredState)
{
    const QString filePath = writeResultFile(makeBatchResult());
    const QString invalidFilePath = writeRawFile("{ invalid json");

    FactorBacktestController controller;
    ASSERT_TRUE(controller.loadResultFromFile(filePath));

    const QVariantMap previousResult = controller.backtestResult();
    const QVariantList previousGroups = controller.groupResults();
    const QVariantMap previousIcir = controller.icirResult();
    const QVariantMap previousSummary = controller.summaryStats();

    EXPECT_FALSE(controller.loadResultFromFile(invalidFilePath));
    EXPECT_EQ(controller.backtestResult(), previousResult);
    EXPECT_EQ(controller.groupResults(), previousGroups);
    EXPECT_EQ(controller.icirResult(), previousIcir);
    EXPECT_EQ(controller.summaryStats(), previousSummary);
}

TEST(FactorBacktestRegressionTest, LoadSingleFactorResultReplacesPriorBatchMetadata)
{
    const QString batchFilePath = writeResultFile(makeBatchResult());
    const QString singleFilePath = writeResultFile(
        makeSingleFactorResult("factor_value", "价值因子", 0.031, 0.012, 95));

    FactorBacktestController controller;
    ASSERT_TRUE(controller.loadResultFromFile(batchFilePath));
    ASSERT_EQ(controller.backtestResult().value("factorCount").toInt(), 2);

    ASSERT_TRUE(controller.loadResultFromFile(singleFilePath));

    const QVariantMap restored = controller.backtestResult();
    EXPECT_FALSE(restored.contains("factorCount"));
    EXPECT_TRUE(restored.value("results").toList().isEmpty());
    EXPECT_TRUE(restored.value("factorIds").toList().isEmpty());
    EXPECT_EQ(restored.value("taskId").toString(), QString("factor_value_task"));
    EXPECT_EQ(controller.groupResults().size(), 2);
    EXPECT_DOUBLE_EQ(controller.icirResult().value("icValue").toDouble(), 0.031);
    EXPECT_DOUBLE_EQ(controller.summaryStats().value("spreadReturn").toDouble(), 0.012);
}

TEST(FactorBacktestRegressionTest, FinalizeBacktestSuccessEmitsCompletedStateForSingleFactorBatch)
{
    QStandardPaths::setTestModeEnabled(true);

    FactorBacktestController controller;
    FactorBacktestControllerTestAccess::primeSingleFactorCompletionState(
        controller,
        QVariantList{QStringLiteral("factor_value")});

    BacktestResult result = makeCachedExecutorResult("factor_value_instance", 0.21, 11);
    result.icirResult.icMean = 0.0;
    result.icirResult.ir = 0.0;
    result.icirResult.icPositiveRatio = 0.0;
    result.dataCoverage = 0.0;
    result.maxDrawdown = 0.9;
    result.turnoverRate = 5.0;

    int completedCount = 0;
    QVariantMap emittedResult;
    QObject::connect(&controller,
                     &FactorBacktestController::backtestCompleted,
                     [&](const QVariantMap& resultMap) {
                         ++completedCount;
                         emittedResult = resultMap;
                     });

    FactorBacktestControllerTestAccess::finalizeBacktestSuccess(
        controller,
        QStringLiteral("factor_value"),
        result,
        0);

    EXPECT_EQ(completedCount, 1);
    EXPECT_FALSE(controller.isRunning());
    EXPECT_EQ(controller.progress(), 100);
    EXPECT_EQ(controller.status(), QStringLiteral("回测完成"));
    EXPECT_EQ(controller.backtestResult().value("status").toString(), QStringLiteral("SUCCESS"));
    EXPECT_EQ(controller.backtestResult().value("config").toMap().value("factorId").toString(), QStringLiteral("factor_value"));
    EXPECT_EQ(controller.groupResults().size(), 5);
    EXPECT_DOUBLE_EQ(controller.icirResult().value("icValue").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(controller.summaryStats().value("spreadReturn").toDouble(), 0.068);
    EXPECT_EQ(emittedResult.value("status").toString(), QStringLiteral("SUCCESS"));
    EXPECT_EQ(emittedResult.value("results").toList().size(), 0);
    EXPECT_EQ(emittedResult.value("groups").toList().size(), 5);
}

TEST(FactorBacktestRegressionTest, StartBacktestWithCachedResultReachesCompletionSignal)
{
    QStandardPaths::setTestModeEnabled(true);

    QVariantMap riskConfig;
    riskConfig[QStringLiteral("forwardDays")] = 3;
    riskConfig[QStringLiteral("rebalanceDays")] = 1;
    riskConfig[QStringLiteral("commissionRate")] = 0.001;
    riskConfig[QStringLiteral("slippageRate")] = 0.0;
    riskConfig[QStringLiteral("riskFreeRate")] = 0.0;
    riskConfig[QStringLiteral("benchmarkSymbol")] = QStringLiteral("000300.SH");
    riskConfig[QStringLiteral("stopLossPercent")] = 0.10;
    riskConfig[QStringLiteral("takeProfitPercent")] = 0.20;
    riskConfig[QStringLiteral("maxDrawdownLimit")] = 0.20;
    riskConfig[QStringLiteral("maxDailyLoss")] = 0.20;
    riskConfig[QStringLiteral("maxPositionPercent")] = 0.20;
    riskConfig[QStringLiteral("maxTotalExposure")] = 1.0;

    auto* riskService = RiskConfigService::instance();
    ASSERT_TRUE(riskService->applyConfiguration(riskConfig));

    static int argc = 1;
    static char appName[] = "factor-backtest-test";
    static char* argv[] = {appName, nullptr};
    if (!QCoreApplication::instance()) {
        new QCoreApplication(argc, argv);
    }

    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_momentum_cached_instance");
    const char* configJson = R"JSON({
        "factorType": "momentum",
        "majorCategory": "动量因子",
        "calculation": {
            "type": "simple",
            "window": 4,
            "skipRecent": 0,
            "adjustPriceType": "post_adjust_factor",
            "useVolume": false
        },
        "boundaryRules": {
            "minDataPoints": 4
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("动量因子"), configJson);
    const auto factorInstance = std::make_shared<factor::MomentumFactor>();
    factor::MomentumFactor::Params momentumParams;
    momentumParams.window = 4;
    momentumParams.type = factor::MomentumCalculationType::SIMPLE;
    momentumParams.adjustPriceType = factor::AdjustPriceType::POST_ADJUST_FACTOR;
    momentumParams.useVolume = false;
    momentumParams.skipRecent = 0;
    factorInstance->setParams(momentumParams);

    const QStringList stockCodes{
        QStringLiteral("AAA"),
        QStringLiteral("BBB"),
        QStringLiteral("CCC"),
        QStringLiteral("DDD"),
        QStringLiteral("EEE")
    };

    QVariantList rows;
    std::vector<factor::CachedMarketBar> cachedBars;
    rows.reserve(125);
    cachedBars.reserve(125);
    for (int day = 2; day <= 26; ++day) {
        const QString tradeDate = QStringLiteral("2024-01-%1").arg(day, 2, 10, QLatin1Char('0'));
        for (int symbolIndex = 0; symbolIndex < stockCodes.size(); ++symbolIndex) {
            const QString symbol = stockCodes.at(symbolIndex);
            const double closeValue = 10.0 + static_cast<double>(day - 2) * 0.1 + static_cast<double>(symbolIndex) * 0.25;
            rows.append(QVariantMap{{"symbol", symbol}, {"trade_date", tradeDate}, {"close", closeValue}, {"pre_adjust_factor", 1.0}, {"post_adjust_factor", 1.0}});
            cachedBars.push_back(factor::CachedMarketBar{
                symbol.toStdString(),
                tradeDate.toStdString(),
                closeValue,
                {{"close", closeValue}, {"adj_factor", 1.0}}});
        }
    }
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close"), QStringLiteral("pre_adjust_factor"), QStringLiteral("post_adjust_factor")},
        stockCodes,
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-26"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setDataSourceMode(QStringLiteral("cache"));
    controller.setSelectedDatasetId(datasetId);
    QVariantList selectedStockPoolSymbols;
    for (const auto& symbol : stockCodes) {
        selectedStockPoolSymbols.append(symbol);
    }
    controller.setSelectedStockPoolSymbols(selectedStockPoolSymbols);
    controller.setBacktestRuntimeParams(riskConfig);

    auto threadPool = std::make_shared<foundation::thread::ThreadPoolExecutor>(1);
    auto cacheManager = std::make_shared<FactorCacheManager>();
    cacheManager->setCacheFacade(makeSharedCacheFacade());
    auto instanceManager = std::make_shared<factor::FactorInstanceManager>(nullptr, nullptr);
    factor::FactorInstanceManagerTestAccess::seedInstance(
        *instanceManager,
        instanceId,
        instanceInfo,
        factorInstance);
    FactorBacktestControllerTestAccess::configureSupportMapRuntimeAndOverrides(
        controller,
        instanceManager,
        instanceInfo,
        factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 1);
    FactorBacktestControllerTestAccess::primeAsyncBacktestRuntime(controller, cacheManager, threadPool, instanceManager);

    const QVariantMap cacheSnapshot{
        {QStringLiteral("availableFields"), QVariantList{QStringLiteral("close"), QStringLiteral("pre_adjust_factor"), QStringLiteral("post_adjust_factor")}},
        {QStringLiteral("tradeDateCount"), 25}
    };

    QVariantMap supportMap;
    QEventLoop supportLoop;
    QObject::connect(&controller,
                     &FactorBacktestController::factorSupportMapReady,
                     &supportLoop,
                     [&](quint64 requestId, const QVariantMap& map) {
                         supportMap = map;
                         controller.handleFactorSupportMapReady(static_cast<int>(requestId), map);
                         supportLoop.quit();
                     });
    controller.requestFactorSupportMapAsync(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-26"),
        cacheSnapshot,
        1);
    QTimer::singleShot(5000, &supportLoop, &QEventLoop::quit);
    supportLoop.exec();

    ASSERT_FALSE(supportMap.isEmpty());
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();
    ASSERT_TRUE(supportInfo.value("supported").toBool())
        << " category=" << supportInfo.value("category").toString().toStdString()
        << " reason=" << supportInfo.value("reason").toString().toStdString()
        << " missingFields=" << QJsonDocument::fromVariant(supportInfo.value("missingFields")).toJson(QJsonDocument::Compact).toStdString();
    ASSERT_EQ(supportInfo.value("instanceId").toString(), instanceId);

    BacktestConfig cachedConfig = makeCachedBacktestConfig(instanceId.toStdString());
    cachedConfig.datasetId = datasetId;
    cachedConfig.startDate = "2024-01-02";
    cachedConfig.endDate = "2024-01-26";
    cachedConfig.forwardDays = 3;
    cachedConfig.rebalanceDays = 1;
    cachedConfig.transactionCost = 0.001;
    cachedConfig.slippageRate = 0.0;
    cachedConfig.riskFreeRate = 0.0;
    cachedConfig.benchmarkSymbol = "000300.SH";
    cachedConfig.stopLossRate = 0.10;
    cachedConfig.takeProfitRate = 0.20;
    cachedConfig.maxDrawdownLimit = 0.20;
    cachedConfig.maxDailyLoss = 0.20;
    cachedConfig.maxPositionPercent = 0.20;
    cachedConfig.maxTotalExposure = 1.0;
    cachedConfig.allowedStockCodes = {"AAA", "BBB", "CCC", "DDD", "EEE"};
    cachedConfig.cachedBars = cachedBars;
    seedBacktestResultCache(cacheManager, cachedConfig, makeCachedExecutorResult(instanceId.toStdString(), 0.21, 11));

    QVariantMap completedResult;
    QString failureMessage;
    QEventLoop backtestLoop;
    QObject::connect(&controller,
                     &FactorBacktestController::backtestCompleted,
                     &backtestLoop,
                     [&](const QVariantMap& resultMap) {
                         completedResult = resultMap;
                         backtestLoop.quit();
                     });
    QObject::connect(&controller,
                     &FactorBacktestController::backtestFailed,
                     &backtestLoop,
                     [&](const QString& errorText) {
                         failureMessage = errorText;
                         backtestLoop.quit();
                     });

    controller.startBacktestWithFactors(
        QVariantList{instanceId},
        QStringLiteral("5组"),
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-26"),
        cacheSnapshot);

    QTimer::singleShot(5000, &backtestLoop, &QEventLoop::quit);
    backtestLoop.exec();

    EXPECT_TRUE(failureMessage.isEmpty()) << failureMessage.toStdString();
    ASSERT_FALSE(completedResult.isEmpty());
    EXPECT_EQ(controller.status(), QStringLiteral("回测完成"));
    EXPECT_EQ(controller.progress(), 100);
    EXPECT_FALSE(controller.isRunning());
    EXPECT_EQ(controller.backtestResult().value("status").toString(), QStringLiteral("SUCCESS"));
    EXPECT_EQ(controller.backtestResult().value("results").toList().size(), 0);
    EXPECT_EQ(controller.groupResults().size(), 5);
    EXPECT_EQ(completedResult.value("config").toMap().value("factorId").toString(), instanceId);
    EXPECT_EQ(completedResult.value("groups").toList().size(), 5);
    EXPECT_GT(controller.summaryStats().value("spreadReturn").toDouble(), 0.0);
    EXPECT_GT(controller.icirResult().value("icValue").toDouble(), 0.9);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, SelectedStockPoolSymbolsNormalizesAndDeduplicatesInput)
{
    FactorBacktestController controller;

    controller.setSelectedStockPoolSymbols(QVariantList{
        QStringLiteral(" sz000001 "),
        QStringLiteral("SZ000001"),
        QStringLiteral("SH600000"),
        QStringLiteral(""),
        QVariant()
    });

    ASSERT_EQ(controller.selectedStockPoolSymbols().size(), 2);
    EXPECT_EQ(controller.selectedStockPoolSymbols().at(0).toString(), QStringLiteral("SZ000001"));
    EXPECT_EQ(controller.selectedStockPoolSymbols().at(1).toString(), QStringLiteral("SH600000"));
}

TEST(FactorBacktestRegressionTest, BacktestMetricSyncPersistsWhenResultQualifiedForProduction)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);
    FactorServiceTestAccess::setDomainSyncOverride(*service, [](const QVariantMap&) {
        return true;
    });

    const QVariantMap existingFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("质量因子展示"));
    repository->records.insert(QStringLiteral("factor_quality"), existingFactor);

    ScopedFactorServiceSingletonOverride singletonOverride(service.get());

    BacktestResult result = makeCachedExecutorResult("factor_quality_instance", 0.19, 11);
    result.icirResult.icMean = 0.073;
    result.icirResult.ir = 0.61;
    result.informationRatio = 1.20;
    result.maxDrawdown = 0.12;
    result.turnoverRate = 137.5;

    FactorBacktestController controller;
    FactorBacktestControllerTestAccess::setAppliedRiskConfigOverrideForSync(controller, []() {
        return QVariantMap{};
    });
    const QVariantMap resultMap = FactorBacktestControllerTestAccess::buildResultMap(
        controller,
        QStringLiteral("factor_quality"),
        result);

    EXPECT_DOUBLE_EQ(resultMap.value("turnoverRate").toDouble(), 137.5);
    EXPECT_DOUBLE_EQ(resultMap.value("summary").toMap().value("turnoverRate").toDouble(), 137.5);

    FactorBacktestControllerTestAccess::syncBacktestMetricsToFactor(
        controller,
        QStringLiteral("factor_quality"),
        result);

    ASSERT_EQ(repository->updateCalls, 1);
    const QVariantMap updated = repository->findById(QStringLiteral("factor_quality"));
    EXPECT_DOUBLE_EQ(updated.value("icValue").toDouble(), 0.073);
    EXPECT_DOUBLE_EQ(updated.value("irValue").toDouble(), 0.61);
    EXPECT_DOUBLE_EQ(updated.value("turnoverRate").toDouble(), 137.5);

    const QVariantMap report = service->lastOperationReport();
    EXPECT_EQ(report.value("operation").toString(), QStringLiteral("updateFactor"));
    EXPECT_TRUE(report.value("success").toBool());
    EXPECT_EQ(report.value("stage").toString(), QStringLiteral("completed"));
}

TEST(FactorBacktestRegressionTest, BacktestMetricSyncSkipsPersistenceWhenResultNotQualified)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);
    FactorServiceTestAccess::setDomainSyncOverride(*service, [](const QVariantMap&) {
        return true;
    });

    const QVariantMap existingFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("质量因子展示"));
    repository->records.insert(QStringLiteral("factor_quality"), existingFactor);

    ScopedFactorServiceSingletonOverride singletonOverride(service.get());

    BacktestResult result = makeCachedExecutorResult("factor_quality_instance", 0.19, 11);
    result.icirResult.icMean = 0.015; // 未达到 |IC| > 0.03 的合格阈值
    result.icirResult.ir = 1.23;
    result.informationRatio = 1.30;
    result.turnoverRate = 88.0;

    FactorBacktestController controller;
    FactorBacktestControllerTestAccess::setAppliedRiskConfigOverrideForSync(controller, []() {
        return QVariantMap{};
    });
    FactorBacktestControllerTestAccess::syncBacktestMetricsToFactor(
        controller,
        QStringLiteral("factor_quality"),
        result);

    EXPECT_EQ(repository->updateCalls, 0);
    const QVariantMap unchanged = repository->findById(QStringLiteral("factor_quality"));
    EXPECT_DOUBLE_EQ(unchanged.value("icValue").toDouble(), existingFactor.value("icValue").toDouble());
    EXPECT_DOUBLE_EQ(unchanged.value("irValue").toDouble(), existingFactor.value("irValue").toDouble());
    EXPECT_DOUBLE_EQ(unchanged.value("turnoverRate").toDouble(), existingFactor.value("turnoverRate").toDouble());
}

TEST(FactorBacktestRegressionTest, BacktestMetricSyncSkipsPersistenceWhenIrIsNegative)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);
    FactorServiceTestAccess::setDomainSyncOverride(*service, [](const QVariantMap&) {
        return true;
    });

    const QVariantMap existingFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("质量因子展示"));
    repository->records.insert(QStringLiteral("factor_quality"), existingFactor);

    ScopedFactorServiceSingletonOverride singletonOverride(service.get());

    BacktestResult result = makeCachedExecutorResult("factor_quality_instance", 0.19, 11);
    result.icirResult.icMean = 0.08;
    result.icirResult.ir = -0.20; // IR 不达标
    result.informationRatio = 1.10;
    result.maxDrawdown = 0.10;
    result.turnoverRate = 120.0;

    FactorBacktestController controller;
    FactorBacktestControllerTestAccess::setAppliedRiskConfigOverrideForSync(controller, []() {
        return QVariantMap{};
    });
    FactorBacktestControllerTestAccess::syncBacktestMetricsToFactor(
        controller,
        QStringLiteral("factor_quality"),
        result);

    EXPECT_EQ(repository->updateCalls, 0);
    const QVariantMap unchanged = repository->findById(QStringLiteral("factor_quality"));
    EXPECT_DOUBLE_EQ(unchanged.value("icValue").toDouble(), existingFactor.value("icValue").toDouble());
    EXPECT_DOUBLE_EQ(unchanged.value("irValue").toDouble(), existingFactor.value("irValue").toDouble());
    EXPECT_DOUBLE_EQ(unchanged.value("turnoverRate").toDouble(), existingFactor.value("turnoverRate").toDouble());
}

TEST(FactorBacktestRegressionTest, BacktestMetricSyncSkipsPersistenceWhenProfitFactorIsLow)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);
    FactorServiceTestAccess::setDomainSyncOverride(*service, [](const QVariantMap&) {
        return true;
    });

    const QVariantMap existingFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("质量因子展示"));
    repository->records.insert(QStringLiteral("factor_quality"), existingFactor);

    ScopedFactorServiceSingletonOverride singletonOverride(service.get());

    BacktestResult result = makeCachedExecutorResult("factor_quality_instance", 0.19, 11);
    result.icirResult.icMean = 0.08;
    result.icirResult.ir = 0.61;
    result.informationRatio = 1.20;
    result.maxDrawdown = 0.10;
    result.turnoverRate = 120.0;
    result.profitFactor = 1.20; // 未达到获利因子 > 1.5

    FactorBacktestController controller;
    FactorBacktestControllerTestAccess::setAppliedRiskConfigOverrideForSync(controller, []() {
        return QVariantMap{};
    });
    FactorBacktestControllerTestAccess::syncBacktestMetricsToFactor(
        controller,
        QStringLiteral("factor_quality"),
        result);

    EXPECT_EQ(repository->updateCalls, 0);
    const QVariantMap unchanged = repository->findById(QStringLiteral("factor_quality"));
    EXPECT_DOUBLE_EQ(unchanged.value("icValue").toDouble(), existingFactor.value("icValue").toDouble());
    EXPECT_DOUBLE_EQ(unchanged.value("irValue").toDouble(), existingFactor.value("irValue").toDouble());
    EXPECT_DOUBLE_EQ(unchanged.value("turnoverRate").toDouble(), existingFactor.value("turnoverRate").toDouble());
}

TEST(FactorBacktestRegressionTest, BacktestMetricSyncUsesConfiguredThresholdOverrides)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);
    FactorServiceTestAccess::setDomainSyncOverride(*service, [](const QVariantMap&) {
        return true;
    });

    const QVariantMap existingFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("质量因子展示"));
    repository->records.insert(QStringLiteral("factor_quality"), existingFactor);

    ScopedFactorServiceSingletonOverride singletonOverride(service.get());

    BacktestResult result = makeCachedExecutorResult("factor_quality_instance", 0.19, 11);
    result.icirResult.icMean = 0.08;
    result.icirResult.ir = 0.61;
    result.annualReturn = 0.25;
    result.informationRatio = 1.20;
    result.maxDrawdown = 0.10;
    result.turnoverRate = 150.0;

    FactorBacktestController strictController;
    FactorBacktestControllerTestAccess::setAppliedRiskConfigOverrideForSync(strictController, []() {
        QVariantMap configured;
        configured.insert(QStringLiteral("metricPersistenceMinIr"), 0.70);
        return configured;
    });
    FactorBacktestControllerTestAccess::syncBacktestMetricsToFactor(
        strictController,
        QStringLiteral("factor_quality"),
        result);

    EXPECT_EQ(repository->updateCalls, 0);

    FactorBacktestController relaxedController;
    FactorBacktestControllerTestAccess::setAppliedRiskConfigOverrideForSync(relaxedController, []() {
        QVariantMap configured;
        configured.insert(QStringLiteral("metricPersistenceMinIr"), 0.50);
        return configured;
    });
    FactorBacktestControllerTestAccess::syncBacktestMetricsToFactor(
        relaxedController,
        QStringLiteral("factor_quality"),
        result);

    EXPECT_EQ(repository->updateCalls, 1);
    const QVariantMap updated = repository->findById(QStringLiteral("factor_quality"));
    EXPECT_DOUBLE_EQ(updated.value("irValue").toDouble(), 0.61);
}

TEST(FactorBacktestRegressionTest, BuildResultMapUsesResearchOnlyGroupMetricsWithoutFabricatedRiskValues)
{
    FactorBacktestController controller;
    BacktestResult result = makeCachedExecutorResult("factor_quality_instance", 0.19, 11);

    const QVariantMap resultMap = FactorBacktestControllerTestAccess::buildResultMap(
        controller,
        QStringLiteral("factor_quality"),
        result);

    const QVariantList groups = resultMap.value("groups").toList();
    ASSERT_FALSE(groups.isEmpty());

    const QVariantMap firstGroup = groups.first().toMap();
    EXPECT_DOUBLE_EQ(firstGroup.value("annualizedReturn").toDouble(), 4.2);
    EXPECT_FALSE(firstGroup.value("volatility").isValid());
    EXPECT_FALSE(firstGroup.value("sharpeRatio").isValid());
    EXPECT_FALSE(firstGroup.value("maxDrawdown").isValid());
    EXPECT_FALSE(firstGroup.value("profitFactor").isValid());
    EXPECT_FALSE(firstGroup.value("alpha").isValid());
    EXPECT_FALSE(firstGroup.value("trackingError").isValid());
}

TEST(FactorBacktestRegressionTest, BuildResultMapComputesSummaryMonotonicityAndDiscriminationFromGroups)
{
    FactorBacktestController controller;
    BacktestResult result = makeCachedExecutorResult("factor_quality_instance", 0.19, 11);

    const QVariantMap resultMap = FactorBacktestControllerTestAccess::buildResultMap(
        controller,
        QStringLiteral("factor_quality"),
        result);

    const QVariantMap summary = resultMap.value("summary").toMap();
    EXPECT_LT(summary.value("monotonicity").toDouble(), -0.95);
    EXPECT_NEAR(summary.value("discrimination").toDouble(), 0.0256125, 1e-6);
    EXPECT_DOUBLE_EQ(summary.value("longShortAnnualReturn").toDouble(), 0.19);
}

TEST(FactorBacktestRegressionTest, BuildResultMapIncludesBenchmarkDerivedSummaryMetrics)
{
    FactorBacktestController controller;
    BacktestResult result = makeCachedExecutorResult("factor_quality_instance", 0.19, 11);
    result.benchmarkAnnualReturn = 0.12;
    result.excessAnnualReturn = 0.07;
    result.trackingError = 0.14;
    result.informationRatio = 0.5;
    result.alpha = 0.03;
    result.beta = 0.85;

    const QVariantMap resultMap = FactorBacktestControllerTestAccess::buildResultMap(
        controller,
        QStringLiteral("factor_quality"),
        result);

    const QVariantMap summary = resultMap.value("summary").toMap();
    EXPECT_DOUBLE_EQ(summary.value("benchmarkAnnualReturn").toDouble(), 0.12);
    EXPECT_DOUBLE_EQ(summary.value("excessAnnualReturn").toDouble(), 0.07);
    EXPECT_DOUBLE_EQ(summary.value("trackingError").toDouble(), 0.14);
    EXPECT_DOUBLE_EQ(summary.value("informationRatio").toDouble(), 0.5);
    EXPECT_DOUBLE_EQ(summary.value("alpha").toDouble(), 0.03);
    EXPECT_DOUBLE_EQ(summary.value("beta").toDouble(), 0.85);
}

TEST(FactorBacktestRegressionTest, MomentumFactorSkipRecentUsesTradingDayOffsetAcrossWeekend)
{
    const auto provider = makeCloseSeriesProvider({
        {"2024-01-02", 100.0},
        {"2024-01-03", 110.0},
        {"2024-01-04", 120.0},
        {"2024-01-05", 150.0},
        {"2024-01-08", 210.0},
    });

    MomentumFactor::Params params;
    params.window = 1;
    params.skipRecent = 3;
    params.adjustPriceType = factor::AdjustPriceType::POST_ADJUST_FACTOR;

    const CalculationResult result = calculateMomentum(params, "2024-01-08", provider);

    ASSERT_EQ(result.values.size(), 1U);
    ASSERT_TRUE(result.values.find("AAA") != result.values.end());
    EXPECT_EQ(provider->lastRequestedEndDate, std::string("2024-01-08"));
    EXPECT_NEAR(result.values.at("AAA"), 0.1, 1e-9);
}

TEST(FactorBacktestRegressionTest, MomentumFactorSkipRecentUsesTradingDayOffsetAcrossLongHoliday)
{
    const auto provider = makeCloseSeriesProvider({
        {"2024-09-24", 100.0},
        {"2024-09-25", 110.0},
        {"2024-09-26", 120.0},
        {"2024-09-27", 130.0},
        {"2024-09-30", 140.0},
        {"2024-10-08", 200.0},
    });

    MomentumFactor::Params params;
    params.window = 1;
    params.skipRecent = 3;
    params.adjustPriceType = factor::AdjustPriceType::POST_ADJUST_FACTOR;

    const CalculationResult result = calculateMomentum(params, "2024-10-08", provider);

    ASSERT_EQ(result.values.size(), 1U);
    ASSERT_TRUE(result.values.find("AAA") != result.values.end());
    EXPECT_EQ(provider->lastRequestedEndDate, std::string("2024-10-08"));
    EXPECT_NEAR(result.values.at("AAA"), (120.0 - 110.0) / 110.0, 1e-9);
}

TEST(FactorBacktestRegressionTest, MomentumFactorReportsEmptyReasonWhenHistoryIsInsufficient)
{
    const auto provider = makeCloseSeriesProvider({
        {"2024-01-04", 100.0},
        {"2024-01-05", 101.0},
        {"2024-01-08", 102.0},
    });

    MomentumFactor::Params params;
    params.window = 3;
    params.skipRecent = 1;
    params.adjustPriceType = factor::AdjustPriceType::POST_ADJUST_FACTOR;


    const CalculationResult result = calculateMomentum(params, "2024-01-08", provider);

    EXPECT_TRUE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("emptyReason"));
    EXPECT_NE(result.metadata.get("emptyReason").asString().find("至少 5 个交易日样本"), std::string::npos);
    ASSERT_TRUE(result.metadata.has("skipRecent"));
    EXPECT_EQ(result.metadata.get("skipRecent").asInt(), 1);
}

TEST(FactorBacktestRegressionTest, MomentumFactorCanUseLaggedEffectiveDateFromProvider)
{
    MomentumFactor factor;
    MomentumFactor::Params params;
    params.window = 1;
    params.lookbackPeriod = 3;
    params.laggedEnabled = true;
    params.adjustPriceType = factor::AdjustPriceType::POST_ADJUST_FACTOR;
    factor.setParams(params);

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {{"AAA", {{"2024-01-10", 100.0}, {"2024-01-11", 110.0}, {"2024-01-12", 121.0}}}}},
            {"pre_adjust_factor", {{"AAA", {{"2024-01-10", 1.0}, {"2024-01-11", 1.0}, {"2024-01-12", 1.0}}}}},
            {"post_adjust_factor", {{"AAA", {{"2024-01-10", 1.0}, {"2024-01-11", 1.0}, {"2024-01-12", 1.0}}}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 1U);
    EXPECT_NEAR(result.values.at("AAA"), 0.1, 1e-9);
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-12"));
}

TEST(FactorBacktestRegressionTest, MomentumFactorUsesAdjFactorAsAdjustedPriceInput)
{
    MomentumFactor factor;
    MomentumFactor::Params params;
    params.window = 1;
    params.adjustPriceType = factor::AdjustPriceType::POST_ADJUST_FACTOR;
    factor.setParams(params);

    CalculationContext context;
    context.date = "2024-01-04";
    context.symbols = {"AAA"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {{"AAA", {{"2024-01-03", 100.0}, {"2024-01-04", 110.0}}}}},
            {"pre_adjust_factor", {{"AAA", {{"2024-01-03", 1.0}, {"2024-01-04", 1.0}}}}},
            {"post_adjust_factor", {{"AAA", {{"2024-01-03", 2.0}, {"2024-01-04", 1.0}}}}}
        });

    const factor::DataRequirements requirements = factor.getDataRequirements();
    ASSERT_EQ(requirements.requiredFields.size(), 2U);
    EXPECT_EQ(requirements.requiredFields[0], "close");
    EXPECT_EQ(requirements.requiredFields[1], "post_adjust_factor");

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 1U);
    EXPECT_NEAR(result.values.at("AAA"), -0.45, 1e-9);
    ASSERT_TRUE(result.metadata.has("adjustPriceType"));
    EXPECT_EQ(normalizeMomentumPriceTypeForTest(scalarTextForTest(result.metadata.get("adjustPriceType"))), QStringLiteral("post_adjust_factor"));
}

TEST(FactorBacktestRegressionTest, MomentumFactorCalculationTypeAffectsResult)
{
    MomentumFactor simpleFactor;
    MomentumFactor::Params simpleParams;
    simpleParams.window = 1;
    simpleParams.adjustPriceType = factor::AdjustPriceType::POST_ADJUST_FACTOR;
    simpleParams.type = factor::MomentumCalculationType::SIMPLE;
    simpleFactor.setParams(simpleParams);

    MomentumFactor rankFactor;
    MomentumFactor::Params rankParams = simpleParams;
    rankParams.type = factor::MomentumCalculationType::RANK;
    rankFactor.setParams(rankParams);

    CalculationContext context;
    context.date = "2024-01-04";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {
                {"AAA", {{"2024-01-03", 100.0}, {"2024-01-04", 110.0}}},
                {"BBB", {{"2024-01-03", 100.0}, {"2024-01-04", 120.0}}}
            }},
            {"pre_adjust_factor", {
                {"AAA", {{"2024-01-03", 1.0}, {"2024-01-04", 1.0}}},
                {"BBB", {{"2024-01-03", 1.0}, {"2024-01-04", 1.0}}}
            }},
            {"post_adjust_factor", {
                {"AAA", {{"2024-01-03", 1.0}, {"2024-01-04", 1.0}}},
                {"BBB", {{"2024-01-03", 1.0}, {"2024-01-04", 1.0}}}
            }}
        });

    const CalculationResult simpleResult = simpleFactor.calculate(context);
    const CalculationResult rankResult = rankFactor.calculate(context);

    ASSERT_TRUE(simpleResult.dataStatus.isValid());
    ASSERT_TRUE(rankResult.dataStatus.isValid());
    ASSERT_EQ(simpleResult.values.size(), 2U);
    ASSERT_EQ(rankResult.values.size(), 2U);
    EXPECT_NE(simpleResult.values.at("AAA"), rankResult.values.at("AAA"));
    EXPECT_NE(simpleResult.values.at("BBB"), rankResult.values.at("BBB"));
    EXPECT_LT(simpleResult.values.at("AAA"), simpleResult.values.at("BBB"));
    EXPECT_LT(rankResult.values.at("AAA"), rankResult.values.at("BBB"));
}

TEST(FactorBacktestRegressionTest, MomentumFactorCanApplyMinMaxStandardization)
{
    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    assignHistoricalSeries(fieldSeries, "close", "AAA", {{"2024-01-11", 100.0}, {"2024-01-12", 110.0}});
    assignHistoricalSeries(fieldSeries, "close", "BBB", {{"2024-01-11", 100.0}, {"2024-01-12", 120.0}});
    assignHistoricalSeries(fieldSeries, "close", "CCC", {{"2024-01-11", 100.0}, {"2024-01-12", 130.0}});
    assignHistoricalSeries(fieldSeries, "post_adjust_factor", "AAA", {{"2024-01-11", 1.0}, {"2024-01-12", 1.0}});
    assignHistoricalSeries(fieldSeries, "post_adjust_factor", "BBB", {{"2024-01-11", 1.0}, {"2024-01-12", 1.0}});
    assignHistoricalSeries(fieldSeries, "post_adjust_factor", "CCC", {{"2024-01-11", 1.0}, {"2024-01-12", 1.0}});

    MomentumFactor factor;
    MomentumFactor::Params params;
    params.window = 1;
    params.standardization = factor::CommonStandardization::MINMAX;
    params.adjustPriceType = factor::AdjustPriceType::POST_ADJUST_FACTOR;
    factor.setParams(params);

    CalculationContext context;
    context.date = "2024-01-12";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_GE(result.values.size(), 2U);
    EXPECT_NEAR(result.values.at("AAA"), 0.0, 1e-9);
    EXPECT_NEAR(result.values.at("BBB"), 0.5, 1e-9);
    EXPECT_NEAR(result.values.at("CCC"), 1.0, 1e-9);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))), QStringLiteral("minmax"));
}

TEST(FactorBacktestRegressionTest, MomentumFactorCanApplyHistoricalViewIndustrySizeNeutralization)
{
    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    assignHistoricalSeries(fieldSeries, "close", "AAA", {{"2024-01-11", 100.0}, {"2024-01-12", 130.0}});
    assignHistoricalSeries(fieldSeries, "close", "BBB", {{"2024-01-11", 100.0}, {"2024-01-12", 120.0}});
    assignHistoricalSeries(fieldSeries, "close", "CCC", {{"2024-01-11", 100.0}, {"2024-01-12", 110.0}});
    assignHistoricalSeries(fieldSeries, "pre_adjust_factor", "AAA", {{"2024-01-11", 1.0}, {"2024-01-12", 1.0}});
    assignHistoricalSeries(fieldSeries, "pre_adjust_factor", "BBB", {{"2024-01-11", 1.0}, {"2024-01-12", 1.0}});
    assignHistoricalSeries(fieldSeries, "pre_adjust_factor", "CCC", {{"2024-01-11", 1.0}, {"2024-01-12", 1.0}});
    assignHistoricalSeries(fieldSeries, "post_adjust_factor", "AAA", {{"2024-01-11", 1.0}, {"2024-01-12", 1.0}});
    assignHistoricalSeries(fieldSeries, "post_adjust_factor", "BBB", {{"2024-01-11", 1.0}, {"2024-01-12", 1.0}});
    assignHistoricalSeries(fieldSeries, "post_adjust_factor", "CCC", {{"2024-01-11", 1.0}, {"2024-01-12", 1.0}});
    assignHistoricalSeries(fieldSeries, "post_adjust_factor", "DDD", {{"2024-01-11", 1.0}, {"2024-01-12", 1.0}});
    assignHistoricalSeries(fieldSeries, "market_cap", "AAA", {{"2024-01-12", 100.0}});
    assignHistoricalSeries(fieldSeries, "market_cap", "BBB", {{"2024-01-12", 150.0}});
    assignHistoricalSeries(fieldSeries, "market_cap", "CCC", {{"2024-01-12", 220.0}});
    assignHistoricalSeries(fieldSeries, "market_cap", "DDD", {{"2024-01-12", 300.0}});
    assignHistoricalSeries(fieldSeries, "industry_code", "AAA", {{"2024-01-12", 10.0}});
    assignHistoricalSeries(fieldSeries, "industry_code", "BBB", {{"2024-01-12", 10.0}});
    assignHistoricalSeries(fieldSeries, "industry_code", "CCC", {{"2024-01-12", 20.0}});
    assignHistoricalSeries(fieldSeries, "industry_code", "DDD", {{"2024-01-12", 20.0}});

    MomentumFactor factor;
    MomentumFactor::Params params;
    params.window = 1;
    params.neutralizationEnabled = true;
    params.adjustPriceType = factor::AdjustPriceType::POST_ADJUST_FACTOR;
    factor.setParams(params);

    CalculationContext context;
    context.date = "2024-01-12";
    context.symbols = {"AAA", "BBB", "CCC", "DDD"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(normalizeNeutralizationModeForTest(scalarTextForTest(result.metadata.get("neutralizationMode"))),
              QStringLiteral("historical_view_cross_section_industry_size"));
}

TEST(FactorBacktestRegressionTest, LowVolFactorUsesTrailingTradingDaysAcrossWeekend)
{
    factor::LowVolFactor factor;
    factor::LowVolFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::LOW_VOLATILITY)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("window"), 5},
                {QStringLiteral("components"), intArrayForTest({
                    static_cast<int>(factor::LowVolComponent::VOLATILITY),
                    static_cast<int>(factor::LowVolComponent::DRAWDOWN)})}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {
                {"AAA", {{"2024-01-09", 10.0}, {"2024-01-10", 10.5}, {"2024-01-11", 11.0}, {"2024-01-12", 12.0}, {"2024-01-15", 12.5}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 1U);
    EXPECT_LT(result.values.at("AAA"), 0.0);
    ASSERT_TRUE(result.metadata.has("window"));
    EXPECT_EQ(result.metadata.get("window").asInt(), 5);
}

TEST(FactorBacktestRegressionTest, LowVolFactorCanUseLaggedEffectiveDateFromProvider)
{
    factor::LowVolFactor factor;
    factor::LowVolFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::LOW_VOLATILITY)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("window"), 5},
                {QStringLiteral("components"), intArrayForTest({
                    static_cast<int>(factor::LowVolComponent::VOLATILITY),
                    static_cast<int>(factor::LowVolComponent::DRAWDOWN)})},
                {QStringLiteral("frequency"), 0},
                {QStringLiteral("lookbackPeriod"), 3},
                {QStringLiteral("laggedEnabled"), true},
                {QStringLiteral("standardization"), static_cast<int>(factor::CommonStandardization::NONE)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {
                {"AAA", {{"2024-01-08", 10.0}, {"2024-01-09", 10.2}, {"2024-01-10", 10.4}, {"2024-01-11", 10.7}, {"2024-01-12", 11.0}, {"2024-01-15", 12.5}}},
                {"BBB", {{"2024-01-08", 20.0}, {"2024-01-09", 20.1}, {"2024-01-10", 20.3}, {"2024-01-11", 20.8}, {"2024-01-12", 21.5}, {"2024-01-15", 24.0}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 2U);
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-12"));
}

TEST(FactorBacktestRegressionTest, LowVolFactorCanApplyMinMaxStandardization)
{
    factor::LowVolFactor factor;
    factor::LowVolFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::LOW_VOLATILITY)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("window"), 5},
                {QStringLiteral("components"), intArrayForTest({
                    static_cast<int>(factor::LowVolComponent::VOLATILITY),
                    static_cast<int>(factor::LowVolComponent::DRAWDOWN)})},
                {QStringLiteral("standardization"), static_cast<int>(factor::CommonStandardization::MINMAX)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-12";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {
                {"AAA", {{"2024-01-08", 10.0}, {"2024-01-09", 10.4}, {"2024-01-10", 10.8}, {"2024-01-11", 11.2}, {"2024-01-12", 11.6}}},
                {"BBB", {{"2024-01-08", 10.0}, {"2024-01-09", 10.1}, {"2024-01-10", 10.4}, {"2024-01-11", 10.3}, {"2024-01-12", 10.6}}},
                {"CCC", {{"2024-01-08", 10.0}, {"2024-01-09", 10.02}, {"2024-01-10", 10.01}, {"2024-01-11", 10.03}, {"2024-01-12", 10.02}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_GE(result.values.size(), 2U);
    EXPECT_NEAR(result.values.at("AAA"), 1.0, 1e-9);
    EXPECT_NEAR(result.values.at("BBB"), 0.0, 1e-9);
    EXPECT_GT(result.values.at("CCC"), 0.0);
    EXPECT_LT(result.values.at("CCC"), 1.0);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(
        normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
        QStringLiteral("minmax"));
}

TEST(FactorBacktestRegressionTest, LowVolFactorCanApplyHistoricalViewIndustrySizeNeutralization)
{
    factor::LowVolFactor factor;
    factor::LowVolFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::LOW_VOLATILITY)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("window"), 5},
                {QStringLiteral("components"), intArrayForTest({
                    static_cast<int>(factor::LowVolComponent::VOLATILITY),
                    static_cast<int>(factor::LowVolComponent::DRAWDOWN)})},
                {QStringLiteral("neutralizationEnabled"), true},
                {QStringLiteral("standardization"), static_cast<int>(factor::CommonStandardization::NONE)}
            }}
        }));

    factor::CalculationContext context;
    context.date = "2024-01-12";
    context.symbols = {"AAA", "BBB", "CCC", "DDD"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {
                {"AAA", {{"2024-01-08", 10.0}, {"2024-01-09", 10.8}, {"2024-01-10", 11.6}, {"2024-01-11", 12.4}, {"2024-01-12", 13.2}}},
                {"BBB", {{"2024-01-08", 10.0}, {"2024-01-09", 10.5}, {"2024-01-10", 11.0}, {"2024-01-11", 11.5}, {"2024-01-12", 12.0}}},
                {"CCC", {{"2024-01-08", 10.0}, {"2024-01-09", 10.2}, {"2024-01-10", 10.4}, {"2024-01-11", 10.6}, {"2024-01-12", 10.8}}},
                {"DDD", {{"2024-01-08", 10.0}, {"2024-01-09", 10.1}, {"2024-01-10", 10.2}, {"2024-01-11", 10.3}, {"2024-01-12", 10.4}}},
                {"000300.SH", {{"2024-01-08", 100.0}, {"2024-01-09", 101.0}, {"2024-01-10", 102.0}, {"2024-01-11", 103.0}, {"2024-01-12", 104.0}}}
            }},
            {"market_cap", {
                {"AAA", {{"2024-01-12", 100.0}}},
                {"BBB", {{"2024-01-12", 150.0}}},
                {"CCC", {{"2024-01-12", 220.0}}},
                {"DDD", {{"2024-01-12", 300.0}}}
            }},
            {"industry_code", {
                {"AAA", {{"2024-01-12", 10.0}}},
                {"BBB", {{"2024-01-12", 10.0}}},
                {"CCC", {{"2024-01-12", 20.0}}},
                {"DDD", {{"2024-01-12", 20.0}}}
            }}
        });

    const auto result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(normalizeNeutralizationModeForTest(scalarTextForTest(result.metadata.get("neutralizationMode"))),
              QStringLiteral("historical_view_cross_section_industry_size"));
}

TEST(FactorBacktestRegressionTest, QualityFactorUsesHistoricalViewCrossSectionWithoutDatabaseFallback)
{
    factor::QualityFactor factor;
    factor::QualityFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::QUALITY)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::QualityMetric::ROE)},
                {QStringLiteral("qualityThreshold"), 0.1}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"roe", {
                {"AAA", {{"2024-01-15", 0.22}}},
                {"BBB", {{"2024-01-15", 0.05}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 1U);
    ASSERT_TRUE(result.values.find("AAA") != result.values.end());
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 0.22);
}

TEST(FactorBacktestRegressionTest, QualityFactorCanUseLaggedEffectiveDateWithinLookbackWindow)
{
    factor::QualityFactor factor;
    factor::QualityFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::QUALITY)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::QualityMetric::ROE)},
                {QStringLiteral("frequency"), 0},
                {QStringLiteral("lookbackPeriod"), 3},
                {QStringLiteral("laggedEnabled"), true},
                {QStringLiteral("qualityThreshold"), 0.1}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"roe", {
                {"AAA", {{"2024-01-12", 0.22}, {"2024-01-15", 0.40}}},
                {"BBB", {{"2024-01-12", 0.18}, {"2024-01-15", 0.30}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 2U);
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 0.22);
    EXPECT_DOUBLE_EQ(result.values.at("BBB"), 0.18);
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-12"));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), 3);
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_TRUE(result.metadata.get("laggedEnabled").asBool());
}

TEST(FactorBacktestRegressionTest, QualityFactorCanApplyMinMaxStandardization)
{
    factor::QualityFactor factor;
    factor::QualityFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::QUALITY)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::QualityMetric::ROE)},
                {QStringLiteral("standardization"), static_cast<int>(factor::CommonStandardization::MINMAX)},
                {QStringLiteral("qualityThreshold"), 0.0}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"roe", {{"AAA", 0.10}, {"BBB", 0.20}, {"CCC", 0.40}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_GE(result.values.size(), 2U);
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 0.0);
    EXPECT_DOUBLE_EQ(result.values.at("BBB"), 1.0 / 3.0);
    EXPECT_DOUBLE_EQ(result.values.at("CCC"), 1.0);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(
        normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
        QStringLiteral("minmax"));
}

TEST(FactorBacktestRegressionTest, QualityFactorCanApplyHistoricalViewIndustrySizeNeutralization)
{
    factor::QualityFactor factor;
    factor::QualityFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::QUALITY)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::QualityMetric::ROE)},
                {QStringLiteral("neutralizationEnabled"), true},
                {QStringLiteral("standardization"), static_cast<int>(factor::CommonStandardization::NONE)},
                {QStringLiteral("qualityThreshold"), 0.0}
            }}
        }));

    factor::CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC", "DDD"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"roe", {{"AAA", 0.10}, {"BBB", 0.15}, {"CCC", 0.25}, {"DDD", 0.45}}},
            {"market_cap", {{"AAA", 100.0}, {"BBB", 150.0}, {"CCC", 220.0}, {"DDD", 300.0}}},
            {"industry_code", {{"AAA", 10.0}, {"BBB", 10.0}, {"CCC", 20.0}, {"DDD", 20.0}}}
        });

    const auto result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(normalizeNeutralizationModeForTest(scalarTextForTest(result.metadata.get("neutralizationMode"))),
              QStringLiteral("historical_view_cross_section_industry_size"));
}

TEST(FactorBacktestRegressionTest, ClassicFactorsRequireHistoricalViewRuntime)
{
    static const std::string expectedMessage = QStringLiteral("已移除因子运行期数据库取数路径，请由引擎提供 HistoricalView").toStdString();
    static const std::string momentumExpectedMessage = QStringLiteral("已移除动量因子运行期数据库取数路径，请由引擎提供 HistoricalView").toStdString();
    auto expectRequiresHistoricalView = [](factor::BaseFactor& factor,
                                          const std::string& expectedMessage) {
        CalculationContext context;
        context.date = "2024-01-15";
        context.symbols = {"AAA"};

        const CalculationResult result = factor.calculate(context);

        EXPECT_FALSE(result.dataStatus.isValid());
        EXPECT_EQ(result.dataStatus.availability, factor::DataAvailability::UNAVAILABLE);
        EXPECT_EQ(result.dataStatus.message, expectedMessage);
        ASSERT_TRUE(result.metadata.has("error"));
        EXPECT_EQ(result.metadata.get("error").asString(), expectedMessage);
    };

    factor::ValueFactor valueFactor;
    factor::SizeFactor sizeFactor;
    factor::MomentumFactor momentumFactor;
    factor::LowVolFactor lowVolFactor;
    factor::QualityFactor qualityFactor;

    expectRequiresHistoricalView(valueFactor, expectedMessage);
    expectRequiresHistoricalView(sizeFactor, expectedMessage);
    expectRequiresHistoricalView(momentumFactor, momentumExpectedMessage);
    expectRequiresHistoricalView(lowVolFactor, expectedMessage);
    expectRequiresHistoricalView(qualityFactor, expectedMessage);
}

TEST(FactorBacktestRegressionTest, BuildBacktestConfigRejectsMissingDateWindowWithoutFallback)
{
    FactorBacktestController controller;
    controller.setDataSourceMode(QStringLiteral("database"));

    try {
        Q_UNUSED(FactorBacktestControllerTestAccess::buildBacktestConfig(
            controller,
            QStringLiteral("quality_factor_instance"),
            QStringLiteral("10组"),
            QString(),
            QStringLiteral("2024-01-31")));
        FAIL() << "expected runtime_error for missing start date";
    } catch (const std::runtime_error& e) {
        EXPECT_EQ(QString::fromUtf8(e.what()), QStringLiteral("回测开始日期缺失，禁止使用默认兜底日期"));
    }

    try {
        Q_UNUSED(FactorBacktestControllerTestAccess::buildBacktestConfig(
            controller,
            QStringLiteral("quality_factor_instance"),
            QStringLiteral("10组"),
            QStringLiteral("2024-01-01"),
            QString()));
        FAIL() << "expected runtime_error for missing end date";
    } catch (const std::runtime_error& e) {
        EXPECT_EQ(QString::fromUtf8(e.what()), QStringLiteral("回测结束日期缺失，禁止使用默认兜底日期"));
    }
}

TEST(FactorBacktestRegressionTest, ValueFactorCfPUsesMarketCapAndOperatingCashFlowFromProvider)
{
    factor::ValueFactor factor;
    factor::ValueFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::VALUE)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("valuationMetrics"), intArrayForTest({static_cast<int>(factor::ValuationMetric::CFP)})}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"market_cap", {{"AAA", 120.0}, {"BBB", 200.0}, {"CCC", 80.0}}},
            {"operating_cash_flow", {{"AAA", 40.0}, {"BBB", 100.0}, {"CCC", 0.0}}}
        });

    const factor::DataRequirements requirements = factor.getDataRequirements();
    ASSERT_EQ(requirements.requiredFields.size(), 2U);
    EXPECT_EQ(requirements.requiredFields[0], "market_cap");
    EXPECT_EQ(requirements.requiredFields[1], "operating_cash_flow");

    const CalculationResult result = factor.calculate(context);

    ASSERT_EQ(result.values.size(), 2U);
    EXPECT_NEAR(result.values.at("AAA"), 1.0 / 3.0, 1e-9);
    EXPECT_NEAR(result.values.at("BBB"), 0.5, 1e-9);
    EXPECT_TRUE(result.values.find("CCC") == result.values.end());
}

TEST(FactorBacktestRegressionTest, ValueFactorDividendYieldUsesDirectYieldFromProvider)
{
    factor::ValueFactor factor;
    factor::ValueFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::VALUE)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("valuationMetrics"), intArrayForTest({static_cast<int>(factor::ValuationMetric::DIVIDEND_YIELD)})}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"dividend_yield", {{"AAA", 0.035}, {"BBB", 0.062}, {"CCC", -0.01}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_EQ(result.values.size(), 2U);
    EXPECT_NEAR(result.values.at("AAA"), 0.035, 1e-9);
    EXPECT_NEAR(result.values.at("BBB"), 0.062, 1e-9);
    EXPECT_TRUE(result.values.find("CCC") == result.values.end());
}

TEST(FactorBacktestRegressionTest, ValueFactorCanApplyPercentileRanking)
{
    factor::ValueFactor factor;
    factor::ValueFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::VALUE)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("valuationMetrics"), intArrayForTest({static_cast<int>(factor::ValuationMetric::BP)})},
                {QStringLiteral("standardization"), static_cast<int>(factor::CommonStandardization::PERCENTILE)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"pb_ratio", {{"AAA", 1.0}, {"BBB", 2.0}, {"CCC", 4.0}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_EQ(result.values.size(), 3U);
    EXPECT_DOUBLE_EQ(result.values.at("CCC"), 0.0);
    EXPECT_DOUBLE_EQ(result.values.at("BBB"), 0.5);
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 1.0);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(
        normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
        QStringLiteral("percentile"));
}

TEST(FactorBacktestRegressionTest, ValueFactorCanUseLaggedEffectiveDateFromProvider)
{
    factor::ValueFactor factor;
    factor::ValueFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::VALUE)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("valuationMetrics"), intArrayForTest({static_cast<int>(factor::ValuationMetric::BP)})},
                {QStringLiteral("laggedEnabled"), true},
                {QStringLiteral("frequency"), 0}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"pb_ratio", {
                {"AAA", {{"2024-01-05", 2.0}, {"2024-01-08", 5.0}}},
                {"BBB", {{"2024-01-05", 4.0}, {"2024-01-08", 8.0}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_EQ(result.values.size(), 2U);
    EXPECT_NEAR(result.values.at("AAA"), 0.5, 1e-9);
    EXPECT_NEAR(result.values.at("BBB"), 0.25, 1e-9);
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-05"));
}

TEST(FactorBacktestRegressionTest, ValueFactorMetricWeightsAffectResult)
{
    factor::ValueFactor bpWeightedFactor;
    factor::ValueFactorTestAccess::loadConfig(bpWeightedFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "value",
        "calculation": {
            "valuationMetrics": ["bp", "ep"],
            "bpWeight": 100,
            "epWeight": 0,
            "standardization": "none"
        }
    })JSON"));

    factor::ValueFactor epWeightedFactor;
    factor::ValueFactorTestAccess::loadConfig(epWeightedFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "value",
        "calculation": {
            "valuationMetrics": ["bp", "ep"],
            "bpWeight": 0,
            "epWeight": 100,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"pb_ratio", {{"AAA", 1.0}, {"BBB", 2.0}}},
            {"pe_ratio", {{"AAA", 10.0}, {"BBB", 1.0}}}
        });

    const CalculationResult bpWeightedResult = bpWeightedFactor.calculate(context);
    const CalculationResult epWeightedResult = epWeightedFactor.calculate(context);

    ASSERT_TRUE(bpWeightedResult.dataStatus.isValid());
    ASSERT_TRUE(epWeightedResult.dataStatus.isValid());
    ASSERT_EQ(bpWeightedResult.values.size(), 2U);
    ASSERT_EQ(epWeightedResult.values.size(), 2U);

    EXPECT_GT(bpWeightedResult.values.at("AAA"), bpWeightedResult.values.at("BBB"));
    EXPECT_LT(epWeightedResult.values.at("AAA"), epWeightedResult.values.at("BBB"));
    EXPECT_NE(bpWeightedResult.values.at("AAA"), epWeightedResult.values.at("AAA"));
    EXPECT_NE(bpWeightedResult.values.at("BBB"), epWeightedResult.values.at("BBB"));
}

TEST(FactorBacktestRegressionTest, ValueFactorCanApplyMinMaxStandardization)
{
    factor::ValueFactor factor;
    factor::ValueFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::VALUE)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("valuationMetrics"), intArrayForTest({static_cast<int>(factor::ValuationMetric::BP)})},
                {QStringLiteral("standardization"), static_cast<int>(factor::CommonStandardization::MINMAX)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"pb_ratio", {{"AAA", 1.0}, {"BBB", 2.0}, {"CCC", 4.0}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_EQ(result.values.size(), 3U);
    EXPECT_DOUBLE_EQ(result.values.at("CCC"), 0.0);
    EXPECT_NEAR(result.values.at("BBB"), 1.0 / 3.0, 1e-9);
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 1.0);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))), QStringLiteral("minmax"));
}

TEST(FactorBacktestRegressionTest, ValueFactorBpMetricIsAcceptedByRequirementInferenceAndRuntime)
{
    const QVariantMap calculation{{QStringLiteral("valuationMetrics"), QVariantList{static_cast<int>(factor::ValuationMetric::BP)}}};
    const auto profile = factor::bridge::resolveFactorRequirementProfile(
        factor::FactorType::VALUE,
        calculation);

    EXPECT_TRUE(profile.supported);
    EXPECT_EQ(profile.metric.toInt(), static_cast<int>(factor::ValuationMetric::BP));
    ASSERT_EQ(profile.requiredFields.orderedValues().size(), 1);
    EXPECT_EQ(profile.requiredFields.orderedValues().at(0), QStringLiteral("pb_ratio"));
    EXPECT_FALSE(factor::bridge::requirementFieldSatisfiedByAvailableFields(
        QStringLiteral("bp"),
        QSet<QString>{QStringLiteral("pb_ratio")}));
    EXPECT_EQ(factor::bridge::requirementDiagnosticFields(
                  QStringLiteral("bp"),
                  QSet<QString>{QStringLiteral("pb_ratio")}),
              QStringList{});

    EXPECT_FALSE(factor::bridge::requirementFieldSatisfiedByAvailableFields(
        QStringLiteral("ep"),
        QSet<QString>{QStringLiteral("pe_ratio")}));
    EXPECT_EQ(factor::bridge::requirementDiagnosticFields(
                  QStringLiteral("ep"),
                  QSet<QString>{QStringLiteral("pe_ratio")}),
              QStringList{});
    EXPECT_FALSE(factor::bridge::requirementFieldSatisfiedByAvailableFields(
        QStringLiteral("net_profit_growth"),
        QSet<QString>{QStringLiteral("net_profit")}));
    EXPECT_FALSE(factor::bridge::requirementFieldSatisfiedByAvailableFields(
        QStringLiteral("delta_roe"),
        QSet<QString>{QStringLiteral("roe")}));
    EXPECT_FALSE(factor::bridge::requirementFieldSatisfiedByAvailableFields(
        QStringLiteral("sue"),
        QSet<QString>{QStringLiteral("eps")}));
    EXPECT_FALSE(factor::bridge::requirementFieldSatisfiedByAvailableFields(
        QStringLiteral("gross_margin"),
        QSet<QString>{QStringLiteral("profit_margin")}));
    EXPECT_EQ(factor::bridge::requirementDiagnosticFields(
                  QStringLiteral("gross_margin"),
                  QSet<QString>{QStringLiteral("profit_margin")}),
              QStringList{QStringLiteral("gross_margin")});
    EXPECT_FALSE(factor::bridge::requirementFieldSatisfiedByAvailableFields(
        QStringLiteral("operating_margin"),
        QSet<QString>{QStringLiteral("profit_margin")}));

    factor::ValueFactor factor;
    factor::ValueFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": 0,
        "calculation": {
            "valuationMetrics": [0],
            "standardization": 3
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"pb_ratio", {{"AAA", 1.0}, {"BBB", 2.0}, {"CCC", 4.0}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 3U);
    EXPECT_DOUBLE_EQ(result.values.at("CCC"), 0.0);
    EXPECT_DOUBLE_EQ(result.values.at("BBB"), 0.5);
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 1.0);
    ASSERT_TRUE(result.metadata.has("valuationMetric"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("valuationMetric").asString()), QStringLiteral("bp"));
}

TEST(FactorBacktestRegressionTest, RealValueFactorBpInstanceReplayUsesConfiguredRuntimeParameters)
{
    constexpr const char* kInstanceId = "____________252_1774717000454";

    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        GTEST_SKIP() << "database unavailable";
    }

    const auto configResult = database->executeQuery(
        "SELECT CAST(full_config AS CHAR) AS full_config FROM factor_instance WHERE instance_id = :instance_id LIMIT 1",
        {{":instance_id", QString::fromUtf8(kInstanceId)}});
    if (configResult.isEmpty()) {
        GTEST_SKIP() << "instance not found in local database: " << kInstanceId;
    }

    const auto fullConfig = foundation::json::JsonFacade::parse(configResult.getRow(0).getString("full_config").toStdString());
    ASSERT_TRUE(fullConfig.has("calculation"));
    const auto calculation = fullConfig.get("calculation");

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);
    factor::FactorInstanceManager instanceManager(database, dataChecker);
    auto factorInstance = instanceManager.createInstance(kInstanceId);
    ASSERT_NE(factorInstance, nullptr);

    const auto latestDateResult = database->executeQuery(
        "SELECT MAX(trade_date) AS trade_date FROM daily_bar WHERE pb_ratio IS NOT NULL AND pb_ratio > 0",
        {});
    ASSERT_FALSE(latestDateResult.isEmpty());
    const QString latestDate = latestDateResult.getRow(0).getString("trade_date");
    ASSERT_FALSE(latestDate.isEmpty());

    factor::CalculationContext context;
    context.date = latestDate.toStdString();
    context.symbols = {"AAA", "BBB", "CCC"};
    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    assignHistoricalSeries(fieldSeries, "pb_ratio", "AAA", buildLinearHistoricalSeries(latestDate, 8, 1.20, 0.03));
    assignHistoricalSeries(fieldSeries, "pb_ratio", "BBB", buildLinearHistoricalSeries(latestDate, 8, 1.55, 0.02));
    assignHistoricalSeries(fieldSeries, "pb_ratio", "CCC", buildLinearHistoricalSeries(latestDate, 8, 1.85, 0.01));
    assignHistoricalSeries(fieldSeries, "pe_ratio", "AAA", buildLinearHistoricalSeries(latestDate, 8, 12.0, 0.1));
    assignHistoricalSeries(fieldSeries, "pe_ratio", "BBB", buildLinearHistoricalSeries(latestDate, 8, 16.0, 0.1));
    assignHistoricalSeries(fieldSeries, "pe_ratio", "CCC", buildLinearHistoricalSeries(latestDate, 8, 21.0, 0.1));
    assignHistoricalSeries(fieldSeries, "dividend_yield", "AAA", buildLinearHistoricalSeries(latestDate, 8, 0.020, 0.001));
    assignHistoricalSeries(fieldSeries, "dividend_yield", "BBB", buildLinearHistoricalSeries(latestDate, 8, 0.028, 0.001));
    assignHistoricalSeries(fieldSeries, "dividend_yield", "CCC", buildLinearHistoricalSeries(latestDate, 8, 0.035, 0.001));
    assignHistoricalSeries(fieldSeries, "market_cap", "AAA", buildLinearHistoricalSeries(latestDate, 8, 120.0, 1.5));
    assignHistoricalSeries(fieldSeries, "market_cap", "BBB", buildLinearHistoricalSeries(latestDate, 8, 180.0, 1.2));
    assignHistoricalSeries(fieldSeries, "market_cap", "CCC", buildLinearHistoricalSeries(latestDate, 8, 260.0, 1.0));
    assignHistoricalSeries(fieldSeries, "industry_code", "AAA", buildLinearHistoricalSeries(latestDate, 8, 10.0, 0.0));
    assignHistoricalSeries(fieldSeries, "industry_code", "BBB", buildLinearHistoricalSeries(latestDate, 8, 10.0, 0.0));
    assignHistoricalSeries(fieldSeries, "industry_code", "CCC", buildLinearHistoricalSeries(latestDate, 8, 20.0, 0.0));
    assignHistoricalSeries(fieldSeries, "operating_cash_flow", "AAA", buildLinearHistoricalSeries(latestDate, 8, 8.0, 0.3));
    assignHistoricalSeries(fieldSeries, "operating_cash_flow", "BBB", buildLinearHistoricalSeries(latestDate, 8, 11.0, 0.2));
    assignHistoricalSeries(fieldSeries, "operating_cash_flow", "CCC", buildLinearHistoricalSeries(latestDate, 8, 15.0, 0.2));
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factorInstance->calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("valuationMetric"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("valuationMetric").asString()), QStringLiteral("bp"));
    ASSERT_TRUE(result.metadata.has("frequency"));
    EXPECT_EQ(normalizeConfigurableFrequencyForTest(scalarTextForTest(result.metadata.get("frequency"))), QStringLiteral("daily"));
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_TRUE(result.metadata.get("laggedEnabled").asBool());
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), 252);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))), QStringLiteral("zscore"));
    ASSERT_TRUE(result.metadata.has("neutralizationEnabled"));
    EXPECT_EQ(result.metadata.get("neutralizationEnabled").asBool(), expectedConfigurableNeutralizationEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(normalizeNeutralizationModeForTest(scalarTextForTest(result.metadata.get("neutralizationMode"))),
              expectedConfigurableNeutralizationEnabled(calculation)
                  ? QStringLiteral("historical_view_cross_section_industry_size")
                  : QStringLiteral("disabled"));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));

    const QString effectiveDate = QString::fromStdString(result.metadata.get("effectiveDate").asString());
    EXPECT_FALSE(effectiveDate.isEmpty());
    EXPECT_LE(effectiveDate, latestDate);

    ASSERT_TRUE(calculation.has("neutralizationEnabled"));
    EXPECT_TRUE(calculation.get("neutralizationEnabled").asBool());
    ASSERT_TRUE(calculation.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(expectedConfigurableStandardizationRaw(calculation)), QStringLiteral("zscore"));
}

TEST(FactorBacktestRegressionTest, RealMomentumFactorInstanceReplayUsesConfiguredRuntimeParameters)
{
    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        GTEST_SKIP() << "database unavailable";
    }

    const auto candidate = loadLatestActiveInstanceByCategory(database, QStringLiteral("动量因子"));
    if (!candidate.has_value()) {
        GTEST_SKIP() << "no active momentum factor instance in local database";
    }

    ASSERT_TRUE(candidate->config.has("calculation"));
    const auto calculation = candidate->config.get("calculation");

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);
    factor::FactorInstanceManager instanceManager(database, dataChecker);
    const auto instanceInfo = instanceManager.getInstanceInfo(candidate->instanceId);
    ASSERT_EQ(instanceInfo.factorType, factor::FactorType::MOMENTUM);
    auto factorInstance = instanceManager.createInstance(candidate->instanceId);
    ASSERT_NE(factorInstance, nullptr);

    const QString latestDate = loadLatestTradeDate(database);
    ASSERT_FALSE(latestDate.isEmpty());

    factor::CalculationContext context;
    context.date = latestDate.toStdString();
    context.symbols = {"AAA", "BBB", "CCC"};
    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    for (const std::string& symbol : context.symbols) {
        std::vector<HistoricalDataPoint> closeSeries;
        std::vector<HistoricalDataPoint> preAdjustFactorSeries;
        std::vector<HistoricalDataPoint> postAdjustFactorSeries;
        std::vector<HistoricalDataPoint> volumeSeries;
        std::vector<HistoricalDataPoint> marketCapSeries;
        std::vector<HistoricalDataPoint> industryCodeSeries;
        const double basePrice = symbol == "AAA" ? 10.0 : (symbol == "BBB" ? 12.0 : 14.0);
        const double baseMarketCap = symbol == "AAA" ? 100.0 : (symbol == "BBB" ? 160.0 : 240.0);
        const double industryCode = symbol == "CCC" ? 20.0 : 10.0;
        for (int index = 0; index < 500; ++index) {
            const QDate date = QDate::fromString(QString::fromStdString(context.date), Qt::ISODate).addDays(-499 + index);
            const QString dateString = date.toString(Qt::ISODate);
            const double close = basePrice + static_cast<double>(index) * 0.05;
            closeSeries.push_back({dateString.toStdString(), close});
            preAdjustFactorSeries.push_back({dateString.toStdString(), 1.01});
            postAdjustFactorSeries.push_back({dateString.toStdString(), 1.01});
            volumeSeries.push_back({dateString.toStdString(), 1000.0 + static_cast<double>(index) * 3.0});
            marketCapSeries.push_back({dateString.toStdString(), baseMarketCap + static_cast<double>(index) * 0.5});
            industryCodeSeries.push_back({dateString.toStdString(), industryCode});
        }
        fieldSeries["close"][symbol] = std::move(closeSeries);
        fieldSeries["pre_adjust_factor"][symbol] = std::move(preAdjustFactorSeries);
        fieldSeries["post_adjust_factor"][symbol] = std::move(postAdjustFactorSeries);
        fieldSeries["volume"][symbol] = std::move(volumeSeries);
        if (expectedConfigurableNeutralizationEnabled(calculation)) {
            fieldSeries["market_cap"][symbol] = std::move(marketCapSeries);
            fieldSeries["industry_code"][symbol] = std::move(industryCodeSeries);
        }
    }
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factorInstance->calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("calculationType"));
    EXPECT_EQ(normalizeMomentumTypeForTest(scalarTextForTest(result.metadata.get("calculationType"))),
              normalizeMomentumTypeForTest(expectedMomentumTypeRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("window"));
    EXPECT_EQ(result.metadata.get("window").asInt(), expectedMomentumWindow(calculation));
    ASSERT_TRUE(result.metadata.has("skipRecent"));
    EXPECT_EQ(result.metadata.get("skipRecent").asInt(), expectedMomentumSkipRecent(calculation));
    ASSERT_TRUE(result.metadata.has("adjustPriceType"));
    EXPECT_EQ(normalizeMomentumPriceTypeForTest(scalarTextForTest(result.metadata.get("adjustPriceType"))),
              normalizeMomentumPriceTypeForTest(expectedMomentumPriceTypeRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("useVolume"));
    EXPECT_EQ(result.metadata.get("useVolume").asBool(), expectedMomentumUseVolume(calculation));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_FALSE(QString::fromStdString(result.metadata.get("effectiveDate").asString()).isEmpty());
    ASSERT_TRUE(result.metadata.has("frequency"));
    EXPECT_EQ(normalizeConfigurableFrequencyForTest(scalarTextForTest(result.metadata.get("frequency"))),
              normalizeConfigurableFrequencyForTest(expectedConfigurableFrequencyRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_EQ(result.metadata.get("laggedEnabled").asBool(), expectedConfigurableLaggedEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), expectedConfigurableLookbackPeriod(calculation));
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
              normalizeConfigurableStandardizationForTest(expectedConfigurableStandardizationRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("neutralizationEnabled"));
    EXPECT_EQ(result.metadata.get("neutralizationEnabled").asBool(), expectedConfigurableNeutralizationEnabled(calculation));
}

TEST(FactorBacktestRegressionTest, RealQualityFactorInstanceReplayUsesConfiguredRuntimeParameters)
{
    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        GTEST_SKIP() << "database unavailable";
    }

    const auto candidate = loadLatestActiveInstanceByCategory(database, QStringLiteral("质量因子"));
    if (!candidate.has_value()) {
        GTEST_SKIP() << "no active quality factor instance in local database";
    }

    ASSERT_TRUE(candidate->config.has("calculation"));
    const auto calculation = candidate->config.get("calculation");

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);
    factor::FactorInstanceManager instanceManager(database, dataChecker);
    auto factorInstance = instanceManager.createInstance(candidate->instanceId);
    ASSERT_NE(factorInstance, nullptr);

    const QString latestDate = loadLatestTradeDate(database);
    ASSERT_FALSE(latestDate.isEmpty());

    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    assignHistoricalSeries(fieldSeries, "roe", "AAA", buildLinearHistoricalSeries(latestDate, 400, 0.10, 0.0003));
    assignHistoricalSeries(fieldSeries, "roe", "BBB", buildLinearHistoricalSeries(latestDate, 400, 0.08, 0.00025));
    assignHistoricalSeries(fieldSeries, "roe", "CCC", buildLinearHistoricalSeries(latestDate, 400, 0.06, 0.0002));
    assignHistoricalSeries(fieldSeries, "roa", "AAA", buildLinearHistoricalSeries(latestDate, 400, 0.05, 0.0002));
    assignHistoricalSeries(fieldSeries, "roa", "BBB", buildLinearHistoricalSeries(latestDate, 400, 0.04, 0.00015));
    assignHistoricalSeries(fieldSeries, "roa", "CCC", buildLinearHistoricalSeries(latestDate, 400, 0.03, 0.0001));
    assignHistoricalSeries(fieldSeries, "profit_margin", "AAA", buildLinearHistoricalSeries(latestDate, 400, 0.14, 0.00025));
    assignHistoricalSeries(fieldSeries, "profit_margin", "BBB", buildLinearHistoricalSeries(latestDate, 400, 0.11, 0.0002));
    assignHistoricalSeries(fieldSeries, "profit_margin", "CCC", buildLinearHistoricalSeries(latestDate, 400, 0.09, 0.00015));
    assignHistoricalSeries(fieldSeries, "net_profit", "AAA", buildLinearHistoricalSeries(latestDate, 400, 12.0, 0.03));
    assignHistoricalSeries(fieldSeries, "net_profit", "BBB", buildLinearHistoricalSeries(latestDate, 400, 10.0, 0.025));
    assignHistoricalSeries(fieldSeries, "net_profit", "CCC", buildLinearHistoricalSeries(latestDate, 400, 8.0, 0.02));
    assignHistoricalSeries(fieldSeries, "equity", "AAA", buildLinearHistoricalSeries(latestDate, 400, 70.0, 0.08));
    assignHistoricalSeries(fieldSeries, "equity", "BBB", buildLinearHistoricalSeries(latestDate, 400, 80.0, 0.07));
    assignHistoricalSeries(fieldSeries, "equity", "CCC", buildLinearHistoricalSeries(latestDate, 400, 90.0, 0.06));
    assignHistoricalSeries(fieldSeries, "market_cap", "AAA", buildLinearHistoricalSeries(latestDate, 400, 120.0, 0.3));
    assignHistoricalSeries(fieldSeries, "market_cap", "BBB", buildLinearHistoricalSeries(latestDate, 400, 180.0, 0.25));
    assignHistoricalSeries(fieldSeries, "market_cap", "CCC", buildLinearHistoricalSeries(latestDate, 400, 260.0, 0.2));
    assignHistoricalSeries(fieldSeries, "industry_code", "AAA", buildLinearHistoricalSeries(latestDate, 400, 10.0, 0.0));
    assignHistoricalSeries(fieldSeries, "industry_code", "BBB", buildLinearHistoricalSeries(latestDate, 400, 10.0, 0.0));
    assignHistoricalSeries(fieldSeries, "industry_code", "CCC", buildLinearHistoricalSeries(latestDate, 400, 20.0, 0.0));

    factor::CalculationContext context;
    context.date = latestDate.toStdString();
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factorInstance->calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("metric"));
    EXPECT_EQ(normalizeQualityMetricForTest(scalarTextForTest(result.metadata.get("metric"))),
              normalizeQualityMetricForTest(expectedQualityMetricRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("frequency"));
    EXPECT_EQ(normalizeConfigurableFrequencyForTest(scalarTextForTest(result.metadata.get("frequency"))),
              normalizeConfigurableFrequencyForTest(expectedQualityFrequency(calculation)));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), expectedConfigurableLookbackPeriod(calculation));
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_EQ(result.metadata.get("laggedEnabled").asBool(), expectedConfigurableLaggedEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
              normalizeConfigurableStandardizationForTest(
                  expectedConfigurableStandardizationRaw(calculation).empty() ? std::string("none") : expectedConfigurableStandardizationRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("neutralizationEnabled"));
    EXPECT_EQ(result.metadata.get("neutralizationEnabled").asBool(), expectedConfigurableNeutralizationEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("qualityThreshold"));
    EXPECT_DOUBLE_EQ(result.metadata.get("qualityThreshold").asDouble(), expectedQualityThreshold(calculation));
}

TEST(FactorBacktestRegressionTest, QualityFactorMetricSelectionAffectsResult)
{
    factor::QualityFactor roeFactor;
    factor::QualityFactorTestAccess::loadConfig(
        roeFactor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::QUALITY)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::QualityMetric::ROE)},
                {QStringLiteral("qualityThreshold"), 0.0}
            }}
        }));

    factor::QualityFactor grossMarginFactor;
    factor::QualityFactorTestAccess::loadConfig(
        grossMarginFactor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::QUALITY)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::QualityMetric::GROSS_MARGIN)},
                {QStringLiteral("qualityThreshold"), 0.0}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"roe", {{"AAA", 0.30}, {"BBB", 0.12}}},
            {"profit_margin", {{"AAA", 0.08}, {"BBB", 0.35}}}
        });

    const CalculationResult roeResult = roeFactor.calculate(context);
    const CalculationResult grossMarginResult = grossMarginFactor.calculate(context);

    ASSERT_TRUE(roeResult.dataStatus.isValid());
    ASSERT_TRUE(grossMarginResult.dataStatus.isValid());
    ASSERT_EQ(roeResult.values.size(), 2U);
    ASSERT_EQ(grossMarginResult.values.size(), 2U);
    EXPECT_GT(roeResult.values.at("AAA"), roeResult.values.at("BBB"));
    EXPECT_LT(grossMarginResult.values.at("AAA"), grossMarginResult.values.at("BBB"));
}

TEST(FactorBacktestRegressionTest, RealLowVolFactorInstanceReplayUsesConfiguredRuntimeParameters)
{
    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        GTEST_SKIP() << "database unavailable";
    }

    const auto candidate = loadLatestActiveInstanceByCategory(database, QStringLiteral("低波因子"));
    if (!candidate.has_value()) {
        GTEST_SKIP() << "no active low-vol factor instance in local database";
    }

    ASSERT_TRUE(candidate->config.has("calculation"));
    const auto calculation = candidate->config.get("calculation");

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);
    factor::FactorInstanceManager instanceManager(database, dataChecker);
    auto factorInstance = instanceManager.createInstance(candidate->instanceId);
    ASSERT_NE(factorInstance, nullptr);
    const std::string benchmarkSymbol = expectedBenchmarkSymbol(calculation);

    const QString latestDate = loadLatestTradeDate(database);
    ASSERT_FALSE(latestDate.isEmpty());

    factor::CalculationContext context;
    context.date = latestDate.toStdString();
    context.symbols = {"AAA", "BBB", "CCC"};
    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    assignHistoricalSeries(fieldSeries, "close", "AAA", buildLinearHistoricalSeries(latestDate, 80, 10.0, 0.08));
    assignHistoricalSeries(fieldSeries, "close", "BBB", buildLinearHistoricalSeries(latestDate, 80, 14.0, 0.06));
    assignHistoricalSeries(fieldSeries, "close", "CCC", buildLinearHistoricalSeries(latestDate, 80, 18.0, 0.05));
    assignHistoricalSeries(fieldSeries, "close", benchmarkSymbol, buildLinearHistoricalSeries(latestDate, 80, 100.0, 0.4));
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factorInstance->calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("window"));
    EXPECT_EQ(result.metadata.get("window").asInt(), expectedLowVolWindow(calculation));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_FALSE(QString::fromStdString(result.metadata.get("effectiveDate").asString()).isEmpty());
    ASSERT_TRUE(result.metadata.has("frequency"));
    EXPECT_EQ(normalizeConfigurableFrequencyForTest(scalarTextForTest(result.metadata.get("frequency"))),
              normalizeConfigurableFrequencyForTest(expectedConfigurableFrequencyRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_EQ(result.metadata.get("laggedEnabled").asBool(), expectedConfigurableLaggedEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), expectedConfigurableLookbackPeriod(calculation));
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
              normalizeConfigurableStandardizationForTest(expectedConfigurableStandardizationRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("neutralizationEnabled"));
    EXPECT_EQ(result.metadata.get("neutralizationEnabled").asBool(), expectedConfigurableNeutralizationEnabled(calculation));
}

TEST(FactorBacktestRegressionTest, RealLiquidityFactorInstanceReplayUsesConfiguredRuntimeParameters)
{
    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        GTEST_SKIP() << "database unavailable";
    }

    const auto candidate = loadLatestActiveInstanceByCategory(database, QStringLiteral("流动性因子"));
    if (!candidate.has_value()) {
        GTEST_SKIP() << "no active liquidity factor instance in local database";
    }

    ASSERT_TRUE(candidate->config.has("calculation"));
    const auto calculation = candidate->config.get("calculation");

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);
    factor::FactorInstanceManager instanceManager(database, dataChecker);
    auto factorInstance = instanceManager.createInstance(candidate->instanceId);
    ASSERT_NE(factorInstance, nullptr);

    const QString latestDate = loadLatestTradeDate(database);
    ASSERT_FALSE(latestDate.isEmpty());

    factor::CalculationContext context;
    context.date = latestDate.toStdString();
    context.symbols = {"AAA", "BBB", "CCC"};
    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    assignHistoricalSeries(fieldSeries, "close", "AAA", buildLinearHistoricalSeries(latestDate, 40, 10.0, 0.07));
    assignHistoricalSeries(fieldSeries, "close", "BBB", buildLinearHistoricalSeries(latestDate, 40, 12.0, 0.05));
    assignHistoricalSeries(fieldSeries, "close", "CCC", buildLinearHistoricalSeries(latestDate, 40, 14.0, 0.06));
    assignHistoricalSeries(fieldSeries, "volume", "AAA", buildLinearHistoricalSeries(latestDate, 40, 1200.0, 20.0));
    assignHistoricalSeries(fieldSeries, "volume", "BBB", buildLinearHistoricalSeries(latestDate, 40, 1800.0, 18.0));
    assignHistoricalSeries(fieldSeries, "volume", "CCC", buildLinearHistoricalSeries(latestDate, 40, 2400.0, 15.0));
    assignHistoricalSeries(fieldSeries, "turnover_rate", "AAA", buildLinearHistoricalSeries(latestDate, 40, 2.0, 0.03));
    assignHistoricalSeries(fieldSeries, "turnover_rate", "BBB", buildLinearHistoricalSeries(latestDate, 40, 2.8, 0.02));
    assignHistoricalSeries(fieldSeries, "turnover_rate", "CCC", buildLinearHistoricalSeries(latestDate, 40, 3.4, 0.01));
    assignHistoricalSeries(fieldSeries, "amplitude", "AAA", buildLinearHistoricalSeries(latestDate, 40, 0.03, 0.0005));
    assignHistoricalSeries(fieldSeries, "amplitude", "BBB", buildLinearHistoricalSeries(latestDate, 40, 0.04, 0.0004));
    assignHistoricalSeries(fieldSeries, "amplitude", "CCC", buildLinearHistoricalSeries(latestDate, 40, 0.05, 0.0003));
    assignHistoricalSeries(fieldSeries, "market_cap", "AAA", buildLinearHistoricalSeries(latestDate, 40, 100.0, 1.0));
    assignHistoricalSeries(fieldSeries, "market_cap", "BBB", buildLinearHistoricalSeries(latestDate, 40, 200.0, 1.5));
    assignHistoricalSeries(fieldSeries, "market_cap", "CCC", buildLinearHistoricalSeries(latestDate, 40, 300.0, 2.0));
    assignHistoricalSeries(fieldSeries, "industry_code", "AAA", buildLinearHistoricalSeries(latestDate, 40, 10.0, 0.0));
    assignHistoricalSeries(fieldSeries, "industry_code", "BBB", buildLinearHistoricalSeries(latestDate, 40, 10.0, 0.0));
    assignHistoricalSeries(fieldSeries, "industry_code", "CCC", buildLinearHistoricalSeries(latestDate, 40, 20.0, 0.0));
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factorInstance->calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("metric"));
    EXPECT_EQ(normalizeLiquidityMetricForTest(scalarTextForTest(result.metadata.get("metric"))),
              normalizeLiquidityMetricForTest(expectedLiquidityMetricRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("window"));
    EXPECT_EQ(result.metadata.get("window").asInt(), expectedConfigurableWindow(calculation));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_FALSE(QString::fromStdString(result.metadata.get("effectiveDate").asString()).isEmpty());
    ASSERT_TRUE(result.metadata.has("frequency"));
    EXPECT_EQ(normalizeConfigurableFrequencyForTest(scalarTextForTest(result.metadata.get("frequency"))),
              normalizeConfigurableFrequencyForTest(expectedConfigurableFrequencyRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_EQ(result.metadata.get("laggedEnabled").asBool(), expectedConfigurableLaggedEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), expectedConfigurableLookbackPeriod(calculation));
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
              normalizeConfigurableStandardizationForTest(expectedConfigurableStandardizationRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("neutralizationEnabled"));
    EXPECT_EQ(result.metadata.get("neutralizationEnabled").asBool(), expectedConfigurableNeutralizationEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(normalizeNeutralizationModeForTest(scalarTextForTest(result.metadata.get("neutralizationMode"))),
              expectedConfigurableNeutralizationEnabled(calculation)
                  ? QStringLiteral("historical_view_cross_section_industry_size")
                  : QStringLiteral("disabled"));
}

TEST(FactorBacktestRegressionTest, RealSizeFactorInstanceReplayUsesConfiguredRuntimeParameters)
{
    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        GTEST_SKIP() << "database unavailable";
    }

    const auto candidate = loadLatestActiveInstanceByCategory(database, QStringLiteral("规模因子"));
    if (!candidate.has_value()) {
        GTEST_SKIP() << "no active size factor instance in local database";
    }

    ASSERT_TRUE(candidate->config.has("calculation"));
    const auto calculation = candidate->config.get("calculation");

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);
    factor::FactorInstanceManager instanceManager(database, dataChecker);
    auto factorInstance = instanceManager.createInstance(candidate->instanceId);
    ASSERT_NE(factorInstance, nullptr);

    const QString latestDate = loadLatestTradeDate(database);
    ASSERT_FALSE(latestDate.isEmpty());

    factor::CalculationContext context;
    context.date = latestDate.toStdString();
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"market_cap", {{"AAA", 120.0}, {"BBB", 180.0}, {"CCC", 260.0}}},
            {"circulating_market_cap", {{"AAA", 90.0}, {"BBB", 140.0}, {"CCC", 210.0}}},
            {"total_assets", {{"AAA", 300.0}, {"BBB", 420.0}, {"CCC", 600.0}}},
            {"industry_code", {{"AAA", 10.0}, {"BBB", 10.0}, {"CCC", 20.0}}}
        });

    const auto result = factorInstance->calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("sizeMetric"));
    EXPECT_EQ(normalizeSizeMetricForTest(scalarTextForTest(result.metadata.get("sizeMetric"))),
              normalizeSizeMetricForTest(expectedSizeMetricRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("logTransform"));
    EXPECT_EQ(result.metadata.get("logTransform").asBool(), expectedSizeLogTransform(calculation));
    ASSERT_TRUE(result.metadata.has("frequency"));
    EXPECT_EQ(normalizeConfigurableFrequencyForTest(scalarTextForTest(result.metadata.get("frequency"))),
              QStringLiteral("daily"));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), expectedConfigurableLookbackPeriod(calculation));
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_EQ(result.metadata.get("laggedEnabled").asBool(), expectedConfigurableLaggedEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
              normalizeConfigurableStandardizationForTest(expectedConfigurableStandardizationRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("neutralizationEnabled"));
    EXPECT_EQ(result.metadata.get("neutralizationEnabled").asBool(), expectedConfigurableNeutralizationEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(normalizeNeutralizationModeForTest(scalarTextForTest(result.metadata.get("neutralizationMode"))),
              expectedConfigurableNeutralizationEnabled(calculation)
                  ? QStringLiteral("historical_view_cross_section_industry_size")
                  : QStringLiteral("disabled"));
    ASSERT_TRUE(result.metadata.has("symbolCount"));
    EXPECT_EQ(result.metadata.get("symbolCount").asInt(), static_cast<int>(result.values.size()));
}

TEST(FactorBacktestRegressionTest, ValueFactorCanApplyHistoricalViewIndustrySizeNeutralization)
{
    factor::ValueFactor factor;
    factor::ValueFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::VALUE)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("valuationMetrics"), intArrayForTest({static_cast<int>(factor::ValuationMetric::BP)})},
                {QStringLiteral("neutralizationEnabled"), true},
                {QStringLiteral("standardization"), static_cast<int>(factor::CommonStandardization::NONE)}
            }}
        }));

    factor::CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC", "DDD"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"pb_ratio", {{"AAA", 1.0}, {"BBB", 1.5}, {"CCC", 2.0}, {"DDD", 2.5}}},
            {"market_cap", {{"AAA", 100.0}, {"BBB", 150.0}, {"CCC", 220.0}, {"DDD", 300.0}}},
            {"industry_code", {{"AAA", 10.0}, {"BBB", 10.0}, {"CCC", 20.0}, {"DDD", 20.0}}}
        });

    const auto result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(normalizeNeutralizationModeForTest(scalarTextForTest(result.metadata.get("neutralizationMode"))),
              QStringLiteral("historical_view_cross_section_industry_size"));
}

TEST(FactorBacktestRegressionTest, SizeFactorCanApplyHistoricalViewIndustrySizeNeutralization)
{
    factor::SizeFactor factor;
    factor::SizeFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::SIZE)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("sizeMetric"), static_cast<int>(factor::SizeMetric::MARKET_CAP)},
                {QStringLiteral("neutralizationEnabled"), true},
                {QStringLiteral("standardization"), static_cast<int>(factor::CommonStandardization::NONE)}
            }}
        }));

    factor::CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC", "DDD"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"market_cap", {{"AAA", 100.0}, {"BBB", 150.0}, {"CCC", 220.0}, {"DDD", 300.0}}},
            {"industry_code", {{"AAA", 10.0}, {"BBB", 10.0}, {"CCC", 20.0}, {"DDD", 20.0}}}
        });

    const auto result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(normalizeNeutralizationModeForTest(scalarTextForTest(result.metadata.get("neutralizationMode"))),
              QStringLiteral("historical_view_cross_section_industry_size"));
}

TEST(FactorBacktestRegressionTest, SizeFactorCanUseLaggedEffectiveDateFromProvider)
{
    factor::SizeFactor factor;
    factor::SizeFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "size",
        "calculation": {
            "sizeMetric": "market_cap",
            "frequency": "daily",
            "lookbackPeriod": 3,
            "laggedEnabled": true,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"market_cap", {
                {"AAA", {{"2024-01-12", 100.0}, {"2024-01-15", 120.0}}},
                {"BBB", {{"2024-01-12", 200.0}, {"2024-01-15", 240.0}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 2U);
    EXPECT_NEAR(result.values.at("AAA"), -std::log(100.0), 1e-9);
    EXPECT_NEAR(result.values.at("BBB"), -std::log(200.0), 1e-9);
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-12"));
}

TEST(FactorBacktestRegressionTest, LowVolFactorComponentWeightsAffectResult)
{
    factor::LowVolFactor volatilityWeightedFactor;
    factor::LowVolFactorTestAccess::loadConfig(volatilityWeightedFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "low_volatility",
        "calculation": {
            "window": 5,
            "components": ["volatility", "drawdown"],
            "volatilityWeight": 100,
            "drawdownWeight": 0,
            "betaWeight": 0,
            "standardization": "none"
        }
    })JSON"));

    factor::LowVolFactor drawdownWeightedFactor;
    factor::LowVolFactorTestAccess::loadConfig(drawdownWeightedFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "low_volatility",
        "calculation": {
            "window": 5,
            "components": ["volatility", "drawdown"],
            "volatilityWeight": 0,
            "drawdownWeight": 100,
            "betaWeight": 0,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-12";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {
                {"AAA", {{"2024-01-08", 10.0}, {"2024-01-09", 9.0}, {"2024-01-10", 8.0}, {"2024-01-11", 7.0}, {"2024-01-12", 6.0}}},
                {"BBB", {{"2024-01-08", 10.0}, {"2024-01-09", 13.0}, {"2024-01-10", 12.0}, {"2024-01-11", 11.0}, {"2024-01-12", 10.0}}}
            }}
        });

    const CalculationResult volatilityWeightedResult = volatilityWeightedFactor.calculate(context);
    const CalculationResult drawdownWeightedResult = drawdownWeightedFactor.calculate(context);

    ASSERT_TRUE(volatilityWeightedResult.dataStatus.isValid());
    ASSERT_TRUE(drawdownWeightedResult.dataStatus.isValid());
    ASSERT_EQ(volatilityWeightedResult.values.size(), 2U);
    ASSERT_EQ(drawdownWeightedResult.values.size(), 2U);

    EXPECT_GT(volatilityWeightedResult.values.at("AAA"), volatilityWeightedResult.values.at("BBB"));
    EXPECT_LT(drawdownWeightedResult.values.at("AAA"), drawdownWeightedResult.values.at("BBB"));
    EXPECT_NE(volatilityWeightedResult.values.at("AAA"), drawdownWeightedResult.values.at("AAA"));
    EXPECT_NE(volatilityWeightedResult.values.at("BBB"), drawdownWeightedResult.values.at("BBB"));
}

TEST(FactorBacktestRegressionTest, SizeFactorCanApplyMinMaxStandardization)
{
    factor::SizeFactor factor;
    factor::SizeFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::SIZE)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("sizeMetric"), static_cast<int>(factor::SizeMetric::MARKET_CAP)},
                {QStringLiteral("logTransform"), false},
                {QStringLiteral("standardization"), static_cast<int>(factor::CommonStandardization::MINMAX)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"market_cap", {{"AAA", 100.0}, {"BBB", 200.0}, {"CCC", 400.0}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 3U);
    EXPECT_DOUBLE_EQ(result.values.at("CCC"), 0.0);
    EXPECT_NEAR(result.values.at("BBB"), 2.0 / 3.0, 1e-9);
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 1.0);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))), QStringLiteral("minmax"));
}

TEST(FactorBacktestRegressionTest, RealGrowthFactorInstanceReplayUsesConfiguredRuntimeParameters)
{
    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        GTEST_SKIP() << "database unavailable";
    }

    const auto replayHandle = ensureReplayInstanceByCategory(
        database,
        QStringLiteral("成长因子"),
        QJsonObject{
            {QStringLiteral("factorType"), QStringLiteral("growth")},
            {QStringLiteral("majorCategory"), QStringLiteral("成长因子")},
            {QStringLiteral("factorName"), QStringLiteral("临时成长因子回放")},
            {QStringLiteral("displayName"), QStringLiteral("临时成长因子回放")},
            {QStringLiteral("calculation"), QJsonObject{{QStringLiteral("growthMetrics"), QJsonArray{QStringLiteral("revenue_growth")}}, {QStringLiteral("growthWeights"), QJsonArray{1.0}}, {QStringLiteral("lookbackPeriod"), 252}}},
            {QStringLiteral("dataRequirements"), QJsonObject{{QStringLiteral("required"), QJsonArray{QStringLiteral("total_revenue")}}}},
            {QStringLiteral("boundaryRules"), QJsonObject{{QStringLiteral("minDataPoints"), 2}}}
        },
        QStringLiteral("临时成长因子回放"));
    const auto& candidate = replayHandle.candidate;
    if (!candidate.has_value()) {
        GTEST_SKIP() << "no growth factor definition available in local database";
    }

    const auto growthConfigResult = database->executeQuery(
        QStringLiteral("SELECT CAST(full_config AS CHAR) AS full_config FROM factor_instance WHERE instance_id = :instance_id LIMIT 1"),
        {{QStringLiteral(":instance_id"), QString::fromStdString(candidate->instanceId)}});
    ASSERT_FALSE(growthConfigResult.isEmpty());
    const auto persistedGrowthConfig = foundation::json::JsonFacade::parse(
        growthConfigResult.getRow(0).getString(QStringLiteral("full_config")).toStdString());
    ASSERT_TRUE(persistedGrowthConfig.has("factorType"));
    EXPECT_TRUE(persistedGrowthConfig.get("factorType").isNumber());
    ASSERT_TRUE(persistedGrowthConfig.has("calculation"));
    const auto persistedGrowthCalculation = persistedGrowthConfig.get("calculation");
    ASSERT_TRUE(persistedGrowthCalculation.has("growthMetrics"));
    ASSERT_TRUE(persistedGrowthCalculation.get("growthMetrics").isArray());
    ASSERT_GT(persistedGrowthCalculation.get("growthMetrics").size(), 0U);
    EXPECT_TRUE(persistedGrowthCalculation.get("growthMetrics").at(0).isNumber());
    ASSERT_TRUE(persistedGrowthCalculation.has("growthWeights"));
    ASSERT_TRUE(persistedGrowthCalculation.get("growthWeights").isArray());
    ASSERT_GT(persistedGrowthCalculation.get("growthWeights").size(), 0U);

    ASSERT_TRUE(candidate->config.has("calculation"));
    const auto calculation = candidate->config.get("calculation");
    ASSERT_TRUE(calculation.has("growthMetrics"));
    ASSERT_TRUE(calculation.get("growthMetrics").isArray());
    ASSERT_GT(calculation.get("growthMetrics").size(), 0U);
    EXPECT_TRUE(calculation.get("growthMetrics").at(0).isNumber());
    ASSERT_TRUE(calculation.has("growthWeights"));
    ASSERT_TRUE(calculation.get("growthWeights").isArray());
    ASSERT_GT(calculation.get("growthWeights").size(), 0U);

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);
    auto factorInstance = factor::TestConfigurableFactorHarness::create(
        candidate->instanceId,
        candidate->instanceName.toStdString(),
        candidate->instanceName.toStdString(),
        candidate->config,
        factor::config::factorTypeFromConfig(candidate->config),
        dataChecker);
    ASSERT_NE(factorInstance, nullptr);

    const QString latestDate = loadLatestTradeDate(database);
    ASSERT_FALSE(latestDate.isEmpty());

    factor::CalculationContext context;
    context.date = latestDate.toStdString();
    context.symbols = {"AAA", "BBB", "CCC"};
    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    assignHistoricalSeries(fieldSeries, "total_revenue", "AAA", buildLinearHistoricalSeries(latestDate, 5, 100.0, 8.0));
    assignHistoricalSeries(fieldSeries, "total_revenue", "BBB", buildLinearHistoricalSeries(latestDate, 5, 120.0, 6.0));
    assignHistoricalSeries(fieldSeries, "total_revenue", "CCC", buildLinearHistoricalSeries(latestDate, 5, 140.0, 5.0));
    assignHistoricalSeries(fieldSeries, "net_profit", "AAA", buildLinearHistoricalSeries(latestDate, 5, 10.0, 1.1));
    assignHistoricalSeries(fieldSeries, "net_profit", "BBB", buildLinearHistoricalSeries(latestDate, 5, 13.0, 0.8));
    assignHistoricalSeries(fieldSeries, "net_profit", "CCC", buildLinearHistoricalSeries(latestDate, 5, 16.0, 0.7));
    assignHistoricalSeries(fieldSeries, "roe", "AAA", buildLinearHistoricalSeries(latestDate, 5, 0.10, 0.01));
    assignHistoricalSeries(fieldSeries, "roe", "BBB", buildLinearHistoricalSeries(latestDate, 5, 0.12, 0.008));
    assignHistoricalSeries(fieldSeries, "roe", "CCC", buildLinearHistoricalSeries(latestDate, 5, 0.14, 0.007));
    assignHistoricalSeries(fieldSeries, "eps", "AAA", buildLinearHistoricalSeries(latestDate, 5, 0.80, 0.06));
    assignHistoricalSeries(fieldSeries, "eps", "BBB", buildLinearHistoricalSeries(latestDate, 5, 0.90, 0.05));
    assignHistoricalSeries(fieldSeries, "eps", "CCC", buildLinearHistoricalSeries(latestDate, 5, 1.00, 0.04));
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factorInstance->calculate(context);

    if (expectedConfigurableNeutralizationEnabled(calculation)) {
        EXPECT_FALSE(result.dataStatus.isValid());
        EXPECT_TRUE(result.values.empty());
        ASSERT_TRUE(result.metadata.has("error"));
        EXPECT_FALSE(QString::fromStdString(result.metadata.get("error").asString()).isEmpty());
        return;
    }

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("metric"));
    EXPECT_EQ(normalizeGrowthMetricForTest(scalarTextForTest(result.metadata.get("metric"))),
              normalizeGrowthMetricForTest(expectedGrowthMetricRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_FALSE(QString::fromStdString(result.metadata.get("effectiveDate").asString()).isEmpty());
    ASSERT_TRUE(result.metadata.has("frequency"));
    EXPECT_EQ(normalizeConfigurableFrequencyForTest(scalarTextForTest(result.metadata.get("frequency"))),
              normalizeConfigurableFrequencyForTest(expectedConfigurableFrequencyRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), expectedConfigurableLookbackPeriod(calculation));
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_EQ(result.metadata.get("laggedEnabled").asBool(), expectedConfigurableLaggedEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
              normalizeConfigurableStandardizationForTest(expectedConfigurableStandardizationRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("neutralizationEnabled"));
    EXPECT_EQ(result.metadata.get("neutralizationEnabled").asBool(), expectedConfigurableNeutralizationEnabled(calculation));
}

TEST(FactorBacktestRegressionTest, RealDividendFactorInstanceReplayUsesConfiguredRuntimeParameters)
{
    const auto normalizedConfig = parseRuntimeCompatibleTestConfig(R"JSON({
        "factorType": "dividend",
        "majorCategory": "红利因子",
        "calculation": {
            "dividendMetrics": ["dividend_yield"]
        }
    })JSON");
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, normalizedConfig);

    ASSERT_TRUE(normalizedConfig.has("calculation"));
    const foundation::json::JsonFacade calculation = normalizedConfig.get("calculation");

    factor::CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"dividend_yield", {{"AAA", 0.035}, {"BBB", 0.062}, {"CCC", 0.01}}}
        });

    const auto result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("metric"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("metric").asString()),
              normalizeDividendMetricForTest(expectedDividendMetricRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_FALSE(QString::fromStdString(result.metadata.get("effectiveDate").asString()).isEmpty());
    ASSERT_TRUE(result.metadata.has("frequency"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("frequency").asString()),
              normalizeConfigurableFrequencyForTest(expectedConfigurableFrequencyRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), expectedConfigurableLookbackPeriod(calculation));
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_EQ(result.metadata.get("laggedEnabled").asBool(), expectedConfigurableLaggedEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("standardization").asString()),
              normalizeConfigurableStandardizationForTest(
                  expectedConfigurableStandardizationRaw(calculation).empty() ? std::string("none") : expectedConfigurableStandardizationRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("neutralizationEnabled"));
    EXPECT_EQ(result.metadata.get("neutralizationEnabled").asBool(), expectedConfigurableNeutralizationEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("dataMode"));
    EXPECT_FALSE(QString::fromStdString(scalarTextForTest(result.metadata.get("dataMode"))).isEmpty());
}

TEST(FactorBacktestRegressionTest, ConfigurableDividendFactorCanUseLaggedEffectiveDateWithinLookbackWindow)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "dividend",
        "calculation": {
            "dividendMetrics": ["dividend_yield"],
            "frequency": "daily",
            "lookbackPeriod": 3,
            "laggedEnabled": true,
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase currentFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(currentFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "dividend",
        "calculation": {
            "dividendMetrics": ["dividend_yield"],
            "frequency": "daily",
            "lookbackPeriod": 3,
            "laggedEnabled": false,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"dividend_yield", {
                {"AAA", {{"2024-01-12", 0.03}, {"2024-01-15", 0.06}}},
                {"BBB", {{"2024-01-12", 0.04}, {"2024-01-15", 0.05}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);
    const CalculationResult currentResult = currentFactor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 2U);
    ASSERT_TRUE(currentResult.dataStatus.isValid());
    ASSERT_EQ(currentResult.values.size(), 2U);
    EXPECT_NE(result.values.at("AAA"), currentResult.values.at("AAA"));
    EXPECT_NE(result.values.at("BBB"), currentResult.values.at("BBB"));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-12"));
    ASSERT_TRUE(currentResult.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(currentResult.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-15"));
}

TEST(FactorBacktestRegressionTest, ConfigurableDividendFactorCanApplyMinMaxStandardization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "dividend",
        "calculation": {
            "dividendMetrics": ["dividend_yield"],
            "standardization": "minmax"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"dividend_yield", {{"AAA", 0.01}, {"BBB", 0.03}, {"CCC", 0.05}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 3U);
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 0.0);
    EXPECT_NEAR(result.values.at("BBB"), 0.5, 1e-9);
    EXPECT_DOUBLE_EQ(result.values.at("CCC"), 1.0);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("standardization").asString()), QStringLiteral("minmax"));
}

TEST(FactorBacktestRegressionTest, ConfigurableDividendFactorAppliesHistoricalViewIndustrySizeNeutralization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "dividend",
        "calculation": {
            "dividendMetrics": ["dividend_yield"],
            "neutralizationEnabled": true,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC", "DDD"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"dividend_yield", {
                {"AAA", {{"2024-01-15", 0.02}}},
                {"BBB", {{"2024-01-15", 0.03}}},
                {"CCC", {{"2024-01-15", 0.04}}},
                {"DDD", {{"2024-01-15", 0.05}}}
            }},
            {"market_cap", {
                {"AAA", {{"2024-01-15", 100.0}}},
                {"BBB", {{"2024-01-15", 200.0}}},
                {"CCC", {{"2024-01-15", 300.0}}},
                {"DDD", {{"2024-01-15", 400.0}}}
            }},
            {"industry_code", {
                {"AAA", {{"2024-01-15", 10.0}}},
                {"BBB", {{"2024-01-15", 10.0}}},
                {"CCC", {{"2024-01-15", 20.0}}},
                {"DDD", {{"2024-01-15", 20.0}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("neutralizationMode").asString()),
              QStringLiteral("historical_view_cross_section_industry_size"));
}

TEST(FactorBacktestRegressionTest, ConfigurableDividendFactorMetricSelectionAffectsResult)
{
    factor::ConfigurableFactorBase yieldFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(yieldFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "dividend",
        "calculation": {
            "dividendMetrics": ["dividend_yield"],
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase payoutFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(payoutFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "dividend",
        "calculation": {
            "dividendMetrics": ["payout_ratio"],
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"dividend_yield", {{"AAA", 0.08}, {"BBB", 0.03}}},
            {"payout_ratio", {{"AAA", 0.20}, {"BBB", 0.70}}}
        });

    const CalculationResult yieldResult = yieldFactor.calculate(context);
    const CalculationResult payoutResult = payoutFactor.calculate(context);

    ASSERT_TRUE(yieldResult.dataStatus.isValid());
    ASSERT_TRUE(payoutResult.dataStatus.isValid());
    ASSERT_EQ(yieldResult.values.size(), 2U);
    ASSERT_EQ(payoutResult.values.size(), 2U);
    EXPECT_GT(yieldResult.values.at("AAA"), yieldResult.values.at("BBB"));
    EXPECT_LT(payoutResult.values.at("AAA"), payoutResult.values.at("BBB"));
}

TEST(FactorBacktestRegressionTest, ConfigurableDividendFactorMinDividendYieldAffectsResult)
{
    factor::ConfigurableFactorBase permissiveFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(permissiveFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "dividend",
        "calculation": {
            "dividendMetrics": ["dividend_yield"],
            "minDividendYield": 0.02,
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase strictFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(strictFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "dividend",
        "calculation": {
            "dividendMetrics": ["dividend_yield"],
            "minDividendYield": 0.05,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"dividend_yield", {{"AAA", 0.06}, {"BBB", 0.03}}}
        });

    const CalculationResult permissiveResult = permissiveFactor.calculate(context);
    const CalculationResult strictResult = strictFactor.calculate(context);

    ASSERT_TRUE(permissiveResult.dataStatus.isValid());
    ASSERT_TRUE(strictResult.dataStatus.isValid());
    EXPECT_EQ(permissiveResult.values.size(), 2U);
    EXPECT_EQ(strictResult.values.size(), 1U);
    EXPECT_TRUE(strictResult.values.find("BBB") == strictResult.values.end());
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRequiresNeutralizationFieldsForDividend)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("dividend_neutralization_instance");
    factor::FactorInstanceInfo instanceInfo;
    instanceInfo.instanceId = instanceId.toStdString();
    instanceInfo.factorType = factor::FactorType::CUSTOM;
    instanceInfo.instanceName = "Dividend Neutralization";
    instanceInfo.description = "Dividend Neutralization";
    instanceInfo.isAvailable = true;
    instanceInfo.config = parseRuntimeCompatibleTestConfig(R"JSON({
        "factorType": "dividend",
        "calculation": {
            "dividendMetrics": ["dividend_yield"],
            "neutralizationEnabled": true,
            "standardization": "none"
        }
    })JSON");

    auto factorInstance = factor::ConfigurableFactorBase::create(instanceInfo, nullptr);
    ASSERT_NE(factorInstance, nullptr);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-02"}, {"dividend_yield", 0.03}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("dividend_yield")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 1);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::DIVIDEND);
    const QVariantList expectedMissingFields{
        QStringLiteral("pre_adjust_factor"),
        QStringLiteral("post_adjust_factor"),
        QStringLiteral("industry_code"),
        QStringLiteral("market_cap")};
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedMissingFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, RealTechnicalFactorInstanceReplayUsesConfiguredRuntimeParameters)
{
    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        GTEST_SKIP() << "database unavailable";
    }

    const auto replayHandle = ensureReplayInstanceByCategory(
        database,
        QStringLiteral("技术因子"),
        QJsonObject{
            {QStringLiteral("factorType"), QStringLiteral("technical")},
            {QStringLiteral("majorCategory"), QStringLiteral("技术因子")},
            {QStringLiteral("factorName"), QStringLiteral("临时技术因子回放")},
            {QStringLiteral("displayName"), QStringLiteral("临时技术因子回放")},
            {QStringLiteral("calculation"), QJsonObject{{QStringLiteral("technicalIndicators"), QJsonArray{QStringLiteral("macd")}}, {QStringLiteral("technicalPriceType"), QStringLiteral("close")}, {QStringLiteral("window"), 20}, {QStringLiteral("rsiWindow"), 20}, {QStringLiteral("useVolume"), false}, {QStringLiteral("frequency"), QStringLiteral("daily")}, {QStringLiteral("lookbackPeriod"), 20}, {QStringLiteral("laggedEnabled"), false}, {QStringLiteral("standardization"), QStringLiteral("none")}, {QStringLiteral("neutralizationEnabled"), false}}},
            {QStringLiteral("dataRequirements"), QJsonObject{{QStringLiteral("required"), QJsonArray{QStringLiteral("close")}}}},
            {QStringLiteral("boundaryRules"), QJsonObject{{QStringLiteral("minDataPoints"), 21}}}
        },
        QStringLiteral("临时技术因子回放"));
    const auto& candidate = replayHandle.candidate;
    if (!candidate.has_value()) {
        GTEST_SKIP() << "no technical factor definition available in local database";
    }

    ASSERT_TRUE(candidate->config.has("calculation"));
    const auto calculation = candidate->config.get("calculation");

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);
    auto factorInstance = factor::TestConfigurableFactorHarness::create(
        candidate->instanceId,
        candidate->instanceName.toStdString(),
        candidate->instanceName.toStdString(),
        candidate->config,
        factor::config::factorTypeFromConfig(candidate->config),
        dataChecker);
    ASSERT_NE(factorInstance, nullptr);

    const QString latestDate = loadLatestTradeDate(database);
    ASSERT_FALSE(latestDate.isEmpty());

    factor::CalculationContext context;
    context.date = latestDate.toStdString();
    context.symbols = {"AAA", "BBB", "CCC"};
    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    assignHistoricalSeries(fieldSeries, "close", "AAA", buildLinearHistoricalSeries(latestDate, 80, 10.0, 0.12));
    assignHistoricalSeries(fieldSeries, "close", "BBB", buildLinearHistoricalSeries(latestDate, 80, 13.0, 0.09));
    assignHistoricalSeries(fieldSeries, "close", "CCC", buildLinearHistoricalSeries(latestDate, 80, 16.0, 0.07));
    assignHistoricalSeries(fieldSeries, "high", "AAA", buildLinearHistoricalSeries(latestDate, 80, 10.6, 0.12));
    assignHistoricalSeries(fieldSeries, "high", "BBB", buildLinearHistoricalSeries(latestDate, 80, 13.5, 0.09));
    assignHistoricalSeries(fieldSeries, "high", "CCC", buildLinearHistoricalSeries(latestDate, 80, 16.5, 0.07));
    assignHistoricalSeries(fieldSeries, "low", "AAA", buildLinearHistoricalSeries(latestDate, 80, 9.4, 0.12));
    assignHistoricalSeries(fieldSeries, "low", "BBB", buildLinearHistoricalSeries(latestDate, 80, 12.5, 0.09));
    assignHistoricalSeries(fieldSeries, "low", "CCC", buildLinearHistoricalSeries(latestDate, 80, 15.5, 0.07));
    assignHistoricalSeries(fieldSeries, "volume", "AAA", buildLinearHistoricalSeries(latestDate, 80, 1000.0, 18.0));
    assignHistoricalSeries(fieldSeries, "volume", "BBB", buildLinearHistoricalSeries(latestDate, 80, 1400.0, 16.0));
    assignHistoricalSeries(fieldSeries, "volume", "CCC", buildLinearHistoricalSeries(latestDate, 80, 1800.0, 14.0));
    assignHistoricalSeries(fieldSeries, "turnover_rate", "AAA", buildLinearHistoricalSeries(latestDate, 80, 2.1, 0.02));
    assignHistoricalSeries(fieldSeries, "turnover_rate", "BBB", buildLinearHistoricalSeries(latestDate, 80, 2.8, 0.015));
    assignHistoricalSeries(fieldSeries, "turnover_rate", "CCC", buildLinearHistoricalSeries(latestDate, 80, 3.4, 0.01));
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factorInstance->calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("indicatorType"));
    EXPECT_EQ(normalizeTechnicalIndicatorTypeForTest(scalarTextForTest(result.metadata.get("indicatorType"))),
              normalizeTechnicalIndicatorTypeForTest(expectedTechnicalIndicatorTypeRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("priceType"));
    EXPECT_EQ(normalizeTechnicalPriceTypeForTest(scalarTextForTest(result.metadata.get("priceType"))),
              normalizeTechnicalPriceTypeForTest(expectedMomentumPriceTypeRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("useVolume"));
    EXPECT_EQ(result.metadata.get("useVolume").asBool(), expectedMomentumUseVolume(calculation));
    ASSERT_TRUE(result.metadata.has("window"));
    EXPECT_EQ(result.metadata.get("window").asInt(), expectedConfigurableWindow(calculation));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_FALSE(QString::fromStdString(result.metadata.get("effectiveDate").asString()).isEmpty());
    ASSERT_TRUE(result.metadata.has("frequency"));
    EXPECT_EQ(normalizeConfigurableFrequencyForTest(scalarTextForTest(result.metadata.get("frequency"))),
              normalizeConfigurableFrequencyForTest(expectedConfigurableFrequencyRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), expectedConfigurableLookbackPeriod(calculation));
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_EQ(result.metadata.get("laggedEnabled").asBool(), expectedConfigurableLaggedEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
              normalizeConfigurableStandardizationForTest(
                  expectedConfigurableStandardizationRaw(calculation).empty() ? std::string("none") : expectedConfigurableStandardizationRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("neutralizationEnabled"));
    EXPECT_EQ(result.metadata.get("neutralizationEnabled").asBool(), expectedConfigurableNeutralizationEnabled(calculation));
}

TEST(FactorBacktestRegressionTest, RealIndustryFactorInstanceReplayUsesConfiguredRuntimeParameters)
{
    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        GTEST_SKIP() << "database unavailable";
    }

    const auto replayHandle = ensureReplayInstanceByCategory(
        database,
        QStringLiteral("行业因子"),
        QJsonObject{
            {QStringLiteral("factorType"), QStringLiteral("industry")},
            {QStringLiteral("majorCategory"), QStringLiteral("行业因子")},
            {QStringLiteral("factorName"), QStringLiteral("临时行业因子回放")},
            {QStringLiteral("displayName"), QStringLiteral("临时行业因子回放")},
            {QStringLiteral("calculation"), QJsonObject{{QStringLiteral("industryMetric"), QStringLiteral("industry_momentum")}, {QStringLiteral("sectorType"), QStringLiteral("申万一级")}, {QStringLiteral("frequency"), QStringLiteral("daily")}, {QStringLiteral("lookbackPeriod"), 20}, {QStringLiteral("laggedEnabled"), false}, {QStringLiteral("standardization"), QStringLiteral("none")}, {QStringLiteral("neutralizationEnabled"), false}}},
            {QStringLiteral("dataRequirements"), QJsonObject{{QStringLiteral("required"), QJsonArray{QStringLiteral("industry_momentum")}}}},
            {QStringLiteral("boundaryRules"), QJsonObject{{QStringLiteral("minDataPoints"), 1}}}
        },
        QStringLiteral("临时行业因子回放"));
    const auto& candidate = replayHandle.candidate;
    if (!candidate.has_value()) {
        GTEST_SKIP() << "no industry factor definition available in local database";
    }

    ASSERT_TRUE(candidate->config.has("calculation"));
    const auto calculation = candidate->config.get("calculation");

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);
    factor::FactorInstanceManager instanceManager(database, dataChecker);
    auto factorInstance = instanceManager.createInstance(candidate->instanceId);
    ASSERT_NE(factorInstance, nullptr);

    const QString latestDate = loadLatestTradeDate(database);
    ASSERT_FALSE(latestDate.isEmpty());

    factor::CalculationContext context;
    context.date = latestDate.toStdString();
    context.symbols = {"AAA", "BBB", "CCC"};
    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    assignHistoricalSeries(fieldSeries, "industry_momentum", "AAA", buildLinearHistoricalSeries(latestDate, 8, 0.10, 0.0));
    assignHistoricalSeries(fieldSeries, "industry_momentum", "BBB", buildLinearHistoricalSeries(latestDate, 8, 0.20, 0.0));
    assignHistoricalSeries(fieldSeries, "industry_momentum", "CCC", buildLinearHistoricalSeries(latestDate, 8, 0.30, 0.0));
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factorInstance->calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("industryMetric"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("industryMetric").asString()), QStringLiteral("industry_momentum"));
    ASSERT_TRUE(result.metadata.has("sectorType"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("sectorType").asString()),
              normalizeSectorTypeForTest(expectedSectorTypeRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_FALSE(QString::fromStdString(result.metadata.get("effectiveDate").asString()).isEmpty());
    ASSERT_TRUE(result.metadata.has("frequency"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("frequency").asString()),
              normalizeConfigurableFrequencyForTest(expectedConfigurableFrequencyRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), expectedConfigurableLookbackPeriod(calculation));
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_EQ(result.metadata.get("laggedEnabled").asBool(), expectedConfigurableLaggedEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("standardization").asString()),
              normalizeConfigurableStandardizationForTest(
                  expectedConfigurableStandardizationRaw(calculation).empty() ? std::string("none") : expectedConfigurableStandardizationRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("neutralizationEnabled"));
    EXPECT_EQ(result.metadata.get("neutralizationEnabled").asBool(), expectedConfigurableNeutralizationEnabled(calculation));
}

TEST(FactorBacktestRegressionTest, ConfigurableIndustryFactorCanUseLaggedEffectiveDateWithinLookbackWindow)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "industry",
        "calculation": {
            "industryMetric": "industry_momentum",
            "sectorType": "申万一级",
            "frequency": "daily",
            "lookbackPeriod": 3,
            "laggedEnabled": true,
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase currentFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(currentFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "industry",
        "calculation": {
            "industryMetric": "industry_momentum",
            "sectorType": "申万一级",
            "frequency": "daily",
            "lookbackPeriod": 3,
            "laggedEnabled": false,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"industry_momentum", {
                {"AAA", {{"2024-01-12", 0.10}, {"2024-01-15", 0.40}}},
                {"BBB", {{"2024-01-12", 0.30}, {"2024-01-15", 0.20}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);
    const CalculationResult currentResult = currentFactor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 2U);
    ASSERT_TRUE(currentResult.dataStatus.isValid());
    ASSERT_EQ(currentResult.values.size(), 2U);
    EXPECT_NE(result.values.at("AAA"), currentResult.values.at("AAA"));
    EXPECT_NE(result.values.at("BBB"), currentResult.values.at("BBB"));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-12"));
    ASSERT_TRUE(currentResult.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(currentResult.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-15"));
}

TEST(FactorBacktestRegressionTest, ConfigurableIndustryFactorCanApplyMinMaxStandardization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "industry",
        "calculation": {
            "industryMetric": "industry_momentum",
            "standardization": "minmax"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"industry_momentum", {{"AAA", 1.0}, {"BBB", 3.0}, {"CCC", 5.0}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 3U);
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 0.0);
    EXPECT_NEAR(result.values.at("BBB"), 0.5, 1e-9);
    EXPECT_DOUBLE_EQ(result.values.at("CCC"), 1.0);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("standardization").asString()), QStringLiteral("minmax"));
}

TEST(FactorBacktestRegressionTest, ConfigurableIndustryFactorAppliesHistoricalViewIndustrySizeNeutralization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "industry",
        "calculation": {
            "industryMetric": "industry_momentum",
            "neutralizationEnabled": true,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC", "DDD"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"industry_momentum", {
                {"AAA", {{"2024-01-15", 0.10}}},
                {"BBB", {{"2024-01-15", 0.20}}},
                {"CCC", {{"2024-01-15", 0.30}}},
                {"DDD", {{"2024-01-15", 0.40}}}
            }},
            {"market_cap", {
                {"AAA", {{"2024-01-15", 100.0}}},
                {"BBB", {{"2024-01-15", 200.0}}},
                {"CCC", {{"2024-01-15", 300.0}}},
                {"DDD", {{"2024-01-15", 400.0}}}
            }},
            {"industry_code", {
                {"AAA", {{"2024-01-15", 10.0}}},
                {"BBB", {{"2024-01-15", 10.0}}},
                {"CCC", {{"2024-01-15", 20.0}}},
                {"DDD", {{"2024-01-15", 20.0}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("neutralizationMode").asString()),
              QStringLiteral("historical_view_cross_section_industry_size"));
}

TEST(FactorBacktestRegressionTest, ConfigurableIndustryFactorMetricSelectionAffectsResult)
{
    factor::ConfigurableFactorBase momentumFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(momentumFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "industry",
        "calculation": {
            "industryMetric": "industry_momentum",
            "sectorType": "申万一级",
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase concentrationFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(concentrationFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "industry",
        "calculation": {
            "industryMetric": "industry_concentration",
            "sectorType": "申万一级",
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"industry_momentum", {{"AAA", 0.80}, {"BBB", 0.20}}},
            {"industry_concentration", {{"AAA", 0.10}, {"BBB", 0.50}}}
        });

    const CalculationResult momentumResult = momentumFactor.calculate(context);
    const CalculationResult concentrationResult = concentrationFactor.calculate(context);

    ASSERT_TRUE(momentumResult.dataStatus.isValid());
    ASSERT_TRUE(concentrationResult.dataStatus.isValid());
    EXPECT_GT(momentumResult.values.at("AAA"), momentumResult.values.at("BBB"));
    EXPECT_LT(concentrationResult.values.at("AAA"), concentrationResult.values.at("BBB"));
}

TEST(FactorBacktestRegressionTest, ConfigurableIndustryFactorWindowAffectsResult)
{
    factor::ConfigurableFactorBase shortWindowFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(shortWindowFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "industry",
        "calculation": {
            "industryMetric": "industry_momentum",
            "sectorType": "申万一级",
            "window": 1,
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase longWindowFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(longWindowFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "industry",
        "calculation": {
            "industryMetric": "industry_momentum",
            "sectorType": "申万一级",
            "window": 3,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"industry_momentum", {
                {"AAA", {{"2024-01-11", 1.0}, {"2024-01-12", 1.0}, {"2024-01-15", 10.0}}},
                {"BBB", {{"2024-01-11", 5.0}, {"2024-01-12", 5.0}, {"2024-01-15", 5.0}}}
            }}
        });

    const CalculationResult shortResult = shortWindowFactor.calculate(context);
    const CalculationResult longResult = longWindowFactor.calculate(context);

    ASSERT_TRUE(shortResult.dataStatus.isValid());
    ASSERT_TRUE(longResult.dataStatus.isValid());
    EXPECT_GT(shortResult.values.at("AAA"), shortResult.values.at("BBB"));
    EXPECT_LT(longResult.values.at("AAA"), longResult.values.at("BBB"));
}

TEST(FactorBacktestRegressionTest, ConfigurableIndustryFactorSectorTypeAffectsResult)
{
    factor::ConfigurableFactorBase swFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(swFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "industry",
        "calculation": {
            "industryMetric": "industry_momentum",
            "sectorType": "申万一级",
            "window": 3,
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase citicFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(citicFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "industry",
        "calculation": {
            "industryMetric": "industry_momentum",
            "sectorType": "中信二级",
            "window": 3,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"industry_momentum", {{"AAA", {{"2024-01-11", 2.0}, {"2024-01-12", 4.0}, {"2024-01-15", 6.0}}}}}
        });

    const CalculationResult swResult = swFactor.calculate(context);
    const CalculationResult citicResult = citicFactor.calculate(context);

    ASSERT_TRUE(swResult.dataStatus.isValid());
    ASSERT_TRUE(citicResult.dataStatus.isValid());
    ASSERT_EQ(swResult.values.size(), 1U);
    ASSERT_EQ(citicResult.values.size(), 1U);
    EXPECT_NE(swResult.values.at("AAA"), citicResult.values.at("AAA"));
    EXPECT_GT(swResult.values.at("AAA"), citicResult.values.at("AAA"));
}

TEST(FactorBacktestRegressionTest, ConfigurableTechnicalFactorCanUseLaggedEffectiveDateWithinLookbackWindow)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "technical",
        "calculation": {
            "technicalIndicators": ["ma"],
            "technicalPriceType": "close",
            "maWindow": 2,
            "frequency": "daily",
            "lookbackPeriod": 3,
            "laggedEnabled": true,
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase currentFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(currentFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "technical",
        "calculation": {
            "technicalIndicators": ["ma"],
            "technicalPriceType": "close",
            "maWindow": 2,
            "frequency": "daily",
            "lookbackPeriod": 3,
            "laggedEnabled": false,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {
                {"AAA", {{"2024-01-11", 10.0}, {"2024-01-12", 10.0}, {"2024-01-15", 20.0}}},
                {"BBB", {{"2024-01-11", 10.0}, {"2024-01-12", 10.0}, {"2024-01-15", 5.0}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);
    const CalculationResult currentResult = currentFactor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 2U);
    ASSERT_TRUE(currentResult.dataStatus.isValid());
    ASSERT_EQ(currentResult.values.size(), 2U);
    EXPECT_NE(result.values.at("AAA"), currentResult.values.at("AAA"));
    EXPECT_NE(result.values.at("BBB"), currentResult.values.at("BBB"));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-12"));
    ASSERT_TRUE(currentResult.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(currentResult.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-15"));
}

TEST(FactorBacktestRegressionTest, ConfigurableTechnicalFactorCanApplyMinMaxStandardization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::TECHNICAL)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("technicalIndicators"), intArrayForTest({static_cast<int>(factor::TechnicalIndicator::MA)})},
                {QStringLiteral("technicalPriceType"), static_cast<int>(factor::TechnicalPriceType::CLOSE)},
                {QStringLiteral("maWindow"), 2},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::MinMax)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {
                {"AAA", {{"2024-01-10", 10.0}, {"2024-01-11", 9.0}, {"2024-01-12", 8.0}, {"2024-01-15", 7.0}}},
                {"BBB", {{"2024-01-10", 10.0}, {"2024-01-11", 10.0}, {"2024-01-12", 10.0}, {"2024-01-15", 10.0}}},
                {"CCC", {{"2024-01-10", 10.0}, {"2024-01-11", 11.0}, {"2024-01-12", 12.0}, {"2024-01-15", 13.0}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 3U);
    double minValue = std::numeric_limits<double>::infinity();
    double maxValue = -std::numeric_limits<double>::infinity();
    for (const auto& [symbol, value] : result.values) {
        EXPECT_TRUE(std::isfinite(value));
        EXPECT_GE(value, 0.0);
        EXPECT_LE(value, 1.0);
        minValue = (std::min)(minValue, value);
        maxValue = (std::max)(maxValue, value);
    }
    EXPECT_DOUBLE_EQ(minValue, 0.0);
    EXPECT_DOUBLE_EQ(maxValue, 1.0);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))), QStringLiteral("minmax"));
}

TEST(FactorBacktestRegressionTest, ConfigurableTechnicalFactorAppliesHistoricalViewIndustrySizeNeutralization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::TECHNICAL)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("technicalIndicators"), intArrayForTest({static_cast<int>(factor::TechnicalIndicator::MA)})},
                {QStringLiteral("technicalPriceType"), static_cast<int>(factor::TechnicalPriceType::CLOSE)},
                {QStringLiteral("maWindow"), 2},
                {QStringLiteral("neutralizationEnabled"), true},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC", "DDD"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {
                {"AAA", {{"2024-01-10", 10.0}, {"2024-01-11", 11.0}, {"2024-01-12", 12.0}, {"2024-01-15", 13.0}}},
                {"BBB", {{"2024-01-10", 9.0}, {"2024-01-11", 10.0}, {"2024-01-12", 11.0}, {"2024-01-15", 12.5}}},
                {"CCC", {{"2024-01-10", 8.0}, {"2024-01-11", 8.5}, {"2024-01-12", 9.0}, {"2024-01-15", 9.8}}},
                {"DDD", {{"2024-01-10", 7.0}, {"2024-01-11", 7.3}, {"2024-01-12", 7.6}, {"2024-01-15", 8.1}}}
            }},
            {"market_cap", {
                {"AAA", {{"2024-01-15", 100.0}}},
                {"BBB", {{"2024-01-15", 150.0}}},
                {"CCC", {{"2024-01-15", 200.0}}},
                {"DDD", {{"2024-01-15", 250.0}}}
            }},
            {"industry_code", {
                {"AAA", {{"2024-01-15", 10.0}}},
                {"BBB", {{"2024-01-15", 10.0}}},
                {"CCC", {{"2024-01-15", 20.0}}},
                {"DDD", {{"2024-01-15", 20.0}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(normalizeNeutralizationModeForTest(scalarTextForTest(result.metadata.get("neutralizationMode"))),
              QStringLiteral("historical_view_cross_section_industry_size"));
}

TEST(FactorBacktestRegressionTest, ConfigurableTechnicalFactorDoesNotAppendDefaultRsiWhenOtherIndicatorsAreSelected)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::TECHNICAL)},
            {QStringLiteral("majorCategory"), QStringLiteral("技术因子")},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("technicalIndicators"), intArrayForTest({static_cast<int>(factor::TechnicalIndicator::MACD)})},
                {QStringLiteral("technicalCombinationMode"), static_cast<int>(factor::TechnicalCombinationMode::EqualWeight)},
                {QStringLiteral("technicalPriceType"), static_cast<int>(factor::TechnicalPriceType::CLOSE)},
                {QStringLiteral("macdFastPeriod"), 12},
                {QStringLiteral("macdSlowPeriod"), 26},
                {QStringLiteral("macdSignalPeriod"), 9}
            }}
        }));

    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    std::vector<HistoricalDataPoint> closeSeries;
    std::vector<HistoricalDataPoint> highSeries;
    std::vector<HistoricalDataPoint> lowSeries;
    std::vector<HistoricalDataPoint> volumeSeries;
    for (int index = 0; index < 50; ++index) {
        const QDate date = QDate::fromString(QStringLiteral("2024-01-01"), Qt::ISODate).addDays(index);
        const QString dateString = date.toString(Qt::ISODate);
        const double close = 10.0 + static_cast<double>(index) * 0.2;
        closeSeries.push_back({dateString.toStdString(), close});
        highSeries.push_back({dateString.toStdString(), close + 0.5});
        lowSeries.push_back({dateString.toStdString(), close - 0.5});
        volumeSeries.push_back({dateString.toStdString(), 1000.0 + static_cast<double>(index) * 8.0});
    }
    fieldSeries["close"]["AAA"] = std::move(closeSeries);
    fieldSeries["high"]["AAA"] = std::move(highSeries);
    fieldSeries["low"]["AAA"] = std::move(lowSeries);
    fieldSeries["volume"]["AAA"] = std::move(volumeSeries);

    factor::CalculationContext context;
    context.date = QDate::fromString(QStringLiteral("2024-01-01"), Qt::ISODate).addDays(49).toString(Qt::ISODate).toStdString();
    context.symbols = {"AAA"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("indicatorType"));
    EXPECT_EQ(normalizeTechnicalIndicatorTypeForTest(scalarTextForTest(result.metadata.get("indicatorType"))), QStringLiteral("macd"));
    ASSERT_TRUE(result.metadata.has("indicatorTypes"));
    ASSERT_TRUE(result.metadata.get("indicatorTypes").isArray());
    ASSERT_EQ(result.metadata.get("indicatorTypes").size(), 1U);
    EXPECT_EQ(normalizeTechnicalIndicatorTypeForTest(scalarTextForTest(result.metadata.get("indicatorTypes").at(0))), QStringLiteral("macd"));
}

TEST(FactorBacktestRegressionTest, ConfigurableTechnicalFactorCombinationModeAffectsResult)
{
    factor::ConfigurableFactorBase equalWeightFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        equalWeightFactor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::TECHNICAL)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("technicalIndicators"), intArrayForTest({
                    static_cast<int>(factor::TechnicalIndicator::MA),
                    static_cast<int>(factor::TechnicalIndicator::EMA)})},
                {QStringLiteral("technicalCombinationMode"), static_cast<int>(factor::TechnicalCombinationMode::EqualWeight)},
                {QStringLiteral("technicalPriceType"), static_cast<int>(factor::TechnicalPriceType::CLOSE)},
                {QStringLiteral("maWindow"), 3},
                {QStringLiteral("emaWindow"), 5},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    factor::ConfigurableFactorBase normalizedFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        normalizedFactor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::TECHNICAL)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("technicalIndicators"), intArrayForTest({
                    static_cast<int>(factor::TechnicalIndicator::MA),
                    static_cast<int>(factor::TechnicalIndicator::EMA)})},
                {QStringLiteral("technicalCombinationMode"), static_cast<int>(factor::TechnicalCombinationMode::NormalizedAverage)},
                {QStringLiteral("technicalPriceType"), static_cast<int>(factor::TechnicalPriceType::CLOSE)},
                {QStringLiteral("maWindow"), 3},
                {QStringLiteral("emaWindow"), 5},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    fieldSeries["close"]["AAA"] = {
        {"2024-01-01", 10.0}, {"2024-01-02", 10.5}, {"2024-01-03", 11.0},
        {"2024-01-04", 11.8}, {"2024-01-05", 12.7}, {"2024-01-08", 13.9}
    };

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const CalculationResult equalWeightResult = equalWeightFactor.calculate(context);
    const CalculationResult normalizedResult = normalizedFactor.calculate(context);

    ASSERT_TRUE(equalWeightResult.dataStatus.isValid());
    ASSERT_TRUE(normalizedResult.dataStatus.isValid());
    ASSERT_EQ(equalWeightResult.values.size(), 1U);
    ASSERT_EQ(normalizedResult.values.size(), 1U);
    EXPECT_NE(equalWeightResult.values.at("AAA"), normalizedResult.values.at("AAA"));
}

TEST(FactorBacktestRegressionTest, ConfigurableSentimentFactorRejectsProxyOnlyInputs)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, parseRuntimeCompatibleTestConfig(R"JSON({
        "factorType": "sentiment",
        "calculation": {
            "metric": 3,
            "sentimentSource": 3,
            "window": 20
        },
        "dataRequirements": {
            "required": ["change_pct", "turnover_rate"]
        }
    })JSON"));

    factor::CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"change_pct", {{"AAA", 0.01}, {"BBB", -0.02}}},
            {"turnover_rate", {{"AAA", 3.2}, {"BBB", 2.7}}},
            {"close", {{"AAA", 10.5}, {"BBB", 8.9}}}
        });

    const auto result = factor.calculate(context);

    EXPECT_FALSE(result.dataStatus.isValid());
    EXPECT_TRUE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("sentimentSource"));
    EXPECT_EQ(normalizeSentimentSourceForTest(scalarTextForTest(result.metadata.get("sentimentSource"))),
              normalizeSentimentSourceForTest("market"));
    ASSERT_TRUE(result.metadata.has("error"));
    EXPECT_TRUE(QString::fromStdString(result.metadata.get("error").asString()).contains(QStringLiteral("真实情绪字段")));
}

TEST(FactorBacktestRegressionTest, ConfigurableMacroFactorUsesProxyRuntime)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::MACRO)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("benchmarkSymbol"), QStringLiteral("BENCH")},
                {QStringLiteral("macroDimensions"), intArrayForTest({
                    static_cast<int>(factor::MacroDimension::GROWTH),
                    static_cast<int>(factor::MacroDimension::CREDIT),
                    static_cast<int>(factor::MacroDimension::RATES)})},
                {QStringLiteral("macroIndicators"), intArrayForTest({
                    static_cast<int>(factor::MacroIndicator::INDUSTRIAL_ADDED_VALUE_YOY),
                    static_cast<int>(factor::MacroIndicator::M2_YOY),
                    static_cast<int>(factor::MacroIndicator::TEN_YEAR_BOND_YIELD)})},
                {QStringLiteral("macroFrequency"), static_cast<int>(factor::DataFrequency::Daily)},
                {QStringLiteral("macroWindow"), 20}
            }}
        }));

    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    std::vector<HistoricalDataPoint> benchmarkGrowthSeries;
    std::vector<HistoricalDataPoint> benchmarkCreditSeries;
    std::vector<HistoricalDataPoint> benchmarkRatesSeries;
    std::vector<HistoricalDataPoint> aaaCloseSeries;
    std::vector<HistoricalDataPoint> bbbCloseSeries;

    for (int index = 0; index < 40; ++index) {
        const QDate date = QDate::fromString(QStringLiteral("2024-01-01"), Qt::ISODate).addDays(index);
        const QString dateString = date.toString(Qt::ISODate);
        benchmarkGrowthSeries.push_back({dateString.toStdString(), 100.0 + static_cast<double>(index) * 0.8});
        benchmarkCreditSeries.push_back({dateString.toStdString(), 50.0 + static_cast<double>(index) * 0.5});
        benchmarkRatesSeries.push_back({dateString.toStdString(), 3.0 + static_cast<double>(index) * 0.02});

        aaaCloseSeries.push_back({dateString.toStdString(), 20.0 + static_cast<double>(index) * 0.95});
        bbbCloseSeries.push_back({dateString.toStdString(), 30.0 + static_cast<double>(39 - index) * 0.55});
    }

    fieldSeries["industrial_added_value_yoy"]["BENCH"] = benchmarkGrowthSeries;
    fieldSeries["m2_yoy"]["BENCH"] = benchmarkCreditSeries;
    fieldSeries["ten_year_bond_yield"]["BENCH"] = benchmarkRatesSeries;
    fieldSeries["close"]["AAA"] = aaaCloseSeries;
    fieldSeries["close"]["BBB"] = bbbCloseSeries;

    factor::CalculationContext context;
    context.date = QDate::fromString(QStringLiteral("2024-01-01"), Qt::ISODate).addDays(39).toString(Qt::ISODate).toStdString();
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("macroMode"));
    EXPECT_EQ(normalizeMacroModeForTest(scalarTextForTest(result.metadata.get("macroMode"))), QStringLiteral("proxy_sensitivity"));
    ASSERT_TRUE(result.metadata.has("benchmarkSymbol"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("benchmarkSymbol").asString()), QStringLiteral("BENCH"));
    ASSERT_TRUE(result.metadata.has("macroIndicators"));
    ASSERT_TRUE(result.metadata.get("macroIndicators").isArray());
    ASSERT_GT(result.metadata.get("macroIndicators").size(), 0U);
    EXPECT_EQ(normalizeMacroIndicatorForTest(scalarTextForTest(result.metadata.get("macroIndicators").at(0))),
              QStringLiteral("industrial_added_value_yoy"));
}

TEST(FactorBacktestRegressionTest, ConfigurableMacroFactorRejectsMissingExplicitMacroConfiguration)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "macro",
        "calculation": {
            "benchmarkSymbol": "BENCH",
            "macroFrequency": "daily",
            "macroWindow": 20
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{});

    const CalculationResult result = factor.calculate(context);

    EXPECT_FALSE(result.dataStatus.isValid());
    EXPECT_TRUE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("error"));
    const QString errorText = QString::fromStdString(result.metadata.get("error").asString());
    EXPECT_FALSE(errorText.isEmpty());
}

TEST(FactorBacktestRegressionTest, ConfigurableMacroFactorCanUseLaggedEffectiveDateWithinLookbackWindow)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "macro",
        "calculation": {
            "benchmarkSymbol": "BENCH",
            "macroIndicators": ["industrial_added_value_yoy"],
            "macroFrequency": "daily",
            "macroWindow": 3,
            "lookbackPeriod": 3,
            "laggedEnabled": true,
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase currentFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(currentFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "macro",
        "calculation": {
            "benchmarkSymbol": "BENCH",
            "macroIndicators": ["industrial_added_value_yoy"],
            "macroFrequency": "daily",
            "macroWindow": 3,
            "lookbackPeriod": 3,
            "laggedEnabled": false,
            "standardization": "none"
        }
    })JSON"));

    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    fieldSeries["close"]["AAA"] = {{"2024-01-10", 10.0}, {"2024-01-11", 11.0}, {"2024-01-12", 12.0}, {"2024-01-15", 18.0}};
    fieldSeries["close"]["BBB"] = {{"2024-01-10", 20.0}, {"2024-01-11", 19.0}, {"2024-01-12", 18.0}, {"2024-01-15", 10.0}};
    fieldSeries["industrial_added_value_yoy"]["BENCH"] = {{"2024-01-10", 1.0}, {"2024-01-11", 1.1}, {"2024-01-12", 1.2}, {"2024-01-15", 2.4}};

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const CalculationResult result = factor.calculate(context);
    const CalculationResult currentResult = currentFactor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 2U);
    ASSERT_TRUE(currentResult.dataStatus.isValid());
    ASSERT_EQ(currentResult.values.size(), 2U);
    EXPECT_NE(result.values.at("AAA"), currentResult.values.at("AAA"));
    EXPECT_NE(result.values.at("BBB"), currentResult.values.at("BBB"));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-12"));
    ASSERT_TRUE(currentResult.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(currentResult.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-15"));
}

TEST(FactorBacktestRegressionTest, ConfigurableMacroFactorCanApplyMinMaxStandardization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::MACRO)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("benchmarkSymbol"), QStringLiteral("BENCH")},
                {QStringLiteral("macroDimensions"), intArrayForTest({static_cast<int>(factor::MacroDimension::GROWTH)})},
                {QStringLiteral("macroIndicators"), intArrayForTest({static_cast<int>(factor::MacroIndicator::INDUSTRIAL_ADDED_VALUE_YOY)})},
                {QStringLiteral("macroFrequency"), static_cast<int>(factor::DataFrequency::Daily)},
                {QStringLiteral("macroWindow"), 3},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::MinMax)}
            }}
        }));

    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    fieldSeries["close"]["AAA"] = {{"2024-01-10", 10.0}, {"2024-01-11", 11.0}, {"2024-01-12", 12.0}, {"2024-01-15", 13.0}};
    fieldSeries["close"]["BBB"] = {{"2024-01-10", 10.0}, {"2024-01-11", 10.0}, {"2024-01-12", 10.0}, {"2024-01-15", 10.0}};
    fieldSeries["close"]["CCC"] = {{"2024-01-10", 10.0}, {"2024-01-11", 9.0}, {"2024-01-12", 8.0}, {"2024-01-15", 7.0}};
    fieldSeries["industrial_added_value_yoy"]["BENCH"] = {{"2024-01-10", 1.0}, {"2024-01-11", 1.1}, {"2024-01-12", 1.2}, {"2024-01-15", 1.3}};

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_GE(result.values.size(), 2U);
    double minValue = std::numeric_limits<double>::infinity();
    double maxValue = -std::numeric_limits<double>::infinity();
    for (const auto& [symbol, value] : result.values) {
        EXPECT_TRUE(std::isfinite(value));
        EXPECT_GE(value, 0.0);
        EXPECT_LE(value, 1.0);
        minValue = (std::min)(minValue, value);
        maxValue = (std::max)(maxValue, value);
    }
    EXPECT_DOUBLE_EQ(minValue, 0.0);
    EXPECT_DOUBLE_EQ(maxValue, 1.0);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))), QStringLiteral("minmax"));
}

TEST(FactorBacktestRegressionTest, ConfigurableMacroFactorAppliesHistoricalViewIndustrySizeNeutralization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::MACRO)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("benchmarkSymbol"), QStringLiteral("BENCH")},
                {QStringLiteral("macroDimensions"), intArrayForTest({static_cast<int>(factor::MacroDimension::GROWTH)})},
                {QStringLiteral("macroIndicators"), intArrayForTest({static_cast<int>(factor::MacroIndicator::INDUSTRIAL_ADDED_VALUE_YOY)})},
                {QStringLiteral("macroFrequency"), static_cast<int>(factor::DataFrequency::Daily)},
                {QStringLiteral("macroWindow"), 3},
                {QStringLiteral("neutralizationEnabled"), true},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    fieldSeries["close"]["AAA"] = {{"2024-01-10", 10.0}, {"2024-01-11", 11.0}, {"2024-01-12", 12.0}, {"2024-01-15", 13.0}};
    fieldSeries["close"]["BBB"] = {{"2024-01-10", 12.0}, {"2024-01-11", 12.5}, {"2024-01-12", 13.0}, {"2024-01-15", 13.5}};
    fieldSeries["close"]["CCC"] = {{"2024-01-10", 8.0}, {"2024-01-11", 8.5}, {"2024-01-12", 9.0}, {"2024-01-15", 9.5}};
    fieldSeries["close"]["DDD"] = {{"2024-01-10", 7.0}, {"2024-01-11", 7.3}, {"2024-01-12", 7.6}, {"2024-01-15", 7.9}};
    fieldSeries["industrial_added_value_yoy"]["BENCH"] = {{"2024-01-10", 1.0}, {"2024-01-11", 1.1}, {"2024-01-12", 1.2}, {"2024-01-15", 1.3}};
    fieldSeries["market_cap"]["AAA"] = {{"2024-01-15", 100.0}};
    fieldSeries["market_cap"]["BBB"] = {{"2024-01-15", 200.0}};
    fieldSeries["market_cap"]["CCC"] = {{"2024-01-15", 300.0}};
    fieldSeries["market_cap"]["DDD"] = {{"2024-01-15", 400.0}};
    fieldSeries["industry_code"]["AAA"] = {{"2024-01-15", 10.0}};
    fieldSeries["industry_code"]["BBB"] = {{"2024-01-15", 10.0}};
    fieldSeries["industry_code"]["CCC"] = {{"2024-01-15", 20.0}};
    fieldSeries["industry_code"]["DDD"] = {{"2024-01-15", 20.0}};

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC", "DDD"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(normalizeNeutralizationModeForTest(scalarTextForTest(result.metadata.get("neutralizationMode"))),
              QStringLiteral("historical_view_cross_section_industry_size"));
}

TEST(FactorBacktestRegressionTest, ConfigurableMacroFactorDimensionsAffectResult)
{
    factor::ConfigurableFactorBase growthFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(growthFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "macro",
        "calculation": {
            "benchmarkSymbol": "BENCH",
            "macroDimensions": ["growth"],
            "macroIndicators": ["industrial_added_value_yoy", "ten_year_bond_yield"],
            "macroFrequency": "daily",
            "macroWindow": 6,
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase ratesFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(ratesFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "macro",
        "calculation": {
            "benchmarkSymbol": "BENCH",
            "macroDimensions": ["rates"],
            "macroIndicators": ["industrial_added_value_yoy", "ten_year_bond_yield"],
            "macroFrequency": "daily",
            "macroWindow": 6,
            "standardization": "none"
        }
    })JSON"));

    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    fieldSeries["close"]["AAA"] = {
        {"2024-01-01", 100.0}, {"2024-01-02", 103.0}, {"2024-01-03", 107.0},
        {"2024-01-04", 112.0}, {"2024-01-05", 118.0}, {"2024-01-08", 125.0}, {"2024-01-09", 133.0}
    };
    fieldSeries["close"]["BBB"] = {
        {"2024-01-01", 133.0}, {"2024-01-02", 125.0}, {"2024-01-03", 118.0},
        {"2024-01-04", 112.0}, {"2024-01-05", 107.0}, {"2024-01-08", 103.0}, {"2024-01-09", 100.0}
    };
    fieldSeries["industrial_added_value_yoy"]["BENCH"] = {
        {"2024-01-01", 100.0}, {"2024-01-02", 103.0}, {"2024-01-03", 107.0},
        {"2024-01-04", 112.0}, {"2024-01-05", 118.0}, {"2024-01-08", 125.0}, {"2024-01-09", 133.0}
    };
    fieldSeries["ten_year_bond_yield"]["BENCH"] = {
        {"2024-01-01", 10.0}, {"2024-01-02", 10.5}, {"2024-01-03", 11.1},
        {"2024-01-04", 11.8}, {"2024-01-05", 12.6}, {"2024-01-08", 13.5}, {"2024-01-09", 14.5}
    };

    CalculationContext context;
    context.date = "2024-01-09";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const CalculationResult growthResult = growthFactor.calculate(context);
    const CalculationResult ratesResult = ratesFactor.calculate(context);

    ASSERT_TRUE(growthResult.dataStatus.isValid());
    ASSERT_TRUE(ratesResult.dataStatus.isValid());
    ASSERT_EQ(growthResult.values.size(), 2U);
    ASSERT_EQ(ratesResult.values.size(), 2U);
    EXPECT_GT(growthResult.values.at("AAA"), growthResult.values.at("BBB"));
    EXPECT_LT(ratesResult.values.at("AAA"), ratesResult.values.at("BBB"));
}

TEST(FactorBacktestRegressionTest, ConfigurableMacroFactorWindowAffectsResult)
{
    factor::ConfigurableFactorBase shortWindowFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(shortWindowFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "macro",
        "calculation": {
            "benchmarkSymbol": "BENCH",
            "macroDimensions": ["growth"],
            "macroIndicators": ["industrial_added_value_yoy"],
            "macroFrequency": "daily",
            "macroWindow": 3,
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase longWindowFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(longWindowFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "macro",
        "calculation": {
            "benchmarkSymbol": "BENCH",
            "macroDimensions": ["growth"],
            "macroIndicators": ["industrial_added_value_yoy"],
            "macroFrequency": "daily",
            "macroWindow": 6,
            "standardization": "none"
        }
    })JSON"));

    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    fieldSeries["close"]["AAA"] = {
        {"2024-01-01", 100.0}, {"2024-01-02", 130.0}, {"2024-01-03", 156.0},
        {"2024-01-04", 171.6}, {"2024-01-05", 154.44}, {"2024-01-08", 123.55}, {"2024-01-09", 86.49}
    };
    fieldSeries["industrial_added_value_yoy"]["BENCH"] = {
        {"2024-01-01", 100.0}, {"2024-01-02", 110.0}, {"2024-01-03", 132.0},
        {"2024-01-04", 171.6}, {"2024-01-05", 154.44}, {"2024-01-08", 123.55}, {"2024-01-09", 86.49}
    };

    CalculationContext context;
    context.date = "2024-01-09";
    context.symbols = {"AAA"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const CalculationResult shortResult = shortWindowFactor.calculate(context);
    const CalculationResult longResult = longWindowFactor.calculate(context);

    ASSERT_TRUE(shortResult.dataStatus.isValid());
    ASSERT_TRUE(longResult.dataStatus.isValid());
    ASSERT_EQ(shortResult.values.size(), 1U);
    ASSERT_EQ(longResult.values.size(), 1U);
    EXPECT_NE(shortResult.values.at("AAA"), longResult.values.at("AAA"));
}

TEST(FactorBacktestRegressionTest, ConfigurableLiquidityFactorMetricAffectsResult)
{
    factor::ConfigurableFactorBase turnoverFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(turnoverFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "liquidity",
        "calculation": {
            "metric": "turnover_rate",
            "window": 3,
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase amplitudeFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(amplitudeFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "liquidity",
        "calculation": {
            "metric": "amplitude",
            "window": 3,
            "standardization": "none"
        }
    })JSON"));

    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    fieldSeries["turnover_rate"]["AAA"] = {{"2024-01-11", 5.0}, {"2024-01-12", 5.0}, {"2024-01-15", 5.0}};
    fieldSeries["turnover_rate"]["BBB"] = {{"2024-01-11", 2.0}, {"2024-01-12", 2.0}, {"2024-01-15", 2.0}};
    fieldSeries["amplitude"]["AAA"] = {{"2024-01-11", 0.10}, {"2024-01-12", 0.10}, {"2024-01-15", 0.10}};
    fieldSeries["amplitude"]["BBB"] = {{"2024-01-11", 0.02}, {"2024-01-12", 0.02}, {"2024-01-15", 0.02}};

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const CalculationResult turnoverResult = turnoverFactor.calculate(context);
    const CalculationResult amplitudeResult = amplitudeFactor.calculate(context);

    ASSERT_TRUE(turnoverResult.dataStatus.isValid());
    ASSERT_TRUE(amplitudeResult.dataStatus.isValid());
    EXPECT_GT(turnoverResult.values.at("AAA"), turnoverResult.values.at("BBB"));
    EXPECT_LT(amplitudeResult.values.at("AAA"), amplitudeResult.values.at("BBB"));
}

TEST(FactorBacktestRegressionTest, ConfigurableLiquidityFactorWindowAffectsResult)
{
    factor::ConfigurableFactorBase shortWindowFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(shortWindowFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "liquidity",
        "calculation": {
            "metric": "turnover_rate",
            "window": 3,
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase longWindowFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(longWindowFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "liquidity",
        "calculation": {
            "metric": "turnover_rate",
            "window": 6,
            "standardization": "none"
        }
    })JSON"));

    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    fieldSeries["turnover_rate"]["AAA"] = {
        {"2024-01-08", 1.0}, {"2024-01-09", 1.0}, {"2024-01-10", 1.0},
        {"2024-01-11", 10.0}, {"2024-01-12", 10.0}, {"2024-01-15", 10.0}
    };
    fieldSeries["turnover_rate"]["BBB"] = {
        {"2024-01-08", 6.0}, {"2024-01-09", 6.0}, {"2024-01-10", 6.0},
        {"2024-01-11", 6.0}, {"2024-01-12", 6.0}, {"2024-01-15", 6.0}
    };

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const CalculationResult shortResult = shortWindowFactor.calculate(context);
    const CalculationResult longResult = longWindowFactor.calculate(context);

    ASSERT_TRUE(shortResult.dataStatus.isValid());
    ASSERT_TRUE(longResult.dataStatus.isValid());
    EXPECT_GT(shortResult.values.at("AAA"), shortResult.values.at("BBB"));
    EXPECT_LT(longResult.values.at("AAA"), longResult.values.at("BBB"));
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRequiresNeutralizationFieldsForMacro)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_macro_neutralization_instance");
    const char* configJson = R"JSON({
        "factorType": "macro",
        "calculation": {
            "benchmarkSymbol": "BENCH",
            "macroIndicators": ["industrial_added_value_yoy"],
            "macroFrequency": "daily",
            "macroWindow": 3,
            "neutralizationEnabled": true,
            "standardization": "none"
        },
        "boundaryRules": {
            "minDataPoints": 4
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("宏观因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-03"}, {"close", 10.0}, {"industrial_added_value_yoy", 1.0}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-04"}, {"close", 10.5}, {"industrial_added_value_yoy", 1.1}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-05"}, {"close", 11.0}, {"industrial_added_value_yoy", 1.2}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-08"}, {"close", 11.5}, {"industrial_added_value_yoy", 1.3}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close"), QStringLiteral("industrial_added_value_yoy")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-08"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 4);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-08"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::MACRO);
    const QVariantList expectedMissingFields{QStringLiteral("industry_code"), QStringLiteral("market_cap")};
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedMissingFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRejectsMacroWithoutExplicitConfiguration)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_macro_missing_config_instance");
    const char* configJson = R"JSON({
        "factorType": "macro",
        "calculation": {
            "benchmarkSymbol": "BENCH",
            "macroFrequency": "daily",
            "macroWindow": 20
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("宏观因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-03"}, {"close", 10.0}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-08"}, {"close", 10.5}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-08"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-08"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, RealMacroFactorInstanceReplayUsesConfiguredRuntimeParameters)
{
    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        GTEST_SKIP() << "database unavailable";
    }

    const auto replayHandle = ensureReplayInstanceByCategory(
        database,
        QStringLiteral("宏观因子"),
        QJsonObject{
            {QStringLiteral("factorType"), QStringLiteral("macro")},
            {QStringLiteral("majorCategory"), QStringLiteral("宏观因子")},
            {QStringLiteral("factorName"), QStringLiteral("临时宏观因子回放")},
            {QStringLiteral("displayName"), QStringLiteral("临时宏观因子回放")},
            {QStringLiteral("calculation"), QJsonObject{{QStringLiteral("macroIndicators"), QJsonArray{QStringLiteral("industrial_added_value_yoy")}}, {QStringLiteral("macroDimensions"), QJsonArray{QStringLiteral("growth")}}, {QStringLiteral("macroFrequency"), QStringLiteral("daily")}, {QStringLiteral("macroWindow"), 20}, {QStringLiteral("lookbackPeriod"), 20}, {QStringLiteral("laggedEnabled"), false}, {QStringLiteral("standardization"), QStringLiteral("none")}, {QStringLiteral("neutralizationEnabled"), false}, {QStringLiteral("benchmarkSymbol"), QStringLiteral("BENCH")}}},
            {QStringLiteral("dataRequirements"), QJsonObject{{QStringLiteral("required"), QJsonArray{QStringLiteral("close"), QStringLiteral("industrial_added_value_yoy")}}}},
            {QStringLiteral("boundaryRules"), QJsonObject{{QStringLiteral("minDataPoints"), 21}}}
        },
        QStringLiteral("临时宏观因子回放"));
    const auto& candidate = replayHandle.candidate;
    if (!candidate.has_value()) {
        GTEST_SKIP() << "no macro factor definition available in local database";
    }

    ASSERT_TRUE(candidate->config.has("calculation"));
    const auto calculation = candidate->config.get("calculation");
    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);
    auto factorInstance = factor::TestConfigurableFactorHarness::create(
        candidate->instanceId,
        candidate->instanceName.toStdString(),
        candidate->instanceName.toStdString(),
        candidate->config,
        factor::config::factorTypeFromConfig(candidate->config),
        dataChecker);
    ASSERT_NE(factorInstance, nullptr);

    const QString latestDate = loadLatestTradeDate(database);
    ASSERT_FALSE(latestDate.isEmpty());

    factor::CalculationContext context;
    context.date = latestDate.toStdString();
    context.symbols = {"AAA", "BBB", "CCC"};
    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    assignHistoricalSeries(fieldSeries, "close", "AAA", buildLinearHistoricalSeries(latestDate, 60, 10.0, 0.12));
    assignHistoricalSeries(fieldSeries, "close", "BBB", buildLinearHistoricalSeries(latestDate, 60, 13.0, 0.08));
    assignHistoricalSeries(fieldSeries, "close", "CCC", buildLinearHistoricalSeries(latestDate, 60, 16.0, 0.05));
    assignHistoricalSeries(fieldSeries, "industrial_added_value_yoy", "BENCH", buildLinearHistoricalSeries(latestDate, 60, 1.0, 0.03));
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factorInstance->calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("macroIndicators"));
    ASSERT_TRUE(result.metadata.get("macroIndicators").isArray());
    ASSERT_GT(result.metadata.get("macroIndicators").size(), 0U);
    EXPECT_EQ(normalizeMacroIndicatorForTest(scalarTextForTest(result.metadata.get("macroIndicators").at(0))),
              normalizeMacroIndicatorForTest(expectedMacroIndicatorRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_FALSE(QString::fromStdString(result.metadata.get("effectiveDate").asString()).isEmpty());
    ASSERT_TRUE(result.metadata.has("frequency"));
    EXPECT_EQ(normalizeConfigurableFrequencyForTest(scalarTextForTest(result.metadata.get("frequency"))),
              normalizeConfigurableFrequencyForTest(scalarTextForTest(calculation.get("macroFrequency"))));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), calculation.get("lookbackPeriod").asInt());
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_EQ(result.metadata.get("laggedEnabled").asBool(), calculation.get("laggedEnabled").asBool());
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
              normalizeConfigurableStandardizationForTest(expectedConfigurableStandardizationRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("neutralizationEnabled"));
    EXPECT_EQ(result.metadata.get("neutralizationEnabled").asBool(), expectedConfigurableNeutralizationEnabled(calculation));
}

TEST(FactorBacktestRegressionTest, RealCustomFactorInstanceReplayUsesConfiguredRuntimeParameters)
{
    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        GTEST_SKIP() << "database unavailable";
    }

    const auto replayHandle = ensureReplayInstanceByCategory(
        database,
        QStringLiteral("自定义因子"),
        QJsonObject{
            {QStringLiteral("factorType"), QStringLiteral("custom")},
            {QStringLiteral("majorCategory"), QStringLiteral("自定义因子")},
            {QStringLiteral("factorName"), QStringLiteral("临时自定义因子回放")},
            {QStringLiteral("displayName"), QStringLiteral("临时自定义因子回放")},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("expression"), QStringLiteral("x / y - 1")},
                {QStringLiteral("variables"), QJsonArray{
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("x")}, {QStringLiteral("field"), QStringLiteral("close")}},
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("y")}, {QStringLiteral("field"), QStringLiteral("open")}}
                }},
                {QStringLiteral("frequency"), QStringLiteral("daily")},
                {QStringLiteral("lookbackPeriod"), 5},
                {QStringLiteral("lagEnabled"), true},
                {QStringLiteral("standardization"), QStringLiteral("none")},
                {QStringLiteral("neutralizationEnabled"), false}
            }},
            {QStringLiteral("dataRequirements"), QJsonObject{{QStringLiteral("required"), QJsonArray{QStringLiteral("close"), QStringLiteral("open")}}}},
            {QStringLiteral("boundaryRules"), QJsonObject{{QStringLiteral("minDataPoints"), 1}}}
        },
        QStringLiteral("临时自定义因子回放"));
    const auto& candidate = replayHandle.candidate;
    if (!candidate.has_value()) {
        GTEST_SKIP() << "no custom factor definition available in local database";
    }

    ASSERT_TRUE(candidate->config.has("calculation"));
    const auto calculation = candidate->config.get("calculation");

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);
    auto factorInstance = factor::TestConfigurableFactorHarness::create(
        candidate->instanceId,
        candidate->instanceName.toStdString(),
        candidate->instanceName.toStdString(),
        candidate->config,
        factor::config::factorTypeFromConfig(candidate->config),
        dataChecker);
    ASSERT_NE(factorInstance, nullptr);

    const QString latestDate = loadLatestTradeDate(database);
    ASSERT_FALSE(latestDate.isEmpty());

    factor::CalculationContext context;
    context.date = latestDate.toStdString();
    context.symbols = {"AAA", "BBB", "CCC"};
    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    assignHistoricalSeries(fieldSeries, "close", "AAA", buildLinearHistoricalSeries(latestDate, 8, 12.0, 0.10));
    assignHistoricalSeries(fieldSeries, "close", "BBB", buildLinearHistoricalSeries(latestDate, 8, 14.0, 0.10));
    assignHistoricalSeries(fieldSeries, "close", "CCC", buildLinearHistoricalSeries(latestDate, 8, 16.0, 0.10));
    assignHistoricalSeries(fieldSeries, "open", "AAA", buildLinearHistoricalSeries(latestDate, 8, 10.0, 0.08));
    assignHistoricalSeries(fieldSeries, "open", "BBB", buildLinearHistoricalSeries(latestDate, 8, 12.0, 0.08));
    assignHistoricalSeries(fieldSeries, "open", "CCC", buildLinearHistoricalSeries(latestDate, 8, 15.0, 0.08));
    if (calculation.has("variables")) {
        const auto variables = calculation.get("variables");
        if (variables.isArray()) {
            for (size_t index = 0; index < variables.size(); ++index) {
                const auto variable = variables.at(index);
                if (!variable.isObject() || !variable.has("field")) {
                    continue;
                }
                const std::string field = variable.get("field").asString();
                if (field.empty() || fieldSeries.find(field) != fieldSeries.end()) {
                    continue;
                }
                assignHistoricalSeries(fieldSeries, field, "AAA", buildLinearHistoricalSeries(latestDate, 8, 1.5, 0.05));
                assignHistoricalSeries(fieldSeries, field, "BBB", buildLinearHistoricalSeries(latestDate, 8, 2.0, 0.05));
                assignHistoricalSeries(fieldSeries, field, "CCC", buildLinearHistoricalSeries(latestDate, 8, 2.5, 0.05));
            }
        }
    }
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factorInstance->calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("expression"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("expression").asString()),
              QString::fromStdString(expectedCustomExpression(calculation)));
    ASSERT_TRUE(result.metadata.has("variableCount"));
    EXPECT_EQ(result.metadata.get("variableCount").asInt(), expectedCustomVariableCount(calculation));
    ASSERT_TRUE(result.metadata.has("symbolCount"));
    EXPECT_EQ(result.metadata.get("symbolCount").asInt(), static_cast<int>(result.values.size()));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_FALSE(QString::fromStdString(result.metadata.get("effectiveDate").asString()).isEmpty());
    ASSERT_TRUE(result.metadata.has("frequency"));
    EXPECT_EQ(normalizeConfigurableFrequencyForTest(scalarTextForTest(result.metadata.get("frequency"))),
              normalizeConfigurableFrequencyForTest(expectedConfigurableFrequencyRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), expectedConfigurableLookbackPeriod(calculation));
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_EQ(result.metadata.get("laggedEnabled").asBool(), expectedConfigurableLaggedEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
              normalizeConfigurableStandardizationForTest(
                  expectedConfigurableStandardizationRaw(calculation).empty() ? std::string("none") : expectedConfigurableStandardizationRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("neutralizationEnabled"));
    EXPECT_EQ(result.metadata.get("neutralizationEnabled").asBool(), expectedConfigurableNeutralizationEnabled(calculation));
}

TEST(FactorBacktestRegressionTest, SizeFactorTotalAssetsUsesConfiguredFinancialField)
{
    factor::SizeFactor factor;
    factor::SizeFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "size",
        "calculation": {
            "sizeMetric": "total_assets",
            "logTransform": false
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"total_assets", {{"AAA", 10.0}, {"BBB", 20.0}, {"CCC", 0.0}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_EQ(result.values.size(), 2U);
    EXPECT_NEAR(result.values.at("AAA"), -10.0, 1e-9);
    EXPECT_NEAR(result.values.at("BBB"), -20.0, 1e-9);
    EXPECT_TRUE(result.values.find("CCC") == result.values.end());
}

TEST(FactorBacktestRegressionTest, ExecutorExecuteReturnsCachedBacktestResultWithoutInstanceManager)
{
    const BacktestConfig config = makeCachedBacktestConfig("cached_factor_sync");
    const BacktestResult cachedResult = makeCachedExecutorResult(config.instanceId, 0.21, 7);
    const auto cacheManager = std::make_shared<FactorCacheManager>();
    cacheManager->setCacheFacade(makeSharedCacheFacade());
    seedBacktestResultCache(cacheManager, config, cachedResult);

    FactorBacktestExecutor executor(nullptr, nullptr, cacheManager);
    const BacktestResult result = executor.execute(config);

    EXPECT_EQ(result.status, std::string("SUCCESS"));
    EXPECT_EQ(result.instanceId, config.instanceId);
    EXPECT_DOUBLE_EQ(result.annualReturn, 0.21);
    EXPECT_EQ(result.executionTimeMs, 7);
    EXPECT_EQ(result.groupResult.groupReturns.size(), 5U);
}

TEST(FactorBacktestRegressionTest, BacktestConfigJsonUsesCanonicalBenchmarkSymbolOnly)
{
    BacktestConfig config = makeCachedBacktestConfig("canonical_benchmark_symbol");
    config.benchmarkSymbol = "000905.SH";

    const auto json = config.toJson();
    ASSERT_TRUE(json.has("benchmarkSymbol"));
    EXPECT_EQ(json.get("benchmarkSymbol").asString(), std::string("000905.SH"));
    EXPECT_FALSE(json.has("benchmark_symbol"));

    BacktestConfig roundTrip;
    roundTrip.fromJson(json);
    EXPECT_EQ(roundTrip.benchmarkSymbol, std::string("000905.SH"));

    BacktestConfig legacyConfig;
    legacyConfig.fromJson(foundation::json::JsonFacade::parse(R"JSON({
        "benchmark_symbol": "000852.SH"
    })JSON"));
    EXPECT_EQ(legacyConfig.benchmarkSymbol, std::string("000300.SH"));
}

TEST(FactorBacktestRegressionTest, ExecutorExecuteAsyncReturnsCachedBacktestResult)
{
    const BacktestConfig config = makeCachedBacktestConfig("cached_factor_async");
    const BacktestResult cachedResult = makeCachedExecutorResult(config.instanceId, 0.18, 5);
    const auto cacheManager = std::make_shared<FactorCacheManager>();
    cacheManager->setCacheFacade(makeSharedCacheFacade());
    seedBacktestResultCache(cacheManager, config, cachedResult);

    FactorBacktestExecutor executor(nullptr, nullptr, cacheManager);
    auto future = executor.executeAsync(config);
    const BacktestResult result = future.get();

    EXPECT_EQ(result.status, std::string("SUCCESS"));
    EXPECT_EQ(result.instanceId, config.instanceId);
    EXPECT_DOUBLE_EQ(result.annualReturn, 0.18);
    EXPECT_EQ(result.executionTimeMs, 5);
}

TEST(FactorBacktestRegressionTest, ExecutorTrackedAsyncReturnsCachedResultAndFinalizesTask)
{
    const BacktestConfig config = makeCachedBacktestConfig("cached_factor_tracked");
    const BacktestResult cachedResult = makeCachedExecutorResult(config.instanceId, 0.16, 9);
    const auto cacheManager = std::make_shared<FactorCacheManager>();
    cacheManager->setCacheFacade(makeSharedCacheFacade());
    seedBacktestResultCache(cacheManager, config, cachedResult);

    auto threadPool = std::make_shared<foundation::thread::ThreadPoolExecutor>(1);
    FactorBacktestExecutor executor(nullptr, threadPool, cacheManager);
    auto handle = executor.executeTrackedAsync(config);
    const BacktestResult result = handle.future.get();
    const auto progress = executor.getProgress(handle.taskId);

    EXPECT_FALSE(handle.taskId.to_string().empty());
    EXPECT_EQ(result.status, std::string("SUCCESS"));
    EXPECT_DOUBLE_EQ(result.annualReturn, 0.16);
    EXPECT_EQ(progress.status, std::string("NOT_FOUND"));

    threadPool->shutdown();
    AStockQuantEngine::Cache::CacheFacade::getInstance().shutdown();
}

TEST(FactorBacktestRegressionTest, ExecutorBatchReturnsCachedResultsForEachConfig)
{
    const BacktestConfig firstConfig = makeCachedBacktestConfig("cached_factor_batch_1");
    const BacktestConfig secondConfig = makeCachedBacktestConfig("cached_factor_batch_2");
    const auto cacheManager = std::make_shared<FactorCacheManager>();
    cacheManager->setCacheFacade(makeSharedCacheFacade());
    seedBacktestResultCache(cacheManager, firstConfig, makeCachedExecutorResult(firstConfig.instanceId, 0.11, 4));
    seedBacktestResultCache(cacheManager, secondConfig, makeCachedExecutorResult(secondConfig.instanceId, 0.13, 6));

    FactorBacktestExecutor executor(nullptr, nullptr, cacheManager);
    const auto results = executor.executeBatch({firstConfig, secondConfig});

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].instanceId, firstConfig.instanceId);
    EXPECT_EQ(results[1].instanceId, secondConfig.instanceId);
    EXPECT_DOUBLE_EQ(results[0].annualReturn, 0.11);
    EXPECT_DOUBLE_EQ(results[1].annualReturn, 0.13);

    AStockQuantEngine::Cache::CacheFacade::getInstance().shutdown();
}

TEST(FactorBacktestRegressionTest, ExecutorRunsRealCustomFactorOnCachedBars)
{
    const QString instanceId = QStringLiteral("real_custom_cached_arrow");
    const char* configJson = R"JSON({
        "factorType": "custom",
        "majorCategory": "自定义因子",
        "dataRequirements": {
            "required": ["close", "open"]
        },
        "calculation": {
            "expression": "x / y - 1",
            "variables": [
                {"name": "x", "field": "close"},
                {"name": "y", "field": "open"}
            ]
        },
        "boundaryRules": {
            "minDataPoints": 1
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("自定义因子"), configJson);
    auto factorInstance = std::make_shared<factor::ConfigurableFactorBase>();
    factor::ConfigurableFactorTestAccess::loadConfig(*factorInstance, foundation::json::JsonFacade::parse(configJson));

    auto instanceManager = std::make_shared<factor::FactorInstanceManager>(nullptr, nullptr);
    factor::FactorInstanceManagerTestAccess::seedInstance(*instanceManager, instanceId, instanceInfo, factorInstance);

    BacktestConfig config = makeCachedBacktestConfig(instanceId.toStdString());
    config.startDate = "2024-01-05";
    config.endDate = "2024-01-09";
    config.forwardDays = 1;
    config.numGroups = 2;

    const std::vector<std::string> tradeDates = {
        "2024-01-05",
        "2024-01-08",
        "2024-01-09",
        "2024-01-10"
    };
    const std::unordered_map<std::string, std::vector<double>> closesBySymbol = {
        {"AAA", {10.0, 10.2, 10.4, 10.6}},
        {"BBB", {20.0, 20.4, 20.8, 21.2}},
        {"CCC", {30.0, 29.4, 31.2, 30.6}},
        {"DDD", {40.0, 41.0, 39.5, 42.0}}
    };
    const std::unordered_map<std::string, std::vector<double>> opensBySymbol = {
        {"AAA", {9.8, 10.0, 10.1, 10.4}},
        {"BBB", {19.8, 20.1, 20.5, 20.9}},
        {"CCC", {29.7, 29.8, 30.9, 30.1}},
        {"DDD", {39.5, 40.3, 39.0, 41.0}}
    };

    std::vector<factor::CachedMarketBar> cachedBars;
    cachedBars.reserve(tradeDates.size() * closesBySymbol.size());
    for (size_t dateIndex = 0; dateIndex < tradeDates.size(); ++dateIndex) {
        for (const auto& [symbol, series] : closesBySymbol) {
            cachedBars.push_back({
                symbol,
                tradeDates[dateIndex],
                series[dateIndex],
                {{"open", opensBySymbol.at(symbol)[dateIndex]}}
            });
        }
    }
    config.cachedBars = std::move(cachedBars);

    FactorBacktestExecutor executor(instanceManager, nullptr, nullptr);
    const auto result = executor.execute(config);

    EXPECT_EQ(result.status, std::string("SUCCESS")) << result.errorMessage;
    EXPECT_TRUE(result.errorMessage.empty()) << result.errorMessage;
    EXPECT_GT(result.dataCoverage, 0.0);
    EXPECT_FALSE(result.groupResult.groupReturns.empty());
    EXPECT_EQ(result.groupResult.groupReturns.size(), 2U);
}

TEST(FactorBacktestRegressionTest, ExecutorPreservesSpreadWhenRiskControlsAdjustAnnualizedLongShort)
{
    const QString instanceId = QStringLiteral("real_custom_cached_arrow_risk_split");
    const char* configJson = R"JSON({
        "factorType": "custom",
        "majorCategory": "自定义因子",
        "dataRequirements": {
            "required": ["close", "open"]
        },
        "calculation": {
            "expression": "x / y - 1",
            "variables": [
                {"name": "x", "field": "close"},
                {"name": "y", "field": "open"}
            ]
        },
        "boundaryRules": {
            "minDataPoints": 1
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("自定义因子"), configJson);
    auto factorInstance = std::make_shared<factor::ConfigurableFactorBase>();
    factor::ConfigurableFactorTestAccess::loadConfig(*factorInstance, foundation::json::JsonFacade::parse(configJson));

    auto instanceManager = std::make_shared<factor::FactorInstanceManager>(nullptr, nullptr);
    factor::FactorInstanceManagerTestAccess::seedInstance(*instanceManager, instanceId, instanceInfo, factorInstance);

    BacktestConfig config = makeCachedBacktestConfig(instanceId.toStdString());
    config.startDate = "2024-01-05";
    config.endDate = "2024-01-09";
    config.forwardDays = 1;
    config.numGroups = 2;
    config.maxPositionPercent = 0.5;

    const std::vector<std::string> tradeDates = {
        "2024-01-05",
        "2024-01-08",
        "2024-01-09",
        "2024-01-10"
    };
    const std::unordered_map<std::string, std::vector<double>> closesBySymbol = {
        {"AAA", {10.0, 10.2, 10.4, 10.6}},
        {"BBB", {20.0, 20.4, 20.8, 21.2}},
        {"CCC", {30.0, 29.4, 31.2, 30.6}},
        {"DDD", {40.0, 41.0, 39.5, 42.0}}
    };
    const std::unordered_map<std::string, std::vector<double>> opensBySymbol = {
        {"AAA", {9.8, 10.0, 10.1, 10.4}},
        {"BBB", {19.8, 20.1, 20.5, 20.9}},
        {"CCC", {29.7, 29.8, 30.9, 30.1}},
        {"DDD", {39.5, 40.3, 39.0, 41.0}}
    };

    std::vector<factor::CachedMarketBar> cachedBars;
    cachedBars.reserve(tradeDates.size() * closesBySymbol.size());
    for (size_t dateIndex = 0; dateIndex < tradeDates.size(); ++dateIndex) {
        for (const auto& [symbol, series] : closesBySymbol) {
            cachedBars.push_back({
                symbol,
                tradeDates[dateIndex],
                series[dateIndex],
                {{"open", opensBySymbol.at(symbol)[dateIndex]}}
            });
        }
    }
    config.cachedBars = std::move(cachedBars);

    FactorBacktestExecutor executor(instanceManager, nullptr, nullptr);
    const auto result = executor.execute(config);

    ASSERT_EQ(result.status, std::string("SUCCESS")) << result.errorMessage;
    const double expectedSpread = result.groupResult.topGroupReturn
        - result.groupResult.bottomGroupReturn
        - (2.0 * config.transactionCost);
    const double annualizationFactor = 252.0 / static_cast<double>(config.forwardDays);

    EXPECT_NEAR(result.groupResult.longShortReturn, expectedSpread, 1e-12);
    EXPECT_NEAR(result.annualReturn,
                expectedSpread * config.maxPositionPercent * annualizationFactor,
                1e-9);
    EXPECT_GT(std::abs(result.groupResult.longShortReturn - (result.annualReturn / annualizationFactor)), 1e-6);
    EXPECT_GT(result.riskTriggeredCount, 0);
}

TEST(FactorBacktestRegressionTest, ExecutorRejectsBacktestWithoutHistoricalView)
{
    const QString instanceId = QStringLiteral("real_custom_without_view");
    const char* configJson = R"JSON({
        "factorType": "custom",
        "majorCategory": "自定义因子",
        "dataRequirements": {
            "required": ["close", "open"]
        },
        "calculation": {
            "expression": "x / y - 1",
            "variables": [
                {"name": "x", "field": "close"},
                {"name": "y", "field": "open"}
            ]
        },
        "boundaryRules": {
            "minDataPoints": 1
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("自定义因子"), configJson);
    auto factorInstance = std::make_shared<factor::ConfigurableFactorBase>();
    factor::ConfigurableFactorTestAccess::loadConfig(*factorInstance, foundation::json::JsonFacade::parse(configJson));

    auto instanceManager = std::make_shared<factor::FactorInstanceManager>(nullptr, nullptr);
    factor::FactorInstanceManagerTestAccess::seedInstance(*instanceManager, instanceId, instanceInfo, factorInstance);

    BacktestConfig config;
    config.instanceId = instanceId.toStdString();
    config.startDate = "2024-01-05";
    config.endDate = "2024-01-09";
    config.forwardDays = 1;
    config.numGroups = 2;

    FactorBacktestExecutor executor(instanceManager, nullptr, nullptr);
    const auto result = executor.execute(config);

    EXPECT_EQ(result.status, std::string("FAILED"));
    EXPECT_NE(result.errorMessage.find("HistoricalView"), std::string::npos);
}

TEST(FactorBacktestRegressionTest, DataServiceCacheRebuildsDataSetIndexFromCatalog)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString cacheKey = QStringLiteral("index_000001.SH_2024-01-01_2024-01-31");
    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "000001.SZ"},
                            {"trade_date", "2024-01-02"},
                            {"open", 9.5},
                            {"high", 10.2},
                            {"low", 9.4},
                            {"close", 10.0},
                            {"volume", 1000.0}});

    cache.storeData(cacheKey, rows);

    const auto storedInfos = cache.getAllDataSetInfos();
    ASSERT_EQ(storedInfos.size(), 1);
    const int dataSetId = storedInfos.front().id;

    DataServiceCacheTestAccess::resetInMemoryIndex(cache);
    EXPECT_EQ(cache.getAllDataSetInfos().size(), 1);

    const QVariantList rebuiltRows = cache.getDataSetById(dataSetId);
    ASSERT_EQ(rebuiltRows.size(), 1);
    EXPECT_EQ(rebuiltRows.front().toMap().value("symbol").toString(), QStringLiteral("000001.SZ"));
    EXPECT_EQ(cache.getAllDataSetInfos().size(), 1);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, DataServiceCacheKeepsIndexedDataSetUntilDatasetLoadActuallyFails)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString cacheKey = QStringLiteral("index_000300_2024-01-01_2024-01-31");
    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "000300.SH"},
                            {"trade_date", "2024-01-02"},
                            {"close", 3500.0}});

    cache.storeData(cacheKey, rows);

    const auto storedInfos = cache.getAllDataSetInfos();
    ASSERT_EQ(storedInfos.size(), 1);
    const int dataSetId = storedInfos.front().id;

    DataServiceCacheTestAccess::removeRawCacheKey(cache, cacheKey);

    EXPECT_TRUE(cache.getData(cacheKey).isEmpty());
    EXPECT_EQ(cache.getAllDataSetInfos().size(), 1);

    const QVariantList persistedRows = cache.getDataSetById(dataSetId);
    ASSERT_EQ(persistedRows.size(), 1);
    EXPECT_EQ(persistedRows.front().toMap().value("symbol").toString(), QStringLiteral("000300.SH"));
    EXPECT_EQ(cache.getAllDataSetInfos().size(), 1);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, WarmupResolveHistoryStartDateUsesTradingDaysAcrossLongHoliday)
{
    const QStringList ascendingTradeDates = {
        "2024-09-23",
        "2024-09-24",
        "2024-09-25",
        "2024-09-26",
        "2024-09-27",
        "2024-09-30"
    };

    const QDate historyStartDate = factor::warmup::resolveWarmupHistoryStartDate(
        QDate::fromString("2024-10-08", "yyyy-MM-dd"),
        ascendingTradeDates,
        3);

    ASSERT_TRUE(historyStartDate.isValid());
    EXPECT_EQ(historyStartDate.toString("yyyy-MM-dd"), QString("2024-09-26"));
}

TEST(FactorBacktestRegressionTest, WarmupResolveHistoryStartDateReturnsEarliestAvailableTradeDate)
{
    const QStringList ascendingTradeDates = {
        "2024-01-02",
        "2024-01-03"
    };

    const QDate historyStartDate = factor::warmup::resolveWarmupHistoryStartDate(
        QDate::fromString("2024-01-08", "yyyy-MM-dd"),
        ascendingTradeDates,
        5);

    ASSERT_TRUE(historyStartDate.isValid());
    EXPECT_EQ(historyStartDate.toString("yyyy-MM-dd"), QString("2024-01-02"));
}

TEST(FactorBacktestRegressionTest, WarmupFallbackCalendarLookbackKeepsLegacySafetyMargin)
{
    EXPECT_EQ(factor::warmup::fallbackWarmupCalendarLookbackDays(5), 365);
    EXPECT_EQ(factor::warmup::fallbackWarmupCalendarLookbackDays(300), 620);
    EXPECT_EQ(factor::warmup::requiredWarmupTradingDays(21, 5), 25);
}

TEST(FactorBacktestRegressionTest, WarmupDailyBarSelectExpressionUsesPhysicalAdjustFactorColumns)
{
    const QSet<QString> availableColumns = {
        QStringLiteral("close"),
        QStringLiteral("pre_adjust_factor"),
        QStringLiteral("post_adjust_factor")
    };

    EXPECT_EQ(factor::warmup::buildDailyBarSelectExpression(QStringLiteral("close"), availableColumns),
              QStringLiteral("close"));
    EXPECT_TRUE(factor::warmup::buildDailyBarSelectExpression(QStringLiteral("adj_factor"), availableColumns).isEmpty());

    const QStringList adjFactorSourceFields = factor::warmup::dailyBarSourceFieldsForField(
        QStringLiteral("adj_factor"),
        availableColumns);
    EXPECT_TRUE(adjFactorSourceFields.isEmpty());
    EXPECT_DOUBLE_EQ(factor::warmup::composeDailyBarAdjFactor(0.8, 1.2), 1.5);
    EXPECT_FALSE(std::isfinite(factor::warmup::composeDailyBarAdjFactor(0.0, 1.2)));
}

TEST(FactorBacktestRegressionTest, CachedBarsExtractTradeDatesNormalizesAndDeduplicatesFormats)
{
    const std::vector<factor::CachedMarketBar> bars = {
        {"AAA", "2024-01-08 15:00:00", 101.0, {}},
        {"BBB", "2024/01/08", 102.0, {}},
        {"AAA", "2024-01-09T15:00:00", 103.0, {}},
        {"AAA", "2024-01-10", 104.0, {}},
    };

    const auto tradeDates = factor::cached_bars::extractTradeDates(bars, "2024-01-08", "2024-01-09");

    ASSERT_EQ(tradeDates.size(), 2U);
    EXPECT_EQ(tradeDates[0], std::string("2024-01-08"));
    EXPECT_EQ(tradeDates[1], std::string("2024-01-09"));
}

TEST(FactorBacktestRegressionTest, CachedBarsExtractSymbolsMatchesNormalizedTradeDateAndFilter)
{
    const std::vector<factor::CachedMarketBar> bars = {
        {"AAA", "2024-01-08 15:00:00", 101.0, {}},
        {"BBB", "2024/01/08", 102.0, {}},
        {"CCC", "2024-01-09", 103.0, {}},
    };

    const std::unordered_set<std::string> allowedSymbols = {"BBB", "CCC"};
    const auto symbols = factor::cached_bars::extractSymbols(bars, "2024-01-08", allowedSymbols);

    ASSERT_EQ(symbols.size(), 1U);
    EXPECT_EQ(symbols.front(), std::string("BBB"));
}

TEST(FactorBacktestRegressionTest, CachedBarsFutureReturnUsesNormalizedTradeDateOrdering)
{
    const std::vector<factor::CachedMarketBar> bars = {
        {"AAA", "2024-01-05 15:00:00", 100.0, {}},
        {"AAA", "2024/01/08 15:00:00", 110.0, {}},
        {"AAA", "2024-01-09T15:00:00", 121.0, {}},
        {"AAA", "2024-01-10", 133.1, {}},
    };

    const double futureReturn = factor::cached_bars::calculateFutureReturn(bars, "AAA", "2024-01-08", 2);

    EXPECT_NEAR(futureReturn, 0.21, 1e-9);
}

TEST(FactorBacktestRegressionTest, ArrowMarketDataBuildsAndDeduplicatesBatchSymbols)
{
    const std::vector<factor::CachedMarketBar> bars = {
        {"AAA", "2024-01-08 15:00:00", 10.0, {{"open", 9.0}}},
        {"BBB", "2024-01-08", 20.0, {{"open", 19.0}}},
        {"AAA", "2024-01-09T15:00:00", 11.0, {{"open", 10.0}}},
        {"BBB", "2024-01-09", 21.0, {{"open", 20.0}}},
        {"AAA", "2024-01-10", 12.0, {{"open", 11.0}}},
    };

    const auto data = factor::ArrowMarketData::fromCachedBars(bars);
    ASSERT_NE(data, nullptr);

    const auto symbols = data->getAvailableSymbols("2024-01-08");
    ASSERT_EQ(symbols.size(), 2U);
    EXPECT_EQ(symbols[0], std::string("AAA"));
    EXPECT_EQ(symbols[1], std::string("BBB"));

    const auto crossSection = data->getCrossSection("2024-01-09", "close");
    ASSERT_EQ(crossSection.size(), 2U);
    EXPECT_DOUBLE_EQ(crossSection.at("AAA"), 11.0);
    EXPECT_DOUBLE_EQ(crossSection.at("BBB"), 21.0);

    const auto batchSeries = data->getBatchTimeSeries({"AAA", "AAA", "BBB"}, "close", 2, "2024-01-09");
    ASSERT_EQ(batchSeries.size(), 2U);
    ASSERT_EQ(batchSeries[0].size(), 2U);
    ASSERT_EQ(batchSeries[1].size(), 2U);
    EXPECT_DOUBLE_EQ(batchSeries[0][0], 10.0);
    EXPECT_DOUBLE_EQ(batchSeries[0][1], 11.0);
    EXPECT_DOUBLE_EQ(batchSeries[1][0], 20.0);
    EXPECT_DOUBLE_EQ(batchSeries[1][1], 21.0);
}

TEST(FactorBacktestRegressionTest, GroupBacktestAggregateBuildsDeterministicTwoGroupResult)
{
    const std::vector<CalculationResult> factorResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 4.0}, {"BBB", 3.0}, {"CCC", 2.0}, {"DDD", 1.0}}),
        makeCalculationResult("2024-01-09", {{"AAA", 8.0}, {"BBB", 6.0}, {"CCC", 4.0}, {"DDD", 2.0}}),
    };
    const std::vector<CalculationResult> returnResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 0.10}, {"BBB", 0.05}, {"CCC", -0.02}, {"DDD", -0.04}}),
        makeCalculationResult("2024-01-09", {{"AAA", 0.08}, {"BBB", 0.04}, {"CCC", -0.01}, {"DDD", -0.03}}),
    };

    BacktestConfig config;
    config.numGroups = 2;
    config.transactionCost = 0.001;
    const auto summary = factor::group_backtest::aggregate(factorResults, returnResults, config);

    ASSERT_TRUE(summary.hasValidGroup);
    ASSERT_EQ(summary.groupResult.groupReturns.size(), 2U);
    EXPECT_NEAR(summary.groupResult.groupReturns[0], 0.0675, 1e-9);
    EXPECT_NEAR(summary.groupResult.groupReturns[1], -0.025, 1e-9);
    EXPECT_EQ(summary.groupResult.groupStockCounts[0], 2);
    EXPECT_EQ(summary.groupResult.groupStockCounts[1], 2);
    EXPECT_DOUBLE_EQ(summary.groupResult.minFactorValues[0], 3.0);
    EXPECT_DOUBLE_EQ(summary.groupResult.maxFactorValues[0], 8.0);
    EXPECT_DOUBLE_EQ(summary.groupResult.minFactorValues[1], 1.0);
    EXPECT_DOUBLE_EQ(summary.groupResult.maxFactorValues[1], 4.0);
    EXPECT_NEAR(summary.groupResult.longShortReturn, 0.0905, 1e-9);
    EXPECT_EQ(summary.overlapDateCount, 2);
    EXPECT_EQ(summary.groupedDateCount, 2);
}

TEST(FactorBacktestRegressionTest, GroupBacktestAggregateLetsLastGroupAbsorbRemainder)
{
    const std::vector<CalculationResult> factorResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 5.0}, {"BBB", 4.0}, {"CCC", 3.0}, {"DDD", 2.0}, {"EEE", 1.0}}),
    };
    const std::vector<CalculationResult> returnResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 0.10}, {"BBB", 0.08}, {"CCC", 0.06}, {"DDD", 0.04}, {"EEE", 0.02}}),
    };

    BacktestConfig config;
    config.numGroups = 3;
    config.transactionCost = 0.0;
    const auto summary = factor::group_backtest::aggregate(factorResults, returnResults, config);

    ASSERT_TRUE(summary.hasValidGroup);
    ASSERT_EQ(summary.groupResult.groupReturns.size(), 3U);
    EXPECT_EQ(summary.groupResult.groupStockCounts[0], 1);
    EXPECT_EQ(summary.groupResult.groupStockCounts[1], 1);
    EXPECT_EQ(summary.groupResult.groupStockCounts[2], 3);
    EXPECT_NEAR(summary.groupResult.groupReturns[2], 0.04, 1e-9);
}

TEST(FactorBacktestRegressionTest, GroupBacktestAggregateReportsInsufficientMatchedStocks)
{
    const std::vector<CalculationResult> factorResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 5.0}}),
    };
    const std::vector<CalculationResult> returnResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 0.10}}),
    };

    BacktestConfig config;
    config.numGroups = 3;
    config.transactionCost = 0.001;
    const auto summary = factor::group_backtest::aggregate(factorResults, returnResults, config);

    EXPECT_FALSE(summary.hasValidGroup);
    EXPECT_EQ(summary.maxMatchedStocks, 1U);
    EXPECT_EQ(summary.overlapDateCount, 1);
    EXPECT_EQ(summary.groupedDateCount, 1);
    EXPECT_TRUE(summary.groupResult.groupReturns.size() < 2);
}

TEST(FactorBacktestRegressionTest, GroupBacktestAggregateHonorsRebalanceDaysForHeldPortfolios)
{
    const std::vector<CalculationResult> factorResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 4.0}, {"BBB", 3.0}, {"CCC", 2.0}, {"DDD", 1.0}}),
        makeCalculationResult("2024-01-09", {{"AAA", 1.0}, {"BBB", 2.0}, {"CCC", 3.0}, {"DDD", 4.0}}),
        makeCalculationResult("2024-01-10", {{"AAA", 1.0}, {"BBB", 2.0}, {"CCC", 3.0}, {"DDD", 4.0}}),
    };
    const std::vector<CalculationResult> returnResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 0.10}, {"BBB", 0.05}, {"CCC", -0.02}, {"DDD", -0.04}}),
        makeCalculationResult("2024-01-09", {{"AAA", 0.12}, {"BBB", 0.06}, {"CCC", -0.03}, {"DDD", -0.05}}),
        makeCalculationResult("2024-01-10", {{"AAA", -0.01}, {"BBB", -0.02}, {"CCC", 0.07}, {"DDD", 0.08}}),
    };

    BacktestConfig holdConfig;
    holdConfig.numGroups = 2;
    holdConfig.transactionCost = 0.0;
    holdConfig.rebalanceDays = 2;
    BacktestConfig dailyConfig;
    dailyConfig.numGroups = 2;
    dailyConfig.transactionCost = 0.0;
    dailyConfig.rebalanceDays = 1;
    const auto holdSummary = factor::group_backtest::aggregate(factorResults, returnResults, holdConfig);
    const auto dailySummary = factor::group_backtest::aggregate(factorResults, returnResults, dailyConfig);

    ASSERT_TRUE(holdSummary.hasValidGroup);
    ASSERT_EQ(holdSummary.longShortReturnsByDate.size(), 3U);
    ASSERT_EQ(holdSummary.longShortTurnoversByDate.size(), 3U);
    EXPECT_NEAR(holdSummary.longShortTurnoversByDate[0], 0.0, 1e-9);
    EXPECT_NEAR(holdSummary.longShortTurnoversByDate[1], 0.0, 1e-9);
    EXPECT_GT(holdSummary.longShortTurnoversByDate[2], 0.9);
    EXPECT_NEAR(holdSummary.longShortReturnsByDate[1], 0.13, 1e-9);

    ASSERT_TRUE(dailySummary.hasValidGroup);
    ASSERT_EQ(dailySummary.longShortReturnsByDate.size(), 3U);
    EXPECT_NEAR(dailySummary.longShortReturnsByDate[1], -0.13, 1e-9);
}

TEST(FactorBacktestRegressionTest, IcIrAggregateBuildsPositiveAndNegativeSeries)
{
    const std::vector<CalculationResult> factorResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 1.0}, {"BBB", 2.0}, {"CCC", 3.0}}),
        makeCalculationResult("2024-01-09", {{"AAA", 1.0}, {"BBB", 2.0}, {"CCC", 3.0}}),
    };
    const std::vector<CalculationResult> returnResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 1.0}, {"BBB", 2.0}, {"CCC", 3.0}}),
        makeCalculationResult("2024-01-09", {{"AAA", 3.0}, {"BBB", 2.0}, {"CCC", 1.0}}),
    };

    const auto summary = factor::icir::aggregate(factorResults, returnResults);

    ASSERT_TRUE(summary.hasValidSeries);
    ASSERT_EQ(summary.result.icSeries.size(), 2U);
    EXPECT_NEAR(summary.result.icSeries[0], 1.0, 1e-9);
    EXPECT_NEAR(summary.result.icSeries[1], -1.0, 1e-9);
    EXPECT_NEAR(summary.result.icMean, 0.0, 1e-9);
    EXPECT_NEAR(summary.result.icStd, 1.0, 1e-9);
    EXPECT_NEAR(summary.result.ir, 0.0, 1e-9);
    EXPECT_NEAR(summary.result.icPositiveRatio, 0.5, 1e-9);
}

TEST(FactorBacktestRegressionTest, IcIrAggregateReturnsInvalidWhenOverlapIsInsufficient)
{
    const std::vector<CalculationResult> factorResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 1.0}}),
        makeCalculationResult("2024-01-09", {{"BBB", 2.0}}),
    };
    const std::vector<CalculationResult> returnResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 0.3}}),
        makeCalculationResult("2024-01-09", {{"CCC", 0.5}}),
    };

    const auto summary = factor::icir::aggregate(factorResults, returnResults);

    EXPECT_FALSE(summary.hasValidSeries);
    EXPECT_TRUE(summary.result.icSeries.empty());
    EXPECT_DOUBLE_EQ(summary.result.icMean, 0.0);
    EXPECT_DOUBLE_EQ(summary.result.icStd, 0.0);
    EXPECT_DOUBLE_EQ(summary.result.ir, 0.0);
    EXPECT_DOUBLE_EQ(summary.result.icPositiveRatio, 0.0);
}

TEST(FactorBacktestRegressionTest, InstanceLookupCandidatesIncludeBaseFactorId)
{
    const FactorInstanceLookupCandidates candidates =
        factor::bridge::buildFactorInstanceLookupCandidates("quality_factor_instance");

    EXPECT_EQ(candidates.primaryId, "quality_factor_instance");
    EXPECT_EQ(candidates.secondaryId, "quality_factor");
}

TEST(FactorBacktestRegressionTest, InstanceLookupPrefersActiveFactorIdFallbackOverInactiveExactMatch)
{
    const QVector<FactorInstanceLookupRecord> records = {
        {"quality_factor_instance", "quality_factor", "INACTIVE"},
        {"quality_factor_v2", "quality_factor", "ACTIVE"}
    };

    EXPECT_EQ(factor::bridge::resolveFactorInstanceId("quality_factor_instance", records),
              "quality_factor_v2");
}

TEST(FactorBacktestRegressionTest, InstanceLookupPrefersExactActiveInstanceOverFactorIdMatch)
{
    const QVector<FactorInstanceLookupRecord> records = {
        {"quality_factor_v2", "quality_factor", "ACTIVE"},
        {"quality_factor_instance", "quality_factor", "ACTIVE"}
    };

    EXPECT_EQ(factor::bridge::resolveFactorInstanceId("quality_factor_instance", records),
              "quality_factor_instance");
}

TEST(FactorBacktestRegressionTest, DomainSyncWritePlanReusesExactInstanceAndDeletesFactorDuplicates)
{
    const QVector<FactorDomainExistingRecord> records = {
        {"quality_factor_instance", "quality_factor"},
        {"quality_factor_legacy", "quality_factor"},
        {"other_factor_instance", "other_factor"}
    };

    const FactorDomainSyncWritePlan plan =
        factor::bridge::planFactorDomainSyncWrite("quality_factor_instance", "quality_factor", records, false);

    EXPECT_TRUE(plan.updateExisting);
    EXPECT_EQ(plan.persistedInstanceId, "quality_factor_instance");
    EXPECT_EQ(plan.duplicateInstanceIds, QStringList{"quality_factor_legacy"});
}

TEST(FactorBacktestRegressionTest, DomainSyncWritePlanFallsBackToFactorMatchWhenRequestedInstanceMissing)
{
    const QVector<FactorDomainExistingRecord> records = {
        {"quality_factor_v2", "quality_factor"}
    };

    const FactorDomainSyncWritePlan plan =
        factor::bridge::planFactorDomainSyncWrite("quality_factor_instance", "quality_factor", records, false);

    EXPECT_TRUE(plan.updateExisting);
    EXPECT_EQ(plan.persistedInstanceId, "quality_factor_v2");
    EXPECT_TRUE(plan.duplicateInstanceIds.isEmpty());
}

TEST(FactorBacktestRegressionTest, DomainSyncWritePlanRetryForcesRequestedInstanceId)
{
    const QVector<FactorDomainExistingRecord> records = {
        {"quality_factor_v2", "quality_factor"}
    };

    const FactorDomainSyncWritePlan plan =
        factor::bridge::planFactorDomainSyncWrite("quality_factor_instance", "quality_factor", records, true);

    EXPECT_FALSE(plan.updateExisting);
    EXPECT_EQ(plan.persistedInstanceId, "quality_factor_instance");
    EXPECT_EQ(plan.duplicateInstanceIds, QStringList{"quality_factor_v2"});
}

TEST(FactorBacktestRegressionTest, DomainSyncRetryDeletesAndRewritesWhenInitialVerificationFails)
{
    QVector<bool> forceRequestedFlags;
    QVector<QString> verifyCalls;
    QString deletedFactorId;
    QString deletedInstanceId;

    QString finalInstanceId;
    const bool success = factor::bridge::executeDomainSyncWithRetry(
        "quality_factor_instance",
        "quality_factor",
        [&forceRequestedFlags](QString* persistedInstanceId, bool forceRequestedInstanceId) {
            forceRequestedFlags.append(forceRequestedInstanceId);
            if (persistedInstanceId) {
                *persistedInstanceId = forceRequestedInstanceId
                    ? QStringLiteral("quality_factor_instance")
                    : QStringLiteral("quality_factor_v2");
            }
            return true;
        },
        [&verifyCalls](const QString& instanceId, QString* errorMessage) {
            verifyCalls.append(instanceId);
            if (verifyCalls.size() == 1) {
                if (errorMessage) {
                    *errorMessage = QString::fromUtf8("首次验证失败");
                }
                return false;
            }
            return true;
        },
        [&deletedFactorId, &deletedInstanceId](const QString& factorId, const QString& instanceId) {
            deletedFactorId = factorId;
            deletedInstanceId = instanceId;
        },
        &finalInstanceId
    );

    EXPECT_TRUE(success);
    EXPECT_EQ(forceRequestedFlags, QVector<bool>({false, true}));
    EXPECT_EQ(verifyCalls, QVector<QString>({QStringLiteral("quality_factor_v2"), QStringLiteral("quality_factor_instance")}));
    EXPECT_EQ(deletedFactorId, QStringLiteral("quality_factor"));
    EXPECT_EQ(deletedInstanceId, QStringLiteral("quality_factor_v2"));
    EXPECT_EQ(finalInstanceId, QStringLiteral("quality_factor_instance"));
}

TEST(FactorBacktestRegressionTest, DomainSyncRetryFailsWhenRebuiltInstanceStillCannotVerify)
{
    int writeCount = 0;
    int verifyCount = 0;
    int deleteCount = 0;

    const bool success = factor::bridge::executeDomainSyncWithRetry(
        "quality_factor_instance",
        "quality_factor",
        [&writeCount](QString* persistedInstanceId, bool forceRequestedInstanceId) {
            ++writeCount;
            if (persistedInstanceId) {
                *persistedInstanceId = forceRequestedInstanceId
                    ? QStringLiteral("quality_factor_instance")
                    : QStringLiteral("quality_factor_v2");
            }
            return true;
        },
        [&verifyCount](const QString&, QString* errorMessage) {
            ++verifyCount;
            if (errorMessage) {
                *errorMessage = QString::fromUtf8("仍然无法验证");
            }
            return false;
        },
        [&deleteCount](const QString&, const QString&) {
            ++deleteCount;
        }
    );

    EXPECT_FALSE(success);
    EXPECT_EQ(writeCount, 2);
    EXPECT_EQ(verifyCount, 2);
    EXPECT_EQ(deleteCount, 1);
}

TEST(FactorBacktestRegressionTest, PreflightFailureSummaryIncludesResolvedInstanceId)
{
    const BacktestPreflightFailure failure{"quality_factor", "quality_factor_instance", "字段 close 缺失", "missing-field"};

    EXPECT_EQ(factor::bridge::summarizeBacktestPreflightFailure(failure),
              "quality_factor (instanceId=quality_factor_instance, 字段缺失: 字段 close 缺失)");
}

TEST(FactorBacktestRegressionTest, PreflightFailureSummaryOmitsMissingInstanceId)
{
    const BacktestPreflightFailure failure{"quality_factor", QString(), "", "instance-missing"};

    EXPECT_EQ(factor::bridge::summarizeBacktestPreflightFailure(failure),
              "quality_factor (实例不可用)");
}

TEST(FactorBacktestRegressionTest, PreflightFailureSummaryClassifiesHistoryAndValueProblems)
{
    const BacktestPreflightFailure invalidValue{
        "value_factor",
        "value_factor_instance",
        "字段 dividend_yield 非正数",
        "invalid-field-value"
    };
    EXPECT_EQ(
        factor::bridge::summarizeBacktestPreflightFailure(invalidValue),
        "value_factor (instanceId=value_factor_instance, 字段值无效: 字段 dividend_yield 非正数)");

    const BacktestPreflightFailure shortHistory{
        "momentum_factor",
        "momentum_factor_instance",
        "仅有 40 个交易日",
        "insufficient-history"
    };
    EXPECT_EQ(
        factor::bridge::summarizeBacktestPreflightFailure(shortHistory),
        "momentum_factor (instanceId=momentum_factor_instance, 历史长度不足: 仅有 40 个交易日)");
}

TEST(FactorBacktestRegressionTest, PreflightFailureVariantListPreservesStructuredFields)
{
    const QList<BacktestPreflightFailure> failures = {
        {"quality_factor", "quality_factor_instance", "字段 close 缺失", "missing-field"},
        {"value_factor", QString(), "", "instance-missing"}
    };

    const QVariantList result = factor::bridge::toVariantList(failures);
    ASSERT_EQ(result.size(), 2);

    const QVariantMap first = result.at(0).toMap();
    EXPECT_EQ(first.value("factorId").toString(), "quality_factor");
    EXPECT_EQ(first.value("instanceId").toString(), "quality_factor_instance");
    EXPECT_EQ(first.value("reason").toString(), "字段缺失: 字段 close 缺失");
    EXPECT_EQ(first.value("category").toString(), "missing-field");

    const QVariantMap second = result.at(1).toMap();
    EXPECT_EQ(second.value("factorId").toString(), "value_factor");
    EXPECT_TRUE(second.value("instanceId").toString().isEmpty());
    EXPECT_EQ(second.value("reason").toString(), "实例不可用");
    EXPECT_EQ(second.value("category").toString(), "instance-missing");
}

TEST(FactorBacktestRegressionTest, RequirementInferenceRejectsLegacyCoreFieldAliases)
{
    const QStringList normalized = factor::bridge::normalizeRequirementFieldNames(QStringList{
        QStringLiteral("adj_factor"),
        QStringLiteral("post_adjust_factor"),
        QStringLiteral("amount"),
        QStringLiteral("industry"),
        QStringLiteral("revenue_growth"),
        QStringLiteral("policy_score"),
        QStringLiteral("policy_score")
    });

    ASSERT_EQ(normalized.size(), 2);
    EXPECT_EQ(normalized.at(0), QStringLiteral("post_adjust_factor"));
    EXPECT_EQ(normalized.at(1), QStringLiteral("policy_score"));

    EXPECT_EQ(factor::bridge::canonicalContractFieldName(QStringLiteral("post_adjust_factor")),
              QStringLiteral("post_adjust_factor"));
    EXPECT_TRUE(factor::bridge::canonicalContractFieldName(QStringLiteral("ep")).isEmpty());
    EXPECT_TRUE(factor::bridge::canonicalContractFieldName(QStringLiteral("net_profit_growth")).isEmpty());
    EXPECT_TRUE(factor::bridge::canonicalContractFieldName(QStringLiteral("delta_roe")).isEmpty());
    EXPECT_TRUE(factor::bridge::canonicalContractFieldName(QStringLiteral("sue")).isEmpty());
    EXPECT_EQ(factor::bridge::canonicalContractFieldName(QStringLiteral("gross_margin")),
              QStringLiteral("gross_margin"));
    EXPECT_EQ(factor::bridge::canonicalContractFieldName(QStringLiteral("operating_margin")),
              QStringLiteral("operating_margin"));
    EXPECT_TRUE(factor::bridge::canonicalContractFieldName(QStringLiteral("hfq_factor")).isEmpty());
    EXPECT_TRUE(factor::bridge::canonicalContractFieldName(QStringLiteral("amount")).isEmpty());
    EXPECT_TRUE(factor::bridge::canonicalContractFieldName(QStringLiteral("turnover_amount")).isEmpty());
    EXPECT_TRUE(factor::bridge::canonicalContractFieldName(QStringLiteral("industry")).isEmpty());

    EXPECT_TRUE(factor::bridge::canonicalContractFieldName(QStringLiteral("adjust_factor")).isEmpty());
    EXPECT_TRUE(factor::bridge::canonicalContractFieldName(QStringLiteral("adjfactor")).isEmpty());
    EXPECT_TRUE(factor::bridge::canonicalContractFieldName(QStringLiteral("adjusted_close")).isEmpty());
    EXPECT_TRUE(factor::bridge::canonicalContractFieldName(QStringLiteral("adjclose")).isEmpty());
}

TEST(FactorBacktestRegressionTest, RequirementInferenceReadsCalculationFromInstanceInfoBoundary)
{
    factor::FactorInstanceInfo info;
    info.factorType = factor::FactorType::DIVIDEND;
    info.config = foundation::json::JsonFacade::parse(R"JSON({
        "calculation": {
            "metric": 2
        },
        "dataRequirements": {
            "required": ["dividend_stability"]
        }
    })JSON");

    const QVariantMap calculation = factor::bridge::extractRequirementCalculationMap(info);
    EXPECT_EQ(calculation.value(QStringLiteral("metric")).toInt(), static_cast<int>(factor::DividendMetric::DIVIDEND_STABILITY));

    const QStringList configuredFields = factor::bridge::configuredRequirementFields(info);
    ASSERT_EQ(configuredFields.size(), 1);
    EXPECT_EQ(configuredFields.at(0), QStringLiteral("dividend_stability"));

    const auto profile = factor::bridge::resolveFactorRequirementProfile(info);
    EXPECT_TRUE(profile.supported);
    EXPECT_EQ(profile.metric.toInt(), static_cast<int>(factor::DividendMetric::DIVIDEND_STABILITY));
    EXPECT_TRUE(profile.requiredFields.contains(QStringLiteral("dividend_stability")));
    EXPECT_EQ(factor::bridge::resolveRequirementSourceTable(info, {}),
              factor::SourceTable::FINANCIAL_INDICATOR);
}

TEST(FactorBacktestRegressionTest, RequirementJsonFacadeQtAdapterPreservesArbitraryObjectKeys)
{
    const foundation::json::JsonFacade value = foundation::json::JsonFacade::parse(R"JSON({
        "metric": 1,
        "growthWeights": [0.6, 0.4],
        "customFlag": true,
        "nested": {
            "freeKey": 7
        }
    })JSON");

    const QVariantMap converted = factor::bridge::RequirementJsonFacadeQtAdapter::toVariantMap(value);
    ASSERT_TRUE(converted.contains(QStringLiteral("metric")));
    ASSERT_TRUE(converted.contains(QStringLiteral("growthWeights")));
    ASSERT_TRUE(converted.contains(QStringLiteral("customFlag")));
    ASSERT_TRUE(converted.contains(QStringLiteral("nested")));
    EXPECT_EQ(converted.value(QStringLiteral("metric")).toInt(), 1);
    EXPECT_TRUE(converted.value(QStringLiteral("customFlag")).toBool());

    const QVariantList growthWeights = converted.value(QStringLiteral("growthWeights")).toList();
    ASSERT_EQ(growthWeights.size(), 2);
    EXPECT_DOUBLE_EQ(growthWeights.at(0).toDouble(), 0.6);
    EXPECT_DOUBLE_EQ(growthWeights.at(1).toDouble(), 0.4);

    const QVariantMap nested = converted.value(QStringLiteral("nested")).toMap();
    ASSERT_TRUE(nested.contains(QStringLiteral("freeKey")));
    EXPECT_EQ(nested.value(QStringLiteral("freeKey")).toInt(), 7);
}

TEST(FactorBacktestRegressionTest, RequirementInferenceMapsExtendedSupplementalFieldsToCanonicalTables)
{
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("policy_score")}),
              factor::SourceTable::POLICY_DATA);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("hot_rank")}),
              factor::SourceTable::ALTERNATIVE_DATA);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("basis_rate")}),
              factor::SourceTable::DERIVATIVES_DATA);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("sentiment_score")}),
              factor::SourceTable::NEWS_SENTIMENT);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("industry_code")}),
              factor::SourceTable::DAILY_BAR);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("revenue_growth")}),
              factor::SourceTable::UNKNOWN);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("roe")}),
              factor::SourceTable::FINANCIAL_INDICATOR);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("adj_close")}),
              factor::SourceTable::UNKNOWN);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("gross_margin")}),
              factor::SourceTable::FINANCIAL_INDICATOR);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("adj_factor")}),
              factor::SourceTable::UNKNOWN);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("post_adjust_factor")}),
              factor::SourceTable::DAILY_BAR);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("operating_margin")}),
              factor::SourceTable::FINANCIAL_INDICATOR);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("close"), QStringLiteral("policy_score")}),
              factor::SourceTable::UNKNOWN);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("amount")}),
              factor::SourceTable::UNKNOWN);
    EXPECT_EQ(factor::bridge::inferRequirementSourceTable(QStringList{QStringLiteral("industry")}),
              factor::SourceTable::UNKNOWN);
}

TEST(FactorBacktestRegressionTest, SupportMapRequirementResolutionRejectsUnknownConfiguredFields)
{
    const factor::bridge::SupportMapRequirementResolution resolution =
        factor::bridge::resolveSupportMapRequirementResolution(
            factor::FactorType::GROWTH,
            QVariantMap{},
            QStringList{QStringLiteral("amount")});

    EXPECT_EQ(resolution.failureCategory, QStringLiteral("unknown-field"));
    EXPECT_TRUE(resolution.failureReason.contains(QStringLiteral("amount")));
    EXPECT_TRUE(resolution.requiredFields.isEmpty());
}

TEST(FactorBacktestRegressionTest, RequirementInferenceSupportsGrowthMetricArraySelection)
{
    const QVariantMap calculation{{QStringLiteral("growthMetrics"), QVariantList{
        static_cast<int>(factor::GrowthMetric::REVENUE_GROWTH),
        static_cast<int>(factor::GrowthMetric::NET_PROFIT_GROWTH),
        static_cast<int>(factor::GrowthMetric::DELTA_ROE),
        static_cast<int>(factor::GrowthMetric::SUE)
    }},
    {QStringLiteral("growthWeights"), QVariantList{25, 25, 25, 25}}};

    const auto profile = factor::bridge::resolveFactorRequirementProfile(
        factor::FactorType::GROWTH,
        calculation);

    EXPECT_TRUE(profile.supported);
    EXPECT_EQ(profile.metric.toInt(), static_cast<int>(factor::GrowthMetric::REVENUE_GROWTH));
    EXPECT_TRUE(profile.requiredFields.contains(QStringLiteral("total_revenue")));
    EXPECT_TRUE(profile.requiredFields.contains(QStringLiteral("net_profit")));
    EXPECT_TRUE(profile.requiredFields.contains(QStringLiteral("roe")));
    EXPECT_TRUE(profile.requiredFields.contains(QStringLiteral("eps")));
}

TEST(FactorBacktestRegressionTest, RequirementInferenceRejectsLegacySingleGrowthMetricConfiguration)
{
    const QVariantMap calculation{{QStringLiteral("growthMetric"), static_cast<int>(factor::GrowthMetric::NET_PROFIT_GROWTH)}};

    const auto profile = factor::bridge::resolveFactorRequirementProfile(
        factor::FactorType::GROWTH,
        calculation);

    EXPECT_FALSE(profile.supported);
    EXPECT_TRUE(profile.requiredFields.isEmpty());
}

TEST(FactorBacktestRegressionTest, RequirementInferenceRejectsIncompleteGrowthConfiguration)
{
    const QVariantMap metricsOnly{{QStringLiteral("growthMetrics"), QVariantList{static_cast<int>(factor::GrowthMetric::REVENUE_GROWTH)}}};
    const QVariantMap mismatched{{QStringLiteral("growthMetrics"), QVariantList{static_cast<int>(factor::GrowthMetric::REVENUE_GROWTH)}},
                                 {QStringLiteral("growthWeights"), QVariantList{25, 75}}};

    const auto metricsOnlyProfile = factor::bridge::resolveFactorRequirementProfile(
        factor::FactorType::GROWTH,
        metricsOnly);
    const auto mismatchedProfile = factor::bridge::resolveFactorRequirementProfile(
        factor::FactorType::GROWTH,
        mismatched);

    EXPECT_FALSE(metricsOnlyProfile.supported);
    EXPECT_TRUE(metricsOnlyProfile.requiredFields.isEmpty());
    EXPECT_FALSE(mismatchedProfile.supported);
    EXPECT_TRUE(mismatchedProfile.requiredFields.isEmpty());
}

TEST(FactorBacktestRegressionTest, BuildDomainConfigPreservesGrowthMetricArraySelection)
{
    QVariantMap factorData = makeValidFactorRecord(
        QStringLiteral("factor_growth_eps"),
        QString::fromUtf8("成长因子"),
        QString::fromUtf8("成长因子展示"));
    factorData["factorType"] = factor::factorTypeIndex(factor::FactorType::GROWTH);
    factorData["parameters"] = QVariantMap{
        {QStringLiteral("growthMetrics"), QVariantList{static_cast<int>(factor::GrowthMetric::SUE)}},
        {QStringLiteral("growthWeights"), QVariantList{100}}
    };

    const QJsonObject config = factor::bridge::test::buildDomainConfigForTesting(factorData);
    const QJsonObject calculation = config.value(QStringLiteral("calculation")).toObject();
    const QJsonObject requirements = config.value(QStringLiteral("dataRequirements")).toObject();
    const QJsonArray required = requirements.value(QStringLiteral("required")).toArray();

    EXPECT_EQ(calculation.value(QStringLiteral("metric")).toInt(), static_cast<int>(factor::GrowthMetric::SUE));
    ASSERT_EQ(required.size(), 1);
    EXPECT_EQ(required.at(0).toString(), QStringLiteral("eps"));
}

TEST(FactorBacktestRegressionTest, BuildDomainConfigUsesQualityMetricSelection)
{
    QVariantMap factorData = makeValidFactorRecord(
        QStringLiteral("factor_quality_margin"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("质量因子展示"));
    factorData["factorType"] = factor::factorTypeIndex(factor::FactorType::QUALITY);
    factorData["parameters"] = QVariantMap{{QStringLiteral("metric"), static_cast<int>(factor::QualityMetric::GROSS_MARGIN)}};

    const QJsonObject config = factor::bridge::test::buildDomainConfigForTesting(factorData);
    const QJsonObject calculation = config.value(QStringLiteral("calculation")).toObject();
    const QJsonObject requirements = config.value(QStringLiteral("dataRequirements")).toObject();
    const QJsonArray required = requirements.value(QStringLiteral("required")).toArray();

    EXPECT_EQ(calculation.value(QStringLiteral("metric")).toInt(), static_cast<int>(factor::QualityMetric::GROSS_MARGIN));
    EXPECT_EQ(calculation.value(QStringLiteral("qualityThreshold")).toDouble(), 0.1);
    EXPECT_FALSE(calculation.contains(QStringLiteral("quality_threshold")));
    ASSERT_EQ(required.size(), 1);
    EXPECT_EQ(required.at(0).toString(), QStringLiteral("gross_margin"));
}

TEST(FactorBacktestRegressionTest, BuildDomainConfigUsesCanonicalSizeMetricSelection)
{
    QVariantMap factorData = makeValidFactorRecord(
        QStringLiteral("factor_size_float"),
        QString::fromUtf8("规模因子"),
        QString::fromUtf8("规模因子展示"));
    factorData["factorType"] = factor::factorTypeIndex(factor::FactorType::SIZE);
    factorData["parameters"] = QVariantMap{{QStringLiteral("sizeMetric"), static_cast<int>(factor::SizeMetric::CIRCULATING_MARKET_CAP)}};

    const QJsonObject config = factor::bridge::test::buildDomainConfigForTesting(factorData);
    const QJsonObject calculation = config.value(QStringLiteral("calculation")).toObject();

    EXPECT_EQ(calculation.value(QStringLiteral("sizeMetric")).toInt(), static_cast<int>(factor::SizeMetric::CIRCULATING_MARKET_CAP));
}

TEST(FactorBacktestRegressionTest, BuildDomainConfigUsesDirectDividendRequirements)
{
    QVariantMap factorData = makeValidFactorRecord(
        QStringLiteral("factor_dividend_stability"),
        QString::fromUtf8("红利因子"),
        QString::fromUtf8("红利因子展示"));
    factorData["factorType"] = factor::factorTypeIndex(factor::FactorType::DIVIDEND);
    factorData["parameters"] = QVariantMap{{QStringLiteral("dividendMetrics"), QVariantList{static_cast<int>(factor::DividendMetric::DIVIDEND_STABILITY)}}};

    const QJsonObject config = factor::bridge::test::buildDomainConfigForTesting(factorData);
    const QJsonObject calculation = config.value(QStringLiteral("calculation")).toObject();
    const QJsonObject requirements = config.value(QStringLiteral("dataRequirements")).toObject();
    const QJsonArray required = requirements.value(QStringLiteral("required")).toArray();
    const QJsonArray optional = requirements.value(QStringLiteral("optional")).toArray();

    EXPECT_EQ(calculation.value(QStringLiteral("metric")).toInt(), static_cast<int>(factor::DividendMetric::DIVIDEND_STABILITY));
    EXPECT_EQ(calculation.value(QStringLiteral("minDividendYield")).toInt(), 0);
    EXPECT_FALSE(calculation.contains(QStringLiteral("min_dividend_yield")));
    ASSERT_EQ(required.size(), 1);
    EXPECT_EQ(required.at(0).toString(), QStringLiteral("dividend_stability"));
    EXPECT_TRUE(optional.isEmpty());
}

TEST(FactorBacktestRegressionTest, BuildDomainConfigMapsSentimentSourceSelectionToDirectRequirements)
{
    QVariantMap factorData = makeValidFactorRecord(
        QStringLiteral("factor_sentiment_social"),
        QString::fromUtf8("情绪因子"),
        QString::fromUtf8("情绪因子展示"));
    factorData["factorType"] = factor::factorTypeIndex(factor::FactorType::SENTIMENT);
    factorData["parameters"] = QVariantMap{
        {QStringLiteral("sentimentSource"), static_cast<int>(factor::SentimentSource::SOCIAL_MEDIA)},
        {QStringLiteral("metric"), static_cast<int>(factor::SentimentMetric::SOCIAL_SENTIMENT)}
    };

    const QJsonObject config = factor::bridge::test::buildDomainConfigForTesting(factorData);
    const QJsonObject calculation = config.value(QStringLiteral("calculation")).toObject();
    const QJsonObject requirements = config.value(QStringLiteral("dataRequirements")).toObject();
    const QJsonArray required = requirements.value(QStringLiteral("required")).toArray();
    const QJsonArray optional = requirements.value(QStringLiteral("optional")).toArray();

    EXPECT_EQ(calculation.value(QStringLiteral("sentimentSource")).toInt(), static_cast<int>(factor::SentimentSource::SOCIAL_MEDIA));
    EXPECT_FALSE(calculation.contains(QStringLiteral("sentiment_source")));
    EXPECT_EQ(calculation.value(QStringLiteral("metric")).toInt(), static_cast<int>(factor::SentimentMetric::SOCIAL_SENTIMENT));
    ASSERT_EQ(required.size(), 1);
    EXPECT_EQ(required.at(0).toString(), QStringLiteral("social_sentiment"));
    EXPECT_EQ(requirements.value(QStringLiteral("sourceTable")).toInt(), static_cast<int>(factor::SourceTable::NEWS_SENTIMENT));
    EXPECT_TRUE(optional.isEmpty());
}

TEST(FactorBacktestRegressionTest, BuildDomainConfigDefaultsSentimentSourceToNewsSentiment)
{
    QVariantMap factorData = makeValidFactorRecord(
        QStringLiteral("factor_sentiment_default"),
        QString::fromUtf8("情绪因子"),
        QString::fromUtf8("情绪因子展示"));
    factorData["factorType"] = factor::factorTypeIndex(factor::FactorType::SENTIMENT);
    factorData["parameters"] = QVariantMap{};

    const QJsonObject config = factor::bridge::test::buildDomainConfigForTesting(factorData);
    const QJsonObject calculation = config.value(QStringLiteral("calculation")).toObject();
    const QJsonObject requirements = config.value(QStringLiteral("dataRequirements")).toObject();

    EXPECT_FALSE(calculation.contains(QStringLiteral("sentimentSource")));
    EXPECT_FALSE(calculation.contains(QStringLiteral("metric")));
    EXPECT_TRUE(requirements.value(QStringLiteral("required")).toArray().isEmpty());
    EXPECT_FALSE(requirements.contains(QStringLiteral("sourceTable")));
}

TEST(FactorBacktestRegressionTest, SentimentSourceInferenceRecognizesExtendedDataFamilies)
{
    EXPECT_EQ(factor::bridge::resolveSentimentSourceTable(factor::SentimentSource::POLICY),
              factor::SourceTable::POLICY_DATA);
    EXPECT_EQ(factor::bridge::resolveSentimentSourceTable(factor::SentimentSource::ALTERNATIVE),
              factor::SourceTable::ALTERNATIVE_DATA);
    EXPECT_EQ(factor::bridge::resolveSentimentSourceTable(factor::SentimentSource::DERIVATIVES),
              factor::SourceTable::DERIVATIVES_DATA);
    EXPECT_EQ(factor::bridge::resolveSentimentSourceTable(factor::SentimentSource::SOCIAL_MEDIA),
              factor::SourceTable::NEWS_SENTIMENT);
    EXPECT_EQ(factor::bridge::resolveSentimentSourceTable(factor::SentimentSource::MARKET),
              factor::SourceTable::NEWS_SENTIMENT);
    EXPECT_EQ(factor::bridge::resolveSentimentSourceTable(factor::SentimentSource::ANALYST_RATING),
              factor::SourceTable::NEWS_SENTIMENT);
}

TEST(FactorBacktestRegressionTest, ConfigurableSentimentFactorUsesDirectSupplementalMetricFromProvider)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::SENTIMENT)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::SentimentMetric::SENTIMENT_SCORE)},
                {QStringLiteral("sentimentSource"), static_cast<int>(factor::SentimentSource::NEWS)},
                {QStringLiteral("window"), 10}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"sentiment_score", {{"AAA", 0.82}, {"BBB", -0.15}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_EQ(result.values.size(), 2U);
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 0.82);
    EXPECT_DOUBLE_EQ(result.values.at("BBB"), -0.15);
    ASSERT_TRUE(result.metadata.has("metric"));
    EXPECT_EQ(
        normalizeSentimentMetricForTest(scalarTextForTest(result.metadata.get("metric"))),
        QStringLiteral("sentiment_score"));
    ASSERT_TRUE(result.metadata.has("dataMode"));
    EXPECT_EQ(result.metadata.get("dataMode").asInt(), 0);
}

TEST(FactorBacktestRegressionTest, ConfigurableSentimentFactorDoesNotInferMetricFromSourceAlias)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::SENTIMENT)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("sentimentSource"), static_cast<int>(factor::SentimentSource::SOCIAL_MEDIA)},
                {QStringLiteral("window"), 10}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"social_sentiment", {{"AAA", 0.91}, {"BBB", -0.2}}}
        });

    const CalculationResult result = factor.calculate(context);

    EXPECT_FALSE(result.dataStatus.isValid());
    EXPECT_TRUE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("emptyReason"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("emptyReason").asString()), QStringLiteral("情绪因子缺少有效 metric 枚举"));
}

TEST(FactorBacktestRegressionTest, ConfigurableSentimentFactorSourceAffectsResult)
{
    factor::ConfigurableFactorBase socialFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        socialFactor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::SENTIMENT)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::SentimentMetric::SOCIAL_SENTIMENT)},
                {QStringLiteral("sentimentSource"), static_cast<int>(factor::SentimentSource::SOCIAL_MEDIA)},
                {QStringLiteral("window"), 10},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    factor::ConfigurableFactorBase analystFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        analystFactor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::SENTIMENT)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::SentimentMetric::INVESTOR_SENTIMENT)},
                {QStringLiteral("sentimentSource"), static_cast<int>(factor::SentimentSource::ANALYST_RATING)},
                {QStringLiteral("window"), 10},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"social_sentiment", {{"AAA", 0.90}, {"BBB", -0.20}}},
            {"investor_sentiment", {{"AAA", 0.10}, {"BBB", 0.80}}}
        });

    const CalculationResult socialResult = socialFactor.calculate(context);
    const CalculationResult analystResult = analystFactor.calculate(context);

    ASSERT_TRUE(socialResult.dataStatus.isValid());
    ASSERT_TRUE(analystResult.dataStatus.isValid());
    EXPECT_GT(socialResult.values.at("AAA"), socialResult.values.at("BBB"));
    EXPECT_LT(analystResult.values.at("AAA"), analystResult.values.at("BBB"));
}

TEST(FactorBacktestRegressionTest, ConfigurableSentimentFactorWindowAffectsResult)
{
    factor::ConfigurableFactorBase shortWindowFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        shortWindowFactor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::SENTIMENT)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::SentimentMetric::SOCIAL_SENTIMENT)},
                {QStringLiteral("sentimentSource"), static_cast<int>(factor::SentimentSource::SOCIAL_MEDIA)},
                {QStringLiteral("window"), 5},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    factor::ConfigurableFactorBase longWindowFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        longWindowFactor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::SENTIMENT)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::SentimentMetric::SOCIAL_SENTIMENT)},
                {QStringLiteral("sentimentSource"), static_cast<int>(factor::SentimentSource::SOCIAL_MEDIA)},
                {QStringLiteral("window"), 10},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"social_sentiment", {
                {"AAA", {{"2024-01-02", 0.0}, {"2024-01-03", 0.0}, {"2024-01-04", 0.0}, {"2024-01-05", 0.0}, {"2024-01-08", 0.0}, {"2024-01-09", 10.0}, {"2024-01-10", 10.0}, {"2024-01-11", 10.0}, {"2024-01-12", 10.0}, {"2024-01-15", 10.0}}},
                {"BBB", {{"2024-01-02", 6.0}, {"2024-01-03", 6.0}, {"2024-01-04", 6.0}, {"2024-01-05", 6.0}, {"2024-01-08", 6.0}, {"2024-01-09", 6.0}, {"2024-01-10", 6.0}, {"2024-01-11", 6.0}, {"2024-01-12", 6.0}, {"2024-01-15", 6.0}}}
            }}
        });

    const CalculationResult shortResult = shortWindowFactor.calculate(context);
    const CalculationResult longResult = longWindowFactor.calculate(context);

    ASSERT_TRUE(shortResult.dataStatus.isValid());
    ASSERT_TRUE(longResult.dataStatus.isValid());
    EXPECT_GT(shortResult.values.at("AAA"), shortResult.values.at("BBB"));
    EXPECT_LT(longResult.values.at("AAA"), longResult.values.at("BBB"));
}

TEST(FactorBacktestRegressionTest, ConfigurableSentimentFactorCanUseLaggedEffectiveDateWithinLookbackWindow)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::SENTIMENT)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::SentimentMetric::SENTIMENT_SCORE)},
                {QStringLiteral("sentimentSource"), static_cast<int>(factor::SentimentSource::NEWS)},
                {QStringLiteral("frequency"), 0},
                {QStringLiteral("lookbackPeriod"), 3},
                {QStringLiteral("lagEnabled"), true},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    factor::ConfigurableFactorBase currentFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        currentFactor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::SENTIMENT)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::SentimentMetric::SENTIMENT_SCORE)},
                {QStringLiteral("sentimentSource"), static_cast<int>(factor::SentimentSource::NEWS)},
                {QStringLiteral("frequency"), 0},
                {QStringLiteral("lookbackPeriod"), 3},
                {QStringLiteral("lagEnabled"), false},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"sentiment_score", {
                {"AAA", {{"2024-01-12", 0.10}, {"2024-01-15", 0.40}}},
                {"BBB", {{"2024-01-12", 0.30}, {"2024-01-15", 0.20}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);
    const CalculationResult currentResult = currentFactor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 2U);
    ASSERT_TRUE(currentResult.dataStatus.isValid());
    ASSERT_EQ(currentResult.values.size(), 2U);
    EXPECT_NE(result.values.at("AAA"), currentResult.values.at("AAA"));
    EXPECT_NE(result.values.at("BBB"), currentResult.values.at("BBB"));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-12"));
    ASSERT_TRUE(currentResult.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(currentResult.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-15"));
}

TEST(FactorBacktestRegressionTest, ConfigurableSentimentFactorCanApplyMinMaxStandardization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::SENTIMENT)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::SentimentMetric::SENTIMENT_SCORE)},
                {QStringLiteral("sentimentSource"), static_cast<int>(factor::SentimentSource::NEWS)},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::MinMax)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"sentiment_score", {{"AAA", 1.0}, {"BBB", 3.0}, {"CCC", 5.0}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 3U);
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 0.0);
    EXPECT_NEAR(result.values.at("BBB"), 0.5, 1e-9);
    EXPECT_DOUBLE_EQ(result.values.at("CCC"), 1.0);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(
        normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
        QStringLiteral("minmax"));
}

TEST(FactorBacktestRegressionTest, ConfigurableSentimentFactorAppliesHistoricalViewIndustrySizeNeutralization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::SENTIMENT)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("metric"), static_cast<int>(factor::SentimentMetric::SENTIMENT_SCORE)},
                {QStringLiteral("sentimentSource"), static_cast<int>(factor::SentimentSource::POLICY)},
                {QStringLiteral("neutralizationEnabled"), true},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC", "DDD"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"sentiment_score", {
                {"AAA", {{"2024-01-15", 0.10}}},
                {"BBB", {{"2024-01-15", 0.20}}},
                {"CCC", {{"2024-01-15", 0.30}}},
                {"DDD", {{"2024-01-15", 0.40}}}
            }},
            {"market_cap", {
                {"AAA", {{"2024-01-15", 100.0}}},
                {"BBB", {{"2024-01-15", 200.0}}},
                {"CCC", {{"2024-01-15", 300.0}}},
                {"DDD", {{"2024-01-15", 400.0}}}
            }},
            {"industry_code", {
                {"AAA", {{"2024-01-15", 10.0}}},
                {"BBB", {{"2024-01-15", 10.0}}},
                {"CCC", {{"2024-01-15", 20.0}}},
                {"DDD", {{"2024-01-15", 20.0}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(normalizeNeutralizationModeForTest(scalarTextForTest(result.metadata.get("neutralizationMode"))),
              QStringLiteral("historical_view_cross_section_industry_size"));
}

TEST(FactorBacktestRegressionTest, RealSentimentFactorInstanceReplayUsesConfiguredRuntimeParameters)
{
    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        GTEST_SKIP() << "database unavailable";
    }

    const auto replayHandle = ensureReplayInstanceByCategory(
        database,
        QStringLiteral("情绪因子"),
        QJsonObject{
            {QStringLiteral("factorType"), QStringLiteral("sentiment")},
            {QStringLiteral("majorCategory"), QStringLiteral("情绪因子")},
            {QStringLiteral("factorName"), QStringLiteral("临时情绪因子回放")},
            {QStringLiteral("displayName"), QStringLiteral("临时情绪因子回放")},
            {QStringLiteral("calculation"), QJsonObject{{QStringLiteral("metric"), QStringLiteral("sentiment_score")}, {QStringLiteral("sentimentSource"), QStringLiteral("news")}, {QStringLiteral("window"), 20}, {QStringLiteral("frequency"), QStringLiteral("daily")}, {QStringLiteral("lookbackPeriod"), 20}, {QStringLiteral("laggedEnabled"), false}, {QStringLiteral("standardization"), QStringLiteral("none")}, {QStringLiteral("neutralizationEnabled"), false}}},
            {QStringLiteral("dataRequirements"), QJsonObject{{QStringLiteral("required"), QJsonArray{QStringLiteral("sentiment_score")}}}},
            {QStringLiteral("boundaryRules"), QJsonObject{{QStringLiteral("minDataPoints"), 1}}}
        },
        QStringLiteral("临时情绪因子回放"));
    const auto& candidate = replayHandle.candidate;
    if (!candidate.has_value()) {
        GTEST_SKIP() << "no sentiment factor definition available in local database";
    }

    const auto sentimentConfigResult = database->executeQuery(
        QStringLiteral("SELECT CAST(full_config AS CHAR) AS full_config FROM factor_instance WHERE instance_id = :instance_id LIMIT 1"),
        {{QStringLiteral(":instance_id"), QString::fromStdString(candidate->instanceId)}});
    ASSERT_FALSE(sentimentConfigResult.isEmpty());
    const auto persistedSentimentConfig = foundation::json::JsonFacade::parse(
        sentimentConfigResult.getRow(0).getString(QStringLiteral("full_config")).toStdString());
    ASSERT_TRUE(persistedSentimentConfig.has("factorType"));
    EXPECT_TRUE(persistedSentimentConfig.get("factorType").isNumber());
    ASSERT_TRUE(persistedSentimentConfig.has("calculation"));
    const auto persistedSentimentCalculation = persistedSentimentConfig.get("calculation");
    ASSERT_TRUE(persistedSentimentCalculation.has("metric"));
    EXPECT_TRUE(persistedSentimentCalculation.get("metric").isNumber());
    ASSERT_TRUE(persistedSentimentCalculation.has("sentimentSource"));
    EXPECT_TRUE(persistedSentimentCalculation.get("sentimentSource").isNumber());

    ASSERT_TRUE(candidate->config.has("calculation"));
    const auto calculation = candidate->config.get("calculation");
    ASSERT_TRUE(calculation.has("metric"));
    EXPECT_TRUE(calculation.get("metric").isNumber());
    ASSERT_TRUE(calculation.has("sentimentSource"));
    EXPECT_TRUE(calculation.get("sentimentSource").isNumber());

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);
    auto factorInstance = factor::TestConfigurableFactorHarness::create(
        candidate->instanceId,
        candidate->instanceName.toStdString(),
        candidate->instanceName.toStdString(),
        candidate->config,
        factor::config::factorTypeFromConfig(candidate->config),
        dataChecker);
    ASSERT_NE(factorInstance, nullptr);

    const QString latestDate = loadLatestTradeDate(database);
    ASSERT_FALSE(latestDate.isEmpty());

    factor::CalculationContext context;
    context.date = latestDate.toStdString();
    context.symbols = {"AAA", "BBB", "CCC"};
    DatedMultiFieldFactorDataProvider::FieldSeriesMap fieldSeries;
    assignHistoricalSeries(fieldSeries, "sentiment_score", "AAA", buildLinearHistoricalSeries(latestDate, 30, 0.10, 0.01));
    assignHistoricalSeries(fieldSeries, "sentiment_score", "BBB", buildLinearHistoricalSeries(latestDate, 30, 0.20, 0.01));
    assignHistoricalSeries(fieldSeries, "sentiment_score", "CCC", buildLinearHistoricalSeries(latestDate, 30, 0.30, 0.01));
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(std::move(fieldSeries));

    const auto result = factorInstance->calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("metric"));
    EXPECT_EQ(normalizeSentimentMetricForTest(scalarTextForTest(result.metadata.get("metric"))),
              normalizeSentimentMetricForTest(expectedSentimentMetricRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("sentimentSource"));
    EXPECT_EQ(normalizeSentimentSourceForTest(scalarTextForTest(result.metadata.get("sentimentSource"))),
              normalizeSentimentSourceForTest(expectedSentimentSourceRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_FALSE(QString::fromStdString(result.metadata.get("effectiveDate").asString()).isEmpty());
    ASSERT_TRUE(result.metadata.has("frequency"));
    EXPECT_EQ(normalizeConfigurableFrequencyForTest(scalarTextForTest(result.metadata.get("frequency"))),
              normalizeConfigurableFrequencyForTest(expectedConfigurableFrequencyRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), expectedConfigurableLookbackPeriod(calculation));
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_EQ(result.metadata.get("laggedEnabled").asBool(), expectedConfigurableLaggedEnabled(calculation));
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
              normalizeConfigurableStandardizationForTest(
                  expectedConfigurableStandardizationRaw(calculation).empty() ? std::string("none") : expectedConfigurableStandardizationRaw(calculation)));
    ASSERT_TRUE(result.metadata.has("neutralizationEnabled"));
    EXPECT_EQ(result.metadata.get("neutralizationEnabled").asBool(), expectedConfigurableNeutralizationEnabled(calculation));
}

TEST(FactorBacktestRegressionTest, ConfigurableTechnicalFactorUsesCanonicalPriceTypeAndVolumeConfirmation)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::TECHNICAL)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("technicalIndicators"), intArrayForTest({static_cast<int>(factor::TechnicalIndicator::RSI)})},
                {QStringLiteral("rsiWindow"), 3},
                {QStringLiteral("technicalPriceType"), static_cast<int>(factor::TechnicalPriceType::CLOSE)},
                {QStringLiteral("useVolume"), true}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {{"AAA", {{"2024-01-03", 100.0}, {"2024-01-04", 103.0}, {"2024-01-05", 106.0}, {"2024-01-08", 110.0}}}}},
            {"volume", {{"AAA", {{"2024-01-03", 1000.0}, {"2024-01-04", 1000.0}, {"2024-01-05", 1000.0}, {"2024-01-08", 2000.0}}}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_EQ(result.values.size(), 1U);
    const double baseMomentum = (110.0 - 100.0) / 100.0;
    EXPECT_GT(result.values.at("AAA"), baseMomentum);
    ASSERT_TRUE(result.metadata.has("priceType"));
    EXPECT_EQ(normalizeTechnicalPriceTypeForTest(scalarTextForTest(result.metadata.get("priceType"))), QStringLiteral("close"));
    ASSERT_TRUE(result.metadata.has("useVolume"));
    EXPECT_TRUE(result.metadata.get("useVolume").asBool());
}

TEST(FactorBacktestRegressionTest, ConfigurableTechnicalFactorRejectsLegacyAdjClosePriceType)
{
    factor::ConfigurableFactorBase factor;
    EXPECT_THROW(
        factor::ConfigurableFactorTestAccess::loadConfig(
            factor,
            foundation::json::JsonFacade::parse(R"JSON({
                "factorType": "technical",
                "calculation": {
                    "technicalIndicators": ["rsi"],
                    "rsiWindow": 3,
                    "technicalPriceType": "adj_close"
                }
            })JSON")),
        std::runtime_error);
}

TEST(FactorBacktestRegressionTest, ConfigurableTechnicalFactorSupportsTurnoverStabilityWithoutPriceSeries)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::TECHNICAL)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("technicalIndicators"), intArrayForTest({static_cast<int>(factor::TechnicalIndicator::TURNOVER_STABILITY)})},
                {QStringLiteral("turnoverStabilityMetric"), static_cast<int>(factor::LiquidityMetric::TURNOVER_RATE)},
                {QStringLiteral("turnoverStabilityWindow"), 4},
                {QStringLiteral("skipRecent"), 1}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"turnover_rate", {{"AAA", {{"2024-01-02", 1.0}, {"2024-01-03", 1.1}, {"2024-01-04", 0.9}, {"2024-01-05", 1.0}, {"2024-01-08", 1.05}}}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 1U);
    EXPECT_TRUE(std::isfinite(result.values.at("AAA")));
    ASSERT_TRUE(result.metadata.has("priceFieldDerived"));
    EXPECT_FALSE(result.metadata.get("priceFieldDerived").asBool());
}

TEST(FactorBacktestRegressionTest, ConfigurableTechnicalFactorUsesConfiguredTurnoverStabilityWindow)
{
    factor::ConfigurableFactorBase shortWindowFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(shortWindowFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "technical",
        "calculation": {
            "technicalIndicators": ["turnover_stability"],
            "turnoverStabilityMetric": "turnover_rate",
            "turnoverStabilityWindow": 3,
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase longWindowFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(longWindowFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "technical",
        "calculation": {
            "technicalIndicators": ["turnover_stability"],
            "turnoverStabilityMetric": "turnover_rate",
            "turnoverStabilityWindow": 6,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"turnover_rate", {{"AAA", {{"2024-01-02", 1.0}, {"2024-01-03", 9.0}, {"2024-01-04", 1.0}, {"2024-01-05", 8.0}, {"2024-01-06", 8.0}, {"2024-01-07", 8.0}, {"2024-01-08", 8.0}}}}}
        });

    const CalculationResult shortResult = shortWindowFactor.calculate(context);
    const CalculationResult longResult = longWindowFactor.calculate(context);

    ASSERT_TRUE(shortResult.dataStatus.isValid());
    ASSERT_TRUE(longResult.dataStatus.isValid());
    ASSERT_EQ(shortResult.values.size(), 1U);
    ASSERT_EQ(longResult.values.size(), 1U);
    EXPECT_GT(shortResult.values.at("AAA"), longResult.values.at("AAA"));
    ASSERT_TRUE(shortResult.metadata.has("turnoverStabilityWindow"));
    EXPECT_EQ(shortResult.metadata.get("turnoverStabilityWindow").asInt(), 3);
    ASSERT_TRUE(longResult.metadata.has("turnoverStabilityWindow"));
    EXPECT_EQ(longResult.metadata.get("turnoverStabilityWindow").asInt(), 6);
}

TEST(FactorBacktestRegressionTest, ConfigurableTechnicalFactorUsesConfiguredObvWindow)
{
    factor::ConfigurableFactorBase shortWindowFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        shortWindowFactor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::TECHNICAL)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("technicalIndicators"), intArrayForTest({static_cast<int>(factor::TechnicalIndicator::OBV)})},
                {QStringLiteral("obvWindow"), 3},
                {QStringLiteral("technicalPriceType"), static_cast<int>(factor::TechnicalPriceType::CLOSE)},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    factor::ConfigurableFactorBase longWindowFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        longWindowFactor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::TECHNICAL)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("technicalIndicators"), intArrayForTest({static_cast<int>(factor::TechnicalIndicator::OBV)})},
                {QStringLiteral("obvWindow"), 6},
                {QStringLiteral("technicalPriceType"), static_cast<int>(factor::TechnicalPriceType::CLOSE)},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {{"AAA", {{"2024-01-02", 10.0}, {"2024-01-03", 11.0}, {"2024-01-04", 12.0}, {"2024-01-05", 11.0}, {"2024-01-06", 10.0}, {"2024-01-07", 9.0}, {"2024-01-08", 9.5}}}}},
            {"volume", {{"AAA", {{"2024-01-02", 1.0}, {"2024-01-03", 100.0}, {"2024-01-04", 100.0}, {"2024-01-05", 1.0}, {"2024-01-06", 1.0}, {"2024-01-07", 1.0}, {"2024-01-08", 1.0}}}}}
        });

    const CalculationResult shortResult = shortWindowFactor.calculate(context);
    const CalculationResult longResult = longWindowFactor.calculate(context);

    ASSERT_TRUE(shortResult.dataStatus.isValid());
    ASSERT_TRUE(longResult.dataStatus.isValid());
    ASSERT_EQ(shortResult.values.size(), 1U);
    ASSERT_EQ(longResult.values.size(), 1U);
    EXPECT_NE(shortResult.values.at("AAA"), longResult.values.at("AAA"));
    EXPECT_GT(shortResult.values.at("AAA"), longResult.values.at("AAA"));
    EXPECT_GT(shortResult.values.at("AAA"), 0.0);
    EXPECT_GT(longResult.values.at("AAA"), 0.0);
    ASSERT_TRUE(shortResult.metadata.has("obvWindow"));
    EXPECT_EQ(shortResult.metadata.get("obvWindow").asInt(), 3);
    ASSERT_TRUE(longResult.metadata.has("obvWindow"));
    EXPECT_EQ(longResult.metadata.get("obvWindow").asInt(), 6);
}

TEST(FactorBacktestRegressionTest, ConfigurableTechnicalFactorDerivesTurnoverRequirementsFromRuntimeConfig)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "technical",
        "dataRequirements": {
            "required": ["close"]
        },
        "boundaryRules": {
            "minDataPoints": 14
        },
        "calculation": {
            "technicalIndicators": ["turnover_stability"],
            "turnoverStabilityMetric": "turnover_rate",
            "turnoverStabilityWindow": 250,
            "skipRecent": 90,
            "lookbackPeriod": 252,
            "technicalPriceType": "close"
        }
    })JSON"));

    const factor::DataRequirements requirements = factor.getDataRequirements();
    const factor::BoundaryRules boundaryRules = factor.getBoundaryRules();

    ASSERT_EQ(requirements.requiredFields.size(), 1U);
    EXPECT_EQ(requirements.requiredFields.front(), "turnover_rate");
    EXPECT_EQ(boundaryRules.minDataPoints, 250);
}

TEST(FactorBacktestRegressionTest, ConfigurableCustomFactorAcceptsAlternativeAndDerivativesFields)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "custom",
        "calculation": {
            "expression": "x + y",
            "variables": [
                {"name": "x", "field": "basis_rate"},
                {"name": "y", "field": "hot_rank"}
            ]
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"basis_rate", {{"AAA", 0.25}, {"BBB", -0.10}}},
            {"hot_rank", {{"AAA", 3.0}, {"BBB", 8.0}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_EQ(result.values.size(), 2U);
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 3.25);
    EXPECT_DOUBLE_EQ(result.values.at("BBB"), 7.9);
}

TEST(FactorBacktestRegressionTest, ConfigurableCustomFactorRejectsMissingExpression)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "custom",
        "calculation": {
            "variables": [
                {"name": "x", "field": "close"}
            ]
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"close", {{"AAA", 10.0}}}
        });

    const CalculationResult result = factor.calculate(context);

    EXPECT_FALSE(result.dataStatus.isValid());
    EXPECT_TRUE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("error"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("error").asString()),
              QStringLiteral("自定义因子必须显式提供 expression"));
}

TEST(FactorBacktestRegressionTest, ConfigurableCustomFactorIgnoresLegacyDefaultValueAlias)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "custom",
        "calculation": {
            "expression": "x + 1",
            "variables": [
                {"name": "x", "field": "missing_field", "default_value": 2.0}
            ]
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-08";
    context.symbols = {"AAA"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{});

    const CalculationResult result = factor.calculate(context);

    EXPECT_FALSE(result.dataStatus.isValid());
    EXPECT_TRUE(result.values.empty());
    EXPECT_TRUE(result.metadata.has("error") || result.metadata.has("emptyReason"));
}

TEST(FactorBacktestRegressionTest, ConfigurableCustomFactorCanUseLaggedEffectiveDateWithinLookbackWindow)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "custom",
        "calculation": {
            "expression": "x - y",
            "variables": [
                {"name": "x", "field": "close"},
                {"name": "y", "field": "open"}
            ],
            "frequency": "daily",
            "lookbackPeriod": 3,
            "laggedEnabled": true,
            "standardization": "none"
        }
    })JSON"));

    factor::ConfigurableFactorBase currentFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(currentFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "custom",
        "calculation": {
            "expression": "x - y",
            "variables": [
                {"name": "x", "field": "close"},
                {"name": "y", "field": "open"}
            ],
            "frequency": "daily",
            "lookbackPeriod": 3,
            "laggedEnabled": false,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"close", {
                {"AAA", {{"2024-01-12", 11.0}, {"2024-01-15", 13.0}}},
                {"BBB", {{"2024-01-12", 21.0}, {"2024-01-15", 24.0}}}
            }},
            {"open", {
                {"AAA", {{"2024-01-12", 10.0}, {"2024-01-15", 10.5}}},
                {"BBB", {{"2024-01-12", 20.0}, {"2024-01-15", 21.5}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);
    const CalculationResult currentResult = currentFactor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 2U);
    ASSERT_TRUE(currentResult.dataStatus.isValid());
    ASSERT_EQ(currentResult.values.size(), 2U);
    EXPECT_NE(result.values.at("AAA"), currentResult.values.at("AAA"));
    EXPECT_NE(result.values.at("BBB"), currentResult.values.at("BBB"));
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-12"));
    ASSERT_TRUE(currentResult.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(currentResult.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-15"));
}

TEST(FactorBacktestRegressionTest, ConfigurableCustomFactorCanApplyMinMaxStandardization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::CUSTOM)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("expression"), QStringLiteral("x")},
                {QStringLiteral("variables"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("x")}, {QStringLiteral("field"), QStringLiteral("basis_rate")}}}},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::MinMax)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<MultiFieldFactorDataProvider>(
        std::unordered_map<std::string, std::unordered_map<std::string, double>>{
            {"basis_rate", {{"AAA", 1.0}, {"BBB", 3.0}, {"CCC", 5.0}}}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 3U);
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 0.0);
    EXPECT_NEAR(result.values.at("BBB"), 0.5, 1e-9);
    EXPECT_DOUBLE_EQ(result.values.at("CCC"), 1.0);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))), QStringLiteral("minmax"));
}

TEST(FactorBacktestRegressionTest, ConfigurableCustomFactorAppliesHistoricalViewIndustrySizeNeutralization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(
        factor,
        jsonFacadeFromObjectForTest(QJsonObject{
            {QStringLiteral("factorType"), factor::factorTypeIndex(factor::FactorType::CUSTOM)},
            {QStringLiteral("calculation"), QJsonObject{
                {QStringLiteral("expression"), QStringLiteral("x")},
                {QStringLiteral("variables"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("x")}, {QStringLiteral("field"), QStringLiteral("basis_rate")}}}},
                {QStringLiteral("neutralizationEnabled"), true},
                {QStringLiteral("standardization"), static_cast<int>(factor::StandardizationMethod::None)}
            }}
        }));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC", "DDD"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"basis_rate", {
                {"AAA", {{"2024-01-15", 0.10}}},
                {"BBB", {{"2024-01-15", 0.20}}},
                {"CCC", {{"2024-01-15", 0.30}}},
                {"DDD", {{"2024-01-15", 0.40}}}
            }},
            {"market_cap", {
                {"AAA", {{"2024-01-15", 100.0}}},
                {"BBB", {{"2024-01-15", 200.0}}},
                {"CCC", {{"2024-01-15", 300.0}}},
                {"DDD", {{"2024-01-15", 400.0}}}
            }},
            {"industry_code", {
                {"AAA", {{"2024-01-15", 10.0}}},
                {"BBB", {{"2024-01-15", 10.0}}},
                {"CCC", {{"2024-01-15", 20.0}}},
                {"DDD", {{"2024-01-15", 20.0}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(normalizeNeutralizationModeForTest(scalarTextForTest(result.metadata.get("neutralizationMode"))),
              QStringLiteral("historical_view_cross_section_industry_size"));
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapMarksSupplementalPolicyFieldSupportedInCacheMode)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_sentiment_policy_instance");
    const char* configJson = R"JSON({
        "factorType": "sentiment",
        "dataRequirements": {
            "required": ["policy_score"],
            "sourceTable": "policy_data"
        },
        "calculation": {
            "metric": "policy_score",
            "sentimentSource": "policy"
        },
        "boundaryRules": {
            "minDataPoints": 1
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("情绪因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"},
                            {"trade_date", "2024-01-02"},
                            {"close", 10.0},
                            {"policy_score", 0.75}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close"), QStringLiteral("policy_score")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    //controller.setDataSourceMode(QStringLiteral("cache"));
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    EXPECT_EQ(FactorBacktestControllerTestAccess::resolveInstanceId(controller, instanceId), instanceId);
    ASSERT_NE(FactorBacktestControllerTestAccess::createInstance(controller, instanceId), nullptr);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();
    SCOPED_TRACE(QString("category=%1 reason=%2")
        .arg(supportInfo.value("category").toString(), supportInfo.value("reason").toString())
        .toStdString());

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(FactorBacktestControllerTestAccess::hasInitializedRuntime(controller));
    EXPECT_TRUE(supportInfo.value("supported").toBool());
    EXPECT_EQ(factor::bridge::test::supportInfoSourceTableForTest(supportInfo), factor::SourceTable::POLICY_DATA);
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::SENTIMENT);
    EXPECT_EQ(supportInfo.value("requiredFields").toList(), QVariantList{QStringLiteral("policy_score")});

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRejectsPartialDateWindowInDatabaseMode)
{
    const QString instanceId = QStringLiteral("factor_sentiment_policy_instance_db_window");
    const char* configJson = R"JSON({
        "factorType": "sentiment",
        "dataRequirements": {
            "required": ["policy_score"],
            "sourceTable": "policy_data"
        },
        "calculation": {
            "metric": "policy_score",
            "sentimentSource": "policy"
        },
        "boundaryRules": {
            "minDataPoints": 1
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("情绪因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    FactorBacktestController controller;
    controller.setDataSourceMode(QStringLiteral("database"));
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QString());
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("invalid-backtest-window"));
    EXPECT_EQ(supportInfo.value("reason").toString(), QStringLiteral("回测开始/结束日期必须同时提供，禁止使用默认兜底日期"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::SENTIMENT);
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapNormalizesMarketSentimentSourceToNewsSentimentInCacheMode)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_sentiment_market_instance");
    const char* configJson = R"JSON({
        "factorType": "sentiment",
        "dataRequirements": {
            "required": ["market_sentiment"],
            "sourceTable": "market_sentiment"
        },
        "calculation": {
            "metric": "market_sentiment",
            "sentimentSource": "market_sentiment",
            "window": 3
        },
        "boundaryRules": {
            "minDataPoints": 3
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("情绪因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-02"}, {"close", 10.0}, {"market_sentiment", 0.2}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-03"}, {"close", 10.1}, {"market_sentiment", 0.3}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-04"}, {"close", 10.3}, {"market_sentiment", 0.4}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close"), QStringLiteral("market_sentiment")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-04"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    //controller.setDataSourceMode(QStringLiteral("cache"));
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 3);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-04"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_TRUE(supportInfo.value("supported").toBool());
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::SENTIMENT);
    EXPECT_EQ(factor::bridge::test::supportInfoSourceTableForTest(supportInfo), factor::SourceTable::NEWS_SENTIMENT);
    EXPECT_EQ(supportInfo.value("requiredFields").toList(), QVariantList{QStringLiteral("market_sentiment")});

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, ConfigurableGrowthFactorReadsGrowthMetricsSelection)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "growth",
        "calculation": {
            "growthMetrics": ["net_profit_growth", "delta_roe", "sue"],
            "growthWeights": [40, 30, 30]
        }
    })JSON"));

    EXPECT_EQ(factor::ConfigurableFactorTestAccess::normalizedType(factor), QStringLiteral("growth"));
    EXPECT_EQ(factor::ConfigurableFactorTestAccess::normalizedMetric(factor), QStringLiteral("net_profit_growth"));
}

TEST(FactorBacktestRegressionTest, ConfigurableGrowthFactorWeightsAffectResult)
{
    factor::ConfigurableFactorBase revenueWeightedFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(revenueWeightedFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "growth",
        "calculation": {
            "growthMetrics": ["revenue_growth", "delta_roe"],
            "growthWeights": [100, 0],
            "standardization": "none",
            "frequency": "daily",
            "laggedEnabled": false
        }
    })JSON"));

    factor::ConfigurableFactorBase roeWeightedFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(roeWeightedFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "growth",
        "calculation": {
            "growthMetrics": ["revenue_growth", "delta_roe"],
            "growthWeights": [0, 100],
            "standardization": "none",
            "frequency": "daily",
            "laggedEnabled": false
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"total_revenue", {
                {"AAA", {{"2023-12-31", 100.0}, {"2024-01-15", 120.0}}},
                {"BBB", {{"2023-12-31", 100.0}, {"2024-01-15", 110.0}}}
            }},
            {"roe", {
                {"AAA", {{"2023-12-31", 10.0}, {"2024-01-15", 11.0}}},
                {"BBB", {{"2023-12-31", 10.0}, {"2024-01-15", 20.0}}}
            }}
        });

    const CalculationResult revenueWeightedResult = revenueWeightedFactor.calculate(context);
    const CalculationResult roeWeightedResult = roeWeightedFactor.calculate(context);

    ASSERT_TRUE(revenueWeightedResult.dataStatus.isValid());
    ASSERT_TRUE(roeWeightedResult.dataStatus.isValid());
    ASSERT_EQ(revenueWeightedResult.values.size(), 2U);
    ASSERT_EQ(roeWeightedResult.values.size(), 2U);

    EXPECT_LT(revenueWeightedResult.values.at("AAA"), revenueWeightedResult.values.at("BBB"));
    EXPECT_GT(roeWeightedResult.values.at("AAA"), roeWeightedResult.values.at("BBB"));
    EXPECT_NE(revenueWeightedResult.values.at("AAA"), roeWeightedResult.values.at("AAA"));
    EXPECT_NE(revenueWeightedResult.values.at("BBB"), roeWeightedResult.values.at("BBB"));
}

TEST(FactorBacktestRegressionTest, ConfigurableGrowthFactorRejectsIncompleteGrowthConfiguration)
{
    EXPECT_THROW(
        factor::ConfigurableFactorTestAccess::loadConfig(
            *std::make_shared<factor::ConfigurableFactorBase>(),
            foundation::json::JsonFacade::parse(R"JSON({
                "factorType": "growth",
                "calculation": {
                    "growthMetrics": ["net_profit_growth"]
                }
            })JSON")),
        std::exception);
}

TEST(FactorBacktestRegressionTest, ConfigurableGrowthFactorAppliesHistoricalViewIndustrySizeNeutralization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "growth",
        "calculation": {
            "growthMetrics": ["revenue_growth"],
            "growthWeights": [100],
            "neutralizationEnabled": true
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC", "DDD"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"total_revenue", {
                {"AAA", {{"2023-12-31", 100.0}, {"2024-01-15", 120.0}}},
                {"BBB", {{"2023-12-31", 90.0}, {"2024-01-15", 99.0}}},
                {"CCC", {{"2023-12-31", 80.0}, {"2024-01-15", 96.0}}},
                {"DDD", {{"2023-12-31", 70.0}, {"2024-01-15", 73.5}}}
            }},
            {"market_cap", {
                {"AAA", {{"2024-01-15", 100.0}}},
                {"BBB", {{"2024-01-15", 200.0}}},
                {"CCC", {{"2024-01-15", 300.0}}},
                {"DDD", {{"2024-01-15", 400.0}}}
            }},
            {"industry_code", {
                {"AAA", {{"2024-01-15", 10.0}}},
                {"BBB", {{"2024-01-15", 10.0}}},
                {"CCC", {{"2024-01-15", 20.0}}},
                {"DDD", {{"2024-01-15", 20.0}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    EXPECT_FALSE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("neutralizationMode"));
    EXPECT_EQ(normalizeNeutralizationModeForTest(scalarTextForTest(result.metadata.get("neutralizationMode"))),
              QStringLiteral("historical_view_cross_section_industry_size"));
}

TEST(FactorBacktestRegressionTest, ConfigurableGrowthFactorCanUseLaggedEffectiveDateWithinLookbackWindow)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "growth",
        "calculation": {
            "growthMetrics": ["revenue_growth"],
            "growthWeights": [100],
            "frequency": "daily",
            "lookbackPeriod": 3,
            "lagEnabled": true,
            "standardization": "none"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"total_revenue", {
                {"AAA", {{"2023-12-31", 100.0}, {"2024-01-12", 110.0}, {"2024-01-15", 130.0}}},
                {"BBB", {{"2023-12-31", 100.0}, {"2024-01-12", 120.0}, {"2024-01-15", 125.0}}}
            }}
        });

    factor::ConfigurableFactorBase currentFactor;
    factor::ConfigurableFactorTestAccess::loadConfig(currentFactor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "growth",
        "calculation": {
            "growthMetrics": ["revenue_growth"],
            "growthWeights": [100],
            "frequency": "daily",
            "lookbackPeriod": 3,
            "lagEnabled": false,
            "standardization": "none"
        }
    })JSON"));

    const CalculationResult result = factor.calculate(context);
    const CalculationResult currentResult = currentFactor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 2U);
    ASSERT_TRUE(currentResult.dataStatus.isValid());
    ASSERT_EQ(currentResult.values.size(), 2U);
    ASSERT_TRUE(result.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(result.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-12"));
    ASSERT_TRUE(currentResult.metadata.has("effectiveDate"));
    EXPECT_EQ(QString::fromStdString(currentResult.metadata.get("effectiveDate").asString()), QStringLiteral("2024-01-15"));
    ASSERT_TRUE(result.metadata.has("lookbackPeriod"));
    EXPECT_EQ(result.metadata.get("lookbackPeriod").asInt(), 3);
    ASSERT_TRUE(result.metadata.has("laggedEnabled"));
    EXPECT_TRUE(result.metadata.get("laggedEnabled").asBool());
}

TEST(FactorBacktestRegressionTest, ConfigurableGrowthFactorCanApplyMinMaxStandardization)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "growth",
        "calculation": {
            "growthMetrics": ["revenue_growth"],
            "growthWeights": [100],
            "standardization": "minmax"
        }
    })JSON"));

    CalculationContext context;
    context.date = "2024-01-15";
    context.symbols = {"AAA", "BBB", "CCC"};
    context.historicalView = std::make_shared<DatedMultiFieldFactorDataProvider>(
        DatedMultiFieldFactorDataProvider::FieldSeriesMap{
            {"total_revenue", {
                {"AAA", {{"2023-12-31", 100.0}, {"2024-01-15", 110.0}}},
                {"BBB", {{"2023-12-31", 100.0}, {"2024-01-15", 120.0}}},
                {"CCC", {{"2023-12-31", 100.0}, {"2024-01-15", 130.0}}}
            }}
        });

    const CalculationResult result = factor.calculate(context);

    ASSERT_TRUE(result.dataStatus.isValid());
    ASSERT_EQ(result.values.size(), 3U);
    EXPECT_DOUBLE_EQ(result.values.at("AAA"), 1.0);
    EXPECT_GT(result.values.at("BBB"), 0.0);
    EXPECT_LT(result.values.at("BBB"), 1.0);
    EXPECT_DOUBLE_EQ(result.values.at("CCC"), 0.0);
    ASSERT_TRUE(result.metadata.has("standardization"));
    EXPECT_EQ(normalizeConfigurableStandardizationForTest(scalarTextForTest(result.metadata.get("standardization"))),
              QStringLiteral("minmax"));
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRequiresNeutralizationFieldsForLiquidity)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("liquidity_neutralization_instance");
    factor::FactorInstanceInfo instanceInfo;
    instanceInfo.instanceId = instanceId.toStdString();
    instanceInfo.factorType = factor::FactorType::CUSTOM;
    instanceInfo.instanceName = "Liquidity Neutralization";
    instanceInfo.description = "Liquidity Neutralization";
    instanceInfo.isAvailable = true;
    instanceInfo.config = parseRuntimeCompatibleTestConfig(R"JSON({
        "factorType": "liquidity",
        "calculation": {
            "metric": "turnover_rate",
            "window": 20,
            "neutralizationEnabled": true
        }
    })JSON");

    auto factorInstance = factor::ConfigurableFactorBase::create(instanceInfo, nullptr);
    ASSERT_NE(factorInstance, nullptr);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-02"}, {"close", 10.0}, {"turnover_rate", 2.0}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close"), QStringLiteral("turnover_rate")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 1);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::LIQUIDITY);
    const QVariantList expectedMissingFields{QStringLiteral("industry_code"), QStringLiteral("market_cap")};
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedMissingFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRequiresNeutralizationFieldsForLowVol)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("lowvol_neutralization_instance");
    factor::FactorInstanceInfo instanceInfo;
    instanceInfo.instanceId = instanceId.toStdString();
    instanceInfo.factorType = factor::FactorType::LOW_VOLATILITY;
    instanceInfo.instanceName = "LowVol Neutralization";
    instanceInfo.description = "LowVol Neutralization";
    instanceInfo.isAvailable = true;
    instanceInfo.config = parseRuntimeCompatibleTestConfig(R"JSON({
        "factorType": "low_volatility",
        "calculation": {
            "window": 20,
            "components": ["volatility", "drawdown"],
            "neutralizationEnabled": true,
            "standardization": "none"
        }
    })JSON");

    auto factorInstance = factor::LowVolFactor::create(instanceInfo, nullptr);
    ASSERT_NE(factorInstance, nullptr);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"},
                            {"trade_date", "2024-01-02"},
                            {"close", 10.0}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 1);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::LOW_VOLATILITY);
    const QVariantList expectedMissingFields{QStringLiteral("industry_code"), QStringLiteral("market_cap")};
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedMissingFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRequiresNeutralizationFieldsForMomentum)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("momentum_neutralization_instance");
    factor::FactorInstanceInfo instanceInfo;
    instanceInfo.instanceId = instanceId.toStdString();
    instanceInfo.factorType = factor::FactorType::MOMENTUM;
    instanceInfo.instanceName = "Momentum Neutralization";
    instanceInfo.description = "Momentum Neutralization";
    instanceInfo.isAvailable = true;
    instanceInfo.config = parseRuntimeCompatibleTestConfig(R"JSON({
        "factorType": "momentum",
        "calculation": {
            "window": 20,
            "neutralizationEnabled": true,
            "standardization": "none"
        }
    })JSON");

    auto factorInstance = factor::MomentumFactor::create(instanceInfo, nullptr);
    ASSERT_NE(factorInstance, nullptr);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"},
                            {"trade_date", "2024-01-02"},
                            {"close", 10.0}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 1);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::MOMENTUM);
    const QVariantList expectedMissingFields{
        QStringLiteral("post_adjust_factor"),
        QStringLiteral("industry_code"),
        QStringLiteral("market_cap")};
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedMissingFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRequiresAdjFactorFieldForMomentum)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("momentum_adj_factor_instance");
    factor::FactorInstanceInfo instanceInfo;
    instanceInfo.instanceId = instanceId.toStdString();
    instanceInfo.factorType = factor::FactorType::MOMENTUM;
    instanceInfo.instanceName = "Momentum AdjFactor";
    instanceInfo.description = "Momentum AdjFactor";
    instanceInfo.isAvailable = true;
    instanceInfo.config = parseRuntimeCompatibleTestConfig(R"JSON({
        "factorType": "momentum",
        "calculation": {
            "window": 20,
            "adjustPriceType": "pre_adjust_factor",
            "standardization": "none"
        }
    })JSON");

    auto factorInstance = factor::MomentumFactor::create(instanceInfo, nullptr);
    ASSERT_NE(factorInstance, nullptr);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"},
                            {"trade_date", "2024-01-02"},
                            {"close", 10.0}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 1);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::MOMENTUM);
    EXPECT_EQ(supportInfo.value("missingFields").toList(), QVariantList{QStringLiteral("pre_adjust_factor")});

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRequiresNeutralizationFieldsForSize)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("size_neutralization_instance");
    factor::FactorInstanceInfo instanceInfo;
    instanceInfo.instanceId = instanceId.toStdString();
    instanceInfo.factorType = factor::FactorType::SIZE;
    instanceInfo.instanceName = "Size Neutralization";
    instanceInfo.description = "Size Neutralization";
    instanceInfo.isAvailable = true;
    instanceInfo.config = parseRuntimeCompatibleTestConfig(R"JSON({
        "factorType": "size",
        "calculation": {
            "sizeMetric": "circulating_market_cap",
            "neutralizationEnabled": true,
            "standardization": "none"
        }
    })JSON");

    auto factorInstance = factor::SizeFactor::create(instanceInfo, nullptr);
    ASSERT_NE(factorInstance, nullptr);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"},
                            {"trade_date", "2024-01-02"},
                            {"circulating_market_cap", 90.0}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("circulating_market_cap")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 1);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::SIZE);
    const QVariantList expectedMissingFields{QStringLiteral("industry_code"), QStringLiteral("market_cap")};
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedMissingFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRequiresNeutralizationFieldsForValue)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("value_neutralization_instance");
    factor::FactorInstanceInfo instanceInfo;
    instanceInfo.instanceId = instanceId.toStdString();
    instanceInfo.factorType = factor::FactorType::VALUE;
    instanceInfo.instanceName = "Value Neutralization";
    instanceInfo.description = "Value Neutralization";
    instanceInfo.isAvailable = true;
    instanceInfo.config = parseRuntimeCompatibleTestConfig(R"JSON({
        "factorType": "value",
        "calculation": {
            "valuationMetrics": ["bp"],
            "neutralizationEnabled": true,
            "standardization": "none"
        }
    })JSON");

    auto factorInstance = factor::ValueFactor::create(instanceInfo, nullptr);
    ASSERT_NE(factorInstance, nullptr);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"},
                            {"trade_date", "2024-01-02"},
                            {"pb_ratio", 1.25}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("pb_ratio")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 1);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::VALUE);
    const QVariantList expectedMissingFields{QStringLiteral("industry_code"), QStringLiteral("market_cap")};
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedMissingFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, ConfigurableDividendFactorReadsDividendMetricsSelection)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "dividend",
        "calculation": {
            "dividendMetrics": ["payout_ratio"]
        }
    })JSON"));

    EXPECT_EQ(factor::ConfigurableFactorTestAccess::normalizedType(factor), QStringLiteral("dividend"));
    EXPECT_EQ(factor::ConfigurableFactorTestAccess::normalizedMetric(factor), QStringLiteral("payout_ratio"));
}

TEST(FactorBacktestRegressionTest, ConfigurableSentimentFactorIgnoresLegacySentimentMetric)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "sentiment",
        "calculation": {
            "sentimentMetric": "市场情绪"
        }
    })JSON"));

    EXPECT_EQ(factor::ConfigurableFactorTestAccess::normalizedType(factor), QStringLiteral("sentiment"));
    EXPECT_TRUE(factor::ConfigurableFactorTestAccess::normalizedMetric(factor).isEmpty());
}

TEST(FactorBacktestRegressionTest, ConfigurableSentimentFactorDoesNotInheritDividendDefaultMetric)
{
    factor::ConfigurableFactorBase factor;
    factor::ConfigurableFactorTestAccess::loadConfig(factor, foundation::json::JsonFacade::parse(R"JSON({
        "factorType": "sentiment",
        "calculation": {
            "sentimentSource": "social_media"
        }
    })JSON"));

    EXPECT_EQ(factor::ConfigurableFactorTestAccess::normalizedType(factor), QStringLiteral("sentiment"));
    EXPECT_TRUE(factor::ConfigurableFactorTestAccess::normalizedMetric(factor).isEmpty());
}

TEST(FactorBacktestRegressionTest, ConfigurableLiquidityFactorIgnoresLegacyLiquidityMetric)
{
    EXPECT_THROW(
        factor::ConfigurableFactorTestAccess::loadConfig(
            *std::make_shared<factor::ConfigurableFactorBase>(),
            foundation::json::JsonFacade::parse(R"JSON({
                "factorType": "liquidity",
                "calculation": {
                    "liquidityMetric": "amihud"
                }
            })JSON")),
        std::exception);
}

TEST(FactorBacktestRegressionTest, CleaningEngineCanonicalizesFieldsForCacheGeneration)
{
    factor::bridge::CleaningEngine engine;
    engine.addRule(std::make_unique<factor::bridge::CompletenessRule>());
    engine.addRule(std::make_unique<factor::bridge::AdjustedPriceRule>());

    QVariantList rawData;
    rawData.append(QVariantMap{
        {QStringLiteral("symbol"), QStringLiteral("AAA")},
        {QStringLiteral("trade_date"), QStringLiteral("2024-01-02")},
        {QStringLiteral("open"), 9.8},
        {QStringLiteral("high"), 10.2},
        {QStringLiteral("low"), 9.7},
        {QStringLiteral("close"), 10.0},
        {QStringLiteral("volume"), 1000.0},
        {QStringLiteral("turnover"), 12000.0},
        {QStringLiteral("pre_adjust_factor"), 0.75},
        {QStringLiteral("post_adjust_factor"), 1.25},
        {QStringLiteral("adj_factor"), 1.25},
        {QStringLiteral("industry_code"), QStringLiteral("SW_01")}
    });

    const QVariantList cleanedData = engine.clean(rawData);
    ASSERT_EQ(cleanedData.size(), 1);

    const QVariantMap cleanedRecord = cleanedData.first().toMap();
    EXPECT_EQ(cleanedRecord.value(QStringLiteral("trade_date")).toString(), QStringLiteral("2024-01-02"));
    EXPECT_EQ(cleanedRecord.value(QStringLiteral("turnover")).toDouble(), 12000.0);
    EXPECT_FALSE(cleanedRecord.contains(QStringLiteral("adj_factor")));
    EXPECT_EQ(cleanedRecord.value(QStringLiteral("industry_code")).toString(), QStringLiteral("SW_01"));
    EXPECT_FALSE(cleanedRecord.contains(QStringLiteral("date")));
    EXPECT_FALSE(cleanedRecord.contains(QStringLiteral("turnover_amount")));
    EXPECT_DOUBLE_EQ(cleanedRecord.value(QStringLiteral("pre_adjust_factor")).toDouble(), 0.75);
    EXPECT_TRUE(cleanedRecord.contains(QStringLiteral("pre_adjust_factor")));
    EXPECT_DOUBLE_EQ(cleanedRecord.value(QStringLiteral("post_adjust_factor")).toDouble(), 1.25);
    EXPECT_TRUE(cleanedRecord.contains(QStringLiteral("post_adjust_factor")));
    EXPECT_FALSE(cleanedRecord.contains(QStringLiteral("adjusted_price_applied")));
    EXPECT_FALSE(cleanedRecord.contains(QStringLiteral("industry")));
}

TEST(FactorBacktestRegressionTest, CanonicalTradeDateRulesGenerateCacheableCleaningDataset)
{
    QStandardPaths::setTestModeEnabled(true);

    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    factor::bridge::CleaningEngine legacyEngine;
    legacyEngine.addRule(std::make_unique<factor::bridge::CompletenessRule>());

    QVariantList legacyRawData;
    legacyRawData.append(QVariantMap{
        {QStringLiteral("symbol"), QStringLiteral("AAA")},
        {QStringLiteral("open"), 9.8},
        {QStringLiteral("high"), 10.2},
        {QStringLiteral("low"), 9.7},
        {QStringLiteral("close"), 10.0},
        {QStringLiteral("volume"), 1000.0},
        {QStringLiteral("turnover"), 12000.0},
        {QStringLiteral("adj_factor"), 1.25}
    });

    const QVariantList legacyCleanedData = legacyEngine.clean(legacyRawData);
    EXPECT_TRUE(legacyCleanedData.isEmpty());

    factor::bridge::CleaningEngine canonicalEngine;
    canonicalEngine.addRule(std::make_unique<factor::bridge::CompletenessRule>());

    QVariantList rawData;
    rawData.append(QVariantMap{
        {QStringLiteral("symbol"), QStringLiteral("AAA")},
        {QStringLiteral("trade_date"), QStringLiteral("2024-01-02")},
        {QStringLiteral("open"), 9.8},
        {QStringLiteral("high"), 10.2},
        {QStringLiteral("low"), 9.7},
        {QStringLiteral("close"), 10.0},
        {QStringLiteral("volume"), 1000.0},
        {QStringLiteral("turnover"), 12000.0},
        {QStringLiteral("adj_factor"), 1.25}
    });

    const QVariantList canonicalCleanedData = canonicalEngine.clean(rawData);
    ASSERT_EQ(canonicalCleanedData.size(), 1);

    DataServiceCache::DataSetInfo info;
    info.displayName = QStringLiteral("cleaning_trade_date_regression_dataset");
    info.description = QStringLiteral("cleaning dataset generated from canonical trade_date rules");
    info.sourceType = QStringLiteral("cleaning");
    info.createdTime = QDateTime::currentDateTime();
    info.schemaVersion = 2;
    info.isBacktestReady = false;
    info.availableFields = factor::bridge::collectContractAvailableFields(canonicalCleanedData);
    info.stockCodes = QStringList{QStringLiteral("AAA")};
    info.startDate = QDate::fromString(QStringLiteral("2024-01-02"), Qt::ISODate);
    info.endDate = QDate::fromString(QStringLiteral("2024-01-02"), Qt::ISODate);
    info.tags = QStringList{QStringLiteral("cleaned"), QStringLiteral("cleaning_result")};

    const int dataSetId = cache.storeDataSet(canonicalCleanedData, info);
    ASSERT_GT(dataSetId, 0);

    const auto storedInfos = cache.getAllDataSetInfos();
    ASSERT_EQ(storedInfos.size(), 1);
    EXPECT_EQ(storedInfos.front().sourceType, QStringLiteral("cleaning"));
    EXPECT_TRUE(storedInfos.front().tags.contains(QStringLiteral("cleaning_result")));
    EXPECT_EQ(storedInfos.front().rowCount, 1);

    const QVariantList storedRows = cache.getDataSetById(dataSetId);
    ASSERT_EQ(storedRows.size(), 1);
    EXPECT_EQ(storedRows.front().toMap().value(QStringLiteral("trade_date")).toString(), QStringLiteral("2024-01-02"));

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, DataFetchControllerStoresCleaningDatasetAfterCleaningCompletes)
{
    QStandardPaths::setTestModeEnabled(true);
    ensureCoreAppAndFoundationForRegressionTests();

    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    DataFetchController controller;

    QVariantList cleanedData;
    cleanedData.append(QVariantMap{
        {QStringLiteral("symbol"), QStringLiteral("AAA")},
        {QStringLiteral("trade_date"), QStringLiteral("2024-01-02")},
        {QStringLiteral("open"), 9.8},
        {QStringLiteral("high"), 10.2},
        {QStringLiteral("low"), 9.7},
        {QStringLiteral("close"), 10.0},
        {QStringLiteral("volume"), 1000.0},
        {QStringLiteral("turnover"), 12000.0},
        {QStringLiteral("pre_adjust_factor"), 0.75},
        {QStringLiteral("post_adjust_factor"), 1.25}
    });

    bool completed = false;
    bool successSignal = false;
    QString completionMessage;
    QEventLoop loop;

    QObject::connect(&controller,
                     &DataFetchController::dataCleaningCompleted,
                     &loop,
                     [&](bool success, const QString& message, const QVariantList&) {
                         completed = true;
                         successSignal = success;
                         completionMessage = message;
                         loop.quit();
                     });

    controller.onDataCleaningCompleted(true,
                                       QStringLiteral("异步数据清洗完成: 原始 1 条 -> 清洗后 1 条"),
                                       cleanedData);

    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    ASSERT_TRUE(completed);
    EXPECT_TRUE(successSignal);
    EXPECT_TRUE(completionMessage.contains(QStringLiteral("缓存"))
                || completionMessage.contains(QStringLiteral("完成")));

    const auto storedInfos = cache.getAllDataSetInfos();
    ASSERT_EQ(storedInfos.size(), 1);
    EXPECT_EQ(storedInfos.front().sourceType, QStringLiteral("cleaning"));
    EXPECT_TRUE(storedInfos.front().tags.contains(QStringLiteral("cleaned")));
    EXPECT_TRUE(storedInfos.front().tags.contains(QStringLiteral("cleaning_result")));
    const QSet<QString> storedAvailableFields(
        storedInfos.front().availableFields.begin(),
        storedInfos.front().availableFields.end());
    EXPECT_TRUE(storedAvailableFields.contains(QStringLiteral("open")));
    EXPECT_TRUE(storedAvailableFields.contains(QStringLiteral("high")));
    EXPECT_TRUE(storedAvailableFields.contains(QStringLiteral("low")));
    EXPECT_TRUE(storedAvailableFields.contains(QStringLiteral("close")));
    EXPECT_TRUE(storedAvailableFields.contains(QStringLiteral("volume")));
    EXPECT_TRUE(storedAvailableFields.contains(QStringLiteral("turnover")));
    EXPECT_TRUE(storedAvailableFields.contains(QStringLiteral("pre_adjust_factor")));
    EXPECT_TRUE(storedAvailableFields.contains(QStringLiteral("post_adjust_factor")));
    EXPECT_FALSE(storedAvailableFields.contains(QStringLiteral("adj_factor")));

    const QVariantList storedRows = cache.getDataSetById(storedInfos.front().id);
    ASSERT_EQ(storedRows.size(), 1);
    EXPECT_EQ(storedRows.front().toMap().value(QStringLiteral("trade_date")).toString(), QStringLiteral("2024-01-02"));
    EXPECT_DOUBLE_EQ(storedRows.front().toMap().value(QStringLiteral("pre_adjust_factor")).toDouble(), 0.75);
    EXPECT_DOUBLE_EQ(storedRows.front().toMap().value(QStringLiteral("post_adjust_factor")).toDouble(), 1.25);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, DataFetchContractAvailableFieldsFollowActualCanonicalRows)
{
    const QVariantList rows{
        QVariantMap{
            {QStringLiteral("symbol"), QStringLiteral("AAA")},
            {QStringLiteral("name"), QStringLiteral("Alpha")},
            {QStringLiteral("trade_date"), QStringLiteral("2024-01-02")},
            {QStringLiteral("close"), 10.0},
            {QStringLiteral("pre_adjust_factor"), 0.75},
            {QStringLiteral("adj_factor"), 1.25},
            {QStringLiteral("industry_code"), QStringLiteral("SW_01")},
            {QStringLiteral("index_symbol"), QStringLiteral("000300.SH")},
            {QStringLiteral("index_name"), QStringLiteral("沪深300")},
            {QStringLiteral("index_snapshot_date"), QStringLiteral("2024-01-02")},
            {QStringLiteral("weight"), 0.08},
            {QStringLiteral("start_date"), QStringLiteral("2023-12-01")},
            {QStringLiteral("report_date"), QStringLiteral("2023-12-31")},
            {QStringLiteral("operating_cash_flow"), 88.0},
            {QStringLiteral("sentiment_score"), 0.61},
            {QStringLiteral("forward_filled"), true},
            {QStringLiteral("date"), QStringLiteral("2024-01-02")},
            {QStringLiteral("turnover_amount"), 12000.0},
            {QStringLiteral("post_adjust_factor"), 1.25},
            {QStringLiteral("industry"), QStringLiteral("legacy")},
            {QStringLiteral("unexpected_debug_key"), QStringLiteral("noise")}
        }
    };

    const QStringList dailyOnlyFields = factor::bridge::collectContractAvailableFields(
        rows,
        QStringList{QStringLiteral("kline_daily")});
    const QSet<QString> dailyOnlyFieldSet(dailyOnlyFields.begin(), dailyOnlyFields.end());

    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("symbol")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("name")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("trade_date")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("close")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("pre_adjust_factor")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("post_adjust_factor")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("industry_code")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("index_symbol")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("index_name")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("index_snapshot_date")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("weight")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("start_date")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("forward_filled")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("report_date")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("operating_cash_flow")));
    EXPECT_TRUE(dailyOnlyFieldSet.contains(QStringLiteral("sentiment_score")));

    EXPECT_FALSE(dailyOnlyFieldSet.contains(QStringLiteral("date")));
    EXPECT_FALSE(dailyOnlyFieldSet.contains(QStringLiteral("turnover_amount")));
    EXPECT_FALSE(dailyOnlyFieldSet.contains(QStringLiteral("adj_factor")));
    EXPECT_FALSE(dailyOnlyFieldSet.contains(QStringLiteral("industry")));
    EXPECT_FALSE(dailyOnlyFieldSet.contains(QStringLiteral("unexpected_debug_key")));

    const QStringList dailyAndFinancialFields = factor::bridge::collectContractAvailableFields(
        rows,
        QStringList{QStringLiteral("kline_daily"), QStringLiteral("financial")});
    const QSet<QString> dailyAndFinancialFieldSet(dailyAndFinancialFields.begin(), dailyAndFinancialFields.end());

    EXPECT_TRUE(dailyAndFinancialFieldSet.contains(QStringLiteral("close")));
    EXPECT_TRUE(dailyAndFinancialFieldSet.contains(QStringLiteral("report_date")));
    EXPECT_TRUE(dailyAndFinancialFieldSet.contains(QStringLiteral("operating_cash_flow")));
    EXPECT_TRUE(dailyAndFinancialFieldSet.contains(QStringLiteral("sentiment_score")));
}

TEST(FactorBacktestRegressionTest, DataFetchContractAvailableFieldsInferMarketBarFromRows)
{
    const QVariantList rows{
        QVariantMap{
            {QStringLiteral("symbol"), QStringLiteral("AAA")},
            {QStringLiteral("trade_date"), QStringLiteral("2024-01-02")},
            {QStringLiteral("open"), 9.8},
            {QStringLiteral("high"), 10.2},
            {QStringLiteral("low"), 9.7},
            {QStringLiteral("close"), 10.0},
            {QStringLiteral("volume"), 1000.0},
            {QStringLiteral("turnover"), 12000.0},
            {QStringLiteral("adj_factor"), 1.25},
            {QStringLiteral("name"), QStringLiteral("Alpha")}
        }
    };

    const QStringList inferredTypes = factor::bridge::resolveSelectedDataTypes({}, rows);
    const QSet<QString> inferredTypeSet(inferredTypes.begin(), inferredTypes.end());
    EXPECT_TRUE(inferredTypeSet.contains(QStringLiteral("market_bar")));

    const QStringList availableFields = factor::bridge::collectContractAvailableFields(rows);
    const QSet<QString> availableFieldSet(availableFields.begin(), availableFields.end());
    EXPECT_TRUE(availableFieldSet.contains(QStringLiteral("open")));
    EXPECT_TRUE(availableFieldSet.contains(QStringLiteral("close")));
    EXPECT_TRUE(availableFieldSet.contains(QStringLiteral("volume")));
    EXPECT_FALSE(availableFieldSet.contains(QStringLiteral("pre_adjust_factor")));
    EXPECT_FALSE(availableFieldSet.contains(QStringLiteral("post_adjust_factor")));
    EXPECT_TRUE(availableFieldSet.contains(QStringLiteral("name")));
}

TEST(FactorBacktestRegressionTest, DataServiceCacheStoreDataPopulatesMetadataFromContent)
{
    QStandardPaths::setTestModeEnabled(true);
    ensureCoreAppAndFoundationForRegressionTests();

    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QVariantList rows{
        QVariantMap{
            {QStringLiteral("symbol"), QStringLiteral("AAA")},
            {QStringLiteral("trade_date"), QStringLiteral("2024-01-02")},
            {QStringLiteral("open"), 9.8},
            {QStringLiteral("high"), 10.2},
            {QStringLiteral("low"), 9.7},
            {QStringLiteral("close"), 10.0},
            {QStringLiteral("volume"), 1000.0},
            {QStringLiteral("turnover"), 12000.0},
            {QStringLiteral("adj_factor"), 1.25},
            {QStringLiteral("pre_adjust_factor"), 0.75},
            {QStringLiteral("post_adjust_factor"), 1.25},
            {QStringLiteral("created_at"), QStringLiteral("2024-01-02T09:30:00")},
            {QStringLiteral("dataType"), QStringLiteral("daily")}
        }
    };

    cache.storeData(QStringLiteral("batch_preview:index:000905.SH:all_types:2023-01-02:2026-05-06"), rows);

    const auto infos = cache.getAllDataSetInfos();
    ASSERT_EQ(infos.size(), 1);
    const auto& info = infos.front();
    const QSet<QString> fieldSet(info.availableFields.begin(), info.availableFields.end());
    EXPECT_TRUE(fieldSet.contains(QStringLiteral("symbol")));
    EXPECT_TRUE(fieldSet.contains(QStringLiteral("trade_date")));
    EXPECT_TRUE(fieldSet.contains(QStringLiteral("open")));
    EXPECT_TRUE(fieldSet.contains(QStringLiteral("close")));
    EXPECT_FALSE(fieldSet.contains(QStringLiteral("adj_factor")));
    EXPECT_TRUE(fieldSet.contains(QStringLiteral("pre_adjust_factor")));
    EXPECT_TRUE(fieldSet.contains(QStringLiteral("post_adjust_factor")));
    EXPECT_TRUE(fieldSet.contains(QStringLiteral("created_at")));
    EXPECT_TRUE(info.tags.contains(QStringLiteral("selected_data_type_market_bar")));
    EXPECT_EQ(info.schemaVersion, 2);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, DataServiceCacheClearDataAndCleaningCachesSyncDatasetMetadata)
{
    QStandardPaths::setTestModeEnabled(true);
    ensureCoreAppAndFoundationForRegressionTests();

    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QVariantList rawRows{
        QVariantMap{
            {QStringLiteral("symbol"), QStringLiteral("AAA")},
            {QStringLiteral("trade_date"), QStringLiteral("2024-01-02")},
            {QStringLiteral("open"), 9.8},
            {QStringLiteral("high"), 10.2},
            {QStringLiteral("low"), 9.7},
            {QStringLiteral("close"), 10.0},
            {QStringLiteral("volume"), 1000.0},
            {QStringLiteral("turnover"), 12000.0},
            {QStringLiteral("adj_factor"), 1.25},
            {QStringLiteral("created_at"), QStringLiteral("2024-01-02T09:30:00")},
            {QStringLiteral("dataType"), QStringLiteral("daily")}
        }
    };

    cache.storeData(QStringLiteral("batch_preview:index:000905.SH:all_types:2023-01-02:2026-05-06"), rawRows);

    DataServiceCache::DataSetInfo cleaningInfo;
    cleaningInfo.displayName = QStringLiteral("cleaning_trade_date_sync_dataset");
    cleaningInfo.description = QStringLiteral("cleaning dataset generated for cache deletion regression");
    cleaningInfo.sourceType = QStringLiteral("cleaning");
    cleaningInfo.createdTime = QDateTime::currentDateTime();
    cleaningInfo.schemaVersion = 2;
    cleaningInfo.isBacktestReady = false;
    cleaningInfo.availableFields = factor::bridge::collectContractAvailableFields(rawRows, QStringList{QStringLiteral("kline_daily")});
    cleaningInfo.stockCodes = QStringList{QStringLiteral("AAA")};
    cleaningInfo.startDate = QDate::fromString(QStringLiteral("2024-01-02"), Qt::ISODate);
    cleaningInfo.endDate = QDate::fromString(QStringLiteral("2024-01-02"), Qt::ISODate);
    cleaningInfo.tags = QStringList{QStringLiteral("cleaned"), QStringLiteral("cleaning_result")};

    const int cleaningDataSetId = cache.storeDataSet(rawRows, cleaningInfo);
    ASSERT_GT(cleaningDataSetId, 0);

    const auto infosBefore = cache.getAllDataSetInfos();
    ASSERT_EQ(infosBefore.size(), 2);

    cache.clearDataCache();

    const auto infosAfterDataClear = cache.getAllDataSetInfos();
    ASSERT_EQ(infosAfterDataClear.size(), 1);
    EXPECT_EQ(infosAfterDataClear.front().sourceType, QStringLiteral("cleaning"));
    EXPECT_TRUE(infosAfterDataClear.front().tags.contains(QStringLiteral("cleaning_result")));

    cache.clearCleaningCache();

    EXPECT_TRUE(cache.getAllDataSetInfos().isEmpty());

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, DataFetchContractFieldSetsCoverRequirementInferenceFields)
{
    EXPECT_TRUE(factor::bridge::financialFields().contains(QStringLiteral("dividend_yield")));

    EXPECT_TRUE(factor::bridge::financialFields().contains(QStringLiteral("bps")));
    EXPECT_TRUE(factor::bridge::financialFields().contains(QStringLiteral("gross_margin")));
    EXPECT_TRUE(factor::bridge::financialFields().contains(QStringLiteral("operating_margin")));
    EXPECT_TRUE(factor::bridge::financialFields().contains(QStringLiteral("total_liabilities")));
    EXPECT_TRUE(factor::bridge::financialFields().contains(QStringLiteral("debt_to_equity")));
    EXPECT_TRUE(factor::bridge::financialFields().contains(QStringLiteral("current_ratio")));
    EXPECT_TRUE(factor::bridge::financialFields().contains(QStringLiteral("quick_ratio")));
    EXPECT_TRUE(factor::bridge::financialFields().contains(QStringLiteral("investing_cash_flow")));
    EXPECT_TRUE(factor::bridge::financialFields().contains(QStringLiteral("financing_cash_flow")));

    EXPECT_TRUE(factor::bridge::newsFields().contains(QStringLiteral("sector_sentiment")));
    EXPECT_TRUE(factor::bridge::newsFields().contains(QStringLiteral("theme_sentiment")));
    EXPECT_TRUE(factor::bridge::newsFields().contains(QStringLiteral("news_count")));

    EXPECT_TRUE(factor::bridge::policyFields().contains(QStringLiteral("policy_strength")));
    EXPECT_TRUE(factor::bridge::policyFields().contains(QStringLiteral("policy_count")));

    EXPECT_TRUE(factor::bridge::alternativeFields().contains(QStringLiteral("popularity_score")));
    EXPECT_TRUE(factor::bridge::alternativeFields().contains(QStringLiteral("comment_count")));
    EXPECT_TRUE(factor::bridge::alternativeFields().contains(QStringLiteral("comment_sentiment")));

    EXPECT_TRUE(factor::bridge::derivativesFields().contains(QStringLiteral("futures_close")));
    EXPECT_TRUE(factor::bridge::derivativesFields().contains(QStringLiteral("futures_volume")));
    EXPECT_TRUE(factor::bridge::derivativesFields().contains(QStringLiteral("open_interest")));
    EXPECT_TRUE(factor::bridge::derivativesFields().contains(QStringLiteral("basis")));

    EXPECT_TRUE(factor::bridge::contextualMetadataFields().contains(QStringLiteral("name")));
    EXPECT_TRUE(factor::bridge::contextualMetadataFields().contains(QStringLiteral("index_symbol")));
    EXPECT_TRUE(factor::bridge::contextualMetadataFields().contains(QStringLiteral("index_name")));
    EXPECT_TRUE(factor::bridge::contextualMetadataFields().contains(QStringLiteral("index_snapshot_date")));
    EXPECT_TRUE(factor::bridge::contextualMetadataFields().contains(QStringLiteral("weight")));
    EXPECT_TRUE(factor::bridge::contextualMetadataFields().contains(QStringLiteral("start_date")));
}

TEST(FactorBacktestRegressionTest, DataFetchFieldGroupCoversSymbolInfoAndIndexConstituents)
{
    EXPECT_EQ(
        factor::bridge::cleanedDataFieldGroupForType(
            factor::bridge::normalizeSelectedDataType(QStringLiteral("symbol_info"))),
        factor::bridge::CleanedDataFieldGroup::SymbolInfo);

    EXPECT_TRUE(factor::bridge::normalizeSelectedDataType(QStringLiteral("index")).isEmpty());

    EXPECT_EQ(
        factor::bridge::cleanedDataFieldGroupForType(
            factor::bridge::normalizeSelectedDataType(QStringLiteral("index_list"))),
        factor::bridge::CleanedDataFieldGroup::IndexList);

    EXPECT_EQ(
        factor::bridge::cleanedDataFieldGroupForType(
            factor::bridge::normalizeSelectedDataType(QStringLiteral("index_constituents"))),
        factor::bridge::CleanedDataFieldGroup::IndexConstituents);

    const QSet<QString> indexListFieldSet = factor::bridge::fieldsForSelectedDataType(QStringLiteral("index_list"));
    EXPECT_TRUE(indexListFieldSet.isEmpty());

    const QSet<QString> symbolInfoFieldSet = factor::bridge::fieldsForSelectedDataType(QStringLiteral("symbol_info"));
    EXPECT_TRUE(symbolInfoFieldSet.contains(QStringLiteral("name")));
    EXPECT_TRUE(symbolInfoFieldSet.contains(QStringLiteral("exchange")));
    EXPECT_FALSE(symbolInfoFieldSet.contains(QStringLiteral("close")));

    const QSet<QString> indexConstituentFieldSet = factor::bridge::fieldsForSelectedDataType(QStringLiteral("index_constituents"));
    EXPECT_TRUE(indexConstituentFieldSet.contains(QStringLiteral("index_symbol")));
    EXPECT_TRUE(indexConstituentFieldSet.contains(QStringLiteral("index_name")));
    EXPECT_TRUE(indexConstituentFieldSet.contains(QStringLiteral("weight")));
    EXPECT_FALSE(indexConstituentFieldSet.contains(QStringLiteral("adj_factor")));
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapReportsMissingSupplementalFieldInCacheMode)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_sentiment_policy_missing_instance");
    const char* configJson = R"JSON({
        "factorType": "sentiment",
        "dataRequirements": {
            "required": ["policy_score"],
            "sourceTable": "policy_data"
        },
        "calculation": {
            "metric": "policy_score",
            "sentimentSource": "policy"
        },
        "boundaryRules": {
            "minDataPoints": 1
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("情绪因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"},
                            {"trade_date", "2024-01-02"},
                            {"close", 10.0}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    //controller.setDataSourceMode(QStringLiteral("cache"));
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    EXPECT_EQ(FactorBacktestControllerTestAccess::resolveInstanceId(controller, instanceId), instanceId);
    ASSERT_NE(FactorBacktestControllerTestAccess::createInstance(controller, instanceId), nullptr);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();
    SCOPED_TRACE(QString("category=%1 reason=%2")
        .arg(supportInfo.value("category").toString(), supportInfo.value("reason").toString())
        .toStdString());

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoSourceTableForTest(supportInfo), factor::SourceTable::POLICY_DATA);
    EXPECT_EQ(supportInfo.value("missingFields").toList(), QVariantList{QStringLiteral("policy_score")});

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRejectsInsufficientHistoryInCacheMode)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_technical_short_history_instance");
    const char* configJson = R"JSON({
        "factorType": "technical",
        "dataRequirements": {
            "required": ["close"]
        },
        "calculation": {
            "metric": "close"
        },
        "boundaryRules": {
            "minDataPoints": 5
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("技术因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"},
                            {"trade_date", "2024-01-02"},
                            {"close", 10.0}});
    rows.append(QVariantMap{{"symbol", "AAA"},
                            {"trade_date", "2024-01-03"},
                            {"close", 10.2}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-03"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    //controller.setDataSourceMode(QStringLiteral("cache"));
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 5);
    EXPECT_EQ(FactorBacktestControllerTestAccess::resolveInstanceId(controller, instanceId), instanceId);
    ASSERT_NE(FactorBacktestControllerTestAccess::createInstance(controller, instanceId), nullptr);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-03"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();
    SCOPED_TRACE(QString("category=%1 reason=%2")
        .arg(supportInfo.value("category").toString(), supportInfo.value("reason").toString())
        .toStdString());

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("insufficient-history"));
    EXPECT_TRUE(supportInfo.value("reason").toString().contains(QStringLiteral("低于该因子所需的 5 个交易日")));

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapUsesRuntimeTechnicalRequirementsInsteadOfStaleCloseConfig)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_technical_turnover_runtime_requirement_instance");
    const char* configJson = R"JSON({
        "factorType": "technical",
        "dataRequirements": {
            "required": ["close"]
        },
        "calculation": {
            "technicalIndicators": ["turnover_stability"],
            "turnoverStabilityMetric": "turnover_rate",
            "turnoverStabilityWindow": 250,
            "skipRecent": 90,
            "lookbackPeriod": 252,
            "technicalPriceType": "close"
        },
        "boundaryRules": {
            "minDataPoints": 14
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("技术因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"},
                            {"trade_date", "2024-01-02"},
                            {"close", 10.0}});
    rows.append(QVariantMap{{"symbol", "AAA"},
                            {"trade_date", "2024-01-03"},
                            {"close", 10.2}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-03"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 250 + 90);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-03"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();
    const QVariantList expectedFields{QStringLiteral("turnover_rate")};

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::TECHNICAL);
    EXPECT_EQ(supportInfo.value("requiredFields").toList(), expectedFields);
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRequiresNeutralizationFieldsForTechnical)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_technical_neutralization_instance");
    const char* configJson = R"JSON({
        "factorType": "technical",
        "calculation": {
            "technicalIndicators": ["rsi"],
            "rsiWindow": 3,
            "technicalPriceType": "close",
            "neutralizationEnabled": true,
            "standardization": "none"
        },
        "boundaryRules": {
            "minDataPoints": 4
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("技术因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-03"}, {"close", 10.0}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-04"}, {"close", 10.5}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-05"}, {"close", 11.0}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-08"}, {"close", 11.5}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-08"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 4);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-08"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::TECHNICAL);
    const QVariantList expectedMissingFields{QStringLiteral("industry_code"), QStringLiteral("market_cap")};
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedMissingFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRequiresNeutralizationFieldsForIndustry)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_industry_neutralization_instance");
    const char* configJson = R"JSON({
        "factorType": "industry",
        "calculation": {
            "industryMetric": "industry_momentum",
            "sectorType": "申万一级",
            "neutralizationEnabled": true,
            "standardization": "none"
        },
        "boundaryRules": {
            "minDataPoints": 1
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("行业因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-03"}, {"industry_momentum", 0.10}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("industry_momentum")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-03"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 1);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-03"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::INDUSTRY);
    const QVariantList expectedMissingFields{QStringLiteral("industry_code"), QStringLiteral("market_cap")};
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedMissingFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRequiresNeutralizationFieldsForSentiment)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_sentiment_neutralization_instance");
    const char* configJson = R"JSON({
        "factorType": "sentiment",
        "dataRequirements": {
            "required": ["policy_score"]
        },
        "calculation": {
            "metric": "policy_score",
            "sentimentSource": "policy",
            "neutralizationEnabled": true,
            "standardization": "none"
        },
        "boundaryRules": {
            "minDataPoints": 1
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("情绪因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-03"}, {"policy_score", 0.10}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("policy_score")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-03"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 1);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-03"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::SENTIMENT);
    const QVariantList expectedMissingFields{QStringLiteral("industry_code"), QStringLiteral("market_cap")};
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedMissingFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRequiresNeutralizationFieldsForCustom)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_custom_neutralization_instance");
    const char* configJson = R"JSON({
        "factorType": "custom",
        "calculation": {
            "expression": "x",
            "variables": [
                {"name": "x", "field": "basis_rate"}
            ],
            "neutralizationEnabled": true,
            "standardization": "none"
        },
        "boundaryRules": {
            "minDataPoints": 1
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("自定义因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-03"}, {"basis_rate", 0.10}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("basis_rate")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-03"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 1);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-03"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::CUSTOM);
    const QVariantList expectedMissingFields{QStringLiteral("industry_code"), QStringLiteral("market_cap")};
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedMissingFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRejectsCustomWithoutExpression)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_custom_missing_expression_instance");
    const char* configJson = R"JSON({
        "factorType": "custom",
        "calculation": {
            "variables": [
                {"name": "x", "field": "close"}
            ]
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("自定义因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-03"}, {"close", 10.0}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-08"}, {"close", 10.5}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-08"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-08"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRejectsLegacyAdjClosePriceType)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_technical_adj_close_legacy_instance");
    const char* configJson = R"JSON({
        "factorType": "technical",
        "calculation": {
            "technicalIndicators": ["rsi"],
            "rsiWindow": 3,
            "technicalPriceType": "adj_close"
        },
        "boundaryRules": {
            "minDataPoints": 4
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("技术因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-03"}, {"close", 50.0}, {"adj_factor", 2.0}, {"pre_adjust_factor", 1.0}, {"post_adjust_factor", 2.0}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-04"}, {"close", 51.5}, {"adj_factor", 2.0}, {"pre_adjust_factor", 1.0}, {"post_adjust_factor", 2.0}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-05"}, {"close", 53.0}, {"adj_factor", 2.0}, {"pre_adjust_factor", 1.0}, {"post_adjust_factor", 2.0}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-08"}, {"close", 55.0}, {"adj_factor", 2.0}, {"pre_adjust_factor", 1.0}, {"post_adjust_factor", 2.0}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close"), QStringLiteral("pre_adjust_factor"), QStringLiteral("post_adjust_factor")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-08"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 4);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-03"),
        QStringLiteral("2024-01-08"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("instance-create-failed"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::TECHNICAL);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapSupportsDividendDirectFieldInCacheMode)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_dividend_legacy_instance");
    const char* configJson = R"JSON({
        "factorType": "dividend",
        "dataRequirements": {
            "required": ["dividend_stability"]
        },
        "calculation": {
            "metric": "dividend_stability"
        },
        "boundaryRules": {
            "minDataPoints": 2
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("红利因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-02"}, {"close", 10.0}, {"dividend_stability", 0.87}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close"), QStringLiteral("dividend_stability")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    //controller.setDataSourceMode(QStringLiteral("cache"));
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();
    const QVariantList expectedFields{QStringLiteral("dividend_stability")};

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_TRUE(supportInfo.value("supported").toBool());
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::DIVIDEND);
    EXPECT_EQ(supportInfo.value("requiredFields").toList(), expectedFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRejectsDividendWhenDirectFieldMissing)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_dividend_missing_direct_instance");
    const char* configJson = R"JSON({
        "factorType": "dividend",
        "dataRequirements": {
            "required": ["dividend_stability"]
        },
        "calculation": {
            "metric": "dividend_stability"
        },
        "boundaryRules": {
            "minDataPoints": 2
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("红利因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-02"}, {"close", 10.0}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    //controller.setDataSourceMode(QStringLiteral("cache"));
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();
    const QVariantList expectedFields{QStringLiteral("dividend_stability")};

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::DIVIDEND);
    EXPECT_EQ(supportInfo.value("requiredFields").toList(), expectedFields);
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapRejectsSentimentWhenDirectFieldMissing)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_sentiment_social_legacy_instance");
    const char* configJson = R"JSON({
        "factorType": "sentiment",
        "dataRequirements": {
            "required": ["social_sentiment"],
            "sourceTable": "news_sentiment"
        },
        "calculation": {
            "metric": "social_sentiment",
            "sentimentSource": "social_media",
            "window": 5
        },
        "boundaryRules": {
            "minDataPoints": 5
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("情绪因子"), configJson);
    const auto factorInstance = makeConfiguredFactor(configJson);

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-02"}, {"close", 10.0}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-03"}, {"close", 10.2}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-04"}, {"close", 10.1}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-05"}, {"close", 10.3}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-08"}, {"close", 10.5}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-08"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    //controller.setDataSourceMode(QStringLiteral("cache"));
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 5);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-08"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();
    const QVariantList expectedFields{QStringLiteral("social_sentiment")};

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_FALSE(supportInfo.value("supported").toBool());
    EXPECT_EQ(supportInfo.value("category").toString(), QStringLiteral("missing-field"));
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::SENTIMENT);
    EXPECT_EQ(supportInfo.value("requiredFields").toList(), expectedFields);
    EXPECT_EQ(supportInfo.value("missingFields").toList(), expectedFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapSupportsLowVolatilityInCacheMode)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_lowvol_instance");
    const char* configJson = R"JSON({
        "factorType": "low_volatility",
        "dataRequirements": {
            "required": ["close"]
        },
        "calculation": {
            "window": 3
        },
        "boundaryRules": {
            "minDataPoints": 3
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("低波因子"), configJson);
    const auto factorInstance = std::make_shared<factor::LowVolFactor>();

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-02"}, {"close", 10.0}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-03"}, {"close", 10.2}});
    rows.append(QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-04"}, {"close", 10.1}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("close")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-04"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    //controller.setDataSourceMode(QStringLiteral("cache"));
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);
    FactorBacktestControllerTestAccess::setRequiredWarmupTradingDays(controller, instanceId, 3);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-04"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();
    const QVariantList expectedFields{QStringLiteral("close")};

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_TRUE(supportInfo.value("supported").toBool());
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::LOW_VOLATILITY);
    EXPECT_EQ(supportInfo.value("requiredFields").toList(), expectedFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapSupportsValueCfPMetricInCacheMode)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_value_cfp_instance");
    const char* configJson = R"JSON({
        "factorType": "value",
        "dataRequirements": {
            "required": ["market_cap", "operating_cash_flow"]
        },
        "calculation": {
            "valuationMetrics": ["cf_p"]
        },
        "boundaryRules": {
            "minDataPoints": 1
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("价值因子"), configJson);
    auto factorInstance = std::make_shared<factor::ValueFactor>();
    factor::ValueFactorTestAccess::loadConfig(*factorInstance, parseRuntimeCompatibleTestConfig(configJson));

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"},
                            {"trade_date", "2024-01-02"},
                            {"market_cap", 120.0},
                            {"operating_cash_flow", 40.0}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("market_cap"), QStringLiteral("operating_cash_flow")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    //controller.setDataSourceMode(QStringLiteral("cache"));
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();
    const QVariantList expectedFields{QStringLiteral("market_cap"), QStringLiteral("operating_cash_flow")};

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_TRUE(supportInfo.value("supported").toBool());
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::VALUE);
    EXPECT_EQ(supportInfo.value("requiredFields").toList(), expectedFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, BuildFactorSupportMapSupportsValueEpMetricAgainstCanonicalCacheField)
{
    auto& cache = DataServiceCache::getInstance();
    ASSERT_TRUE(cache.initializeCache());
    cache.clearAllCache();

    const QString instanceId = QStringLiteral("factor_value_ep_instance");
    const char* configJson = R"JSON({
        "factorType": "value",
        "dataRequirements": {
            "required": ["ep"]
        },
        "calculation": {
            "valuationMetrics": ["ep"]
        },
        "boundaryRules": {
            "minDataPoints": 1
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("价值因子"), configJson);
    auto factorInstance = std::make_shared<factor::ValueFactor>();
    factor::ValueFactorTestAccess::loadConfig(*factorInstance, parseRuntimeCompatibleTestConfig(configJson));

    QVariantList rows;
    rows.append(QVariantMap{{"symbol", "AAA"},
                            {"trade_date", "2024-01-02"},
                            {"pe_ratio", 12.5}});
    const int datasetId = storeSupportMapDataset(
        rows,
        QStringList{QStringLiteral("pe_ratio")},
        QStringList{QStringLiteral("AAA")},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    ASSERT_GT(datasetId, 0);

    FactorBacktestController controller;
    controller.setSelectedDatasetId(datasetId);
    FactorBacktestControllerTestAccess::configureSupportMapOverrides(controller, instanceInfo, factorInstance);

    const QVariantMap supportMap = controller.buildFactorSupportMap(
        QVariantList{instanceId},
        QStringLiteral("2024-01-02"),
        QStringLiteral("2024-01-02"));
    const QVariantMap supportInfo = supportMap.value(instanceId).toMap();
    const QVariantList expectedFields{QStringLiteral("pe_ratio")};

    ASSERT_FALSE(supportInfo.isEmpty());
    EXPECT_TRUE(supportInfo.value("supported").toBool());
    EXPECT_EQ(factor::bridge::test::supportInfoRuntimeTypeForTest(supportInfo), factor::FactorType::VALUE);
    EXPECT_EQ(supportInfo.value("requiredFields").toList(), expectedFields);

    cache.clearAllCache();
}

TEST(FactorBacktestRegressionTest, AddFactorRollsBackRepositoryWriteWhenDomainSyncFails)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);
    FactorServiceTestAccess::setDomainSyncOverride(*service, [](const QVariantMap&) {
        return false;
    });

    const QVariantMap factor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("质量因子展示"));

    EXPECT_TRUE(service->addFactor(factor).isEmpty());
    EXPECT_EQ(repository->saveCalls, 1);
    EXPECT_EQ(repository->removeCalls, 1);
    EXPECT_EQ(repository->removedIds.value(0), QStringLiteral("factor_quality"));
    EXPECT_FALSE(repository->exists(QStringLiteral("factor_quality")));
    EXPECT_TRUE(FactorServiceTestAccess::memoryCache(*service).isEmpty());

    const QVariantMap report = service->lastOperationReport();
    EXPECT_EQ(report.value("operation").toString(), QStringLiteral("addFactor"));
    EXPECT_EQ(report.value("factorId").toString(), QStringLiteral("factor_quality"));
    EXPECT_FALSE(report.value("success").toBool());
    EXPECT_EQ(report.value("stage").toString(), QStringLiteral("sync_domain_failed_rolled_back"));
    EXPECT_TRUE(report.value("message").toString().contains(QString::fromUtf8("已回滚 factors 表")));
    EXPECT_FALSE(service->mutationInProgress());

    const QVariantList history = service->recentOperationReports();
    ASSERT_EQ(history.size(), 1);
    EXPECT_EQ(history.at(0).toMap().value("stage").toString(), QStringLiteral("sync_domain_failed_rolled_back"));
}

TEST(FactorBacktestRegressionTest, UpdateFactorRollsBackRepositoryWriteWhenDomainSyncFails)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);
    FactorServiceTestAccess::setDomainSyncOverride(*service, [](const QVariantMap&) {
        return false;
    });

    const QVariantMap previousFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("旧显示名"));
    repository->records.insert(QStringLiteral("factor_quality"), previousFactor);

    QVariantMap updatedFactor = previousFactor;
    updatedFactor["displayName"] = QString::fromUtf8("新显示名");
    updatedFactor["description"] = QString::fromUtf8("更新后的描述");

    EXPECT_FALSE(service->updateFactor(QStringLiteral("factor_quality"), updatedFactor));
    EXPECT_EQ(repository->updateCalls, 2);
    ASSERT_EQ(repository->updateHistory.size(), 2);
    EXPECT_EQ(repository->updateHistory.at(0).value("displayName").toString(), QString::fromUtf8("新显示名"));
    EXPECT_EQ(repository->updateHistory.at(1).value("displayName").toString(), QString::fromUtf8("旧显示名"));
    EXPECT_EQ(repository->findById(QStringLiteral("factor_quality")).value("displayName").toString(),
              QString::fromUtf8("旧显示名"));
    EXPECT_TRUE(FactorServiceTestAccess::memoryCache(*service).isEmpty());
}

TEST(FactorBacktestRegressionTest, DeleteFactorKeepsRepositoryAndCacheWhenDomainDeleteFails)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);
    FactorServiceTestAccess::setDomainDeleteOverride(*service, [](const QString&) {
        return false;
    });

    const QVariantMap persistedFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("缓存中的显示名"));
    repository->records.insert(QStringLiteral("factor_quality"), persistedFactor);
    FactorServiceTestAccess::setMemoryCache(*service, QStringLiteral("factor_quality"), persistedFactor);

    EXPECT_FALSE(service->deleteFactor(QStringLiteral("factor_quality")));
    EXPECT_EQ(repository->removeCalls, 0);
    EXPECT_TRUE(repository->exists(QStringLiteral("factor_quality")));
    ASSERT_TRUE(FactorServiceTestAccess::memoryCache(*service).contains(QStringLiteral("factor_quality")));
    EXPECT_EQ(FactorServiceTestAccess::memoryCache(*service)
                  .value(QStringLiteral("factor_quality"))
                  .value("displayName")
                  .toString(),
              QString::fromUtf8("缓存中的显示名"));

    const QVariantMap report = service->lastOperationReport();
    EXPECT_EQ(report.value("operation").toString(), QStringLiteral("deleteFactor"));
    EXPECT_EQ(report.value("factorId").toString(), QStringLiteral("factor_quality"));
    EXPECT_FALSE(report.value("success").toBool());
    EXPECT_EQ(report.value("stage").toString(), QStringLiteral("delete_domain_failed"));
    EXPECT_TRUE(report.value("message").toString().contains(QString::fromUtf8("factor_instance 删除失败")));
    EXPECT_FALSE(service->mutationInProgress());
}

TEST(FactorBacktestRegressionTest, ConcurrentAddFactorMutationsAreSerialized)
{
    using namespace std::chrono_literals;

    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);

    std::promise<void> firstEnteredPromise;
    std::shared_future<void> firstEnteredFuture(firstEnteredPromise.get_future());
    std::promise<void> allowFirstFinishPromise;
    std::shared_future<void> allowFirstFinishFuture(allowFirstFinishPromise.get_future());
    std::promise<void> secondEnteredPromise;
    std::shared_future<void> secondEnteredFuture(secondEnteredPromise.get_future());
    std::atomic<int> callCount{0};

    FactorServiceTestAccess::setDomainSyncOverride(*service, [&](const QVariantMap&) {
        const int currentCall = ++callCount;
        if (currentCall == 1) {
            firstEnteredPromise.set_value();
            allowFirstFinishFuture.wait();
            return true;
        }

        secondEnteredPromise.set_value();
        return true;
    });

    const QVariantMap firstFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality_1"),
        QString::fromUtf8("质量因子一"),
        QString::fromUtf8("质量因子一"));
    const QVariantMap secondFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality_2"),
        QString::fromUtf8("质量因子二"),
        QString::fromUtf8("质量因子二"));

    auto firstTask = std::async(std::launch::async, [&]() {
        return service->addFactor(firstFactor);
    });

    ASSERT_EQ(firstEnteredFuture.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(service->mutationInProgress());

    auto secondTask = std::async(std::launch::async, [&]() {
        return service->addFactor(secondFactor);
    });

    EXPECT_EQ(secondEnteredFuture.wait_for(50ms), std::future_status::timeout);

    allowFirstFinishPromise.set_value();

    EXPECT_EQ(firstTask.get(), QStringLiteral("factor_quality_1"));
    ASSERT_EQ(secondEnteredFuture.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(secondTask.get(), QStringLiteral("factor_quality_2"));
    EXPECT_FALSE(service->mutationInProgress());
    EXPECT_EQ(repository->saveCalls, 2);

    const QVariantList history = service->recentOperationReports();
    ASSERT_EQ(history.size(), 2);
    EXPECT_EQ(history.at(0).toMap().value("factorId").toString(), QStringLiteral("factor_quality_2"));
    EXPECT_EQ(history.at(1).toMap().value("factorId").toString(), QStringLiteral("factor_quality_1"));
}

TEST(FactorBacktestRegressionTest, GetAllFactorsRefreshesMemoryCacheFromRepository)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);

    QVariantMap firstFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("质量展示"));
    QVariantMap secondFactor = makeValidFactorRecord(
        QStringLiteral("factor_growth"),
        QString::fromUtf8("成长因子"),
        QString::fromUtf8("成长展示"));
    repository->records.insert(QStringLiteral("factor_quality"), firstFactor);
    repository->records.insert(QStringLiteral("factor_growth"), secondFactor);

    const QVariantList factors = service->getAllFactors();

    ASSERT_EQ(factors.size(), 2);
    EXPECT_TRUE(FactorServiceTestAccess::memoryCache(*service).contains(QStringLiteral("factor_quality")));
    EXPECT_TRUE(FactorServiceTestAccess::memoryCache(*service).contains(QStringLiteral("factor_growth")));
}

TEST(FactorBacktestRegressionTest, SearchFactorsFiltersByKeyword)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);

    QVariantMap qualityFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("高质量展示"));
    qualityFactor["description"] = QString::fromUtf8("用于质量筛选");

    QVariantMap growthFactor = makeValidFactorRecord(
        QStringLiteral("factor_growth"),
        QString::fromUtf8("成长因子"),
        QString::fromUtf8("成长展示"));
    growthFactor["description"] = QString::fromUtf8("用于成长筛选");

    repository->records.insert(QStringLiteral("factor_quality"), qualityFactor);
    repository->records.insert(QStringLiteral("factor_growth"), growthFactor);

    const QVariantList matched = service->searchFactors(QString::fromUtf8("高质量"));

    ASSERT_EQ(matched.size(), 1);
    EXPECT_EQ(matched.at(0).toMap().value("factorId").toString(), QStringLiteral("factor_quality"));
}

TEST(FactorBacktestRegressionTest, FilterFactorsByCategoryMatchesMajorCategory)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);

    QVariantMap qualityFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("质量展示"));
    qualityFactor["majorCategory"] = QString::fromUtf8("质量因子");

    QVariantMap valueFactor = makeValidFactorRecord(
        QStringLiteral("factor_value"),
        QString::fromUtf8("价值因子"),
        QString::fromUtf8("价值展示"));
    valueFactor["majorCategory"] = QString::fromUtf8("价值因子");

    repository->records.insert(QStringLiteral("factor_quality"), qualityFactor);
    repository->records.insert(QStringLiteral("factor_value"), valueFactor);

    const QVariantList matched = service->filterFactorsByCategory(QString::fromUtf8("质量因子"));

    ASSERT_EQ(matched.size(), 1);
    EXPECT_EQ(matched.at(0).toMap().value("factorId").toString(), QStringLiteral("factor_quality"));
}

TEST(FactorBacktestRegressionTest, FilterFactorsByTagsMatchesTagIntersection)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);

    QVariantMap qualityFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("质量展示"));
    qualityFactor["tags"] = QStringList{QStringLiteral("quality"), QStringLiteral("profitability")};

    QVariantMap valueFactor = makeValidFactorRecord(
        QStringLiteral("factor_value"),
        QString::fromUtf8("价值因子"),
        QString::fromUtf8("价值展示"));
    valueFactor["tags"] = QStringList{QStringLiteral("value"), QStringLiteral("cheap")};

    repository->records.insert(QStringLiteral("factor_quality"), qualityFactor);
    repository->records.insert(QStringLiteral("factor_value"), valueFactor);

    const QVariantList matched = service->filterFactorsByTags(QStringList{QStringLiteral("profitability")});

    ASSERT_EQ(matched.size(), 1);
    EXPECT_EQ(matched.at(0).toMap().value("factorId").toString(), QStringLiteral("factor_quality"));
}

TEST(FactorBacktestRegressionTest, GetFactorValuesUsesUnifiedRuntimeCalculation)
{
    auto service = makeTestFactorService();
    auto instanceManager = std::make_shared<factor::FactorInstanceManager>(nullptr, nullptr);

    const QString instanceId = QStringLiteral("factor_size_runtime_instance");
    const char* configJson = R"JSON({
        "factorType": "size",
        "calculation": {
            "sizeMetric": "market_cap",
            "logTransform": false,
            "lookbackPeriod": 1,
            "laggedEnabled": false,
            "frequency": "daily",
            "standardization": "none",
            "neutralizationEnabled": false
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("规模因子"), configJson);
    auto factorInstance = std::make_shared<factor::SizeFactor>();
    factor::SizeFactorTestAccess::loadConfig(*factorInstance, parseRuntimeCompatibleTestConfig(configJson));
    factor::FactorInstanceManagerTestAccess::seedInstance(*instanceManager, instanceId, instanceInfo, factorInstance);

    FactorServiceTestAccess::setFactorInstanceManager(*service, instanceManager);
    FactorServiceTestAccess::setQueryDatabaseDataOverride(*service, [](const QString& minDate, const QString& maxDate) {
        EXPECT_EQ(minDate, QStringLiteral("1900-01-01"));
        EXPECT_EQ(maxDate, QStringLiteral("2024-01-08"));
        return QVariantList{
            QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-08"}, {"close", 10.0}, {"market_cap", 100.0}},
            QVariantMap{{"symbol", "BBB"}, {"trade_date", "2024-01-08"}, {"close", 20.0}, {"market_cap", 400.0}}
        };
    });

    const QVariantMap result = service->getFactorValues(instanceId, QStringLiteral("2024-01-08"));

    EXPECT_EQ(result.value("status").toString(), QStringLiteral("success"));
    const QVariantMap stockValues = result.value("stockValues").toMap();
    ASSERT_EQ(stockValues.size(), 2);
    EXPECT_GT(stockValues.value("AAA").toDouble(), stockValues.value("BBB").toDouble());
}

TEST(FactorBacktestRegressionTest, GetFactorValuesBatchReturnsPerDateDataMap)
{
    auto service = makeTestFactorService();
    auto instanceManager = std::make_shared<factor::FactorInstanceManager>(nullptr, nullptr);

    const QString instanceId = QStringLiteral("factor_size_runtime_batch_instance");
    const char* configJson = R"JSON({
        "factorType": "size",
        "calculation": {
            "sizeMetric": "market_cap",
            "logTransform": false,
            "lookbackPeriod": 1,
            "laggedEnabled": false,
            "frequency": "daily",
            "standardization": "none",
            "neutralizationEnabled": false
        }
    })JSON";

    const auto instanceInfo = makeFactorInstanceInfo(instanceId, QString::fromUtf8("规模因子"), configJson);
    auto factorInstance = std::make_shared<factor::SizeFactor>();
    factor::SizeFactorTestAccess::loadConfig(*factorInstance, parseRuntimeCompatibleTestConfig(configJson));
    factor::FactorInstanceManagerTestAccess::seedInstance(*instanceManager, instanceId, instanceInfo, factorInstance);

    FactorServiceTestAccess::setFactorInstanceManager(*service, instanceManager);
    FactorServiceTestAccess::setQueryDatabaseDataOverride(*service, [](const QString& minDate, const QString& maxDate) {
        EXPECT_EQ(minDate, QStringLiteral("1900-01-01"));
        EXPECT_EQ(maxDate, QStringLiteral("2024-01-09"));
        return QVariantList{
            QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-08"}, {"close", 10.0}, {"market_cap", 100.0}},
            QVariantMap{{"symbol", "BBB"}, {"trade_date", "2024-01-08"}, {"close", 20.0}, {"market_cap", 400.0}},
            QVariantMap{{"symbol", "AAA"}, {"trade_date", "2024-01-09"}, {"close", 11.0}, {"market_cap", 121.0}},
            QVariantMap{{"symbol", "BBB"}, {"trade_date", "2024-01-09"}, {"close", 21.0}, {"market_cap", 441.0}}
        };
    });

    const QVariantMap result = service->getFactorValuesBatch(
        instanceId,
        QStringList{QStringLiteral("2024-01-08"), QStringLiteral("2024-01-09")});

    EXPECT_EQ(result.value("status").toString(), QStringLiteral("success"));
    const QVariantMap data = result.value("data").toMap();
    ASSERT_EQ(data.size(), 2);
    EXPECT_TRUE(data.contains(QStringLiteral("2024-01-08")));
    EXPECT_TRUE(data.contains(QStringLiteral("2024-01-09")));
}

} // namespace
