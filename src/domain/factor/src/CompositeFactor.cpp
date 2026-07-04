#include "domain/factor/include/CompositeFactor.h"

#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "domain/factor/include/IFactorResolver.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_set>

namespace factor {

namespace {

constexpr const char* kChildrenKey = "children";
constexpr const char* kCombineModeKey = "combineMode";
constexpr const char* kMissingPolicyKey = "missingPolicy";
constexpr const char* kMinimumCoverageRatioKey = "minimumCoverageRatio";
constexpr const char* kInstanceIdKey = "instanceId";
constexpr const char* kWeightKey = "weight";
constexpr const char* kAscendingKey = "ascending";
constexpr const char* kNormalizeModeKey = "normalizeMode";

double clampMinimumCoverageRatio(double value)
{
    return (std::max)(0.0, (std::min)(1.0, value));
}

int newStockStrictness(NewStockHandling handling)
{
    switch (handling) {
    case NewStockHandling::EXCLUDE_IF_LT_60D: return 1;
    case NewStockHandling::INCLUDE:
    default:
        return 0;
    }
}

int suspendedStrictness(SuspendedHandling handling)
{
    switch (handling) {
    case SuspendedHandling::EXCLUDE: return 2;
    case SuspendedHandling::SET_NULL: return 1;
    case SuspendedHandling::FORWARD_FILL:
    default:
        return 0;
    }
}

int delistedStrictness(DelistedHandling handling)
{
    switch (handling) {
    case DelistedHandling::EXCLUDE: return 1;
    case DelistedHandling::KEEP_UNTIL_DELIST:
    default:
        return 0;
    }
}

int outlierStrictness(OutlierHandling handling)
{
    switch (handling) {
    case OutlierHandling::EXCLUDE: return 2;
    case OutlierHandling::WINSORIZE_3SIGMA: return 1;
    case OutlierHandling::KEEP:
    default:
        return 0;
    }
}

double percentileValue(std::vector<double> values, double quantile)
{
    if (values.empty()) {
        return 0.0;
    }
    quantile = (std::max)(0.0, (std::min)(1.0, quantile));
    const double position = quantile * static_cast<double>(values.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(position));
    const size_t upper = static_cast<size_t>(std::ceil(position));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(lower), values.end());
    const double lowValue = values[lower];
    if (lower == upper) {
        return lowValue;
    }
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(upper), values.end());
    const double highValue = values[upper];
    return lowValue + (highValue - lowValue) * (position - static_cast<double>(lower));
}

void applyZScore(std::vector<double>& values, const std::vector<size_t>& validIndexes)
{
    if (validIndexes.empty()) {
        return;
    }
    double sum = 0.0;
    for (const size_t index : validIndexes) {
        sum += values[index];
    }
    const double mean = sum / static_cast<double>(validIndexes.size());
    double variance = 0.0;
    for (const size_t index : validIndexes) {
        const double delta = values[index] - mean;
        variance += delta * delta;
    }
    const double stdev = std::sqrt(variance / static_cast<double>(validIndexes.size()));
    if (stdev <= 1e-12) {
        for (const size_t index : validIndexes) {
            values[index] = 0.0;
        }
        return;
    }
    for (const size_t index : validIndexes) {
        values[index] = (values[index] - mean) / stdev;
    }
}

void applyRankLike(std::vector<double>& values,
                   const std::vector<size_t>& validIndexes,
                   bool percentile)
{
    if (validIndexes.empty()) {
        return;
    }
    std::vector<std::pair<size_t, double>> ranked;
    ranked.reserve(validIndexes.size());
    for (const size_t index : validIndexes) {
        ranked.emplace_back(index, values[index]);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        return left.second < right.second;
    });

    const double denominator = percentile
        ? static_cast<double>(ranked.size())
        : (ranked.size() > 1 ? static_cast<double>(ranked.size() - 1) : 1.0);
    for (size_t rankIndex = 0; rankIndex < ranked.size(); ++rankIndex) {
        const double numerator = percentile ? static_cast<double>(rankIndex) : static_cast<double>(rankIndex);
        values[ranked[rankIndex].first] = denominator > 0.0 ? numerator / denominator : 0.0;
    }
}

void applyWinsorizedZScore(std::vector<double>& values, const std::vector<size_t>& validIndexes)
{
    if (validIndexes.empty()) {
        return;
    }
    std::vector<double> finiteValues;
    finiteValues.reserve(validIndexes.size());
    for (const size_t index : validIndexes) {
        finiteValues.push_back(values[index]);
    }
    const double lower = percentileValue(finiteValues, 0.05);
    const double upper = percentileValue(finiteValues, 0.95);
    for (const size_t index : validIndexes) {
        values[index] = (std::max)(lower, (std::min)(upper, values[index]));
    }
    applyZScore(values, validIndexes);
}

double neutralValueForMode(CompositeNormalizeMode mode)
{
    switch (mode) {
    case CompositeNormalizeMode::Rank:
    case CompositeNormalizeMode::Percentile:
        return 0.5;
    default:
        return 0.0;
    }
}

std::vector<double> normalizeChildValues(const CalculationResult& childResult,
                                         const std::vector<std::string>& symbols,
                                         const CompositeChildSpec& spec,
                                         std::vector<uint8_t>& validMask)
{
    std::vector<double> alignedValues(symbols.size(), 0.0);
    validMask.assign(symbols.size(), 0);

    for (size_t index = 0; index < symbols.size(); ++index) {
        const auto it = childResult.values.find(symbols[index]);
        if (it == childResult.values.end() || !std::isfinite(it->second)) {
            continue;
        }
        alignedValues[index] = spec.ascending ? it->second : -it->second;
        validMask[index] = 1;
    }

    std::vector<size_t> validIndexes;
    validIndexes.reserve(symbols.size());
    for (size_t index = 0; index < validMask.size(); ++index) {
        if (validMask[index] != 0) {
            validIndexes.push_back(index);
        }
    }

    switch (spec.normalizeMode) {
    case CompositeNormalizeMode::None:
        break;
    case CompositeNormalizeMode::ZScore:
        applyZScore(alignedValues, validIndexes);
        break;
    case CompositeNormalizeMode::Rank:
        applyRankLike(alignedValues, validIndexes, false);
        break;
    case CompositeNormalizeMode::Percentile:
        applyRankLike(alignedValues, validIndexes, true);
        break;
    case CompositeNormalizeMode::WinsorizedZScore:
        applyWinsorizedZScore(alignedValues, validIndexes);
        break;
    default:
        throw std::runtime_error("normalizeMode 非法");
    }

    return alignedValues;
}

CompositeFactorParams compositeParamsFromJson(const foundation::json::JsonFacade& json)
{
    CompositeFactorParams params;
    if (!json.has(kChildrenKey)) {
        throw std::runtime_error("children 字段缺失");
    }
    const auto children = json.get(kChildrenKey);
    if (!children.isArray() || children.size() < 2) {
        throw std::runtime_error("children 必须是至少含 2 个元素的数组");
    }
    if (children.size() > 16) {
        throw std::runtime_error("children 第一版最多允许 16 个子因子");
    }

    if (!json.has(kCombineModeKey)) {
        throw std::runtime_error("combineMode 字段缺失");
    }
    params.combineMode = requireNumericEnumField<CompositeCombineMode>(
        json,
        kCombineModeKey,
        static_cast<int>(CompositeCombineMode::WeightedAverage),
        static_cast<int>(CompositeCombineMode::MinScore));

    if (!json.has(kMissingPolicyKey)) {
        throw std::runtime_error("missingPolicy 字段缺失");
    }
    params.missingPolicy = requireNumericEnumField<CompositeMissingPolicy>(
        json,
        kMissingPolicyKey,
        static_cast<int>(CompositeMissingPolicy::DropSymbol),
        static_cast<int>(CompositeMissingPolicy::RequireMinCoverage));

    if (!json.has(kMinimumCoverageRatioKey) || !json.get(kMinimumCoverageRatioKey).isNumber()) {
        throw std::runtime_error("minimumCoverageRatio 字段缺失或非法");
    }
    params.minimumCoverageRatio = json.get(kMinimumCoverageRatioKey).asDouble();
    if (!std::isfinite(params.minimumCoverageRatio) || params.minimumCoverageRatio <= 0.0 || params.minimumCoverageRatio > 1.0) {
        throw std::runtime_error("minimumCoverageRatio 必须位于 (0, 1] 区间");
    }

    std::unordered_set<std::string> seenChildIds;
    params.children.reserve(children.size());
    for (size_t index = 0; index < children.size(); ++index) {
        const auto child = children.at(index);
        if (!child.isObject()) {
            throw std::runtime_error("children 项不是对象");
        }

        CompositeChildSpec spec;
        if (!child.has(kInstanceIdKey) || !child.get(kInstanceIdKey).isString()) {
            throw std::runtime_error("child.instanceId 字段缺失或非法");
        }
        spec.instanceId = child.get(kInstanceIdKey).asString();
        if (spec.instanceId.empty()) {
            throw std::runtime_error("child.instanceId 不能为空");
        }
        if (!seenChildIds.insert(spec.instanceId).second) {
            throw std::runtime_error("child.instanceId 存在重复");
        }

        if (!child.has(kWeightKey) || !child.get(kWeightKey).isNumber()) {
            throw std::runtime_error("child.weight 字段缺失或非法");
        }
        spec.weight = child.get(kWeightKey).asDouble();
        if (!std::isfinite(spec.weight) || spec.weight <= 0.0) {
            throw std::runtime_error("child.weight 必须是正有限值");
        }

        if (!child.has(kAscendingKey) || !child.get(kAscendingKey).isBool()) {
            throw std::runtime_error("child.ascending 字段缺失或非法");
        }
        spec.ascending = child.get(kAscendingKey).asBool();

        if (!child.has(kNormalizeModeKey)) {
            throw std::runtime_error("child.normalizeMode 字段缺失");
        }
        spec.normalizeMode = requireNumericEnumValue<CompositeNormalizeMode>(
            child.get(kNormalizeModeKey),
            kNormalizeModeKey,
            static_cast<int>(CompositeNormalizeMode::None),
            static_cast<int>(CompositeNormalizeMode::WinsorizedZScore));

        params.children.push_back(spec);
    }

    return params;
}

void appendUnique(std::vector<std::string>& target, const std::vector<std::string>& source)
{
    for (const auto& value : source) {
        if (std::find(target.begin(), target.end(), value) == target.end()) {
            target.push_back(value);
        }
    }
}

} // namespace

CompositeFactor::CompositeFactor()
{
    factorType_ = FactorType::COMPOSITE;
}

std::shared_ptr<CompositeFactor> CompositeFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker,
    std::shared_ptr<IFactorResolver> resolver)
{
    if (!resolver) {
        throw std::runtime_error("组合因子缺少 IFactorResolver");
    }

    auto factor = std::make_shared<CompositeFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->resolver_ = std::move(resolver);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

CalculationResult CompositeFactor::calculate(const CalculationContext& context)
{
    std::vector<CalculationContext> contexts{context};
    std::vector<CalculationResult> results = calculateBatch(contexts);
    return results.empty() ? CalculationResult::createError("组合因子未生成结果") : results.front();
}

std::vector<CalculationResult> CompositeFactor::calculateBatch(const std::vector<CalculationContext>& contexts)
{
    const auto& children = resolveChildrenOrThrow();
    std::vector<std::vector<CalculationResult>> childResults;
    childResults.reserve(children.size());

    for (const auto& child : children) {
        std::vector<CalculationResult> results = child.factor->calculateBatch(contexts);
        if (results.size() != contexts.size()) {
            throw std::runtime_error("组合子因子返回的结果数量与上下文数量不一致");
        }
        childResults.push_back(std::move(results));
    }

    std::vector<CalculationResult> combinedResults;
    combinedResults.reserve(contexts.size());
    for (size_t contextIndex = 0; contextIndex < contexts.size(); ++contextIndex) {
        const CalculationContext& context = contexts[contextIndex];
        CalculationResult combined;
        combined.calculationId = foundation::utils::Uuid::generate_v4();
        combined.date = context.date;
        combined.dataStatus.availability = DataAvailability::AVAILABLE;
        combined.dataStatus.coverage = 1.0;
        combined.dataStatus.message = "组合因子计算完成";
        combined.metadata = foundation::json::JsonFacade::createObject();

        std::vector<std::vector<double>> normalizedScores;
        std::vector<std::vector<uint8_t>> validMasks;
        normalizedScores.reserve(children.size());
        validMasks.reserve(children.size());
        const double totalWeight = std::accumulate(params_.children.begin(),
                                                   params_.children.end(),
                                                   0.0,
                                                   [](double acc, const CompositeChildSpec& spec) {
                                                       return acc + spec.weight;
                                                   });

        for (size_t childIndex = 0; childIndex < children.size(); ++childIndex) {
            const CalculationResult& childResult = childResults[childIndex][contextIndex];
            if (!childResult.dataStatus.isValid() && childResult.values.empty()) {
                throw std::runtime_error("组合子因子执行失败: " + children[childIndex].spec.instanceId);
            }
            std::vector<uint8_t> validMask;
            normalizedScores.push_back(normalizeChildValues(childResult, context.symbols, children[childIndex].spec, validMask));
            validMasks.push_back(std::move(validMask));
        }

        for (size_t symbolIndex = 0; symbolIndex < context.symbols.size(); ++symbolIndex) {
            const std::string& symbol = context.symbols[symbolIndex];
            int validChildCount = 0;
            double validWeightSum = 0.0;
            double weightedScore = 0.0;
            double maxScore = -std::numeric_limits<double>::infinity();
            double minScore = std::numeric_limits<double>::infinity();
            double voteScore = 0.0;
            bool shouldDrop = false;

            for (size_t childIndex = 0; childIndex < children.size(); ++childIndex) {
                const auto& spec = children[childIndex].spec;
                const bool valid = validMasks[childIndex][symbolIndex] != 0;
                if (!valid) {
                    if (params_.missingPolicy == CompositeMissingPolicy::DropSymbol) {
                        shouldDrop = true;
                        break;
                    }
                    continue;
                }

                const double score = normalizedScores[childIndex][symbolIndex];
                validWeightSum += spec.weight;
                weightedScore += spec.weight * score;
                maxScore = (std::max)(maxScore, score);
                minScore = (std::min)(minScore, score);
                voteScore += score >= neutralValueForMode(spec.normalizeMode) ? spec.weight : -spec.weight;
                ++validChildCount;
            }

            if (shouldDrop || validChildCount == 0) {
                continue;
            }

            const double coverageRatio = totalWeight > 0.0 ? validWeightSum / totalWeight : 0.0;
            if (params_.missingPolicy == CompositeMissingPolicy::RequireMinCoverage
                && coverageRatio < clampMinimumCoverageRatio(params_.minimumCoverageRatio)) {
                continue;
            }

            double adjustedWeightedScore = weightedScore;
            if (params_.missingPolicy == CompositeMissingPolicy::FillNeutral) {
                for (size_t childIndex = 0; childIndex < children.size(); ++childIndex) {
                    if (validMasks[childIndex][symbolIndex] != 0) {
                        continue;
                    }
                    const auto& spec = children[childIndex].spec;
                    adjustedWeightedScore += spec.weight * neutralValueForMode(spec.normalizeMode);
                }
            }

            double compositeScore = 0.0;
            switch (params_.combineMode) {
            case CompositeCombineMode::WeightedAverage:
            case CompositeCombineMode::RankAverage:
                compositeScore = params_.missingPolicy == CompositeMissingPolicy::FillNeutral
                    ? (totalWeight > 0.0 ? adjustedWeightedScore / totalWeight : 0.0)
                    : (validWeightSum > 0.0 ? weightedScore / validWeightSum : 0.0);
                break;
            case CompositeCombineMode::WeightedSum:
                compositeScore = params_.missingPolicy == CompositeMissingPolicy::RenormalizeWeights && validWeightSum > 0.0
                    ? weightedScore / validWeightSum
                    : adjustedWeightedScore;
                break;
            case CompositeCombineMode::Vote:
                compositeScore = validWeightSum > 0.0 ? voteScore / validWeightSum : 0.0;
                break;
            case CompositeCombineMode::MaxScore:
                compositeScore = maxScore;
                break;
            case CompositeCombineMode::MinScore:
                compositeScore = minScore;
                break;
            default:
                throw std::runtime_error("combineMode 非法");
            }

            combined.values[symbol] = compositeScore;
        }

        if (combined.values.empty()) {
            combined.dataStatus.availability = DataAvailability::UNAVAILABLE;
            combined.dataStatus.coverage = 0.0;
            combined.dataStatus.message = "组合因子在当前截面没有有效结果";
            combined.metadata.set("emptyReason", json_helper::toJsonValue("组合因子在当前截面没有有效结果"));
        }

        combined.metadata.set("composite", json_helper::toJsonValue(true));
        combined.metadata.set("combineMode", json_helper::toJsonValue(static_cast<int>(params_.combineMode)));
        combined.metadata.set("missingPolicy", json_helper::toJsonValue(static_cast<int>(params_.missingPolicy)));
        combined.metadata.set("childCount", json_helper::toJsonValue(static_cast<int>(children.size())));
        combined.metadata.set("minimumCoverageRatio", json_helper::toJsonValue(params_.minimumCoverageRatio));
        combined.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(combined.values.size())));

        auto childrenJson = foundation::json::JsonFacade::createArray();
        for (const auto& child : children) {
            auto childJson = foundation::json::JsonFacade::createObject();
            childJson.set("instanceId", json_helper::toJsonValue(child.spec.instanceId));
            childJson.set("weight", json_helper::toJsonValue(child.spec.weight));
            childJson.set("ascending", json_helper::toJsonValue(child.spec.ascending));
            childJson.set("normalizeMode", json_helper::toJsonValue(static_cast<int>(child.spec.normalizeMode)));
            childrenJson.push_back(childJson);
        }
        combined.metadata.set("children", childrenJson);
        combinedResults.push_back(std::move(combined));
    }

    return combinedResults;
}

DataRequirements CompositeFactor::getDataRequirements() const
{
    return mergeDataRequirements(resolveChildrenOrThrow());
}

BoundaryRules CompositeFactor::getBoundaryRules() const
{
    return mergeBoundaryRules(resolveChildrenOrThrow());
}

int CompositeFactor::getLookbackDays() const
{
    int m = 0;
    for (const auto& c : resolveChildrenOrThrow())
        if (c.factor) m = std::max(m, c.factor->getLookbackDays());
    return m > 0 ? m : 252;
}

void CompositeFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (!config::hasCalculationConfig(config)) {
        throw std::runtime_error("组合因子 calculation 字段缺失");
    }
    params_ = compositeParamsFromJson(config::calculationConfig(config));
    children_.clear();
}

const std::vector<CompositeFactor::ChildRuntime>& CompositeFactor::resolveChildrenOrThrow() const
{
    if (!children_.empty()) {
        return children_;
    }
    if (!resolver_) {
        throw std::runtime_error("组合因子缺少 IFactorResolver");
    }

    std::vector<ChildRuntime> resolvedChildren;
    resolvedChildren.reserve(params_.children.size());
    for (const auto& spec : params_.children) {
        FactorInstanceInfo info = resolver_->getInfo(spec.instanceId);
        if (info.instanceId.empty()) {
            throw std::runtime_error("组合子因子实例不存在: " + spec.instanceId);
        }
        if (info.factorType == FactorType::COMPOSITE) {
            throw std::runtime_error("第一版不支持嵌套组合因子: " + spec.instanceId);
        }
        std::shared_ptr<BaseFactor> factor = resolver_->createIsolated(spec.instanceId);
        if (!factor) {
            throw std::runtime_error("组合子因子实例创建失败: " + spec.instanceId);
        }
        ChildRuntime childRuntime;
        childRuntime.spec = spec;
        childRuntime.info = info;
        childRuntime.factor = std::move(factor);
        resolvedChildren.push_back(std::move(childRuntime));
    }
    children_ = std::move(resolvedChildren);
    return children_;
}

DataRequirements CompositeFactor::mergeDataRequirements(const std::vector<ChildRuntime>& children)
{
    DataRequirements requirements;
    requirements.sourceTable = SourceTable::UNKNOWN;
    for (const auto& child : children) {
        const DataRequirements childRequirements = child.factor->getDataRequirements();
        appendUnique(requirements.requiredFields, childRequirements.requiredFields);
        appendUnique(requirements.optionalFields, childRequirements.optionalFields);
        appendUnique(requirements.alternativeFields, childRequirements.alternativeFields);
        if (requirements.sourceTable == SourceTable::UNKNOWN) {
            requirements.sourceTable = childRequirements.sourceTable;
        } else if (requirements.sourceTable != childRequirements.sourceTable) {
            requirements.sourceTable = SourceTable::UNKNOWN;
        }
    }
    return requirements;
}

BoundaryRules CompositeFactor::mergeBoundaryRules(const std::vector<ChildRuntime>& children)
{
    BoundaryRules rules;
    rules.minDataPoints = 0;
    rules.handleNewStock = NewStockHandling::INCLUDE;
    rules.handleSuspended = SuspendedHandling::FORWARD_FILL;
    rules.handleDelisted = DelistedHandling::KEEP_UNTIL_DELIST;
    rules.handleOutliers = OutlierHandling::KEEP;
    int newStockRank = -1;
    int suspendedRank = -1;
    int delistedRank = -1;
    int outlierRank = -1;
    for (const auto& child : children) {
        const BoundaryRules childRules = child.factor->getBoundaryRules();
        rules.minDataPoints = (std::max)(rules.minDataPoints, childRules.minDataPoints);
        const int candidateNewStockRank = newStockStrictness(childRules.handleNewStock);
        if (candidateNewStockRank > newStockRank) {
            newStockRank = candidateNewStockRank;
            rules.handleNewStock = childRules.handleNewStock;
        }
        const int candidateSuspendedRank = suspendedStrictness(childRules.handleSuspended);
        if (candidateSuspendedRank > suspendedRank) {
            suspendedRank = candidateSuspendedRank;
            rules.handleSuspended = childRules.handleSuspended;
        }
        const int candidateDelistedRank = delistedStrictness(childRules.handleDelisted);
        if (candidateDelistedRank > delistedRank) {
            delistedRank = candidateDelistedRank;
            rules.handleDelisted = childRules.handleDelisted;
        }
        const int candidateOutlierRank = outlierStrictness(childRules.handleOutliers);
        if (candidateOutlierRank > outlierRank) {
            outlierRank = candidateOutlierRank;
            rules.handleOutliers = childRules.handleOutliers;
        }
    }
    return rules;
}

} // namespace factor