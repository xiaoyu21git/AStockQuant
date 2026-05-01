// local_cache_manager.h
// Local cache manager interface

#pragma once

#include "cache_manager.h"
#include <string>
#include <chrono>
#include <memory>
#include <mutex>

namespace AStockQuantEngine {
namespace Cache {

// Local cache configuration
struct LocalCacheConfig {
    bool enabled{true};
    size_t maxSize{10000};
    size_t initialCapacity{1000};
    std::chrono::seconds expireAfterAccess{3600};
    std::chrono::seconds expireAfterWrite{300};
    bool recordStats{true};
    bool weakKeys{false};
    bool weakValues{false};
    bool softValues{false};
};

// Local cache manager
class LocalCacheManager : public ICacheManager {
public:
    explicit LocalCacheManager(const LocalCacheConfig& config);
    ~LocalCacheManager() override;
    
    // ICacheManager implementation
    bool initialize() override;
    void shutdown() override;
    
    bool get(const std::string& key, std::string& value) override;
    bool set(const std::string& key, const std::string& value, 
             std::chrono::seconds ttl) override;
    bool remove(const std::string& key) override;
    bool exists(const std::string& key) override;
    void clear() override;
    
    std::map<std::string, std::string> getBulk(const std::vector<std::string>& keys) override;
    void setBulk(const std::map<std::string, std::string>& keyValues,
                 std::chrono::seconds ttl) override;
    
    CacheStats getStats() const override;
    void resetStats() override;
    
    void setPolicy(const std::string& keyPrefix, CachePolicy policy) override;
    CachePolicy getPolicy(const std::string& key) const override;
    
    bool isEnabled() const override;
    
    // Local cache specific helpers
    size_t size() const;
    size_t estimatedSize() const;
    void cleanup();
    std::vector<std::string> keys() const;
    
    // Access configuration
    const LocalCacheConfig& getConfig() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace Cache
} // namespace AStockQuantEngine