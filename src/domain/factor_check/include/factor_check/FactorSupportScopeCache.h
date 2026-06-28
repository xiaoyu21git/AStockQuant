#pragma once

#include <string>
#include <unordered_map>

namespace factor::check {

struct PersistedFactorEntry final {
    std::string definitionFingerprint;
    std::string checkedAt;
    std::string supportInfoJson;
};

using PersistedFactorEntryMap = std::unordered_map<std::string, PersistedFactorEntry>;

[[nodiscard]] PersistedFactorEntryMap loadScopeEntries(const std::string& filePath,
                                                       const std::string& scopeKey);

bool persistScopeEntries(const std::string& filePath,
                         const std::string& scopeKey,
                         const PersistedFactorEntryMap& entries,
                         const std::string& updatedAt);

} // namespace factor::check
