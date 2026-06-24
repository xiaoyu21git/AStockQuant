#include "database/NativeMySQLConnectionPool.h"
#include "database/NativePgDatabase.h"

#include <sstream>

namespace astock {
namespace database {

NativeMySQLConnectionPool& NativeMySQLConnectionPool::instance()
{
    static NativeMySQLConnectionPool pool;
    return pool;
}

NativeMySQLConnectionPool::NativeMySQLConnectionPool() = default;
NativeMySQLConnectionPool::~NativeMySQLConnectionPool() { shutdown(); }

bool NativeMySQLConnectionPool::initialize(const DatabaseConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return true;
    config_ = config;
    initialized_ = true;
    return true;
}

std::shared_ptr<ISqlDatabase> NativeMySQLConnectionPool::getConnection()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        DatabaseConfig cfg;
        cfg.host = "127.0.0.1"; cfg.port = 5432;
        cfg.database = "astock_quant"; cfg.username = "astock";
        cfg.password = "astock123"; cfg.charset = "utf8";
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
}

void NativeMySQLConnectionPool::shutdown()
{
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.clear(); initialized_ = false;
}

bool NativeMySQLConnectionPool::isInitialized() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

size_t NativeMySQLConnectionPool::activeConnectionCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

std::string NativeMySQLConnectionPool::statusReport() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << "NativeMySQLConnectionPool[PG init=" << (initialized_ ? "yes" : "no")
       << " conns=" << connections_.size() << "]";
    return ss.str();
}

std::shared_ptr<ISqlDatabase> NativeMySQLConnectionPool::createConnection()
{
    return std::make_shared<NativePgDatabase>(config_);
}

} // namespace database
} // namespace astock
