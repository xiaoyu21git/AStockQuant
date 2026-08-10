#include "UiLifecycleCoordinator.h"
#include "foundation/log/logging.hpp"

namespace bridge {

UiLifecycleCoordinator::UiLifecycleCoordinator(QObject* parent)
    : QObject(parent) {}

void UiLifecycleCoordinator::activateTradingPage() {
    m_tradingPageActivated = true;
    emit tradingPageActivated();
}

void UiLifecycleCoordinator::activateStrategyLibraryPage() {
    INTERNAL_INFO_STREAM << "[Lifecycle] 激活策略库页面";
    m_strategyLibraryPageActivated = true;
    emit strategyLibraryPageActivated();
    INTERNAL_INFO_STREAM << "[Lifecycle] 激活策略库页面 完成";
}

} // namespace bridge
