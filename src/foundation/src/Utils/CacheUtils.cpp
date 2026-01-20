// foundation/src/utils/CacheUtils.cpp
#include "utils/CacheUtils.h"
#include "Utils/string.hpp"
#include "Utils/time.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <unordered_set>
#include <functional> // 用于std::hash

namespace foundation {
namespace utils {

// ============ 统计序列化实现 ============

std::string CacheUtils::statsToJson(const CacheStats& stats) {
    try {
        auto json = statsToJsonObject(stats);
        return json.toString();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to serialize cache stats to JSON: " + std::string(e.what()));
        return "{}";
    }
}

CacheStats CacheUtils::jsonToStats(const std::string& json_str) {
    try {
        auto json = foundation::json::JsonFacade::parse(json_str);
        return jsonObjectToStats(json);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to deserialize cache stats from JSON: " + std::string(e.what()));
        return CacheStats{};
    }
}

foundation::json::JsonFacade CacheUtils::statsToJsonObject(const CacheStats& stats) {
    auto json = foundation::json::JsonFacade::createObject();
    
    json.set("hits", static_cast<int>(stats.hits));
    json.set("misses", static_cast<int>(stats.misses));
    json.set("size", static_cast<int>(stats.size));
    json.set("capacity", static_cast<int>(stats.capacity));
    json.set("memory_usage", static_cast<int>(stats.memory_usage));
    json.set("hit_rate", stats.hit_rate);
    json.set("evictions", static_cast<int>(stats.evictions));
    json.set("insertions", static_cast<int>(stats.insertions));
    json.set("deletions", static_cast<int>(stats.deletions));
    json.set("expired_items", static_cast<int>(stats.expired_items));
    json.set("uptime_ms", static_cast<int>(stats.uptime.count()));
    
    // 计算额外指标
    double utilization = stats.capacity > 0 ? 
        (static_cast<double>(stats.size) / stats.capacity) * 100.0 : 0.0;
    json.set("utilization_percent", utilization);
    
    double avg_memory_per_item = stats.size > 0 ?
        static_cast<double>(stats.memory_usage) / stats.size : 0.0;
    json.set("avg_memory_per_item_bytes", avg_memory_per_item);
    
    return json;
}

CacheStats CacheUtils::jsonObjectToStats(const foundation::json::JsonFacade& json) {
    CacheStats stats;
    
    if (json.isNull() || !json.isObject()) {
        return stats;
    }
    
    try {
        stats.hits = json.get("hits").asInt(0);
        stats.misses = json.get("misses").asInt(0);
        stats.size = json.get("size").asInt(0);
        stats.capacity = json.get("capacity").asInt(0);
        stats.memory_usage = json.get("memory_usage").asInt(0);
        stats.hit_rate = json.get("hit_rate").asDouble(0.0);
        stats.evictions = json.get("evictions").asInt(0);
        stats.insertions = json.get("insertions").asInt(0);
        stats.deletions = json.get("deletions").asInt(0);
        stats.expired_items = json.get("expired_items").asInt(0);
        
        int64_t uptime_ms = json.get("uptime_ms").asInt(0);
        stats.uptime = std::chrono::milliseconds(uptime_ms);
        
        // 更新命中率（如果JSON中的可能过期）
        stats.updateHitRate();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing cache stats from JSON: " + std::string(e.what()));
    }
    
    return stats;
}

// ============ 缓存键生成 ============

std::string CacheUtils::generateKey(const std::string& prefix,
                                   const std::vector<std::string>& parts,
                                   const std::string& separator) {
    if (parts.empty()) {
        return prefix;
    }
    
    std::ostringstream oss;
    oss << prefix;
    
    for (const auto& part : parts) {
        if (!part.empty()) {
            oss << separator << part;
        }
    }
    
    return oss.str();
}

std::string CacheUtils::generateHashedKey(const std::string& prefix,
                                         const std::vector<std::string>& parts) {
    // 生成基础键
    std::string base_key = generateKey(prefix, parts, ":");
    
    // 计算哈希
    std::size_t hash = std::hash<std::string>{}(base_key);
    
    // 转换为十六进制字符串
    std::ostringstream oss;
    oss << prefix << ":";
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    
    return oss.str();
}

// ============ 内存估算 ============

size_t CacheUtils::estimateStringMemory(const std::string& str) {
    // 字符串内存 = 字符串大小 + 额外开销（通常8-16字节）
    return str.capacity() + sizeof(std::string);
}

template<typename Container>
size_t CacheUtils::estimateContainerMemory(const Container& container) {
    size_t total = 0;
    
    // 估算容器本身的内存
    total += sizeof(Container);
    
    // 估算每个元素的内存
    for (const auto& element : container) {
        total += estimateMemoryUsage(element);
    }
    
    return total;
}

size_t CacheUtils::estimatePrimitiveMemory(size_t size) {
    // 基础类型的内存加上可能的对齐开销
    return size + (size % 8); // 假设8字节对齐
}

std::string CacheUtils::hashString(const std::string& str) {
    // 使用std::hash生成哈希
    std::size_t hash = std::hash<std::string>{}(str);
    
    // 转换为十六进制
    std::ostringstream oss;
    oss << std::hex << hash;
    return oss.str();
}

// ============ 缓存策略 ============

double CacheUtils::calculateLRUScore(
    std::chrono::system_clock::time_point last_access,
    size_t access_count) {
    
    auto now = std::chrono::system_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - last_access).count();
    
    // LRU分数 = 年龄（秒），年龄越大分数越高，越容易被淘汰
    // 加入访问次数作为权重
    double score = static_cast<double>(age) * (1.0 / (1.0 + std::log(1.0 + access_count)));
    
    return score;
}

double CacheUtils::calculateLFUScore(
    size_t access_count,
    std::chrono::system_clock::time_point last_access) {
    
    auto now = std::chrono::system_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::hours>(now - last_access).count();
    
    // LFU分数 = 访问频率 + 年龄衰减
    // 频率越高分数越低（越不容易被淘汰）
    // 年龄作为衰减因子，防止旧的高频项永久占据缓存
    
    double frequency_score = 1.0 / (1.0 + access_count);
    double age_decay = std::exp(-age / 24.0); // 24小时衰减一半
    
    return frequency_score * age_decay;
}

double CacheUtils::calculateARCScore(
    std::chrono::system_clock::time_point recency,
    double frequency,
    size_t size) {
    
    auto now = std::chrono::system_clock::now();
    auto recency_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - recency).count();
    
    // ARC算法综合考虑近期性和频率
    // 分数越低越容易被淘汰
    
    double recency_factor = 1.0 / (1.0 + recency_seconds);
    double frequency_factor = 1.0 / (1.0 + frequency);
    double size_factor = std::log(1.0 + size) / 1000.0; // 大小的影响较小
    
    return recency_factor + frequency_factor + size_factor;
}

// ============ TTL计算 ============

bool CacheUtils::isExpired(
    std::chrono::system_clock::time_point creation_time,
    std::chrono::milliseconds ttl) {
    
    if (ttl.count() < 0) {
        return false; // 永不过期
    }
    
    auto now = std::chrono::system_clock::now();
    auto age = now - creation_time;
    
    return age > ttl;
}

std::chrono::milliseconds CacheUtils::getRemainingTTL(
    std::chrono::system_clock::time_point creation_time,
    std::chrono::milliseconds ttl) {
    
    if (ttl.count() < 0) {
        return std::chrono::milliseconds(-1); // 永不过期
    }
    
    auto now = std::chrono::system_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - creation_time);
    
    return ttl - age;
}

std::chrono::milliseconds CacheUtils::calculateAdaptiveTTL(
    size_t access_count,
    std::chrono::milliseconds avg_access_interval,
    std::chrono::milliseconds base_ttl) {
    
    if (access_count == 0) {
        return base_ttl;
    }
    
    // 根据访问频率调整TTL
    // 访问次数越多，TTL越长
    // 平均访问间隔越短，TTL越长
    
    double frequency_factor = std::log(1.0 + access_count);
    double interval_factor = base_ttl.count() / 
                           std::max<int64_t>(avg_access_interval.count(), 1);
    
    double adaptive_factor = std::min(frequency_factor * interval_factor, 10.0); // 最大10倍
    
    return std::chrono::milliseconds(
        static_cast<int64_t>(base_ttl.count() * adaptive_factor)
    );
}

// ============ 性能分析 ============

double CacheUtils::calculateEfficiency(const CacheStats& stats) {
    if (stats.capacity == 0) {
        return 0.0;
    }
    
    // 计算多个效率指标
    double hit_rate_score = stats.hit_rate; // 0-100
    
    double utilization = static_cast<double>(stats.size) / stats.capacity;
    double utilization_score = (utilization > 0.7 && utilization < 0.9) ? 100.0 : 
                              (100.0 - std::abs(utilization - 0.8) * 500.0); // 最优利用率80%
    
    double memory_efficiency = stats.size > 0 ? 
        static_cast<double>(stats.memory_usage) / stats.size : 0.0;
    double memory_score = std::max(0.0, 100.0 - memory_efficiency / 1000.0); // 越小越好
    
    // 加权平均
    return (hit_rate_score * 0.5 + utilization_score * 0.3 + memory_score * 0.2);
}

size_t CacheUtils::predictOptimalSize(
    const std::vector<std::pair<std::string, size_t>>& access_pattern,
    double desired_hit_rate) {
    
    if (access_pattern.empty()) {
        return 0;
    }
    
    // 按访问频率排序
    std::vector<std::pair<std::string, size_t>> sorted_pattern = access_pattern;
    std::sort(sorted_pattern.begin(), sorted_pattern.end(),
             [](const auto& a, const auto& b) {
                 return a.second > b.second;
             });
    
    // 计算累积访问次数
    size_t total_accesses = 0;
    for (const auto& [key, count] : sorted_pattern) {
        total_accesses += count;
    }
    
    // 找到达到期望命中率所需的最小缓存大小
    size_t cumulative_accesses = 0;
    for (size_t i = 0; i < sorted_pattern.size(); ++i) {
        cumulative_accesses += sorted_pattern[i].second;
        double current_hit_rate = static_cast<double>(cumulative_accesses) / total_accesses;
        
        if (current_hit_rate >= desired_hit_rate) {
            return i + 1; // 需要缓存前i+1个最热的项
        }
    }
    
    return sorted_pattern.size(); // 需要缓存所有项
}

std::vector<std::string> CacheUtils::analyzeHotspots(
    const std::vector<std::string>& access_log,
    size_t top_n) {
    
    std::unordered_map<std::string, size_t> access_counts;
    
    // 统计访问频率
    for (const auto& key : access_log) {
        access_counts[key]++;
    }
    
    // 转换为向量并排序
    std::vector<std::pair<std::string, size_t>> sorted_counts(
        access_counts.begin(), access_counts.end()
    );
    
    std::sort(sorted_counts.begin(), sorted_counts.end(),
             [](const auto& a, const auto& b) {
                 return a.second > b.second;
             });
    
    // 提取前N个热点
    std::vector<std::string> hotspots;
    hotspots.reserve(std::min(top_n, sorted_counts.size()));
    
    for (size_t i = 0; i < std::min(top_n, sorted_counts.size()); ++i) {
        hotspots.push_back(sorted_counts[i].first);
    }
    
    return hotspots;
}

// ============ 模板方法实现 ============

// estimateMemoryUsage 模板特化和通用实现
template<typename T>
size_t CacheUtils::estimateMemoryUsage(const T& obj) {
    // 对于基础类型
    if constexpr (std::is_fundamental_v<T>) {
        return sizeof(T);
    }
    // 对于字符串
    else if constexpr (std::is_same_v<T, std::string>) {
        return estimateStringMemory(obj);
    }
    // 对于智能指针
    else if constexpr (std::is_same_v<T, std::shared_ptr<typename T::element_type>> ||
                      std::is_same_v<T, std::unique_ptr<typename T::element_type>>) {
        return obj ? estimateMemoryUsage(*obj) + sizeof(T) : sizeof(T);
    }
    // 对于容器
    else if constexpr (std::is_same_v<T, std::vector<typename T::value_type>> ||
                      std::is_same_v<T, std::list<typename T::value_type>> ||
                      std::is_same_v<T, std::deque<typename T::value_type>> ||
                      std::is_same_v<T, std::set<typename T::value_type>> ||
                      std::is_same_v<T, std::unordered_set<typename T::value_type>>) {
        return estimateContainerMemory(obj);
    }
    // 对于映射
    else if constexpr (std::is_same_v<T, std::map<typename T::key_type, typename T::mapped_type>> ||
                      std::is_same_v<T, std::unordered_map<typename T::key_type, typename T::mapped_type>>) {
        size_t total = sizeof(T);
        for (const auto& [key, value] : obj) {
            total += estimateMemoryUsage(key) + estimateMemoryUsage(value);
        }
        return total;
    }
    // 对于自定义类型，尝试使用serialize方法估算
    else if constexpr (HasSerializeMethod<T>::value) {
        try {
            std::string serialized = obj.serialize();
            return serialized.size() + sizeof(T);
        } catch (...) {
            return sizeof(T);
        }
    }
    // 默认情况
    else {
        return sizeof(T);
    }
}

// serializeEntry 模板实现
template<typename K, typename V>
std::string CacheUtils::serializeEntry(
    const K& key,
    const V& value,
    const foundation::json::JsonFacade& metadata) {
    
    auto json = foundation::json::JsonFacade::createObject();
    
    // 序列化键
    if constexpr (std::is_same_v<K, std::string>) {
        json.set("key", key);
    } else if constexpr (std::is_integral_v<K>) {
        json.set("key", static_cast<int64_t>(key));
    } else if constexpr (std::is_floating_point_v<K>) {
        json.set("key", static_cast<double>(key));
    } else if constexpr (HasSerializeMethod<K>::value) {
        json.set("key", key.serialize());
    } else {
        // 尝试使用字符串转换
        std::ostringstream oss;
        oss << key;
        json.set("key", oss.str());
    }
    
    // 序列化值
    if constexpr (std::is_same_v<V, std::string>) {
        json.set("value", value);
    } else if constexpr (std::is_integral_v<V>) {
        json.set("value", static_cast<int64_t>(value));
    } else if constexpr (std::is_floating_point_v<V>) {
        json.set("value", static_cast<double>(value));
    } else if constexpr (HasSerializeMethod<V>::value) {
        json.set("value", value.serialize());
    } else {
        // 对于复杂类型，可以使用JSON序列化
        // 这里需要类型支持toJson方法或类似接口
        // 暂时使用字符串表示
        std::ostringstream oss;
        oss << value;
        json.set("value", oss.str());
    }
    
    // 添加元数据
    if (!metadata.isNull()) {
        json.set("metadata", metadata);
    }
    
    // 添加时间戳
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    json.set("timestamp", static_cast<int64_t>(timestamp));
    
    return json.toString();
}

// deserializeEntry 模板实现
template<typename K, typename V>
bool CacheUtils::deserializeEntry(
    const std::string& serialized,
    K& key,
    V& value,
    foundation::json::JsonFacade& metadata) {
    
    try {
        auto json = foundation::json::JsonFacade::parse(serialized);
        
        if (json.isNull() || !json.isObject()) {
            return false;
        }
        
        // 反序列化键
        auto key_node = json.get("key");
        if constexpr (std::is_same_v<K, std::string>) {
            key = key_node.asString();
        } else if constexpr (std::is_integral_v<K>) {
            key = static_cast<K>(key_node.asInt());
        } else if constexpr (std::is_floating_point_v<K>) {
            key = static_cast<K>(key_node.asDouble());
        } else if constexpr (HasDeserializeMethod<K>::value) {
            key = K::deserialize(key_node.asString());
        } else {
            // 从字符串解析
            std::istringstream iss(key_node.asString());
            iss >> key;
        }
        
        // 反序列化值
        auto value_node = json.get("value");
        if constexpr (std::is_same_v<V, std::string>) {
            value = value_node.asString();
        } else if constexpr (std::is_integral_v<V>) {
            value = static_cast<V>(value_node.asInt());
        } else if constexpr (std::is_floating_point_v<V>) {
            value = static_cast<V>(value_node.asDouble());
        } else if constexpr (HasDeserializeMethod<V>::value) {
            value = V::deserialize(value_node.asString());
        } else {
            // 从字符串解析
            std::istringstream iss(value_node.asString());
            iss >> value;
        }
        
        // 获取元数据
        metadata = json.get("metadata");
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to deserialize cache entry: " + std::string(e.what()));
        return false;
    }
}

// 显式实例化常用类型
template size_t CacheUtils::estimateMemoryUsage<int>(const int&);
template size_t CacheUtils::estimateMemoryUsage<double>(const double&);
template size_t CacheUtils::estimateMemoryUsage<std::string>(const std::string&);
template size_t CacheUtils::estimateMemoryUsage<std::vector<int>>(const std::vector<int>&);
template size_t CacheUtils::estimateMemoryUsage<std::map<std::string, int>>(const std::map<std::string, int>&);

template std::string CacheUtils::serializeEntry<std::string, std::string>(
    const std::string&, const std::string&, const foundation::json::JsonFacade&);
template bool CacheUtils::deserializeEntry<std::string, std::string>(
    const std::string&, std::string&, std::string&, foundation::json::JsonFacade&);

template std::string CacheUtils::serializeEntry<std::string, int>(
    const std::string&, const int&, const foundation::json::JsonFacade&);
template bool CacheUtils::deserializeEntry<std::string, int>(
    const std::string&, std::string&, int&, foundation::json::JsonFacade&);

} // namespace utils
} // namespace foundation