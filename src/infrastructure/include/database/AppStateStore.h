#pragma once
// AppStateStore — app_state.json 唯一持久化写者 (P0 基础件)
// 职责: 多服务共享的 JSON 状态文件读写; 路径键控单例 + 互斥 + 原子写
// 合并语义: 写一个键时保留文件内其余全部键 (逐键合并, 与既有实现等价)

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace foundation { namespace json { class JsonFacade; } }

namespace astock::infrastructure::database {

class AppStateStore {
public:
    /// @brief 取路径对应的存储实例 (同路径共享, 保证跨服务互斥串行)
    static std::shared_ptr<AppStateStore> forPath(const std::string& path);

    /// @brief 读顶层整型键; 不存在/解析失败 → false
    bool readInt(const std::string& key, std::int64_t& out) const;

    /// @brief 读顶层对象 section 内 key 的整型值; 不存在/解析失败 → false
    bool readInt(const std::string& section, const std::string& key,
                 std::int64_t& out) const;

    /// @brief 写顶层整型键 (保留其余顶层键; dropAliases 为需同时剔除的旧键名)
    void writeInt(const std::string& key, std::int64_t value,
                  const std::vector<std::string>& dropAliases = {});

    /// @brief 写顶层对象 section 内 key 的整型值
    /// (保留 section 内其他条目与其余顶层键, lastEvalDay.<strategyId>.<period> 等场景)
    void writeInt(const std::string& section, const std::string& key,
                  std::int64_t value);

    /// @brief 顶层对象 section 内 key 是否存在 (键存在性判定, 与值类型无关)
    /// positionBook adopt 双分支 (§9): 键存在(即使载荷损坏)即不采纳券商快照
    bool hasKey(const std::string& section, const std::string& key) const;

    /// @brief 读顶层字符串键; 不存在/解析失败 → false
    /// (P6: 顶层单键形式, lastAdjFactorDate 等场景, 与 readInt(key) 对齐)
    bool readString(const std::string& key, std::string& out) const;

    /// @brief 写顶层字符串键 (保留其余顶层键; dropAliases 为需同时剔除的旧键名)
    void writeString(const std::string& key, const std::string& value,
                     const std::vector<std::string>& dropAliases = {});

    /// @brief 读顶层对象 section 内 key 的字符串值; 不存在/解析失败 → false
    bool readString(const std::string& section, const std::string& key,
                    std::string& out) const;

    /// @brief 写顶层对象 section 内 key 的字符串值 (合并语义同 writeInt(section,key))
    void writeString(const std::string& section, const std::string& key,
                     const std::string& value);

    /// @brief 持久化文件路径
    const std::string& path() const noexcept { return m_path; }

private:
    explicit AppStateStore(std::string path);

    /// @brief 原子写盘 (tmp 写入 → 删旧 → rename), 调用方需已持有 m_mutex
    void writeFileLocked(const foundation::json::JsonFacade& root);

    std::string m_path;
    mutable std::mutex m_mutex;  // 同路径读写互斥 (跨服务串行)

    static std::mutex s_registryMutex;
    static std::unordered_map<std::string, std::weak_ptr<AppStateStore>> s_stores;
};

} // namespace astock::infrastructure::database
