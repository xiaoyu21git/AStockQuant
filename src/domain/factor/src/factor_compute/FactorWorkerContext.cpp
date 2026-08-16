#include "factor_compute/FactorWorkerContext.h"
#include "factor_compute/ArrowMarketDataView.h"
#include "FactorInstanceManager.h"

namespace factor::compute {

FactorWorkerContext::FactorWorkerContext(factor::FactorInstanceManager& instanceManager,
                                         const ArrowMarketDataView& arrowView)
    : m_instanceManager(instanceManager)
{
    // 独立读句柄: 复用与主视图构造相同的打开实现 (createReaderHandle 静态工厂)
    m_handle = ArrowMarketDataView::createReaderHandle(arrowView.arrowPath());
}

const ArrowReaderHandle& FactorWorkerContext::readerHandle() const
{
    return *m_handle;
}

std::shared_ptr<factor::BaseFactor> FactorWorkerContext::factor(const std::string& factorId)
{
    auto it = m_factors.find(factorId);
    if (it != m_factors.end()) return it->second;

    // createIsolatedInstance: 不进入全局实例缓存, 与 createInstance 同一构造路径 —
    // 回测管线从不调 EventDrivenFactor::loadEventsFromDb (唯一调用点 RuntimeFactorSvc
    // 实盘路径), 隔离实例与共享实例在回测路径行为一致, 无需补事件注入
    auto isolated = m_instanceManager.createIsolatedInstance(factorId);
    if (isolated) m_factors[factorId] = isolated;
    return isolated;
}

} // namespace factor::compute
