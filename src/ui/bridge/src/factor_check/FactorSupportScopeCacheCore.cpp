#include "factor_check/FactorSupportScopeCacheCore.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace factor::bridge::check {

namespace {

constexpr int kCurrentVersion = 2;

nlohmann::json readRoot(const std::string& filePath)
{
    if (filePath.empty()) {
        return nlohmann::json::object();
    }

    std::ifstream in(filePath, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return nlohmann::json::object();
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (...) {
        return nlohmann::json::object();
    }

    if (!root.is_object()) {
        return nlohmann::json::object();
    }

    return root;
}

bool writeRoot(const std::string& filePath, const nlohmann::json& root)
{
    if (filePath.empty()) {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path target(filePath);
    const std::filesystem::path parent = target.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return false;
        }
    }

    const std::filesystem::path temp = target.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << root.dump(2);
        if (!out.good()) {
            return false;
        }
    }

    std::filesystem::rename(temp, target, ec);
    if (ec) {
        std::filesystem::remove(target, ec);
        ec.clear();
        std::filesystem::rename(temp, target, ec);
        if (ec) {
            std::filesystem::remove(temp, ec);
            return false;
        }
    }

    return true;
}

} // namespace

PersistedFactorEntryMap loadScopeEntries(const std::string& filePath,
                                         const std::string& scopeKey)
{
    PersistedFactorEntryMap result;
    if (filePath.empty() || scopeKey.empty()) {
        return result;
    }

    const nlohmann::json root = readRoot(filePath);
    if (!root.contains("scopes") || !root["scopes"].is_object()) {
        return result;
    }

    const nlohmann::json& scopes = root["scopes"];
    if (!scopes.contains(scopeKey) || !scopes[scopeKey].is_object()) {
        return result;
    }

    const nlohmann::json& scope = scopes[scopeKey];
    if (!scope.contains("factors") || !scope["factors"].is_object()) {
        return result;
    }

    const nlohmann::json& factors = scope["factors"];
    result.reserve(factors.size());

    for (auto it = factors.begin(); it != factors.end(); ++it) {
        if (!it.value().is_object()) {
            continue;
        }

        PersistedFactorEntry entry;
        entry.definitionFingerprint = it.value().value("definitionFingerprint", std::string());
        entry.checkedAt = it.value().value("checkedAt", std::string());
        if (it.value().contains("supportInfo") && it.value()["supportInfo"].is_object()) {
            entry.supportInfoJson = it.value()["supportInfo"].dump();
        }

        result.emplace(it.key(), std::move(entry));
    }

    return result;
}

bool persistScopeEntries(const std::string& filePath,
                         const std::string& scopeKey,
                         const PersistedFactorEntryMap& entries,
                         const std::string& updatedAt)
{
    if (filePath.empty() || scopeKey.empty()) {
        return false;
    }

    nlohmann::json root = readRoot(filePath);
    if (!root.is_object()) {
        root = nlohmann::json::object();
    }

    if (!root.contains("scopes") || !root["scopes"].is_object()) {
        root["scopes"] = nlohmann::json::object();
    }

    nlohmann::json factors = nlohmann::json::object();
    for (const auto& [factorId, entry] : entries) {
        nlohmann::json record = nlohmann::json::object();
        record["definitionFingerprint"] = entry.definitionFingerprint;
        record["checkedAt"] = entry.checkedAt;
        if (!entry.supportInfoJson.empty()) {
            try {
                nlohmann::json supportInfo = nlohmann::json::parse(entry.supportInfoJson);
                if (!supportInfo.is_object()) {
                    return false;
                }
                record["supportInfo"] = std::move(supportInfo);
            } catch (...) {
                return false;
            }
        } else {
            record["supportInfo"] = nlohmann::json::object();
        }
        factors[factorId] = std::move(record);
    }

    nlohmann::json scope = nlohmann::json::object();
    scope["updatedAt"] = updatedAt;
    scope["factors"] = std::move(factors);

    root["version"] = kCurrentVersion;
    root["scopes"][scopeKey] = std::move(scope);

    return writeRoot(filePath, root);
}

} // namespace factor::bridge::check
