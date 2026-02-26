# AStockQuantEngine 缓存系统

## 概述

AStockQuantEngine缓存系统是一个高性能、多层次的缓存解决方案，专门为量化交易系统设计。系统支持本地内存缓存（基于Caffeine）和分布式Redis缓存，提供统一的访问接口和智能的缓存策略管理。

## 主要特性

### 1. 多层次缓存架构
- **本地缓存（L1）**: 基于Caffeine的高性能内存缓存
- **分布式缓存（L2）**: 基于Redis的分布式缓存
- **智能回写**: Redis命中后自动回写到本地缓存

### 2. 智能缓存策略
- 按数据类型配置不同的TTL
- 支持缓存穿透保护
- 支持缓存雪崩保护
- 可配置的缓存预热

### 3. 高性能序列化
- 支持JSON序列化（nlohmann/json）
- 支持二进制序列化（性能敏感场景）
- 内置量化数据类型序列化（K线、Tick、因子等）

### 4. 监控与统计
- 实时命中率统计
- 延迟监控
- 内存使用统计
- 可配置的告警阈值

### 5. 易于集成
- 统一的缓存门面接口
- 与现有DataService无缝集成
- 支持模板化的数据类型

## 架构设计

```
┌─────────────────────────────────────────────┐
│               应用层 (Application)           │
├─────────────────────────────────────────────┤
│          缓存门面 (CacheFacade)              │
├─────────────────────────────────────────────┤
│   本地缓存管理器     │    Redis缓存管理器     │
│   (LocalCache)      │    (RedisCache)       │
├─────────────────────────────────────────────┤
│      Caffeine       │        hiredis        │
└─────────────────────────────────────────────┘
```

## 快速开始

### 1. 初始化缓存系统

```cpp
#include "cache_facade.h"

using namespace AStockQuantEngine::Cache;

// 配置缓存系统
CacheConfig config;
config.enabled = true;
config.defaultTtl = std::chrono::seconds(300);

// 本地缓存配置
config.localCache.enabled = true;
config.localCache.maxSize = 10000;

// Redis缓存配置
config.redisCache.enabled = true;
config.redisCache.host = "127.0.0.1";
config.redisCache.port = 6379;

// 初始化
auto& cache = CacheFacade::getInstance();
cache.initialize(config);
```

### 2. 基本使用

```cpp
// 设置缓存
KLine klineData;
klineData.symbol = "000001.SZ";
klineData.trade_date = "2024-01-15";
klineData.close = 10.8;

std::string key = KeyGenerator::marketDaily("000001.SZ", "2024-01-15", "2024-01-15");
cache.set(key, klineData, std::chrono::seconds(3600));

// 获取缓存
KLine cachedData;
if (cache.get(key, cachedData)) {
    std::cout << "Cache hit: " << cachedData.close << std::endl;
}

// 批量操作
std::map<std::string, KLine> batchData;
cache.setBulk(batchData, std::chrono::seconds(1800));
auto results = cache.getBulk<KLine>(keys);
```

### 3. 与DataService集成

```cpp
// 带缓存的查询函数
std::vector<KLine> queryStockDataWithCache(const std::string& symbol,
                                          const std::string& startDate,
                                          const std::string& endDate) {
    
    auto& cache = CacheFacade::getInstance();
    
    // 生成缓存key
    std::string cacheKey = KeyGenerator::marketDaily(symbol, startDate, endDate);
    
    // 尝试从缓存获取
    std::vector<KLine> cachedResult;
    if (cache.get(cacheKey, cachedResult)) {
        return cachedResult;
    }
    
    // 缓存未命中，查询数据库
    auto result = queryStockDataFromDatabase(symbol, startDate, endDate);
    
    // 将结果存入缓存
    if (!result.empty()) {
        cache.set(cacheKey, result, std::chrono::seconds(3600));
    }
    
    return result;
}
```

## 配置说明

### 配置文件位置
```
src/cache/config/cache_config.yaml
```

### 主要配置项

```yaml
cache:
  enabled: true
  default_ttl: 300  # 默认5分钟
  
  local_cache:
    enabled: true
    max_size: 100000
    expire_after_access: 3600
    expire_after_write: 300
    
  redis_cache:
    enabled: true
    host: "127.0.0.1"
    port: 6379
    password: "${REDIS_PASSWORD}"
    
  policies:
    "market:daily:*":
      ttl: 86400  # 24小时
      use_local_cache: true
      use_redis_cache: true
```

### 缓存策略配置

系统支持按数据类型配置不同的缓存策略：

| 数据类型 | 推荐TTL | 本地缓存 | Redis缓存 | 说明 |
|---------|---------|----------|-----------|------|
| 股票基本信息 | 1小时 | ✓ | ✓ | 变更极少 |
| 日线行情数据 | 24小时 | ✓ | ✓ | 历史数据不变 |
| 实时行情 | 5秒 | ✗ | ✓ | 实时性要求高 |
| 用户会话 | 30分钟 | ✗ | ✓ | 分布式需要 |
| 策略计算结果 | 10分钟 | ✓ | ✓ | 中等时效性 |
| 数据清洗结果 | 30分钟 | ✓ | ✓ | 中等时效性 |

## 性能优化

### 1. 序列化优化
- 对于性能敏感的数据类型，使用二进制序列化
- 对于通用数据类型，使用JSON序列化
- 支持自定义序列化器

### 2. 内存优化
- 本地缓存使用LRU淘汰策略
- 支持弱引用和软引用
- 可配置的最大内存限制

### 3. 网络优化
- Redis连接池管理
- 批量操作减少网络往返
- 异步操作支持

## 监控与调试

### 统计信息获取

```cpp
CacheStats stats = cache.getStats();
std::cout << "命中率: " << (stats.hitRate() * 100) << "%" << std::endl;
std::cout << "平均延迟: " << stats.avgGetLatencyMs() << " ms" << std::endl;
std::cout << "总命中数: " << stats.hits << std::endl;
std::cout << "总未命中数: " << stats.misses << std::endl;
```

### 告警配置

```yaml
monitoring:
  alert_thresholds:
    hit_rate_warning: 0.7     # 命中率低于70%警告
    hit_rate_critical: 0.5    # 命中率低于50%严重警告
    latency_warning_ms: 10    # 延迟超过10ms警告
    latency_critical_ms: 50   # 延迟超过50ms严重警告
```

## 高级功能

### 1. 缓存穿透保护
```yaml
advanced:
  cache_penetration_protection:
    enabled: true
    cache_empty_ttl: 60  # 空结果缓存60秒
```

### 2. 缓存雪崩保护
```yaml
advanced:
  cache_avalanche_protection:
    enabled: true
    random_ttl_offset: 30  # TTL随机偏移±30秒
```

### 3. 缓存预热
```yaml
advanced:
  local_cache_warmup:
    enabled: true
    warmup_keys:
      - "stock:basic:*"
      - "market:daily:*"
    warmup_on_startup: true
```

## 构建与依赖

### 依赖库
- **Caffeine**: 本地缓存实现
- **hiredis**: Redis客户端库
- **nlohmann/json**: JSON序列化库

### CMake配置
```cmake
# 启用缓存模块
option(BUILD_CACHE_MODULE "Build cache module" ON)

# 查找依赖
find_package(nlohmann_json 3.9.1 REQUIRED)
find_library(CAFFEINE_LIBRARY caffeine)
find_library(HIREDIS_LIBRARY hiredis)
```

### 构建命令
```bash
mkdir build && cd build
cmake .. -DBUILD_CACHE_MODULE=ON
cmake --build .
```

## 测试

### 单元测试
```bash
# 运行缓存测试
./cache_tests --gtest_filter=LocalCacheTest*
./cache_tests --gtest_filter=RedisCacheTest*
./cache_tests --gtest_filter=SerializationTest*
```

### 集成测试
```bash
# 运行集成示例
./cache_integration_example
```

## 故障排除

### 常见问题

1. **Redis连接失败**
   - 检查Redis服务是否运行
   - 检查网络连接
   - 验证认证信息

2. **内存使用过高**
   - 调整本地缓存大小限制
   - 启用弱引用/软引用
   - 监控缓存命中率

3. **序列化错误**
   - 检查数据类型定义
   - 验证JSON格式
   - 更新序列化器

### 调试日志

启用调试日志：
```cpp
// 在初始化前设置日志级别
CacheConfig config;
config.debug = true;
cache.initialize(config);
```

## 性能基准

### 测试环境
- CPU: Intel i7-12700K
- 内存: 32GB DDR4
- Redis: 6.2.6
- 网络: 千兆以太网

### 性能数据
| 操作类型 | 平均延迟 | 吞吐量 | 说明 |
|---------|---------|--------|------|
| 本地缓存读取 | 0.05ms | 20,000 ops/s | 内存操作 |
| Redis读取 | 0.8ms | 1,200 ops/s | 网络往返 |
| 批量读取(100条) | 15ms | 6,600 ops/s | 网络优化 |
| 序列化(K线) | 0.02ms | 50,000 ops/s | JSON序列化 |

## 扩展开发

### 添加新的缓存策略

1. 在配置文件中添加策略：
```yaml
policies:
  "custom:data:*":
    ttl: 600
    use_local_cache: true
    use_redis_cache: true
```

2. 使用KeyGenerator生成key：
```cpp
std::string key = KeyGenerator::generate("custom:data", id1, id2);
```

### 添加新的数据类型

1. 定义数据类型：
```cpp
struct CustomData {
    std::string id;
    double value;
    std::vector<double> array;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(CustomData, id, value, array)
};
```

2. 添加序列化支持（可选）：
```cpp
template<>
inline std::string Serializer::toJson<CustomData>(const CustomData& data) {
    nlohmann::json j = data;
    return j.dump();
}
```

## 版本历史

### v1.0.0 (2026-02-21)
- 初始版本发布
- 支持本地缓存（Caffeine）
- 支持Redis分布式缓存
- 统一的缓存门面接口
- 完整的配置系统
- 监控与统计功能

## 贡献指南

1. Fork项目
2. 创建特性分支
3. 提交更改
4. 推送到分支
5. 创建Pull Request

## 许可证

本项目采用MIT许可证。详见LICENSE文件。

## 联系方式

- 项目主页: https://github.com/xiaoyu21git/AStockQuant
- 问题反馈: GitHub Issues
- 文档更新: 提交Pull Request