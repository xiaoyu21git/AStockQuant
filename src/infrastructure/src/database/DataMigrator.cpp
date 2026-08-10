#include "database/DataMigrator.h"
#include "database/NativePgConnectionPool.h"
#include "foundation/log/logging.hpp"
#include "foundation/json/json_facade.h"

#include <sstream>

namespace astock {
namespace infrastructure {
namespace database {

std::string MigrationResult::summary() const {
    std::ostringstream ss;
    ss << "[DataMigrator] 迁移完成: v" << schemaVersionBefore << " → v" << schemaVersionAfter
       << " | 策略总数=" << totalStrategies
       << " 已迁移=" << migrated
       << " 跳过=" << skipped
       << " 失败=" << failed;
    if (!errors.empty()) {
        ss << "\n  错误详情:";
        for (const auto& e : errors) {
            ss << "\n    - " << e;
        }
    }
    return ss.str();
}

bool DataMigrator::ensureSchemaVersionTable() {
    auto& pool = astock::database::NativePgConnectionPool::instance();
    if (!pool.isInitialized()) return false;
    auto db = pool.getConnection();
    if (!db || !db->isOpen()) return false;

    try {
        db->executeUpdate(
            "CREATE TABLE IF NOT EXISTS live._schema_version ("
            "  id          INT PRIMARY KEY DEFAULT 1,"
            "  version     VARCHAR(16) NOT NULL,"
            "  updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),"
            "  CHECK (id = 1)"  // 确保只有一行
            ")");
        return true;
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[DataMigrator] 创建 _schema_version 表失败: " << e.what();
        return false;
    }
}

std::string DataMigrator::readSchemaVersion() {
    auto& pool = astock::database::NativePgConnectionPool::instance();
    if (!pool.isInitialized()) return "";
    auto db = pool.getConnection();
    if (!db || !db->isOpen()) return "";

    try {
        auto result = db->executeQuery(
            "SELECT version FROM live._schema_version WHERE id=1");
        if (!result.isEmpty()) {
            return result.getRow(0).getString("version");
        }
    } catch (const std::exception& e) {
        INTERNAL_WARN_STREAM << "[DataMigrator] 读取 schema_version 失败: " << e.what();
    }
    return "";  // 表为空 = 首次运行
}

bool DataMigrator::writeSchemaVersion(const std::string& version) {
    auto& pool = astock::database::NativePgConnectionPool::instance();
    if (!pool.isInitialized()) return false;
    auto db = pool.getConnection();
    if (!db || !db->isOpen()) return false;

    try {
        db->executeUpdate(
            "INSERT INTO live._schema_version (id, version, updated_at) "
            "VALUES (1, ?, NOW()) "
            "ON CONFLICT(id) DO UPDATE SET version=EXCLUDED.version, updated_at=NOW()",
            {astock::database::SqlParam{version}});
        return true;
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[DataMigrator] 写入 schema_version 失败: " << e.what();
        return false;
    }
}

MigrationResult DataMigrator::migrateAccountId(const std::string& defaultAccountId) {
    MigrationResult result;

    auto& pool = astock::database::NativePgConnectionPool::instance();
    if (!pool.isInitialized()) {
        result.errors.push_back("PG 连接池未初始化");
        result.failed = 1;
        return result;
    }
    auto db = pool.getConnection();
    if (!db || !db->isOpen()) {
        result.errors.push_back("无法获取 DB 连接");
        result.failed = 1;
        return result;
    }

    // 查询所有策略
    auto strategies = db->executeQuery(
        "SELECT strategy_id, parameters FROM strategy "
        "WHERE parameters IS NOT NULL AND parameters != 'null' AND parameters != ''");
    result.totalStrategies = strategies.rowCount();

    for (int i = 0; i < strategies.rowCount(); ++i) {
        const auto& row = strategies.getRow(i);
        std::string strategyId = row.getString("strategy_id");
        std::string parameters = row.getString("parameters");

        try {
            auto root = foundation::json::JsonFacade::parse(parameters);

            // 已有 account_id → 跳过
            if (root.has("account_id") && !root.get("account_id").asString().empty()) {
                ++result.skipped;
                continue;
            }

            // 无默认 accountId → 跳过 (等待用户手动配置)
            if (defaultAccountId.empty()) {
                ++result.skipped;
                INTERNAL_WARN_STREAM << "[DataMigrator] 无可用 accountId, 跳过策略: " << strategyId;
                continue;
            }

            // 注入 account_id
            root.set("account_id", foundation::json::JsonFacade::createString(defaultAccountId));
            std::string updated = root.toString();

            int affected = db->executeUpdate(
                "UPDATE strategy SET parameters = ? WHERE strategy_id = ?",
                {astock::database::SqlParam{updated}, astock::database::SqlParam{strategyId}});

            if (affected > 0) {
                ++result.migrated;
                INTERNAL_INFO_STREAM << "[DataMigrator] accountId 已注入: " << strategyId
                                    << " → " << defaultAccountId;
            } else {
                ++result.failed;
                result.errors.push_back("UPDATE 未影响行: " + strategyId);
            }

        } catch (const std::exception& e) {
            ++result.failed;
            result.errors.push_back(std::string("策略 ") + strategyId + " 迁移异常: " + e.what());
            INTERNAL_ERROR_STREAM << "[DataMigrator] 策略迁移失败: " << strategyId
                                 << " — " << e.what();
        }
    }

    return result;
}

MigrationResult DataMigrator::run(const std::string& defaultAccountId) {
    INTERNAL_INFO_STREAM << "[DataMigrator] 开始迁移 v0.15→v0.16";

    MigrationResult result;
    result.schemaVersionBefore = readSchemaVersion();
    INTERNAL_INFO_STREAM << "[DataMigrator] 当前 schema_version: "
                         << (result.schemaVersionBefore.empty() ? "(空/首次运行)" : result.schemaVersionBefore);

    // 已是当前版本 → 跳过
    if (result.schemaVersionBefore == kCurrentVersion) {
        INTERNAL_INFO_STREAM << "[DataMigrator] schema 已是最新版本 " << kCurrentVersion << ", 跳过迁移";
        result.schemaVersionAfter = kCurrentVersion;
        return result;
    }

    // 确保版本表存在
    if (!ensureSchemaVersionTable()) {
        result.errors.push_back("无法创建 _schema_version 表");
        result.failed = 1;
        return result;
    }

    // ── 迁移步骤 ──
    // Step 1: accountId 填充
    auto accResult = migrateAccountId(defaultAccountId);
    result.totalStrategies = accResult.totalStrategies;
    result.migrated = accResult.migrated;
    result.skipped = accResult.skipped;
    result.failed = accResult.failed;
    result.errors = std::move(accResult.errors);

    // 迁移原子性: 失败率 > 10% → 不更新版本号, 下次启动重试
    if (result.shouldAbort()) {
        INTERNAL_ERROR_STREAM << "[DataMigrator] 迁移失败率超过10% ("
                             << result.failed << "/" << result.totalStrategies
                             << "), 拒绝更新 schema_version, 下次启动重试";
        result.schemaVersionAfter = result.schemaVersionBefore;
        return result;
    }

    // 写入新版本号
    if (writeSchemaVersion(kCurrentVersion)) {
        result.schemaVersionAfter = kCurrentVersion;
    } else {
        result.errors.push_back("写入 schema_version 失败");
        result.schemaVersionAfter = result.schemaVersionBefore;
    }

    INTERNAL_INFO_STREAM << result.summary();
    return result;
}

} // namespace database
} // namespace infrastructure
} // namespace astock
