#pragma once

#include <string>
#include <vector>

namespace astock::infrastructure::database {

/// @brief 数据迁移引擎 — 启动时按 schema_version 执行增量迁移
///
/// 位置: AppBootstrap, 配置三段式加载之后、引擎启动之前
///
/// 原则:
///   - 全部成功或全部回滚 (无 DEGRADED 状态)
///   - 读到废弃字段 → 拒绝启动, 要求用户手动删除
///   - 单条失败记录详细信息, 继续处理剩余; 完成后失败率>0 → 回滚
class DataMigrator {
public:
    struct MigrationResult {
        bool success = true;
        std::string fromVersion;
        std::string toVersion;
        std::vector<std::string> errors;     // 每条错误详情
        std::vector<std::string> warnings;   // 非致命提示
    };

    static DataMigrator& instance();

    /// @brief 执行所有待处理的迁移
    /// @param configDir 配置文件目录 (用于扫描废弃字段)
    /// @return 迁移结果 — success==false 时调用方必须拒绝启动
    MigrationResult run(const std::string& configDir);

private:
    DataMigrator() = default;

    /// @brief 扫描配置文件中的废弃字段
    void scanDeprecatedFields(const std::string& configDir, MigrationResult& result);

    /// @brief 检查配置文件是否包含指定废弃字段
    bool configContainsDeprecatedField(const std::string& filePath,
                                       const std::string& fieldName);
};

} // namespace astock::infrastructure::database
