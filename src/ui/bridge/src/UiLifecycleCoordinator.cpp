#include "UiLifecycleCoordinator.h"

namespace bridge {

UiLifecycleCoordinator::UiLifecycleCoordinator(QObject* parent)
    : QObject(parent) {}

void UiLifecycleCoordinator::activateTradingPage() {
    m_tradingPageActivated = true;
    emit tradingPageActivated();
}

void UiLifecycleCoordinator::activateStrategyLibraryPage() {
    m_strategyLibraryPageActivated = true;
    emit strategyLibraryPageActivated();
}

} // namespace bridge
