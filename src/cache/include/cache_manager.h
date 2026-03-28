// cache_manager.h
// Cache manager base interfaces

#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <vector>
#include <map>

namespace AStockQuantEngine {
namespace Cache {

// Cache statistics
struct CacheStats {
    uint64_t hits{0};
    uint64_t misses{0};
    uint64_t puts{0};
    uint64_t removes{0};
    
    std::chrono::microseconds totalGetTime{0};
    std::chrono::microseconds totalSetTime{0};
    
    size_t estimatedSize{0};
    size_t memoryUsage{0};
    
    // Compute hit rate.
    double hitRate() const {
        auto total = hits + misses;
        return total > 0 ? static_cast<double>(hits) / total : 0.0;
    }
    
    // Average get latency in milliseconds.
    double avgGetLatencyMs() const {
        auto total = hits + misses;
        return total > 0 ? totalGetTime.count() / 1000.0 / total : 0.0;
    }
    
    // Format as text.
    std::string toString() const;
};

// Cache policy configuration
struct CachePolicy {
    std::chrono::seconds ttl{300};
    bool useLocalCache{true};
    bool useRedisCache{true};
    bool cacheEmptyResults{false};
    int maxSize{100000};
};

// Cache manager base interface
class ICacheManager {
public:
    virtual ~ICacheManager() = default;
    
    // Lifecycle
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    
    // Basic operations
    virtual bool get(const std::string& key, std::string& value) = 0;
    virtual bool set(const std::string& key, const std::string& value, 
                     std::chrono::seconds ttl) = 0;
    virtual bool remove(const std::string& key) = 0;
    virtual bool exists(const std::string& key) = 0;
    virtual void clear() = 0;
    
    // Bulk operations
    virtual std::map<std::string, std::string> getBulk(const std::vector<std::string>& keys) = 0;
    virtual void setBulk(const std::map<std::string, std::string>& keyValues,
                         std::chrono::seconds ttl) = 0;
    
    // Statistics
    virtual CacheStats getStats() const = 0;
    virtual void resetStats() = 0;
    
    // Policies
    virtual void setPolicy(const std::string& keyPrefix, CachePolicy policy) = 0;
    virtual CachePolicy getPolicy(const std::string& key) const = 0;
    
    // Enabled state
    virtual bool isEnabled() const = 0;
};

} // namespace Cache
} // namespace AStockQuantEngine