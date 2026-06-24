// DatabaseConnectionManager.h
// 数据库连接管理器 - 单例模式，管理全局数据库连接
#pragma once

#include "../../../infrastructure/include/database/NativePgDatabase.h"
#include "../../../infrastructure/include/database/DatabaseConfig.h"
#include "../../../foundation/include/foundation.h"
#include <memory>
#include <mutex>
#include <QDebug>
#include <QtGlobal>

namespace {

inline bool verboseDatabasePoolLogging()
{
    return qEnvironmentVariableIntValue("ASTOCK_VERBOSE_DB_POOL") > 0;
}

}

namespace astock {
namespace database {

// FROZEN INTERFACE - 数据库连接管理器
// 此接口已冻结，不再添加新的公共方法
// 如需扩展功能，请创建新的DatabaseService类
class DatabaseConnectionManager {
public:
    // 单例访问
    static DatabaseConnectionManager& instance() {
        // 进程生命周期单例，避免 Qt SQL/连接池在静态析构阶段出现销毁顺序竞态。
        static DatabaseConnectionManager* instance = new DatabaseConnectionManager();
        return *instance;
    }
    
    // 获取纯 C++ 数据库连接 (基于 libpq，零 Qt 依赖)
    std::shared_ptr<astock::database::ISqlDatabase> getNativeConnection() {
        std::lock_guard<std::mutex> lock(m_mutex);
        try {
            DatabaseConfig config = readDatabaseConfig();
            auto db = std::make_shared<astock::database::NativePgDatabase>(config);
            if (db->isOpen()) return db;
        } catch (const std::exception& e) {
            fprintf(stderr, "[DB] PG connect failed: %s\n", e.what());
            fflush(stderr);
        }
        return nullptr;
    }

    bool initialize() { return true; }
    void close() {}
    bool isConnected() const { return true; }

private:
    DatabaseConnectionManager() {}
    ~DatabaseConnectionManager() {}
    DatabaseConnectionManager(const DatabaseConnectionManager&) = delete;
    DatabaseConnectionManager& operator=(const DatabaseConnectionManager&) = delete;
    mutable std::mutex m_mutex;

    static DatabaseConfig readDatabaseConfig() {
        DatabaseConfig config;
        try {
            auto& configManager = foundation::config::ConfigManager::instance();
            config.host     = configManager.get_app_config_string("pg.host", "127.0.0.1");
            config.port     = configManager.get_app_config_int("pg.port", 5432);
            config.database = configManager.get_app_config_string("pg.database", "astock_quant");
            config.username = configManager.get_app_config_string("pg.user", "astock");
            config.password = configManager.get_app_config_string("pg.password", "astock123");
            config.charset  = "utf8";
        } catch (...) {
            config.host     = "127.0.0.1";
            config.port     = 5432;
            config.database = "astock_quant";
            config.username = "astock";
            config.password = "astock123";
            config.charset  = "utf8";
        }
        return config;
    }
};

} // namespace database
} // namespace astock