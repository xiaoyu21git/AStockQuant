#include "domain/factor/include/DividendFactor.h"
#include "domain/factor/include/FactorInstanceManager.h"

namespace factor {

DividendFactor::DividendFactor()
    : ConfigurableFactorBase(FactorType::DIVIDEND)
{
}

CalculationResult DividendFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(
            context,
            QStringLiteral("已移除因子运行期数据库取数路径，请由引擎提供 HistoricalView").toStdString());
    }
    return calculateDividend(context);
}

std::shared_ptr<DividendFactor> DividendFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<DividendFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

} // namespace factor