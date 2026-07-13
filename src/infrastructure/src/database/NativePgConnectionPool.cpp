#include "database/NativePgConnectionPool.h"
#include "database/NativePgDatabase.h"
#include "foundation/config/ConfigManager.hpp"
#include "foundation/log/logging.hpp"

#include <sstream>

namespace astock {
namespace database {

NativePgConnectionPool& NativePgConnectionPool::instance()
{
    static NativePgConnectionPool pool;
    return pool;
}

NativePgConnectionPool::NativePgConnectionPool() = default;
NativePgConnectionPool::~NativePgConnectionPool() { shutdown(); }

bool NativePgConnectionPool::initialize(const DatabaseConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return true;
    config_ = config;
    initialized_ = true;
    return true;
}

std::shared_ptr<ISqlDatabase> NativePgConnectionPool::getConnection()
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            auto& cfgMgr = foundation::config::ConfigManager::instance();
            DatabaseConfig cfg;
            cfg.host     = cfgMgr.get_app_config_string("pg.host", "127.0.0.1");
            cfg.port     = cfgMgr.get_app_config_int("pg.port", 5432);
            cfg.database = cfgMgr.get_app_config_string("pg.database", "astock_quant");
            cfg.username = cfgMgr.get_app_config_string("pg.user", "astock");
            cfg.password = cfgMgr.get_app_config_string("pg.password", "astock123");
            cfg.charset  = "utf8";
            INTERNAL_INFO_STREAM << "[Pool] PG: host='" << cfg.host << "' port=" << cfg.port
                                 << " db=" << cfg.database << " user=" << cfg.username;
            config_ = cfg; initialized_ = true;
        }
        std::thread::id tid = std::this_thread::get_id();
        for (auto& c : connections_) {
            if (c.threadId == tid) {
                if (c.db && c.db->isOpen()) return c.db;
                c.db = createConnection(); return c.db;
            }
        }
        auto db = createConnection();
        connections_.push_back({db, tid});
        return db;
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[Pool] getConnection exception: " << e.what();
        return nullptr;
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[Pool] getConnection unknown exception";
        return nullptr;
    }
}

void NativePgConnectionPool::shutdown()
{
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.clear(); initialized_ = false;
}

bool NativePgConnectionPool::isInitialized() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

size_t NativePgConnectionPool::activeConnectionCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

std::string NativePgConnectionPool::statusReport() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << "NativePgConnectionPool[PG init=" << (initialized_ ? "yes" : "no")
       << " conns=" << connections_.size() << "]";
    return ss.str();
}

std::shared_ptr<ISqlDatabase> NativePgConnectionPool::createConnection()
{
    return std::make_shared<NativePgDatabase>(config_);
}

} // namespace database
} // namespace astock
