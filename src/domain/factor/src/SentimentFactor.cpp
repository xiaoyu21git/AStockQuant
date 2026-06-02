#include "domain/factor/include/SentimentFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"

namespace factor {

SentimentFactor::SentimentFactor()
    : ConfigurableFactorBase(FactorType::SENTIMENT)
{
}

CalculationResult SentimentFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(
            context,
            "已移除因子运行期数据库取数路径，请由引擎提供 HistoricalView");
    }
    return calculateSentiment(context);
}

std::shared_ptr<SentimentFactor> SentimentFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<SentimentFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

} // namespace factor