#pragma once

#include <string>
#include <unordered_map>

namespace domain::trading {

struct ExecutionScopeId {
    std::string value;
    bool empty() const { return value.empty(); }
    bool operator==(const ExecutionScopeId& o) const { return value == o.value; }
};

struct BatchId {
    std::string value;
    bool empty() const { return value.empty(); }
};

class ExecutionCheckpointManager {
public:
    void pauseScope(ExecutionScopeId scopeId, BatchId batchId, const std::string& reason);
    void resumeScope(ExecutionScopeId scopeId);
    bool isPaused(ExecutionScopeId scopeId) const;
    BatchId pausedBatchId(ExecutionScopeId scopeId) const;

    void approveCheckpoint(ExecutionScopeId scopeId, BatchId batchId);
    bool isCheckpointApproved(ExecutionScopeId scopeId, BatchId batchId) const;

    void clear();

private:
    struct PausedScope {
        BatchId batchId;
        std::string reason;
    };
    std::unordered_map<std::string, PausedScope> m_pausedScopes;

    using CheckpointKey = uint64_t;
    std::unordered_map<CheckpointKey, bool> m_approvedCheckpoints;

    static CheckpointKey makeCheckpointKey(const std::string& scopeId, const std::string& batchId);
};

} // namespace domain::trading