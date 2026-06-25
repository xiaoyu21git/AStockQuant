# 配置读取统一方案 —— ConfigFile 枚举

> **状态**: 待评审（V2 — 已根据评审意见修正）  
> **日期**: 2026-06-25

---

## 一、现状问题

| 配置文件 | 读取位置 | 读取方式 |
|----------|---------|---------|
| `config/trading_connection.json` | [JujinMarketConnector.cpp:247](src/app/src/JujinMarketConnector.cpp#L247) | `std::ifstream` 直接读 |
| `config/trading_connection.json` | [JujinMarketConnector.cpp:861](src/app/src/JujinMarketConnector.cpp#L861) | `std::ifstream` 直接读 |
| `config/trading_connection.json` | [TradingBridges.cpp:48](src/ui/bridge/src/TradingBridges.cpp#L48) | `QFile` + `QJsonDocument` |
| `config/trading_connection.json` | [TradingConnectionConfigService.cpp:309](src/ui/bridge/src/TradingConnectionConfigService.cpp#L309) | `QFile` + `QJsonDocument` |
| `config/risk_config.json` | [RiskConfigService.cpp:134](src/ui/bridge/src/RiskConfigService.cpp#L134) | `QFile` + `QJsonDocument` |

另：JMC `get_app_config_string("jujin.config")` 当前 ConfigManager 扫描路径中不存在此 key，实际总是返回空。

---

## 二、方案设计

### 2.1 核心思路

```
每个配置文件 → 对应 ConfigFile 枚举值
              → ConfigManager 维护 枚举→相对路径 映射
              → 所有模块通过 ConfigManager::instance() 按枚举 加载/保存
```

### 2.2 枚举定义（仅纳入本次改造范围的文件）

```cpp
// foundation/config/ConfigManager.hpp

/// @brief 配置文件名枚举 —— 每个值对应 config/ 下唯一的具体文件
/// @note 仅包含已通过 ConfigManager 统一加载的配置文件；
///       QML 视图 UI 配置 (factor_common.json 等) 不在本枚举范围内
enum class ConfigFile {
    TradingConnection,   // config/trading_connection.json
    RiskConfig,          // config/risk_config.json
    Jujin,               // config/jujin.json (新建)
};
```

**设计决定**: 不加入 `FactorCommon` / `FactorCommonParams`，等真正改造 UI 侧时再加。避免产生"枚举有值但未接入"的双轨并行误用风险。

### 2.3 枚举 → 相对路径映射

```cpp
// ConfigManager.cpp 匿名 namespace
const std::unordered_map<ConfigFile, std::string>& s_configFilePaths() {
    static const std::unordered_map<ConfigFile, std::string> map = {
        {ConfigFile::TradingConnection, "trading_connection.json"},
        {ConfigFile::RiskConfig,        "risk_config.json"},
        {ConfigFile::Jujin,             "jujin.json"},
    };
    return map;
}
```

### 2.4 ConfigManager 新增公共方法

```cpp
class ConfigManager {
public:
    // ── 命名配置文件操作 ──

    /// @brief 解析配置文件完整路径 (configBaseDir_ + "/" + 相对路径)
    /// @throw ConfigException 若 ConfigManager 尚未初始化 (configBaseDir_ 为空)
    std::string configFilePath(ConfigFile file) const;

    /// @brief 加载配置文件 (自动缓存; 文件不存在/解析失败 → 返回空 ConfigNode)
    /// 内部使用双重检查锁保证线程安全; 便捷函数 (get_config_file_xxx) 内部调用此方法
    ConfigNode::Ptr loadConfigFile(ConfigFile file);

    /// @brief 保存配置文件到磁盘 (原子写入: 临时文件 → flush → rename)
    /// 成功后自动更新缓存 (使 read-after-write 立即生效)
    /// @return true 成功, false 失败 (保留原文件不变)
    bool saveConfigFile(ConfigFile file, const ConfigNode& config);

    /// @brief 清除指定文件的缓存 (下次 loadConfigFile 将重新读磁盘)
    /// @note saveConfigFile 成功后自动更新缓存，通常无需手动调用
    void invalidateConfigFileCache(ConfigFile file);

    // ── 快速取值 (内部自动调用 loadConfigFile，按需加载) ──

    std::string get_config_file_string(ConfigFile, const std::string& key,
                                       const std::string& defaultVal = "");
    int         get_config_file_int(ConfigFile, const std::string& key, int def = 0);
    double      get_config_file_double(ConfigFile, const std::string& key, double def = 0.0);
    bool        get_config_file_bool(ConfigFile, const std::string& key, bool def = false);

private:
    mutable std::shared_mutex m_fileCacheMutex;
    std::map<ConfigFile, ConfigNode::Ptr> m_fileConfigCache;
};
```

### 2.5 `loadConfigFile` 双重检查锁实现要点

```cpp
ConfigNode::Ptr ConfigManager::loadConfigFile(ConfigFile file) {
    // 第一次检查 (读锁) — 快速路径
    {
        std::shared_lock<std::shared_mutex> lock(m_fileCacheMutex);
        auto it = m_fileConfigCache.find(file);
        if (it != m_fileConfigCache.end()) return it->second;
    }
    // 未命中 — 升级为写锁，加载
    std::unique_lock<std::shared_mutex> lock(m_fileCacheMutex);
    // 第二次检查 — 防止并发加载
    auto it = m_fileConfigCache.find(file);
    if (it != m_fileConfigCache.end()) return it->second;

    std::string path = configFilePath(file);
    ConfigNode::Ptr node;
    if (foundation::fs::File::exists(path)) {
        ConfigLoader::LoadOptions opts;
        opts.profile = currentProfile_;
        try {
            node = loader_->load(path, opts);
        } catch (const std::exception& e) {
            // JSON 语法错误等 → 缓存空节点，避免反复重试解析坏文件
            INTERNAL_ERROR_STREAM << "[ConfigManager] Failed to parse " << path << ": " << e.what();
            node = std::make_shared<ConfigNode>();
        }
    } else {
        node = std::make_shared<ConfigNode>();  // 文件不存在 → 空节点
    }
    m_fileConfigCache[file] = node;
    return node;
}
```

### 2.6 `saveConfigFile` 原子写入实现

```cpp
bool ConfigManager::saveConfigFile(ConfigFile file, const ConfigNode& config) {
    std::string targetPath = configFilePath(file);
    // 临时文件必须与目标文件在同一目录，避免跨文件系统 rename 失败（Windows 跨卷 / POSIX /tmp）
    std::string tmpPath = targetPath + ".tmp." + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());

    // 1. 写临时文件
    if (!foundation::fs::File::writeText(tmpPath, config.toJsonString(true)))
        return false;

    // 2. 原子 rename（同目录内，POSIX rename 原子；Windows MoveFileEx 同卷原子）
    if (std::rename(tmpPath.c_str(), targetPath.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        return false;
    }

    // 3. 更新缓存 (保存后读一致性)
    {
        std::unique_lock<std::shared_mutex> lock(m_fileCacheMutex);
        m_fileConfigCache[file] = std::make_shared<ConfigNode>(config);
    }
    return true;
}
```

若 `foundation::fs::File` 尚不支持原子 rename，需先封装 `foundation::fs::File::atomicWrite(path, content)` 工具函数。

### 2.7 便捷函数自动加载

```cpp
std::string ConfigManager::get_config_file_string(ConfigFile file,
    const std::string& key, const std::string& defaultVal) {
    auto cfg = loadConfigFile(file);  // 内部已缓存，不会重复读磁盘
    auto node = cfg->getPath(key, '.');
    return node.isNull() ? defaultVal : node.asString();
}
```

调用方只需一行：
```cpp
auto token = ConfigManager::instance()
    .get_config_file_string(ConfigFile::TradingConnection, "token", "");
```

### 2.8 初始化时序防御

```cpp
std::string ConfigManager::configFilePath(ConfigFile file) const {
    if (configBaseDir_.empty()) {
        INTERNAL_FATAL_STREAM << "ConfigManager::configFilePath called before initialize()";
        std::abort();  // 致命错误，不应在生产中发生
    }
    auto it = s_configFilePaths().find(file);
    if (it == s_configFilePaths().end()) {
        INTERNAL_FATAL_STREAM << "Unknown ConfigFile enum value: "
                              << static_cast<int>(file);
        std::abort();
    }
    return configBaseDir_ + "/" + it->second;
}
```

---

## 三、迁移兼容 — jujin.config → jujin.json

### 问题

JMC 原从 `get_app_config_string("jujin.config")` 取值（但该 key 实际不存在），现在改为独立文件 `config/jujin.json`。若将来有旧环境直接在 app config 中写入过此 key，升级后静默丢失。

### 方案

`loadConfigFile(ConfigFile::Jujin)` 增加降级逻辑：

```cpp
// loadConfigFile 内部, 当 ConfigFile::Jujin 文件不存在时
if (file == ConfigFile::Jujin && !foundation::fs::File::exists(path)) {
    // 尝试从旧 app config 迁移
    auto appCfg = getAppConfig();
    auto legacy = appCfg->getPath("jujin.config", '.');
    if (!legacy.isNull()) {
        // 自动生成 jujin.json
        foundation::config::ConfigNode newNode(legacy);
        saveConfigFile(ConfigFile::Jujin, newNode);
        INTERNAL_INFO_STREAM << "[ConfigManager] migrated jujin.config → jujin.json";
        // 存入缓存
        std::unique_lock<std::shared_mutex> lock(m_fileCacheMutex);
        m_fileConfigCache[file] = std::make_shared<ConfigNode>(newNode);
        return m_fileConfigCache[file];
    }
}
// 旧配置也不存在 → 返回空节点（调用方使用默认值）
```

此逻辑仅首次加载时执行一次，迁移完成后 `jujin.json` 存在即走正常路径。

---

## 四、各模块改造方案

### 4.1 JujinMarketConnector

**文件**: [src/app/src/JujinMarketConnector.cpp](src/app/src/JujinMarketConnector.cpp)

三处改动：

| 原位置 | 改动 |
|--------|------|
| `start()` 行 246-257 (读 token/accountId/gmStrategyId) | `ConfigManager::instance().loadConfigFile(TradingConnection)` → `cfg->get("token").asString()` 等 |
| `watchlistFromEnvironment()` 行 861-870 (读 symbols) | 同上 |
| `start()` 行 71-72 (读订阅参数) | `ConfigManager::instance().get_config_file_int(Jujin, "maxMarketSubscriptions", 500)` |

关键：每次调用都检查返回值是否为空节点，空则使用默认值。

### 4.2 TradingConnectionConfigService

**文件**: [src/ui/bridge/src/TradingConnectionConfigService.cpp](src/ui/bridge/src/TradingConnectionConfigService.cpp)

- 保留全部 QML 接口（返回 QVariantMap）
- `resolveConfigFilePath()` → `ConfigManager::configFilePath(TradingConnection)`
- `readConfigFile()` → `ConfigManager::loadConfigFile(TradingConnection)` → `toJsonString()` → `QJsonDocument` → `QVariantMap`
- `writeConfigFile()` → `QVariantMap` → `QJsonDocument` → JSON string → `JsonFacade` → `ConfigNode` → `ConfigManager::saveConfigFile(TradingConnection, node)`

内部新增两个转换辅助：

```cpp
static foundation::config::ConfigNode toConfigNode(const QVariantMap& map) {
    QJsonDocument doc(QJsonObject::fromVariantMap(map));
    return foundation::config::ConfigNode(
        foundation::json::JsonFacade::parse(doc.toJson().toStdString()));
}
static QVariantMap toVariantMap(const foundation::config::ConfigNode& node) {
    if (node.isNull()) return {};
    auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(node.toJsonString()));
    return doc.object().toVariantMap();
}
```

### 4.3 TradingBridges

**文件**: [src/ui/bridge/src/TradingBridges.cpp](src/ui/bridge/src/TradingBridges.cpp#L48)

`ensureInitialized()` 中的直接 QFile 读取改为调用 `TradingConnectionConfigService::loadConfiguration()`（复用已有依赖），或直接 `ConfigManager::loadConfigFile(TradingConnection)` + 转换。

### 4.4 RiskConfigService

**文件**: [src/ui/bridge/src/RiskConfigService.cpp](src/ui/bridge/src/RiskConfigService.cpp)

同 TradingConnectionConfigService 模式 — 读写委托给 ConfigManager。

---

## 五、新建 `config/jujin.json`

```json
{
  "maxMarketSubscriptions": 500,
  "marketSubscriptionBatchSize": 4
}
```

---

## 六、改造顺序

| 步骤 | 内容 | 依赖 |
|------|------|------|
| **1** | `foundation::fs::File` 新增 `atomicWrite(path, content)` | 无 |
| **2** | ConfigManager 新增 `ConfigFile` 枚举 + `loadConfigFile`/`saveConfigFile` + 缓存 + 双重检查锁 | 步骤 1 |
| **3** | 新建 `config/jujin.json` | 步骤 2 |
| **4** | 改造 JujinMarketConnector (3 处) | 步骤 2, 3 |
| **5** | 改造 TradingConnectionConfigService | 步骤 2 |
| **6** | 改造 TradingBridges | 步骤 2 |
| **7** | 改造 RiskConfigService | 步骤 2 |
| **8** | 验证 | 全部 |

---

## 七、不改的范围

| 项目 | 原因 |
|------|------|
| QML 视图配置 (`factor_common.json` 等) | UI 资源加载，不在此次范围；`ConfigFile` 枚举中**不包含**它们 |
| `ConfigManager::loadAppConfigs()` 域扫描 | 保持现有 foundation/profiles/system 域扫描逻辑不变 |
| `ConfigManager::getAppConfig()` / `get_app_config_xxx()` | 保持现有 API 不变 |

---

## 八、验证清单

| 类别 | 验证项 |
|------|--------|
| **基础** | 编译通过；TradingConnection / RiskConfig / Jujin 三个文件正常加载 |
| **原子写入** | 写入过程中 kill 进程 → 原文件完整无损 |
| **写后读** | `saveConfigFile` 后立即 `loadConfigFile` → 读到刚写入的内容 |
| **并发** | 3 个线程同时 `loadConfigFile(TradingConnection)` → 仅一次磁盘 IO |
| **缓存失效** | 外部修改文件 → `invalidateCache` → 重读 → 新内容生效 |
| **降级** | 删除 `trading_connection.json` → loadConfigFile 返回空节点 → 调用方使用默认值不崩溃 |
| **迁移** | 旧环境仅在 app config 中有 `jujin.config` → 首次 loadConfigFile(Jujin) 自动生成 `jujin.json` |
| **时序** | 在 ConfigManager::initialize() 之前调用 loadConfigFile → 触发 abort (符合预期) |
| **残留** | `grep -rn "trading_connection.json" src/` → 仅出现在 ConfigManager 枚举路径映射中 |

---

## 九、架构前后对比

### 改前（5 个入口，3 种方式）

```
trading_connection.json
  ├─ std::ifstream   (JMC::start)
  ├─ std::ifstream   (JMC::watchlistFromEnvironment)
  ├─ QFile           (TradingConnectionConfigService)
  └─ QFile           (TradingBridges)

risk_config.json
  └─ QFile           (RiskConfigService)

jujin 配置
  └─ ConfigManager::get_app_config_string → 总是空
```

### 改后（统一入口）

```
ConfigFile::TradingConnection ──→┐
ConfigFile::RiskConfig        ──→├── ConfigManager::instance()
ConfigFile::Jujin             ──→┘      │
                                        ├── loadConfigFile()
                                        ├── saveConfigFile()    (原子写)
                                        ├── get_config_file_xxx() (自动加载)
                                        └── invalidateConfigFileCache()
```

---

## 十、关键文件清单

| 文件 | 操作 |
|------|------|
| `src/foundation/include/foundation/config/ConfigManager.hpp` | **新增** ConfigFile 枚举 + 6 个方法声明 |
| `src/foundation/src/config/ConfigManager.cpp` | **新增** 路径映射 + 双重检查锁 + 原子写入 + 迁移逻辑 |
| `src/foundation/include/foundation/fs/File.hpp` | **可能新增** `atomicWrite(path, content)` |
| `config/jujin.json` | **新建** |
| `src/app/src/JujinMarketConnector.cpp` | **改造** 去掉 ifstream；jujin 改走新文件 + 降级迁移 |
| `src/ui/bridge/src/TradingConnectionConfigService.cpp` | **改造** read/write 委托 ConfigManager |
| `src/ui/bridge/src/TradingBridges.cpp` | **改造** 去掉 QFile |
| `src/ui/bridge/src/RiskConfigService.cpp` | **改造** read/write 委托 ConfigManager |
