#include "CashManager.h"

namespace domain::trading {

CashManager::CashManager(IAccountProvider& provider)
    : m_provider(provider)
{}

double CashManager::availableCash(bool forceRefresh) {
    if (forceRefresh) {
        m_provider.refresh();
    }
    return m_provider.availableCash();
}

bool CashManager::hasReceivedData() const {
    return m_provider.hasReceivedData();
}

} // namespace domain::trading
