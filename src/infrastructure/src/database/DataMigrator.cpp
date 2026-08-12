#include "database/DataMigrator.h"
#include "database/NativePgConnectionPool.h"
#include "foundation/fs/File.hpp"
#include "foundation/log/logging.hpp"
#include "foundation/json/json_facade.h"

#include <filesystem>

namespace astock::infrastructure::database {

DataMigrator& DataMigrator::instance() {
    static DataMigrator s_instance;
    return s_instance;
}

DataMigrator::MigrationResult DataMigrator::run(const std::string& configDir) {
    MigrationResult result;
    result.fromVersion = "0.15.0";
    result.toVersion   = "0.16.0";

    INTERNAL_INFO_STREAM << "[DataMigrator] 开始迁移: " << result.fromVersion
                         << " → " << result.toVersion;

    // ── Step 1: 扫描配置文件中的废弃字段 ──
    scanDeprecatedFields(configDir, result);

    // ── Step 2: 数据库 schema_version 记录 ──
    {
        auto db = astock::database::NativePgConnectionPool::instance().getConnection();
        if (!db || !db->isOpen()) {
            result.warnings.push_back("DB 连接不可用, 跳过 schema_version 写入");
        } else {
            try {
                // 确保 migration 表存在
                db->executeUpdate(
                    "CREATE TABLE IF NOT EXISTS public.schema_version ("
                    "  version     VARCHAR(16) PRIMARY KEY,"
                    "  applied_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),"
                    "  description TEXT"
                    ")");

                // 写入当前版本 (幂等)
                db->executeUpdate(
                    "INSERT INTO public.schema_version (version, description) "
                    "VALUES ('0.16.0', 'v0.16.0 健壮性优化: ConfigManager 三段式 + accountId 强制 + 废弃字段扫描') "
                    "ON CONFLICT(version) DO NOTHING");

                INTERNAL_INFO_STREAM << "[DataMigrator] schema_version 已记录: 0.16.0";
            } catch (const std::exception& e) {
                result.warnings.push_back(
                    std::string("schema_version 写入失败: ") + e.what());
            }
        }
    }

    // ── 结果判定 ──
    if (!result.errors.empty()) {
        result.success = false;
        INTERNAL_ERROR_STREAM << "[DataMigrator] 迁移失败: " << result.errors.size()
                              << " 个错误, " << result.warnings.size() << " 个警告";
        for (const auto& err : result.errors)
            INTERNAL_ERROR_STREAM << "  - " << err;
    } else {
        INTERNAL_INFO_STREAM << "[DataMigrator] 迁移成功: " << result.fromVersion
                             << " → " << result.toVersion;
    }

    return result;
}

void DataMigrator::scanDeprecatedFields(const std::string& configDir,
                                         MigrationResult& result) {
    // v0.16.0 废弃字段列表 — 任何配置文件中出现这些字段则拒绝启动
    static const std::vector<std::string> kDeprecatedFields = {
        "industryNeutral",
        "usePercentile"
    };

    namespace fs = std::filesystem;
    std::error_code ec;

    if (!fs::exists(configDir, ec)) {
        result.warnings.push_back("配置目录不存在, 跳过废弃字段扫描: " + configDir);
        return;
    }

    for (const auto& entry : fs::recursive_directory_iterator(configDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;

        std::string path = entry.path().string();
        // 仅扫描 JSON 配置文件
        if (path.size() < 5 || path.substr(path.size() - 5) != ".json") continue;

        for (const auto& field : kDeprecatedFields) {
            if (configContainsDeprecatedField(path, field)) {
                std::string err = "配置文件包含废弃字段 \"" + field
                                + "\": " + path
                                + " — 请手动删除该字段后重试";
                result.errors.push_back(err);
            }
        }
    }
}

bool DataMigrator::configContainsDeprecatedField(const std::string& filePath,
                                                   const std::string& fieldName) {
    try {
        std::string content = foundation::fs::File::readText(filePath);
        if (content.empty()) return false;

        auto json = foundation::json::JsonFacade::parse(content);
        // 递归检查 JSON 中是否包含该字段名
        // 简化实现: 字符串搜索 (足够可靠, JSON key 必然以 "fieldName" 形式出现)
        std::string searchPattern = "\"" + fieldName + "\"";
        return content.find(searchPattern) != std::string::npos;
    } catch (const std::exception&) {
        // 解析失败不阻塞 — 配置三段式加载已经处理了
        return false;
    }
}

} // namespace astock::infrastructure::database
