// DataMigrator.h — 数据库 schema 版本迁移 (v0.15→v0.16)
// 在 AppBootstrap 配置加载后、引擎启动前执行
#pragma once

#include <string>
#include <vector>

namespace astock {
namespace infrastructure {
namespace database {

/// @brief schema 版本迁移结果
struct MigrationResult {
    int totalStrategies{0};
    int migrated{0};       // 成功迁移字段数
    int skipped{0};        // 已是最新, 无需迁移
    int failed{0};         // 迁移失败数
    std::string schemaVersionBefore;   // 迁移前版本
    std::string schemaVersionAfter;    // 迁移后版本
    std::vector<std::string> errors;   // 错误详情

    /// @brief 失败率是否超过阈值 (10%), 超过则拒绝启动
    [[nodiscard]] bool shouldAbort() const {
        if (totalStrategies <= 0) return false;
        return static_cast<double>(failed) / static_cast<double>(totalStrategies) > 0.10;
    }

    [[nodiscard]] std::string summary() const;
};

/// @brief 数据迁移器 — 在启动时执行版本间数据迁移
class DataMigrator {
public:
    DataMigrator() = default;

    /// @brief 执行迁移 (v0.15→v0.16)
    /// @param defaultAccountId 从 trading_connection.json 读取的默认账户ID
    /// @return 迁移结果
    MigrationResult run(const std::string& defaultAccountId);

private:
    /// @brief 确保 schema_version 记录存在
    bool ensureSchemaVersionTable();

    /// @brief 读取当前 schema_version
    std::string readSchemaVersion();

    /// @brief 写入新的 schema_version
    bool writeSchemaVersion(const std::string& version);

    /// @brief 为核心策略填充缺失的 account_id
    MigrationResult migrateAccountId(const std::string& defaultAccountId);

    static constexpr const char* kCurrentVersion = "0.16.0";
    static constexpr const char* kSchemaTable = "live._schema_version";
};

} // namespace database
} // namespace infrastructure
} // namespace astock
