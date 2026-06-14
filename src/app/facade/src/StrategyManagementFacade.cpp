#include "StrategyManagementFacade.h"

#include "StrategyLifecycleManager.h"
#include "StrategyFactory.h"

namespace app::facade {

StrategyManagementFacade::StrategyManagementFacade() : m_lifecycleMgr(std::make_shared<domain::strategy::StrategyLifecycleManager>()) {}

std::string StrategyManagementFacade::startStrategy(const std::string& id) {
    auto result = m_lifecycleMgr->start(id);
    return result.ok ? "" : result.message;
}

std::string StrategyManagementFacade::stopStrategy(const std::string& id) {
    auto result = m_lifecycleMgr->stop(id);
    return result.ok ? "" : result.message;
}

} // namespace app::facade