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
    INTERNAL_INFO_STREAM << "[Lifecycle] activateStrategyLibraryPage";
    m_strategyLibraryPageActivated = true;
    emit strategyLibraryPageActivated();
    INTERNAL_INFO_STREAM << "[Lifecycle] activateStrategyLibraryPage DONE";
}

} // namespace bridge
