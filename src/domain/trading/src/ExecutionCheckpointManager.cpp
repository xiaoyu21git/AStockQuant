#include "ExecutionCheckpointManager.h"

#include <cstring>

namespace domain::trading {

void ExecutionCheckpointManager::pauseScope(ExecutionScopeId scopeId, BatchId batchId, const std::string& reason) {
    if (scopeId.empty()) return;
    m_pausedScopes[scopeId.value] = {std::move(batchId), reason};
}

void ExecutionCheckpointManager::resumeScope(ExecutionScopeId scopeId) {
    if (scopeId.empty()) return;
    m_pausedScopes.erase(scopeId.value);
}

bool ExecutionCheckpointManager::isPaused(ExecutionScopeId scopeId) const {
    if (scopeId.empty()) return false;
    return m_pausedScopes.find(scopeId.value) != m_pausedScopes.end();
}

BatchId ExecutionCheckpointManager::pausedBatchId(ExecutionScopeId scopeId) const {
    auto it = m_pausedScopes.find(scopeId.value);
    return it != m_pausedScopes.end() ? it->second.batchId : BatchId{};
}

void ExecutionCheckpointManager::approveCheckpoint(ExecutionScopeId scopeId, BatchId batchId) {
    auto key = makeCheckpointKey(scopeId.value, batchId.value);
    m_approvedCheckpoints[key] = true;
}

bool ExecutionCheckpointManager::isCheckpointApproved(ExecutionScopeId scopeId, BatchId batchId) const {
    auto key = makeCheckpointKey(scopeId.value, batchId.value);
    auto it = m_approvedCheckpoints.find(key);
    return it != m_approvedCheckpoints.end() && it->second;
}

void ExecutionCheckpointManager::clear() {
    m_pausedScopes.clear();
    m_approvedCheckpoints.clear();
}

uint64_t ExecutionCheckpointManager::makeCheckpointKey(const std::string& scopeId, const std::string& batchId) {
    uint64_t hash = 0;
    for (char c : scopeId) hash = hash * 31 + static_cast<unsigned char>(c);
    for (char c : batchId) hash = hash * 31 + static_cast<unsigned char>(c);
    return hash;
}

} // namespace domain::trading