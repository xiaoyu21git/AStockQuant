// foundation/include/foundation/utils/CacheUtils.h
#pragma once

#include "foundation/json/json_facade.h"
#include "foundation/log/logger.hpp"
#include <string>
#include <chrono>
#include <unordered_map>
#include <memory>

namespace foundation {
namespace utils {

/**
 * @brief 缓存统计信息
 */
struct CacheStats {
    size_t hits = 0;                      // 命中次数
    size_t misses = 0;                    // 未命中次数
    size_t size = 0;                      // 当前大小
    size_t capacity = 0;                  // 容量
    size_t memory_usage = 0;              // 内存使用量（字节）
    double hit_rate = 0.0;                // 命中率
    size_t evictions = 0;                 // 淘汰次数
    size_t insertions = 0;                // 插入次数
    size_t deletions = 0;                 // 删除次数
    size_t expired_items = 0;             // 过期项数量
    std::chrono::milliseconds uptime{0};  // 运行时间
    
    // 重置统计
    void reset() {
        hits = 0;
        misses = 0;
        size = 0;
        memory_usage = 0;
        hit_rate = 0.0;
        evictions = 0;
        insertions = 0;
        deletions = 0;
        expired_items = 0;
        uptime = std::chrono::milliseconds(0);
    }
    
    // 更新命中率
    void updateHitRate() {
        size_t total = hits + misses;
        hit_rate = total > 0 ? (static_cast<double>(hits) / total) * 100.0 : 0.0;
    }
};

/**
 * @brief 缓存工具类
 */
class CacheUtils {
public:
    CacheUtils() = delete; // 纯工具类，禁止实例化
    
    // ============ 统计序列化 ============
    
    /**
     * @brief 缓存统计信息转换为JSON字符串
     */
    static std::string statsToJson(const CacheStats& stats);
    
    /**
     * @brief JSON字符串转换为缓存统计信息
     */
    static CacheStats jsonToStats(const std::string& json_str);
    
    /**
     * @brief 缓存统计信息转换为JSON对象
     */
    static foundation::json::JsonFacade statsToJsonObject(const CacheStats& stats);
    
    /**
     * @brief JSON对象转换为缓存统计信息
     */
    static CacheStats jsonObjectToStats(const foundation::json::JsonFacade& json);
    
    // ============ 缓存键生成 ============
    
    /**
     * @brief 生成缓存键
     * @param prefix 键前缀
     * @param parts 键组成部分
     * @param separator 分隔符，默认为 ":"
     */
    static std::string generateKey(const std::string& prefix,
                                 const std::vector<std::string>& parts,
                                 const std::string& separator = ":");
    
    /**
     * @brief 生成哈希缓存键
     */
    static std::string generateHashedKey(const std::string& prefix,
                                        const std::vector<std::string>& parts);
    
    // ============ 内存估算 ============
    
    /**
     * @brief 估算对象内存使用量
     * @tparam T 对象类型
     * @param obj 对象引用
     * @return 估算的内存使用量（字节）
     */
    template<typename T>
    static size_t estimateMemoryUsage(const T& obj);
    
    /**
     * @brief 估算字符串内存使用量
     */
    static size_t estimateStringMemory(const std::string& str);
    
    /**
     * @brief 估算容器内存使用量
     */
    template<typename Container>
    static size_t estimateContainerMemory(const Container& container);
    
    // ============ 缓存策略 ============
    
    /**
     * @brief 计算缓存淘汰分数（LRU算法）
     * @param last_access 最后访问时间
     * @param access_count 访问次数
     * @return 淘汰分数（越低越容易被淘汰）
     */
    static double calculateLRUScore(
        std::chrono::system_clock::time_point last_access,
        size_t access_count = 1);
    
    /**
     * @brief 计算缓存淘汰分数（LFU算法）
     * @param access_count 访问次数
     * @param last_access 最后访问时间（防止缓存污染）
     * @return 淘汰分数（越低越容易被淘汰）
     */
    static double calculateLFUScore(
        size_t access_count,
        std::chrono::system_clock::time_point last_access);
    
    /**
     * @brief 计算缓存淘汰分数（ARC算法）
     * @param recency 最近访问时间
     * @param frequency 访问频率
     * @param size 对象大小
     * @return 淘汰分数
     */
    static double calculateARCScore(
        std::chrono::system_clock::time_point recency,
        double frequency,
        size_t size);
    
    // ============ TTL计算 ============
    
    /**
     * @brief 检查是否过期
     * @param creation_time 创建时间
     * @param ttl TTL时长
     * @return 是否过期
     */
    static bool isExpired(
        std::chrono::system_clock::time_point creation_time,
        std::chrono::milliseconds ttl);
    
    /**
     * @brief 计算剩余存活时间
     * @param creation_time 创建时间
     * @param ttl TTL时长
     * @return 剩余存活时间（毫秒），负数表示已过期
     */
    static std::chrono::milliseconds getRemainingTTL(
        std::chrono::system_clock::time_point creation_time,
        std::chrono::milliseconds ttl);
    
    /**
     * @brief 计算自适应TTL（基于访问模式）
     * @param access_count 访问次数
     * @param avg_access_interval 平均访问间隔
     * @param base_ttl 基础TTL
     * @return 自适应TTL
     */
    static std::chrono::milliseconds calculateAdaptiveTTL(
        size_t access_count,
        std::chrono::milliseconds avg_access_interval,
        std::chrono::milliseconds base_ttl);
    
    // ============ 性能分析 ============
    
    /**
     * @brief 计算缓存效率
     * @param stats 缓存统计
     * @return 效率分数（0-100）
     */
    static double calculateEfficiency(const CacheStats& stats);
    
    /**
     * @brief 预测缓存大小需求
     * @param access_pattern 访问模式数据
     * @param desired_hit_rate 期望命中率
     * @return 预测的缓存大小
     */
    static size_t predictOptimalSize(
        const std::vector<std::pair<std::string, size_t>>& access_pattern,
        double desired_hit_rate = 0.8);
    
    /**
     * @brief 分析缓存热点
     * @param access_log 访问日志
     * @param top_n 返回前N个热点
     * @return 热点键列表
     */
    static std::vector<std::string> analyzeHotspots(
        const std::vector<std::string>& access_log,
        size_t top_n = 10);
    
    // ============ 序列化辅助 ============
    
    /**
     * @brief 序列化缓存条目
     * @tparam K 键类型
     * @tparam V 值类型
     * @param key 键
     * @param value 值
     * @param metadata 元数据
     * @return 序列化字符串
     */
    template<typename K, typename V>
    static std::string serializeEntry(
        const K& key,
        const V& value,
        const foundation::json::JsonFacade& metadata = foundation::json::JsonFacade());
    
    /**
     * @brief 反序列化缓存条目
     * @tparam K 键类型
     * @tparam V 值类型
     * @param serialized 序列化字符串
     * @param key 输出键
     * @param value 输出值
     * @param metadata 输出元数据
     * @return 是否成功
     */
    template<typename K, typename V>
    static bool deserializeEntry(
        const std::string& serialized,
        K& key,
        V& value,
        foundation::json::JsonFacade& metadata);
    
private:
    // 内部工具函数
    static std::string hashString(const std::string& str);
    static size_t estimatePrimitiveMemory(size_t size);
    
    // 类型特征检查
    template<typename T>
    struct HasSerializeMethod {
        template<typename U>
        static auto test(int) -> decltype(std::declval<U>().serialize(), std::true_type{});
        
        template<typename>
        static std::false_type test(...);
        
        static constexpr bool value = decltype(test<T>(0))::value;
    };
    
    template<typename T>
    struct HasDeserializeMethod {
        template<typename U>
        static auto test(int) -> decltype(U::deserialize(std::declval<std::string>()), std::true_type{});
        
        template<typename>
        static std::false_type test(...);
        
        static constexpr bool value = decltype(test<T>(0))::value;
    };
};
}
}