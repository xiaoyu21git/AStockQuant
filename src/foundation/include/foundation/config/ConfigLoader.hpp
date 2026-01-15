// foundation/include/foundation/config/ConfigLoader.hpp
#ifndef CONFIG_LOADER_HPP
#define CONFIG_LOADER_HPP

#include "foundation/config/IConfigProvider.hpp"
#include "foundation/config/ConfigNode.hpp"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "foundation/core/exception.hpp"
#include <string>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <future>
#include <mutex>
#include <atomic>

namespace foundation {
namespace config {

class ConfigLoader {
public:
    using Ptr = std::shared_ptr<ConfigLoader>;
    
    struct LoadOptions {
        std::string profile;
        bool enableCache = true;
        int cacheTTL = 300; // 5分钟
        std::vector<std::string> overridePaths;
        
        LoadOptions() = default;
        LoadOptions(const std::string& p) : profile(p) {}
    };
    
    struct LoadLayer {
        std::string path;
        std::string description;
        int priority = 0;
        bool required = true;
    };
    
    struct Stats {
        size_t totalLoads = 0;
        size_t cacheHits = 0;
        size_t errors = 0;
        std::chrono::milliseconds totalLoadTime{0};
        
        // 添加便捷方法
        double hitRate() const {
            if (totalLoads == 0) return 0.0;
            return static_cast<double>(cacheHits) / totalLoads * 100.0;
        }
        
        double averageLoadTime() const {
            if (totalLoads == 0) return 0.0;
            return static_cast<double>(totalLoadTime.count()) / totalLoads;
        }
    };
    
    ConfigLoader();
    ~ConfigLoader();
    
    void registerProvider(const std::string& extension, IConfigProvider::Ptr provider);
    std::shared_ptr<ConfigNode> load(const std::string& path, 
                                    const LoadOptions& options = LoadOptions());
    
    std::future<std::shared_ptr<ConfigNode>> loadAsync(const std::string& path,
                                                      const LoadOptions& options = LoadOptions());
    
    std::shared_ptr<ConfigNode> loadLayered(const std::vector<LoadLayer>& layers,
                                           const LoadOptions& options = LoadOptions());
    
    void clearCache();
    void removeFromCache(const std::string& key);
    Stats getStats() const;
    
private:
    struct CacheItem {
        std::shared_ptr<ConfigNode> config;
        std::chrono::steady_clock::time_point timestamp;
        int ttl = 300; // 秒
    };
    
    std::pair<std::string, std::string> resolvePathAndProfile(
        const std::string& path, const std::string& profile) const;
    
    std::string getCacheKey(const std::string& path, const std::string& profile) const;
    std::shared_ptr<ConfigNode> getFromCache(const std::string& key);
    void saveToCache(const std::string& key, std::shared_ptr<ConfigNode> config, int ttl);
    bool isCacheExpired(const CacheItem& item) const;
    
    std::unordered_map<std::string, IConfigProvider::Ptr> providers_;
    std::unordered_map<std::string, CacheItem> cache_;
    std::unique_ptr<foundation::thread::ThreadPoolExecutor> threadPool_;
    mutable std::mutex providersMutex_;
    mutable std::mutex cacheMutex_;
    
    // 使用单独的原子变量代替 std::atomic<Stats>
    std::atomic<size_t> totalLoads_{0};
    std::atomic<size_t> cacheHits_{0};
    std::atomic<size_t> errors_{0};
    std::atomic<int64_t> totalLoadTime_{0};  // 毫秒
    
    // 保留普通 Stats 变量用于初始化和临时存储
    Stats stats_;
};

} // namespace config
} // namespace foundation

#endif // CONFIG_LOADER_HPP// foundation/include/foundation/config/ConfigLoader.hpp
#ifndef CONFIG_LOADER_HPP
#define CONFIG_LOADER_HPP

#include "foundation/config/IConfigProvider.hpp"
#include "foundation/config/ConfigNode.hpp"
#include <string>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <future>
#include <mutex>
#include <atomic>

namespace foundation {
namespace config {

class ConfigLoader {
public:
    using Ptr = std::shared_ptr<ConfigLoader>;
    
    struct LoadOptions {
        std::string profile;
        bool enableCache = true;
        int cacheTTL = 300; // 5分钟
        std::vector<std::string> overridePaths;
        
        LoadOptions() = default;
        LoadOptions(const std::string& p) : profile(p) {}
    };
    
    struct LoadLayer {
        std::string path;
        std::string description;
        int priority = 0;
        bool required = true;
    };
    
    struct Stats {
        size_t totalLoads = 0;
        size_t cacheHits = 0;
        size_t errors = 0;
        std::chrono::milliseconds totalLoadTime{0};
        
        // 添加便捷方法
        double hitRate() const {
            if (totalLoads == 0) return 0.0;
            return static_cast<double>(cacheHits) / totalLoads * 100.0;
        }
        
        double averageLoadTime() const {
            if (totalLoads == 0) return 0.0;
            return static_cast<double>(totalLoadTime.count()) / totalLoads;
        }
    };
    
    ConfigLoader();
    ~ConfigLoader();
    
    void registerProvider(const std::string& extension, IConfigProvider::Ptr provider);
    std::shared_ptr<ConfigNode> load(const std::string& path, 
                                    const LoadOptions& options = LoadOptions());
    
    std::future<std::shared_ptr<ConfigNode>> loadAsync(const std::string& path,
                                                      const LoadOptions& options = LoadOptions());
    
    std::shared_ptr<ConfigNode> loadLayered(const std::vector<LoadLayer>& layers,
                                           const LoadOptions& options = LoadOptions());
    
    void clearCache();
    void removeFromCache(const std::string& key);
    Stats getStats() const;
    
private:
    struct CacheItem {
        std::shared_ptr<ConfigNode> config;
        std::chrono::steady_clock::time_point timestamp;
        int ttl = 300; // 秒
    };
    
    std::pair<std::string, std::string> resolvePathAndProfile(
        const std::string& path, const std::string& profile) const;
    
    std::string getCacheKey(const std::string& path, const std::string& profile) const;
    std::shared_ptr<ConfigNode> getFromCache(const std::string& key);
    void saveToCache(const std::string& key, std::shared_ptr<ConfigNode> config, int ttl);
    bool isCacheExpired(const CacheItem& item) const;
    
    std::unordered_map<std::string, IConfigProvider::Ptr> providers_;
    std::unordered_map<std::string, CacheItem> cache_;
    std::unique_ptr<foundation::thread::ThreadPoolExecutor> threadPool_;
    mutable std::mutex providersMutex_;
    mutable std::mutex cacheMutex_;
    
    // 使用单独的原子变量代替 std::atomic<Stats>
    std::atomic<size_t> totalLoads_{0};
    std::atomic<size_t> cacheHits_{0};
    std::atomic<size_t> errors_{0};
    std::atomic<int64_t> totalLoadTime_{0};  // 毫秒
    
    // 保留普通 Stats 变量用于初始化和临时存储
    Stats stats_;
};

} // namespace config
} // namespace foundation

#endif // CONFIG_LOADER_HPP// foundation/include/foundation/config/ConfigLoader.hpp
#ifndef CONFIG_LOADER_HPP
#define CONFIG_LOADER_HPP

#include "foundation/config/IConfigProvider.hpp"
#include "foundation/config/ConfigNode.hpp"
#include <string>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <future>
#include <mutex>
#include <atomic>

namespace foundation {
namespace config {

class ConfigLoader {
public:
    using Ptr = std::shared_ptr<ConfigLoader>;
    
    struct LoadOptions {
        std::string profile;
        bool enableCache = true;
        int cacheTTL = 300; // 5分钟
        std::vector<std::string> overridePaths;
        
        LoadOptions() = default;
        LoadOptions(const std::string& p) : profile(p) {}
    };
    
    struct LoadLayer {
        std::string path;
        std::string description;
        int priority = 0;
        bool required = true;
    };
    
    struct Stats {
        size_t totalLoads = 0;
        size_t cacheHits = 0;
        size_t errors = 0;
        std::chrono::milliseconds totalLoadTime{0};
        
        // 添加便捷方法
        double hitRate() const {
            if (totalLoads == 0) return 0.0;
            return static_cast<double>(cacheHits) / totalLoads * 100.0;
        }
        
        double averageLoadTime() const {
            if (totalLoads == 0) return 0.0;
            return static_cast<double>(totalLoadTime.count()) / totalLoads;
        }
    };
    
    ConfigLoader();
    ~ConfigLoader();
    
    void registerProvider(const std::string& extension, IConfigProvider::Ptr provider);
    std::shared_ptr<ConfigNode> load(const std::string& path, 
                                    const LoadOptions& options = LoadOptions());
    
    std::future<std::shared_ptr<ConfigNode>> loadAsync(const std::string& path,
                                                      const LoadOptions& options = LoadOptions());
    
    std::shared_ptr<ConfigNode> loadLayered(const std::vector<LoadLayer>& layers,
                                           const LoadOptions& options = LoadOptions());
    
    void clearCache();
    void removeFromCache(const std::string& key);
    Stats getStats() const;
    
private:
    struct CacheItem {
        std::shared_ptr<ConfigNode> config;
        std::chrono::steady_clock::time_point timestamp;
        int ttl = 300; // 秒
    };
    
    std::pair<std::string, std::string> resolvePathAndProfile(
        const std::string& path, const std::string& profile) const;
    
    std::string getCacheKey(const std::string& path, const std::string& profile) const;
    std::shared_ptr<ConfigNode> getFromCache(const std::string& key);
    void saveToCache(const std::string& key, std::shared_ptr<ConfigNode> config, int ttl);
    bool isCacheExpired(const CacheItem& item) const;
    
    std::unordered_map<std::string, IConfigProvider::Ptr> providers_;
    std::unordered_map<std::string, CacheItem> cache_;
    std::unique_ptr<foundation::thread::ThreadPoolExecutor> threadPool_;
    mutable std::mutex providersMutex_;
    mutable std::mutex cacheMutex_;
    
    // 使用单独的原子变量代替 std::atomic<Stats>
    std::atomic<size_t> totalLoads_{0};
    std::atomic<size_t> cacheHits_{0};
    std::atomic<size_t> errors_{0};
    std::atomic<int64_t> totalLoadTime_{0};  // 毫秒
    
    // 保留普通 Stats 变量用于初始化和临时存储
    Stats stats_;
};

} // namespace config
} // namespace foundation

#endif // CONFIG_LOADER_HPP// foundation/include/foundation/config/ConfigLoader.hpp
#ifndef CONFIG_LOADER_HPP
#define CONFIG_LOADER_HPP

#include "foundation/config/IConfigProvider.hpp"
#include "foundation/config/ConfigNode.hpp"
#include <string>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <future>
#include <mutex>
#include <atomic>

namespace foundation {
namespace config {

class ConfigLoader {
public:
    using Ptr = std::shared_ptr<ConfigLoader>;
    
    struct LoadOptions {
        std::string profile;
        bool enableCache = true;
        int cacheTTL = 300; // 5分钟
        std::vector<std::string> overridePaths;
        
        LoadOptions() = default;
        LoadOptions(const std::string& p) : profile(p) {}
    };
    
    struct LoadLayer {
        std::string path;
        std::string description;
        int priority = 0;
        bool required = true;
    };
    
    struct Stats {
        size_t totalLoads = 0;
        size_t cacheHits = 0;
        size_t errors = 0;
        std::chrono::milliseconds totalLoadTime{0};
        
        // 添加便捷方法
        double hitRate() const {
            if (totalLoads == 0) return 0.0;
            return static_cast<double>(cacheHits) / totalLoads * 100.0;
        }
        
        double averageLoadTime() const {
            if (totalLoads == 0) return 0.0;
            return static_cast<double>(totalLoadTime.count()) / totalLoads;
        }
    };
    
    ConfigLoader();
    ~ConfigLoader();
    
    void registerProvider(const std::string& extension, IConfigProvider::Ptr provider);
    std::shared_ptr<ConfigNode> load(const std::string& path, 
                                    const LoadOptions& options = LoadOptions());
    
    std::future<std::shared_ptr<ConfigNode>> loadAsync(const std::string& path,
                                                      const LoadOptions& options = LoadOptions());
    
    std::shared_ptr<ConfigNode> loadLayered(const std::vector<LoadLayer>& layers,
                                           const LoadOptions& options = LoadOptions());
    
    void clearCache();
    void removeFromCache(const std::string& key);
    Stats getStats() const;
    
private:
    struct CacheItem {
        std::shared_ptr<ConfigNode> config;
        std::chrono::steady_clock::time_point timestamp;
        int ttl = 300; // 秒
    };
    
    std::pair<std::string, std::string> resolvePathAndProfile(
        const std::string& path, const std::string& profile) const;
    
    std::string getCacheKey(const std::string& path, const std::string& profile) const;
    std::shared_ptr<ConfigNode> getFromCache(const std::string& key);
    void saveToCache(const std::string& key, std::shared_ptr<ConfigNode> config, int ttl);
    bool isCacheExpired(const CacheItem& item) const;
    
    std::unordered_map<std::string, IConfigProvider::Ptr> providers_;
    std::unordered_map<std::string, CacheItem> cache_;
    std::unique_ptr<foundation::thread::ThreadPoolExecutor> threadPool_;
    mutable std::mutex providersMutex_;
    mutable std::mutex cacheMutex_;
    
    // 使用单独的原子变量代替 std::atomic<Stats>
    std::atomic<size_t> totalLoads_{0};
    std::atomic<size_t> cacheHits_{0};
    std::atomic<size_t> errors_{0};
    std::atomic<int64_t> totalLoadTime_{0};  // 毫秒
    
    // 保留普通 Stats 变量用于初始化和临时存储
    Stats stats_;
};

} // namespace config
} // namespace foundation

#endif // CONFIG_LOADER_HPP// foundation/include/foundation/config/ConfigLoader.hpp
#ifndef CONFIG_LOADER_HPP
#define CONFIG_LOADER_HPP

#include "foundation/config/IConfigProvider.hpp"
#include "foundation/config/ConfigNode.hpp"
#include <string>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <future>
#include <mutex>
#include <atomic>

namespace foundation {
namespace config {

class ConfigLoader {
public:
    using Ptr = std::shared_ptr<ConfigLoader>;
    
    struct LoadOptions {
        std::string profile;
        bool enableCache = true;
        int cacheTTL = 300; // 5分钟
        std::vector<std::string> overridePaths;
        
        LoadOptions() = default;
        LoadOptions(const std::string& p) : profile(p) {}
    };
    
    struct LoadLayer {
        std::string path;
        std::string description;
        int priority = 0;
        bool required = true;
    };
    
    struct Stats {
        size_t totalLoads = 0;
        size_t cacheHits = 0;
        size_t errors = 0;
        std::chrono::milliseconds totalLoadTime{0};
        
        // 添加便捷方法
        double hitRate() const {
            if (totalLoads == 0) return 0.0;
            return static_cast<double>(cacheHits) / totalLoads * 100.0;
        }
        
        double averageLoadTime() const {
            if (totalLoads == 0) return 0.0;
            return static_cast<double>(totalLoadTime.count()) / totalLoads;
        }
    };
    
    ConfigLoader();
    ~ConfigLoader();
    
    void registerProvider(const std::string& extension, IConfigProvider::Ptr provider);
    std::shared_ptr<ConfigNode> load(const std::string& path, 
                                    const LoadOptions& options = LoadOptions());
    
    std::future<std::shared_ptr<ConfigNode>> loadAsync(const std::string& path,
                                                      const LoadOptions& options = LoadOptions());
    
    std::shared_ptr<ConfigNode> loadLayered(const std::vector<LoadLayer>& layers,
                                           const LoadOptions& options = LoadOptions());
    
    void clearCache();
    void removeFromCache(const std::string& key);
    Stats getStats() const;
    
private:
    struct CacheItem {
        std::shared_ptr<ConfigNode> config;
        std::chrono::steady_clock::time_point timestamp;
        int ttl = 300; // 秒
    };
    
    std::pair<std::string, std::string> resolvePathAndProfile(
        const std::string& path, const std::string& profile) const;
    
    std::string getCacheKey(const std::string& path, const std::string& profile) const;
    std::shared_ptr<ConfigNode> getFromCache(const std::string& key);
    void saveToCache(const std::string& key, std::shared_ptr<ConfigNode> config, int ttl);
    bool isCacheExpired(const CacheItem& item) const;
    
    std::unordered_map<std::string, IConfigProvider::Ptr> providers_;
    std::unordered_map<std::string, CacheItem> cache_;
    std::unique_ptr<foundation::thread::ThreadPoolExecutor> threadPool_;
    mutable std::mutex providersMutex_;
    mutable std::mutex cacheMutex_;
    
    // 使用单独的原子变量代替 std::atomic<Stats>
    std::atomic<size_t> totalLoads_{0};
    std::atomic<size_t> cacheHits_{0};
    std::atomic<size_t> errors_{0};
    std::atomic<int64_t> totalLoadTime_{0};  // 毫秒
    
    // 保留普通 Stats 变量用于初始化和临时存储
    Stats stats_;
};

} // namespace config
} // namespace foundation

#endif // CONFIG_LOADER_HPP