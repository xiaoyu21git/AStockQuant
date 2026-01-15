// foundation/src/config/ConfigLoader.cpp
#include "foundation/config/ConfigLoader.hpp"
#include "foundation/log/logging.hpp"

#include <algorithm>

namespace foundation {
namespace config {

// ============ ConfigLoader 实现 ============

ConfigLoader::ConfigLoader() 
    : threadPool_(std::make_unique<foundation::thread::ThreadPoolExecutor>(4)) {
    
    INTERNAL_INFO("ConfigLoader initialized");
    
    // 初始化统计信息
    Stats initialStats;
    initialStats.totalLoads = 0;
    initialStats.cacheHits = 0;
    initialStats.errors = 0;
    initialStats.totalLoadTime = std::chrono::milliseconds(0);
    stats_ = initialStats;
}

ConfigLoader::~ConfigLoader() {
    auto currentStats = getStats();
    INTERNAL_DEBUG_STREAM << "ConfigLoader destroyed - "
                         << "Loads: " << currentStats.totalLoads 
                         << ", Cache hits: " << currentStats.cacheHits
                         << ", Errors: " << currentStats.errors
                         << ", Hit rate: " << currentStats.hitRate() << "%"
                         << ", Avg load time: " << currentStats.averageLoadTime() << "ms";
}

void ConfigLoader::registerProvider(
    const std::string& extension, 
    std::shared_ptr<IConfigProvider> provider) {
    
    std::lock_guard<std::mutex> lock(providersMutex_);
    
    // 规范化扩展名（确保以点开头）
    std::string normalizedExt = extension;
    if (!normalizedExt.empty() && normalizedExt[0] != '.') {
        normalizedExt = "." + normalizedExt;
    }
    
    providers_[normalizedExt] = provider;
    
    INTERNAL_INFO_STREAM << "Registered config provider: " << normalizedExt
                        << " -> " << provider->type();
}

std::shared_ptr<ConfigNode> ConfigLoader::load(
    const std::string& path,
    const LoadOptions& options) {
    
    auto startTime = std::chrono::steady_clock::now();
    
    try {
        // 获取缓存键
        std::string cacheKey = getCacheKey(path, options.profile);
        
        // 检查缓存
        if (options.enableCache) {
            auto cached = getFromCache(cacheKey);
            if (cached) {
                // 原子递增缓存命中
                cacheHits_.fetch_add(1, std::memory_order_relaxed);
                INTERNAL_DEBUG_STREAM << "Config cache hit: " << cacheKey;
                return cached;
            }
        }
        
        // 解析路径和环境
        auto [resolvedPath, profile] = resolvePathAndProfile(path, options.profile);
        
        // 查找合适的提供者
        std::shared_ptr<IConfigProvider> provider;
        {
            std::lock_guard<std::mutex> lock(providersMutex_);
            
            std::string extension;
            size_t dotPos = resolvedPath.find_last_of('.');
            if (dotPos != std::string::npos) {
                extension = resolvedPath.substr(dotPos);
            }
            
            auto it = providers_.find(extension);
            if (it != providers_.end()) {
                provider = it->second;
            }
        }
        
        if (!provider) {
            errors_.fetch_add(1, std::memory_order_relaxed);
            INTERNAL_ERROR_STREAM << "No config provider found for file: " << resolvedPath;
            throw foundation::ConfigException(
                "No provider for file: " + resolvedPath);
        }
        
        // 检查文件是否存在
        if (!provider->exists(resolvedPath)) {
            errors_.fetch_add(1, std::memory_order_relaxed);
            INTERNAL_ERROR_STREAM << "Config file not found: " << resolvedPath;
            throw foundation::FileException(
                "Config file not found: " + resolvedPath);
        }
        
        // 加载配置
        INTERNAL_INFO_STREAM << "Loading config from: " << resolvedPath 
                            << " (profile: " << profile << ")";
        
        // 原子递增总加载次数
        totalLoads_.fetch_add(1, std::memory_order_relaxed);
        auto config = provider->load(resolvedPath, profile);
        
        // 应用覆盖配置
        if (!options.overridePaths.empty()) {
            for (const auto& overridePath : options.overridePaths) {
                try {
                    auto overrideConfig = load(overridePath, options);
                    config->overlay(*overrideConfig);
                    INTERNAL_DEBUG_STREAM << "Applied override config: " << overridePath;
                } catch (const std::exception& e) {
                    INTERNAL_WARN_STREAM << "Failed to load override config " 
                                        << overridePath << ": " << e.what();
                }
            }
        }
        
        // 保存到缓存
        if (options.enableCache) {
            saveToCache(cacheKey, config, options.cacheTTL);
        }
        
        // 更新加载时间统计
        auto endTime = std::chrono::steady_clock::now();
        auto loadTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime);
        totalLoadTime_.fetch_add(loadTime.count(), std::memory_order_relaxed);
        
        INTERNAL_INFO_STREAM << "Config loaded successfully: " << resolvedPath 
                           << " (took " << loadTime.count() << "ms)";
        
        return config;
        
    } catch (const std::exception& e) {
        errors_.fetch_add(1, std::memory_order_relaxed);
        INTERNAL_ERROR_STREAM << "Failed to load config " << path << ": " << e.what();
        throw;
    }
}

std::future<std::shared_ptr<ConfigNode>> ConfigLoader::loadAsync(
     const std::string& path,
    const LoadOptions& options) {
    
    auto promise = std::make_shared<std::promise<std::shared_ptr<ConfigNode>>>();
    auto future = promise->get_future();
    
    // 使用现有的 post() 方法
    threadPool_->post([this, path, options, promise]() {
        try {
            auto result = this->load(path, options);
            promise->set_value(result);
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    });
    
    return future;
}

std::shared_ptr<ConfigNode> ConfigLoader::loadLayered(
    const std::vector<LoadLayer>& layers,
    const LoadOptions& options) {
    
    std::vector<std::shared_ptr<ConfigNode>> configs;
    
    // 按优先级排序
    std::vector<LoadLayer> sortedLayers = layers;
    std::sort(sortedLayers.begin(), sortedLayers.end(),
              [](const LoadLayer& a, const LoadLayer& b) {
                  return a.priority < b.priority;
              });
    
    // 加载每一层配置
    for (const auto& layer : sortedLayers) {
        try {
            auto config = load(layer.path, options);
            configs.push_back(config);
            INTERNAL_DEBUG_STREAM << "Loaded config layer: " << layer.description 
                                << " (priority: " << layer.priority << ")";
        } catch (const std::exception& e) {
            if (layer.required) {
                INTERNAL_ERROR_STREAM << "Failed to load required config layer " 
                                     << layer.description << ": " << e.what();
                throw;
            } else {
                INTERNAL_WARN_STREAM << "Failed to load optional config layer " 
                                    << layer.description << ": " << e.what();
            }
        }
    }
    
    // 合并所有配置（优先级高的覆盖优先级低的）
    if (configs.empty()) {
        return std::make_shared<ConfigNode>();
    }
    
    auto result = configs[0];
    for (size_t i = 1; i < configs.size(); ++i) {
        result->overlay(*configs[i]);
    }
    
    return result;
}

// ============ 私有方法实现 ============

std::pair<std::string, std::string> ConfigLoader::resolvePathAndProfile(
    const std::string& path,
    const std::string& profile) const {
    
    // 简单实现：直接返回路径和环境
    // 实际可以支持路径模板和变量替换
    return {path, profile};
}

std::string ConfigLoader::getCacheKey(
    const std::string& path,
    const std::string& profile) const {
    
    return path + ":" + profile;
}

std::shared_ptr<ConfigNode> ConfigLoader::getFromCache(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        if (!isCacheExpired(it->second)) {
            return it->second.config;
        } else {
            // 移除过期的缓存项
            cache_.erase(it);
            INTERNAL_DEBUG_STREAM << "Cache expired: " << key;
        }
    }
    
    return nullptr;
}

void ConfigLoader::saveToCache(const std::string& key, 
                              std::shared_ptr<ConfigNode> config,
                              int ttl) {
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    CacheItem item;
    item.config = config;
    item.timestamp = std::chrono::steady_clock::now();
    item.ttl = ttl;
    
    cache_[key] = item;
    
    INTERNAL_DEBUG_STREAM << "Saved config to cache: " << key 
                         << " (TTL: " << ttl << "s)";
}

bool ConfigLoader::isCacheExpired(const CacheItem& item) const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - item.timestamp);
    return elapsed.count() > item.ttl;
}

void ConfigLoader::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_.clear();
    INTERNAL_INFO("Config cache cleared");
}

void ConfigLoader::removeFromCache(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_.erase(key);
    INTERNAL_DEBUG_STREAM << "Removed from cache: " << key;
}

ConfigLoader::Stats ConfigLoader::getStats() const {
    Stats stats;
    stats.totalLoads = totalLoads_.load(std::memory_order_relaxed);
    stats.cacheHits = cacheHits_.load(std::memory_order_relaxed);
    stats.errors = errors_.load(std::memory_order_relaxed);
    stats.totalLoadTime = std::chrono::milliseconds(
        totalLoadTime_.load(std::memory_order_relaxed));
    return stats;
}

} // namespace config
} // namespace foundation