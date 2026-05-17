#include "domain/factor/include/ConfigurableFactor.h"
#include "domain/factor/include/ConfigurableFactorDetail.h"
#include "domain/factor/include/CustomExpressionUtils.h"
#include "domain/factor/include/factor_enums.h"
#include <QDate>

#include <algorithm>
#include <limits>

namespace factor {
using namespace configurable_factor_detail;

namespace {

std::vector<double> takeLatestValues(const std::vector<double>& ascendingValues, int limit)
{
    std::vector<double> latestValues;
    if (limit <= 0 || ascendingValues.empty()) {
        return latestValues;
    }

    latestValues.reserve(static_cast<size_t>(limit));
    for (auto it = ascendingValues.rbegin(); it != ascendingValues.rend(); ++it) {
        if (!std::isfinite(*it)) {
            continue;
        }
        latestValues.push_back(*it);
        if (latestValues.size() >= static_cast<size_t>(limit)) {
            break;
        }
    }
    return latestValues;
}

} // namespace

ConfigurableFactorBase::ConfigurableFactorBase(FactorType factorType)
{
    factorType_ = factorType;
    switch (factorType) {
    case FactorType::GROWTH:
        specificParams_ = GrowthParams{};
        break;
    case FactorType::LIQUIDITY:
        specificParams_ = LiquidityParams{};
        break;
    case FactorType::TECHNICAL:
        specificParams_ = TechnicalParams{};
        break;
    case FactorType::DIVIDEND:
        specificParams_ = DividendParams{};
        break;
    case FactorType::MACRO:
        specificParams_ = MacroParams{};
        break;
    case FactorType::INDUSTRY:
        specificParams_ = IndustryParams{};
        break;
    case FactorType::SENTIMENT:
        specificParams_ = SentimentParams{};
        break;
    case FactorType::CUSTOM:
        specificParams_ = CustomParams{};
        break;
    default:
        specificParams_ = GrowthParams{};
        break;
    }
}

std::vector<CalculationResult> ConfigurableFactorBase::calculateBatch(const std::vector<CalculationContext>& contexts)
{
    if (contexts.empty()) {
        return {};
    }

    BatchComputationCache cache;
    cache.historicalView = contexts.front().historicalView;
    BatchComputationCacheScope scope(cache);
    return BaseFactor::calculateBatch(contexts);
}

FactorType ConfigurableFactorBase::configuredFactorType() const
{
    return factorType_;
}

const ConfigurableFactorBase::GrowthParams& ConfigurableFactorBase::growthParams() const
{
    return std::get<GrowthParams>(specificParams_);
}

const ConfigurableFactorBase::LiquidityParams& ConfigurableFactorBase::liquidityParams() const
{
    return std::get<LiquidityParams>(specificParams_);
}

const ConfigurableFactorBase::TechnicalParams& ConfigurableFactorBase::technicalParams() const
{
    return std::get<TechnicalParams>(specificParams_);
}

const ConfigurableFactorBase::DividendParams& ConfigurableFactorBase::dividendParams() const
{
    return std::get<DividendParams>(specificParams_);
}

const ConfigurableFactorBase::MacroParams& ConfigurableFactorBase::macroParams() const
{
    return std::get<MacroParams>(specificParams_);
}

const ConfigurableFactorBase::IndustryParams& ConfigurableFactorBase::industryParams() const
{
    return std::get<IndustryParams>(specificParams_);
}

const ConfigurableFactorBase::SentimentParams& ConfigurableFactorBase::sentimentParams() const
{
    return std::get<SentimentParams>(specificParams_);
}

const ConfigurableFactorBase::CustomParams& ConfigurableFactorBase::customParams() const
{
    return std::get<CustomParams>(specificParams_);
}

std::vector<std::string> ConfigurableFactorBase::effectiveSymbols(const CalculationContext& context) const
{
    if (!context.symbols.empty()) {
        return context.symbols;
    }
    if (context.historicalView) {
        return context.historicalView->getAvailableSymbols(context.date);
    }
    return {};
}

std::unordered_map<std::string, double> ConfigurableFactorBase::currentFieldCrossSection(
    const CalculationContext& context,
    const QString& field) const
{
    const QString normalizedField = field.trimmed().toLower();
    if (normalizedField.isEmpty()) {
        return {};
    }
    const std::string fieldName = normalizedField.toStdString();

    if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
        std::string batchKey;
        buildBatchCrossSectionKey(batchKey, context.date, normalizedField);
        const auto cacheIt = activeBatchComputationCache->crossSectionsByKey.find(batchKey);
        if (cacheIt != activeBatchComputationCache->crossSectionsByKey.end()) {
            return cacheIt->second;
        }
    }

    const std::vector<std::string> symbols = effectiveSymbols(context);
    if (!context.historicalView || !context.historicalView->hasField(fieldName)) {
        return {};
    }

    const auto batchValues = context.historicalView->getBatchCrossSections(
        context.date,
        symbols,
        {fieldName});
    std::unordered_map<std::string, double> resolvedValues;
    const auto fieldIt = batchValues.find(fieldName);
    if (fieldIt != batchValues.end()) {
        resolvedValues = fieldIt->second;
    }
    if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
        std::string batchKey;
        buildBatchCrossSectionKey(batchKey, context.date, normalizedField);
        activeBatchComputationCache->crossSectionsByKey[batchKey] = resolvedValues;
    }
    return resolvedValues;
}

std::vector<double> ConfigurableFactorBase::seriesForField(
    const CalculationContext& context,
    const std::string& symbol,
    const QString& field,
    int window) const
{
    if (window <= 0) {
        return {};
    }

    const QString trimmedField = field.trimmed();
    const std::string fieldName = trimmedField.toStdString();

    const auto seriesBySymbol = fetchBatchSeriesMap(context, field, window);
    const auto seriesIt = seriesBySymbol.find(symbol);
    if (seriesIt != seriesBySymbol.end()) {
        return seriesIt->second;
    }

    if (context.historicalView && context.historicalView->hasField(fieldName)) {
        const auto batchValues = context.historicalView->getBatchTimeSeries({symbol}, context.date, window, {fieldName});
        const auto fieldIt = batchValues.find(fieldName);
        if (fieldIt != batchValues.end()) {
            const auto symbolIt = fieldIt->second.find(symbol);
            if (symbolIt != fieldIt->second.end()) {
                return symbolIt->second;
            }
        }
    }

    return {};
}

std::unordered_map<std::string, double> ConfigurableFactorBase::latestFinancialMetric(
    const CalculationContext& context,
    const QString& field,
    const QString& date) const
{
    Q_UNUSED(date);
    return currentFieldCrossSection(context, field);
}

std::unordered_map<std::string, std::vector<double>> ConfigurableFactorBase::latestFinancialSeries(
    const CalculationContext& context,
    const QString& field,
    const QString& date,
    int limit) const
{
    std::unordered_map<std::string, std::vector<double>> result;
    if (limit <= 0) {
        return result;
    }

    const std::vector<std::string> symbols = effectiveSymbols(context);
    const QString trimmedField = field.trimmed();
    const std::string fieldName = trimmedField.toStdString();

    if (context.historicalView && context.historicalView->hasField(fieldName)) {
        const auto batchValues = context.historicalView->getBatchTimeSeries(
            symbols,
            std::string(),
            date.toStdString(),
            {fieldName});
        const auto fieldIt = batchValues.find(fieldName);
        if (fieldIt != batchValues.end()) {
            result.reserve(fieldIt->second.size());
            for (const auto& symbol : symbols) {
                const auto symbolIt = fieldIt->second.find(symbol);
                if (symbolIt == fieldIt->second.end()) {
                    continue;
                }

                auto latestValues = takeLatestValues(symbolIt->second, limit);
                if (!latestValues.empty()) {
                    result.emplace(symbol, std::move(latestValues));
                }
            }
        }
        return result;
    }

    for (const auto& symbol : symbols) {
        const auto series = seriesForField(context, symbol, field, limit);
        if (!series.empty()) {
            result[symbol] = series;
        }
    }
    return result;
}

std::unordered_map<std::string, QString> ConfigurableFactorBase::industryBySymbol(const CalculationContext& context) const
{
    std::unordered_map<std::string, QString> result;
    if (!context.historicalView || !context.historicalView->hasField(QString(factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE).toStdString())) {
        return result;
    }

    const auto values = context.historicalView->getCrossSection(
        context.date,
        QString(factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE).toStdString(),
        effectiveSymbols(context));
    for (const auto& [symbol, value] : values) {
        if (std::isfinite(value)) {
            result.emplace(symbol, QString::number(static_cast<long long>(std::llround(value))));
        }
    }
    return result;
}

const ConfigurableFactorBase::CustomVariableBinding* ConfigurableFactorBase::findCustomVariableBinding(const QString& variableName) const
{
    const QString normalized = variableName.trimmed().toLower();
    for (const auto& binding : customParams().variables) {
        if (QString::fromStdString(binding.name).trimmed().toLower() == normalized) {
            return &binding;
        }
    }
    return nullptr;
}

} // namespace factor
