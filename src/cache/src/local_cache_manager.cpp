// local_cache_manager.cpp
// Local cache manager implementation

#include "local_cache_manager.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>

namespace AStockQuantEngine {
namespace Cache {

class LocalCacheManager::Impl {
public:
    struct CacheEntry {
        std::string value;
        std::chrono::steady_clock::time_point writeTime;
        std::chrono::steady_clock::time_point lastAccessTime;
        std::chrono::seconds ttl{0};
    };

    explicit Impl(const LocalCacheConfig& config) : config_(config) {}
    
    bool initialize() {
        std::cout << "LocalCacheManager: initialized" << std::endl;
        initialized_ = true;
        return true;
    }
    
    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
        initialized_ = false;
        std::cout << "LocalCacheManager: shutdown" << std::endl;
    }
    
    bool get(const std::string& key, std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!config_.enabled || !initialized_) {
            return false;
        }

        cleanupExpiredLocked();

        auto it = entries_.find(key);
        if (it == entries_.end()) {
            stats_.misses++;
            return false;
        }

        it->second.lastAccessTime = std::chrono::steady_clock::now();
        value = it->second.value;
        stats_.hits++;
        return true;
    }
    
    bool set(const std::string& key, const std::string& value, 
             std::chrono::seconds ttl) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!config_.enabled || !initialized_) {
            return false;
        }

        cleanupExpiredLocked();

        const auto now = std::chrono::steady_clock::now();
        CacheEntry entry;
        entry.value = value;
        entry.writeTime = now;
        entry.lastAccessTime = now;
        entry.ttl = ttl.count() > 0 ? ttl : config_.expireAfterWrite;

        entries_[key] = std::move(entry);
        stats_.puts++;
        stats_.estimatedSize = entries_.size();
        stats_.memoryUsage = estimateMemoryUsageLocked();

        enforceCapacityLocked();
        return true;
    }
    
    bool remove(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto removed = entries_.erase(key);
        if (removed > 0) {
            stats_.removes += removed;
            stats_.estimatedSize = entries_.size();
            stats_.memoryUsage = estimateMemoryUsageLocked();
            return true;
        }
        return false;
    }
    
    bool exists(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        cleanupExpiredLocked();
        return entries_.find(key) != entries_.end();
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
        stats_.estimatedSize = 0;
        stats_.memoryUsage = 0;
    }
    
    std::map<std::string, std::string> getBulk(const std::vector<std::string>& keys) {
        std::map<std::string, std::string> result;
        std::lock_guard<std::mutex> lock(mutex_);
        cleanupExpiredLocked();
        for (const auto& key : keys) {
            auto it = entries_.find(key);
            if (it != entries_.end()) {
                it->second.lastAccessTime = std::chrono::steady_clock::now();
                result[key] = it->second.value;
                stats_.hits++;
            } else {
                stats_.misses++;
            }
        }
        return result;
    }
    
    void setBulk(const std::map<std::string, std::string>& keyValues,
                 std::chrono::seconds ttl) {
        for (const auto& [key, value] : keyValues) {
            set(key, value, ttl);
        }
    }
    
    CacheStats getStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        CacheStats snapshot = stats_;
        snapshot.estimatedSize = entries_.size();
        snapshot.memoryUsage = estimateMemoryUsageLocked();
        return snapshot;
    }
    
    void resetStats() {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_ = CacheStats{};
        stats_.estimatedSize = entries_.size();
        stats_.memoryUsage = estimateMemoryUsageLocked();
    }
    
    void setPolicy(const std::string& keyPrefix, CachePolicy policy) {
        std::lock_guard<std::mutex> lock(mutex_);
        policies_[keyPrefix] = policy;
    }
    
    CachePolicy getPolicy(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        CachePolicy policy;
        policy.ttl = config_.expireAfterWrite;
        policy.useLocalCache = true;
        policy.useRedisCache = false;

        size_t bestMatchLength = 0;
        for (const auto& [prefix, candidate] : policies_) {
            if (key.rfind(prefix, 0) == 0 && prefix.size() >= bestMatchLength) {
                policy = candidate;
                bestMatchLength = prefix.size();
            }
        }

        return policy;
    }
    
    bool isEnabled() const {
        return config_.enabled && initialized_;
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

    size_t estimatedSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        cleanupExpiredLocked();
    }
    
    const LocalCacheConfig& getConfig() const { return config_; }
    
private:
    bool isExpired(const CacheEntry& entry, std::chrono::steady_clock::time_point now) const {
        if (entry.ttl.count() > 0 && now - entry.writeTime >= entry.ttl) {
            return true;
        }

        if (config_.expireAfterAccess.count() > 0 &&
            now - entry.lastAccessTime >= config_.expireAfterAccess) {
            return true;
        }

        return false;
    }

    void cleanupExpiredLocked() const {
        const auto now = std::chrono::steady_clock::now();
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (isExpired(it->second, now)) {
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void enforceCapacityLocked() {
        while (entries_.size() > config_.maxSize && !entries_.empty()) {
            auto victim = std::min_element(
                entries_.begin(), entries_.end(),
                [](const auto& left, const auto& right) {
                    return left.second.lastAccessTime < right.second.lastAccessTime;
                });
            if (victim == entries_.end()) {
                break;
            }
            entries_.erase(victim);
            stats_.removes++;
        }
        stats_.estimatedSize = entries_.size();
        stats_.memoryUsage = estimateMemoryUsageLocked();
    }

    size_t estimateMemoryUsageLocked() const {
        size_t total = 0;
        for (const auto& [key, entry] : entries_) {
            total += key.size() + entry.value.size();
        }
        return total;
    }

    LocalCacheConfig config_;
    mutable std::mutex mutex_;
    mutable std::map<std::string, CacheEntry> entries_;
    mutable std::map<std::string, CachePolicy> policies_;
    mutable CacheStats stats_;
    bool initialized_{false};
};

// LocalCacheManager implementation
LocalCacheManager::LocalCacheManager(const LocalCacheConfig& config)
    : pImpl(std::make_unique<Impl>(config)) {}

LocalCacheManager::~LocalCacheManager() = default;

bool LocalCacheManager::initialize() {
    return pImpl->initialize();
}

void LocalCacheManager::shutdown() {
    pImpl->shutdown();
}

bool LocalCacheManager::get(const std::string& key, std::string& value) {
    return pImpl->get(key, value);
}

bool LocalCacheManager::set(const std::string& key, const std::string& value, 
                           std::chrono::seconds ttl) {
    return pImpl->set(key, value, ttl);
}

bool LocalCacheManager::remove(const std::string& key) {
    return pImpl->remove(key);
}

bool LocalCacheManager::exists(const std::string& key) {
    return pImpl->exists(key);
}

void LocalCacheManager::clear() {
    pImpl->clear();
}

std::map<std::string, std::string> LocalCacheManager::getBulk(const std::vector<std::string>& keys) {
    return pImpl->getBulk(keys);
}

void LocalCacheManager::setBulk(const std::map<std::string, std::string>& keyValues,
                               std::chrono::seconds ttl) {
    pImpl->setBulk(keyValues, ttl);
}

CacheStats LocalCacheManager::getStats() const {
    return pImpl->getStats();
}

void LocalCacheManager::resetStats() {
    pImpl->resetStats();
}

void LocalCacheManager::setPolicy(const std::string& keyPrefix, CachePolicy policy) {
    pImpl->setPolicy(keyPrefix, policy);
}

CachePolicy LocalCacheManager::getPolicy(const std::string& key) const {
    return pImpl->getPolicy(key);
}

bool LocalCacheManager::isEnabled() const {
    return pImpl->isEnabled();
}

size_t LocalCacheManager::size() const {
    return pImpl->size();
}

size_t LocalCacheManager::estimatedSize() const {
    return pImpl->estimatedSize();
}

void LocalCacheManager::cleanup() {
    pImpl->cleanup();
}

const LocalCacheConfig& LocalCacheManager::getConfig() const {
    return pImpl->getConfig();
}

} // namespace Cache
} // namespace AStockQuantEngine