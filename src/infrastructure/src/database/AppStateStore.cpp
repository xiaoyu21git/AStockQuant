#include "database/AppStateStore.h"
#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace astock::infrastructure::database {

std::mutex AppStateStore::s_registryMutex;
std::unordered_map<std::string, std::weak_ptr<AppStateStore>> AppStateStore::s_stores;

std::shared_ptr<AppStateStore> AppStateStore::forPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(s_registryMutex);
    auto it = s_stores.find(path);
    if (it != s_stores.end()) {
        if (auto store = it->second.lock())
            return store;
        s_stores.erase(it);
    }
    auto store = std::shared_ptr<AppStateStore>(new AppStateStore(path));
    s_stores.emplace(path, store);
    return store;
}

AppStateStore::AppStateStore(std::string path)
    : m_path(std::move(path)) {}

bool AppStateStore::readInt(const std::string& key, std::int64_t& out) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto json = foundation::json::JsonFacade::parseFile(m_path);
    if (json.isNull() || !json.isObject() || !json.has(key)) return false;
    try { out = json.get(key).asInt(); } catch (...) { return false; }
    return true;
}

bool AppStateStore::readInt(const std::string& section, const std::string& key,
                            std::int64_t& out) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto json = foundation::json::JsonFacade::parseFile(m_path);
    if (json.isNull() || !json.isObject() || !json.has(section)) return false;
    auto sec = json.get(section);
    if (!sec.isObject() || !sec.has(key)) return false;
    try { out = sec.get(key).asInt(); } catch (...) { return false; }
    return true;
}

void AppStateStore::writeInt(const std::string& key, std::int64_t value,
                             const std::vector<std::string>& dropAliases) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 读取现有 JSON, 保留除 key 与旧键别名外的全部顶层键
    auto root = foundation::json::JsonFacade::createObject();
    {
        auto existing = foundation::json::JsonFacade::parseFile(m_path);
        if (!existing.isNull() && existing.isObject()) {
            for (const auto& k : existing.keys()) {
                if (k == key) continue;
                if (std::find(dropAliases.begin(), dropAliases.end(), k) != dropAliases.end())
                    continue;
                root.set(k, existing.get(k));
            }
        }
    }
    root.set(key, foundation::json::JsonFacade::createInt(static_cast<int>(value)));
    writeFileLocked(root);
}

void AppStateStore::writeInt(const std::string& section, const std::string& key,
                             std::int64_t value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 读取现有 JSON: 保留其余顶层键; section 内保留其他条目 (如其他策略)
    auto root = foundation::json::JsonFacade::createObject();
    auto secMap = foundation::json::JsonFacade::createObject();
    {
        auto existing = foundation::json::JsonFacade::parseFile(m_path);
        if (!existing.isNull() && existing.isObject()) {
            for (const auto& k : existing.keys()) {
                if (k == section) {
                    auto oldMap = existing.get(k);
                    if (oldMap.isObject()) {
                        for (const auto& sk : oldMap.keys())
                            secMap.set(sk, oldMap.get(sk));
                    }
                } else {
                    root.set(k, existing.get(k));
                }
            }
        }
    }
    secMap.set(key, foundation::json::JsonFacade::createInt(static_cast<int>(value)));
    root.set(section, secMap);
    writeFileLocked(root);
}

bool AppStateStore::hasKey(const std::string& section, const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto json = foundation::json::JsonFacade::parseFile(m_path);
    if (json.isNull() || !json.isObject() || !json.has(section)) return false;
    auto sec = json.get(section);
    return sec.isObject() && sec.has(key);
}

bool AppStateStore::readString(const std::string& key, std::string& out) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto json = foundation::json::JsonFacade::parseFile(m_path);
    if (json.isNull() || !json.isObject() || !json.has(key)) return false;
    try { out = json.get(key).asString(); } catch (...) { return false; }
    return true;
}

void AppStateStore::writeString(const std::string& key, const std::string& value,
                                const std::vector<std::string>& dropAliases) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 读取现有 JSON, 保留除 key 与旧键别名外的全部顶层键
    auto root = foundation::json::JsonFacade::createObject();
    {
        auto existing = foundation::json::JsonFacade::parseFile(m_path);
        if (!existing.isNull() && existing.isObject()) {
            for (const auto& k : existing.keys()) {
                if (k == key) continue;
                if (std::find(dropAliases.begin(), dropAliases.end(), k) != dropAliases.end())
                    continue;
                root.set(k, existing.get(k));
            }
        }
    }
    root.set(key, foundation::json::JsonFacade::createString(value));
    writeFileLocked(root);
}

bool AppStateStore::readString(const std::string& section, const std::string& key,
                               std::string& out) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto json = foundation::json::JsonFacade::parseFile(m_path);
    if (json.isNull() || !json.isObject() || !json.has(section)) return false;
    auto sec = json.get(section);
    if (!sec.isObject() || !sec.has(key)) return false;
    try { out = sec.get(key).asString(); } catch (...) { return false; }
    return true;
}

void AppStateStore::writeString(const std::string& section, const std::string& key,
                                const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 读取现有 JSON: 保留其余顶层键; section 内保留其他条目
    auto root = foundation::json::JsonFacade::createObject();
    auto secMap = foundation::json::JsonFacade::createObject();
    {
        auto existing = foundation::json::JsonFacade::parseFile(m_path);
        if (!existing.isNull() && existing.isObject()) {
            for (const auto& k : existing.keys()) {
                if (k == section) {
                    auto oldMap = existing.get(k);
                    if (oldMap.isObject()) {
                        for (const auto& sk : oldMap.keys())
                            secMap.set(sk, oldMap.get(sk));
                    }
                } else {
                    root.set(k, existing.get(k));
                }
            }
        }
    }
    secMap.set(key, foundation::json::JsonFacade::createString(value));
    root.set(section, secMap);
    writeFileLocked(root);
}

void AppStateStore::writeFileLocked(const foundation::json::JsonFacade& root) {
    // 原子写入: 先写临时文件, 再重命名 (Windows rename 不覆盖已有文件, 先删再 rename)
    std::string tmpPath = m_path + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::trunc);
        if (!f.is_open()) {
            INTERNAL_WARN_STREAM << "[AppState] 无法写入持久化文件: " << tmpPath;
            return;
        }
        f << root.toString() << "\n";
        f.close();
    }
    std::remove(m_path.c_str());
    if (std::rename(tmpPath.c_str(), m_path.c_str()) != 0) {
        INTERNAL_WARN_STREAM << "[AppState] 持久化文件重命名失败: " << m_path;
    }
}

} // namespace astock::infrastructure::database
