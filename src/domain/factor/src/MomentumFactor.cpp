#include "domain/factor/include/MomentumFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/HistoricalView.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"

#include <QDate>
#include <QDebug>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace factor {

QString MomentumFactor::adjustPriceTypeToString(AdjustPriceType type)
{
    switch (type) {
    case AdjustPriceType::PRE_ADJUST_FACTOR:
        return QStringLiteral("pre_adjust_factor");
    case AdjustPriceType::POST_ADJUST_FACTOR:
        return QStringLiteral("post_adjust_factor");
    default:
        return {};
    }
}

AdjustPriceType MomentumFactor::adjustPriceTypeFromString(const QString& rawType)
{
    const QString type = rawType.trimmed().toLower();
    if (type == QStringLiteral("pre_adjust_factor")) {
        return AdjustPriceType::PRE_ADJUST_FACTOR;
    }
    if (type == QStringLiteral("post_adjust_factor")) {
        return AdjustPriceType::POST_ADJUST_FACTOR;
    }
    return AdjustPriceType::UNKNOWN;
}

QString MomentumFactor::earliestMomentumSeriesDate(const QDate& anchorDate, int window, int skipRecent)
{
    const int lookbackDays = std::max(365, (window + skipRecent + 10) * 2);
    return anchorDate.addDays(-lookbackDays).toString("yyyy-MM-dd");
}

QString MomentumFactor::normalizeMomentumType(const std::string& rawType)
{
    const QString type = QString::fromStdString(rawType).trimmed().toLower();
    if ( type == QStringLiteral("simple")) {
        return QStringLiteral("simple");
    }
    if (type == QStringLiteral("exponential")) {
        return QStringLiteral("exponential");
    }
    if (  type == QStringLiteral("normalized")) {
        return QStringLiteral("normalized");
    }
    if (type == QStringLiteral("rank")) {
        return QStringLiteral("rank");
    }
    return QStringLiteral("simple");
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
                const QString momentumType = normalizeMomentumType(params_.type);
                if (momentumType == QStringLiteral("simple")) {
                    momentumValues = calculateSimpleMomentum(resolvedContext);
                } else if (momentumType == QStringLiteral("rank")) {
                    momentumValues = calculateRankMomentum(resolvedContext);
                } else if (momentumType == QStringLiteral("normalized") || momentumType == QStringLiteral("exponential")) {
                    momentumValues = calculateNormalizedMomentum(resolvedContext);
                } else {
                    momentumValues = calculateSimpleMomentum(resolvedContext);
                }

                result.values = applyBoundaryRules(momentumValues, resolvedContext);
                result.metadata.set("calculationType", json_helper::toJsonValue(momentumType.toStdString()));
            },
            [this](const CommonFactorRuntimeState&, CalculationResult& result) {
                result.values = handleOutliers(result.values);
            },
            [this, adjustField](const CommonFactorRuntimeState&, CalculationResult& result) {
                result.metadata.set("params", params_.toJson());
                if (!result.metadata.has("calculationType")) {
                    result.metadata.set("calculationType", json_helper::toJsonValue(normalizeMomentumType(params_.type).toStdString()));
                }
                result.metadata.set("window", json_helper::toJsonValue(params_.window));
                result.metadata.set("skipRecent", json_helper::toJsonValue(params_.skipRecent));
                result.metadata.set("adjustPriceType", json_helper::toJsonValue(adjustPriceTypeToString(params_.adjustPriceType).toStdString()));
                result.metadata.set("useVolume", json_helper::toJsonValue(params_.useVolume));

                if (result.values.empty()) {
                    const QString emptyReason = QString("动量因子需要至少 %1 个交易日样本（窗口 %2，跳过最近 %3 个交易日），当前区间内未找到满足条件的股票")
                        .arg(params_.window + params_.skipRecent + 1)
                        .arg(params_.window)
                        .arg(params_.skipRecent);
                    result.metadata.set("emptyReason", json_helper::toJsonValue(emptyReason.toStdString()));
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
    const QString adjustField = resolveAdjustFieldName(params_.adjustPriceType);
    if (adjustField.isEmpty()) {
        throw std::runtime_error("动量因子 adjustPriceType 配置非法");
    }
    Q_UNUSED(adjustField);
    req.requiredFields = {
        QString(factor::bridge::MarketBarFieldKeys::CLOSE).toStdString(),
        QString(factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR).toStdString(),
        QString(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR).toStdString()
    };

    if (params_.neutralizationEnabled) {
        req.requiredFields.push_back(QString(factor::bridge::SymbolInfoFieldKeys::INDUSTRY_CODE).toStdString());
        req.requiredFields.push_back(QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP).toStdString());
    }
    
    if (params_.useVolume) {
        req.optionalFields.push_back(QString(factor::bridge::MarketBarFieldKeys::VOLUME).toStdString());
    }
    
    return req;
}

BoundaryRules MomentumFactor::getBoundaryRules() const {
    BoundaryRules rules;
    rules.minDataPoints = params_.window + 1;  // 需要足够的数据点计算动量
    rules.handleNewStock = "exclude_if_lt_60d";
    rules.handleSuspended = "forward_fill";
    rules.handleDelisted = "keep_until_delist";
    rules.handleOutliers = "winsorize_3sigma";
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

    if (normalizeMomentumType(params_.type) == QStringLiteral("exponential")) {
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
    if (config.has("calculation")) {
        auto calcConfig = config.get("calculation");
        params_.fromJson(calcConfig);

        params_.type = normalizeMomentumType(params_.type).toStdString();
    }

    const int requiredMinDataPoints = std::max(1, params_.window + 1);
    if (boundaryRules_.minDataPoints < requiredMinDataPoints) {
        boundaryRules_.minDataPoints = requiredMinDataPoints;
    }
    
    dataRequirements_ = getDataRequirements();
}

} // namespace factor