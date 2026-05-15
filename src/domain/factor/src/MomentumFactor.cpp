#include "domain/factor/include/MomentumFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/HistoricalView.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"

#include <QDate>
#include <QDebug>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace factor {

namespace {

namespace momentum_json {

constexpr const char* kWindowKey = "window";
constexpr const char* kLookbackPeriodKey = "lookbackPeriod";
constexpr const char* kLaggedEnabledKey = "laggedEnabled";
constexpr const char* kFrequencyKey = "frequency";
constexpr const char* kStandardizationKey = "standardization";
constexpr const char* kNeutralizationEnabledKey = "neutralizationEnabled";
constexpr const char* kTypeKey = "type";
constexpr const char* kAdjustPriceTypeKey = "adjustPriceType";
constexpr const char* kUseVolumeKey = "useVolume";
constexpr const char* kSkipRecentKey = "skipRecent";

constexpr const char* kParamsMetadataKey = "params";
constexpr const char* kCalculationTypeMetadataKey = "calculationType";
constexpr const char* kEmptyReasonMetadataKey = "emptyReason";

void setParamMetadata(foundation::json::JsonFacade& json, const MomentumFactor::Params& params)
{
    json.set(kWindowKey, json_helper::toJsonValue(params.window));
    json.set(kLookbackPeriodKey, json_helper::toJsonValue(params.lookbackPeriod));
    json.set(kLaggedEnabledKey, json_helper::toJsonValue(params.laggedEnabled));
    json.set(kFrequencyKey, json_helper::toJsonValue(static_cast<int>(params.frequency)));
    json.set(kStandardizationKey, json_helper::toJsonValue(static_cast<int>(params.standardization)));
    json.set(kNeutralizationEnabledKey, json_helper::toJsonValue(params.neutralizationEnabled));
    json.set(kTypeKey, json_helper::toJsonValue(static_cast<int>(params.type)));
    json.set(kAdjustPriceTypeKey, json_helper::toJsonValue(static_cast<int>(params.adjustPriceType)));
    json.set(kUseVolumeKey, json_helper::toJsonValue(params.useVolume));
    json.set(kSkipRecentKey, json_helper::toJsonValue(params.skipRecent));
}

} // namespace momentum_json

MomentumFactor::Params momentumParamsFromJson(const foundation::json::JsonFacade& json)
{
    MomentumFactor::Params params;
    if (json.has(momentum_json::kWindowKey)) params.window = json.get(momentum_json::kWindowKey).asInt();
    if (json.has(momentum_json::kLookbackPeriodKey)) params.lookbackPeriod = json.get(momentum_json::kLookbackPeriodKey).asInt();
    if (json.has(momentum_json::kLaggedEnabledKey)) params.laggedEnabled = json.get(momentum_json::kLaggedEnabledKey).asBool();
    if (json.has(momentum_json::kFrequencyKey)) params.frequency = requireNumericEnumField<CommonFrequency>(json, momentum_json::kFrequencyKey, static_cast<int>(CommonFrequency::DAILY), static_cast<int>(CommonFrequency::ANNUAL));
    if (json.has(momentum_json::kStandardizationKey)) params.standardization = requireNumericEnumField<CommonStandardization>(json, momentum_json::kStandardizationKey, static_cast<int>(CommonStandardization::NONE), static_cast<int>(CommonStandardization::PERCENTILE));
    if (json.has(momentum_json::kNeutralizationEnabledKey)) params.neutralizationEnabled = json.get(momentum_json::kNeutralizationEnabledKey).asBool();
    if (json.has(momentum_json::kTypeKey)) {
        params.type = requireNumericEnumField<MomentumCalculationType>(json, momentum_json::kTypeKey, static_cast<int>(MomentumCalculationType::SIMPLE), static_cast<int>(MomentumCalculationType::EXPONENTIAL));
    }
    if (json.has(momentum_json::kAdjustPriceTypeKey)) {
        params.adjustPriceType = requireNumericEnumField<AdjustPriceType>(json, momentum_json::kAdjustPriceTypeKey, static_cast<int>(AdjustPriceType::PRE_ADJUST_FACTOR), static_cast<int>(AdjustPriceType::POST_ADJUST_FACTOR));
    }
    if (json.has(momentum_json::kUseVolumeKey)) params.useVolume = json.get(momentum_json::kUseVolumeKey).asBool();
    if (json.has(momentum_json::kSkipRecentKey)) params.skipRecent = json.get(momentum_json::kSkipRecentKey).asInt();
    return params;
}

foundation::json::JsonFacade momentumParamsToJson(const MomentumFactor::Params& params)
{
    auto json = foundation::json::JsonFacade::createObject();
    momentum_json::setParamMetadata(json, params);
    return json;
}

}

QString MomentumFactor::earliestMomentumSeriesDate(const QDate& anchorDate, int window, int skipRecent)
{
    const int lookbackDays = std::max(365, (window + skipRecent + 10) * 2);
    return anchorDate.addDays(-lookbackDays).toString("yyyy-MM-dd");
}

/// 将回测配置中的 adjustPriceType 解析为实际使用的复权因子字段名
QString MomentumFactor::resolveAdjustFieldName(factor::AdjustPriceType priceType)
{
    switch (priceType) {
    case factor::AdjustPriceType::PRE_ADJUST_FACTOR:
        return QStringLiteral("pre_adjust_factor");
    case factor::AdjustPriceType::POST_ADJUST_FACTOR:
        return QStringLiteral("post_adjust_factor");
    default:
        return {};
    }
}

double MomentumFactor::volumeConfirmationMultiplier(const std::vector<double>& volumes)
{
    if (volumes.size() < 2) {
        return 1.0;
    }

    const double latestVolume = volumes.back();
    const double historyMean = std::accumulate(volumes.begin(), volumes.end() - 1, 0.0)
        / static_cast<double>(volumes.size() - 1);
    if (latestVolume <= 0.0 || historyMean <= 1e-12) {
        return 1.0;
    }

    return std::clamp(latestVolume / historyMean, 0.5, 1.5);
}

MomentumFactor::MomentumFactor() {
    factorType_ = FactorType::MOMENTUM;
}

CalculationResult MomentumFactor::calculate(const CalculationContext& context) {
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(
            context,
            QStringLiteral("已移除动量因子运行期数据库取数路径，请由引擎提供 HistoricalView").toStdString());
    }

    const QString adjustField = resolveAdjustFieldName(params_.adjustPriceType);
    if (adjustField.isEmpty()) {
        return createHistoricalViewRuntimeError(
            context,
            QStringLiteral("动量因子 adjustPriceType 配置为空，无法确定复权因子字段").toStdString());
    }

    const CommonFactorParams commonParams{
        params_.lookbackPeriod,
        params_.laggedEnabled,
        params_.frequency,
        params_.standardization,
        params_.neutralizationEnabled};

    try {
        QStringList dateResolutionFields{QString(factor::bridge::MarketBarFieldKeys::CLOSE)};
        dateResolutionFields.append(adjustField);

        return executeWithCommonParams(
            context,
            commonParams,
            dateResolutionFields,
            [this, &context](const CommonFactorRuntimeState& runtime, CalculationResult& result) {
                if (params_.useVolume && !context.historicalView->hasField("volume")) {
                    const QString errorMessage = QStringLiteral("动量因子 HistoricalView 回测缺少 volume 字段，已禁止成交量确认数据库回退");
                    result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                    return;
                }

                CalculationContext resolvedContext = context;
                resolvedContext.date = runtime.effectiveDate.toStdString();

                std::unordered_map<std::string, double> momentumValues;
                switch (params_.type) {
                case MomentumCalculationType::SIMPLE:
                    momentumValues = calculateSimpleMomentum(resolvedContext);
                    break;
                case MomentumCalculationType::RANK:
                    momentumValues = calculateRankMomentum(resolvedContext);
                    break;
                case MomentumCalculationType::NORMALIZED:
                case MomentumCalculationType::EXPONENTIAL:
                    momentumValues = calculateNormalizedMomentum(resolvedContext);
                    break;
                case MomentumCalculationType::UNKNOWN:
                default:
                    momentumValues = calculateSimpleMomentum(resolvedContext);
                    break;
                }

                result.values = applyBoundaryRules(momentumValues, resolvedContext);
                result.metadata.set(momentum_json::kCalculationTypeMetadataKey, json_helper::toJsonValue(static_cast<int>(params_.type)));
            },
            [this](const CommonFactorRuntimeState&, CalculationResult& result) {
                result.values = handleOutliers(result.values);
            },
            [this, adjustField](const CommonFactorRuntimeState&, CalculationResult& result) {
                result.metadata.set(momentum_json::kParamsMetadataKey, momentumParamsToJson(params_));
                if (!result.metadata.has(momentum_json::kCalculationTypeMetadataKey)) {
                    result.metadata.set(momentum_json::kCalculationTypeMetadataKey, json_helper::toJsonValue(static_cast<int>(params_.type)));
                }
                result.metadata.set(momentum_json::kWindowKey, json_helper::toJsonValue(params_.window));
                result.metadata.set(momentum_json::kSkipRecentKey, json_helper::toJsonValue(params_.skipRecent));
                result.metadata.set(momentum_json::kAdjustPriceTypeKey, json_helper::toJsonValue(static_cast<int>(params_.adjustPriceType)));
                result.metadata.set(momentum_json::kUseVolumeKey, json_helper::toJsonValue(params_.useVolume));

                if (result.values.empty()) {
                    const QString emptyReason = QString("动量因子需要至少 %1 个交易日样本（窗口 %2，跳过最近 %3 个交易日），当前区间内未找到满足条件的股票")
                        .arg(params_.window + params_.skipRecent + 1)
                        .arg(params_.window)
                        .arg(params_.skipRecent);
                    result.metadata.set(momentum_json::kEmptyReasonMetadataKey, json_helper::toJsonValue(emptyReason.toStdString()));
                }
            });
    } catch (const std::exception& e) {
        CalculationResult result;
        result.calculationId = foundation::utils::Uuid::generate_v4();
        result.date = context.date;
        result.dataStatus.availability = DataAvailability::UNAVAILABLE;
        result.dataStatus.message = "计算失败: " + std::string(e.what());
        result.metadata.set("error", json_helper::toJsonValue(e.what()));
        return result;
    }
}

DataRequirements MomentumFactor::getDataRequirements() const {
    DataRequirements req;
    req.sourceTable = SourceTable::DAILY_BAR;
    const QString adjustField = resolveAdjustFieldName(params_.adjustPriceType);
    if (adjustField.isEmpty()) {
        throw std::runtime_error("动量因子 adjustPriceType 配置非法");
    }
    req.requiredFields = {
        QString(factor::bridge::MarketBarFieldKeys::CLOSE).toStdString(),
        adjustField.toStdString()
    };

    if (params_.neutralizationEnabled) {
        req.requiredFields.push_back(QString(factor::bridge::SymbolInfoFieldKeys::INDUSTRY_CODE).toStdString());
        req.requiredFields.push_back(QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP).toStdString());
    }
    
    if (params_.useVolume) {
        req.optionalFields.push_back(QString(factor::bridge::MarketBarFieldKeys::VOLUME).toStdString());
    }

    if (params_.neutralizationEnabled) {
        req.sourceTable = SourceTable::UNKNOWN;
    }
    
    return req;
}

BoundaryRules MomentumFactor::getBoundaryRules() const {
    BoundaryRules rules;
    rules.minDataPoints = params_.window + 1;  // 需要足够的数据点计算动量
    rules.handleNewStock = NewStockHandling::EXCLUDE_IF_LT_60D;
    rules.handleSuspended = SuspendedHandling::FORWARD_FILL;
    rules.handleDelisted = DelistedHandling::KEEP_UNTIL_DELIST;
    rules.handleOutliers = OutlierHandling::WINSORIZE_3SIGMA;
    return rules;
}

std::shared_ptr<MomentumFactor> MomentumFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {
    
    auto factor = std::make_shared<MomentumFactor>();
    factor->dataChecker_ = dataChecker;
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

// ============ 私有方法实现 ============

double MomentumFactor::calculateSymbolMomentum(const std::string& symbol,
                                               const CalculationContext& context) {
    auto prices = getPriceData(symbol, context);
    const double currentClose = prices.first;
    const double previousClose = prices.second;

    if (currentClose <= 0.0 || previousClose <= 0.0) {
        throw std::runtime_error("价格数据无效");
    }

    double momentum = (currentClose - previousClose) / previousClose;

    if (params_.useVolume) {
        const QDate currentDate = QDate::fromString(QString::fromStdString(context.date), "yyyy-MM-dd");
        const int requiredPoints = params_.window + params_.skipRecent + 1;
        std::vector<double> volumes;
        if (context.historicalView && context.historicalView->hasField("volume")) {
            const auto series = context.historicalView->getSeries(
                symbol,
                earliestMomentumSeriesDate(currentDate, params_.window, params_.skipRecent).toStdString(),
                currentDate.toString("yyyy-MM-dd").toStdString(),
                "volume"
            );
            for (const auto& point : series) {
                if (std::isfinite(point.value) && point.value > 0.0) {
                    volumes.push_back(point.value);
                }
            }
        }
        momentum *= volumeConfirmationMultiplier(volumes);
    }

    return momentum;
}

std::unordered_map<std::string, double> MomentumFactor::calculateSimpleMomentum(
    const CalculationContext& context) {
    
    std::unordered_map<std::string, double> momentumValues;

    std::vector<std::string> symbols = context.symbols;
    if (symbols.empty() && context.historicalView) {
        symbols = context.historicalView->getAvailableSymbols(context.date);
    }

    for (const auto& symbol : symbols) {
        try {
            momentumValues[symbol] = calculateSymbolMomentum(symbol, context);
        } catch (const std::exception&) {
            continue;
        }
    }
    
    return momentumValues;
}

std::unordered_map<std::string, double> MomentumFactor::calculateRankMomentum(
    const CalculationContext& context) {
    
    auto simpleMomentum = calculateSimpleMomentum(context);
    
    // 转换为排名（0-1）
    std::vector<double> values;
    for (const auto& [symbol, value] : simpleMomentum) {
        values.push_back(value);
    }
    
    // 排序
    std::sort(values.begin(), values.end());
    
    std::unordered_map<std::string, double> rankValues;
    if (values.empty()) {
        return rankValues;
    }

    for (const auto& [symbol, value] : simpleMomentum) {
        // 计算百分位排名
        auto it = std::lower_bound(values.begin(), values.end(), value);
        double rank = static_cast<double>(std::distance(values.begin(), it)) / values.size();
        rankValues[symbol] = rank;
    }
    
    return rankValues;
}

std::unordered_map<std::string, double> MomentumFactor::calculateNormalizedMomentum(
    const CalculationContext& context) {
    
    auto simpleMomentum = calculateSimpleMomentum(context);
    if (simpleMomentum.empty()) {
        return {};
    }

    if (params_.type == MomentumCalculationType::EXPONENTIAL) {
        for (auto& [symbol, value] : simpleMomentum) {
            value *= (1.0 + 1.0 / std::max(1, params_.window));
        }
    }
    
    // 计算均值和标准差
    std::vector<double> values;
    for (const auto& [symbol, value] : simpleMomentum) {
        values.push_back(value);
    }
    
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    double mean = sum / values.size();
    
    double sq_sum = std::inner_product(values.begin(), values.end(), 
                                      values.begin(), 0.0);
    double stdev = std::sqrt(sq_sum / values.size() - mean * mean);
    
    // 标准化
    std::unordered_map<std::string, double> normalizedValues;
    for (const auto& [symbol, value] : simpleMomentum) {
        if (stdev > 0) {
            normalizedValues[symbol] = (value - mean) / stdev;
        } else {
            normalizedValues[symbol] = 0.0;
        }
    }
    
    return normalizedValues;
}

std::pair<double, double> MomentumFactor::getPriceData(const std::string& symbol,
                                                       const CalculationContext& context) {
    const QDate currentDate = QDate::fromString(QString::fromStdString(context.date), "yyyy-MM-dd");
    if (!currentDate.isValid()) {
        throw std::runtime_error("非法计算日期");
    }

    const int requiredPoints = params_.window + params_.skipRecent + 1;
    const QString adjustField = resolveAdjustFieldName(params_.adjustPriceType);
    if (adjustField.isEmpty()) {
        throw std::runtime_error("动量因子 adjustPriceType 配置为空");
    }

    if (context.historicalView) {
        std::vector<HistoricalDataPoint> series;
        const std::string adjustFieldStd = adjustField.toStdString();
        if (!context.historicalView->hasField("close") || !context.historicalView->hasField(adjustFieldStd)) {
            throw std::runtime_error("动量因子在 " + adjustFieldStd + " 价格模式下要求 HistoricalView 同时提供 close 和 " + adjustFieldStd + " 字段");
        }

        const auto closeSeries = context.historicalView->getSeries(
            symbol,
            earliestMomentumSeriesDate(currentDate, params_.window, params_.skipRecent).toStdString(),
            currentDate.toString("yyyy-MM-dd").toStdString(),
            "close"
        );
        const auto factorSeries = context.historicalView->getSeries(
            symbol,
            earliestMomentumSeriesDate(currentDate, params_.window, params_.skipRecent).toStdString(),
            currentDate.toString("yyyy-MM-dd").toStdString(),
            adjustFieldStd
        );
        const size_t pairCount = std::min(closeSeries.size(), factorSeries.size());
        series.reserve(pairCount);
        for (size_t index = 0; index < pairCount; ++index) {
            if (closeSeries[index].date != factorSeries[index].date) {
                continue;
            }
            series.push_back(HistoricalDataPoint{closeSeries[index].date, closeSeries[index].value * factorSeries[index].value});
        }

        if (static_cast<int>(series.size()) < requiredPoints) {
            qDebug() << "MomentumFactor: 缓存序列长度不足"
                     << "symbol=" << QString::fromStdString(symbol)
                     << "date=" << QString::fromStdString(context.date)
                     << "requiredPoints=" << requiredPoints
                     << "actualPoints=" << static_cast<int>(series.size());
            throw std::runtime_error("缓存集中缺少足够的历史交易日数据");
        }

        const size_t anchorIndex = series.size() - static_cast<size_t>(params_.skipRecent) - 1;
        const size_t previousIndex = anchorIndex - static_cast<size_t>(params_.window);
        const double currentClose = series[anchorIndex].value;
        const double previousClose = series[previousIndex].value;
        return {currentClose, previousClose};
    }

    throw std::runtime_error("已移除动量因子运行期数据库取数路径");
}

void MomentumFactor::loadConfig(const foundation::json::JsonFacade& config) {
    // 调用基类加载
    BaseFactor::loadConfig(config);
    
    // 加载动量因子特定配置
    if (config::hasCalculationConfig(config)) {
        auto calcConfig = config::calculationConfig(config);
        params_ = momentumParamsFromJson(calcConfig);
    }

    const int requiredMinDataPoints = std::max(1, params_.window + 1);
    if (boundaryRules_.minDataPoints < requiredMinDataPoints) {
        boundaryRules_.minDataPoints = requiredMinDataPoints;
    }
    
    dataRequirements_ = getDataRequirements();
}

} // namespace factor