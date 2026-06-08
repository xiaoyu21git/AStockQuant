#include "database/NativeMySQLConnectionPool.h"
#include "database/NativeMySQLDatabase.h"

#include <mysql.h>

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace astock {
namespace database {

#define POOL_TRACE(msg) do { fprintf(stderr, "%s\n", msg); fflush(stderr); } while(0)

static bool s_mySQLibraryInitialized = false;
static std::mutex s_mySQLibraryMutex;

static void ensureMySQLLibraryInit()
{
    std::lock_guard<std::mutex> lock(s_mySQLibraryMutex);
    if (s_mySQLibraryInitialized) return;
    if (mysql_library_init(0, nullptr, nullptr) != 0) {
        fprintf(stderr, "[POOL] mysql_library_init FAILED\n");
        fflush(stderr);
        return;
    }
    s_mySQLibraryInitialized = true;
    fprintf(stderr, "[POOL] mysql_library_init OK\n");
    fflush(stderr);
}

NativeMySQLConnectionPool& NativeMySQLConnectionPool::instance()
{
    static NativeMySQLConnectionPool pool;
    return pool;
}

NativeMySQLConnectionPool::NativeMySQLConnectionPool()
{
    POOL_TRACE("[POOL] ctor");
}

NativeMySQLConnectionPool::~NativeMySQLConnectionPool()
{
    POOL_TRACE("[POOL] dtor");
    shutdown();
}

bool NativeMySQLConnectionPool::initialize(const DatabaseConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return true;

    // 确保 MySQL client library 已初始化（线程安全，必须在使用任何 MySQL API 之前调用）
    ensureMySQLLibraryInit();

    config_ = config;
    initialized_ = true;
    POOL_TRACE("[POOL] initialize OK");
    return true;
}

std::shared_ptr<ISqlDatabase> NativeMySQLConnectionPool::getConnection()
{
    POOL_TRACE("[POOL] getConnection START");
    if (!initialized_) {
        POOL_TRACE("[POOL] getConnection NOT INITIALIZED");
        return nullptr;
    }

    std::thread::id tid = std::this_thread::get_id();

    // 先尝试复用已有连接（短锁）
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& conn : connections_) {
            if (conn.threadId == tid) {
                if (conn.db && conn.db->isOpen()) {
                    POOL_TRACE("[POOL] getConnection reuse existing");
                    return conn.db;
                }
                // 当前线程的连接已断开，标记清除
                POOL_TRACE("[POOL] getConnection reconnect (was dead)");
                conn.db.reset();
            }
        }
    }

    // 在锁外创建新连接（mysql_real_connect 可能耗时数秒，不应长时间持有互斥锁）
    auto db = createConnection();
    if (!db || !db->isOpen()) {
        POOL_TRACE("[POOL] getConnection create FAILED");
        return nullptr;
    }

    // 将新连接注册到池中（短锁）
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 清理所有已断开连接的槽位（包括之前标记清除的本线程旧连接）
        connections_.erase(
            std::remove_if(connections_.begin(), connections_.end(),
                [](const PooledConnection& c) { return !c.db || !c.db->isOpen(); }),
            connections_.end());

        // 再次检查是否有其他线程恰好为本线程创建了连接（极端竞态）
        for (auto& conn : connections_) {
            if (conn.threadId == tid && conn.db && conn.db->isOpen()) {
                POOL_TRACE("[POOL] getConnection race resolved, reuse others");
                return conn.db;
            }
        }

        PooledConnection pc;
        pc.db = db;
        pc.threadId = tid;
        connections_.push_back(std::move(pc));
        POOL_TRACE("[POOL] getConnection created OK");
        return db;
    }
}

void NativeMySQLConnectionPool::shutdown()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& conn : connections_) {
        if (conn.db) conn.db->close();
    }
    connections_.clear();
    initialized_ = false;
}

bool NativeMySQLConnectionPool::isInitialized() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

size_t NativeMySQLConnectionPool::activeConnectionCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& conn : connections_) {
        if (conn.db && conn.db->isOpen()) ++count;
    }
    return count;
}

std::string NativeMySQLConnectionPool::statusReport() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << "NativeMySQLConnectionPool[init=" << (initialized_ ? "yes" : "no")
       << " total=" << connections_.size()
       << " active=" << activeConnectionCount() << "]";
    return ss.str();
}

std::shared_ptr<ISqlDatabase> NativeMySQLConnectionPool::createConnection()
{
    POOL_TRACE("[POOL] createConnection START");
    try {
        auto db = std::make_shared<NativeMySQLDatabase>(config_);
        if (!db) {
            POOL_TRACE("[POOL] createConnection null shared_ptr");
            return nullptr;
        }
        if (!db->isOpen()) {
            fprintf(stderr, "[POOL] createConnection isOpen=false: %s\n", db->lastError().c_str());
            fflush(stderr);
            return nullptr;
        }
        POOL_TRACE("[POOL] createConnection OK");
        return db;
    } catch (const std::exception& e) {
        fprintf(stderr, "[POOL] createConnection EXCEPTION: %s\n", e.what());
        fflush(stderr);
    } catch (...) {
        POOL_TRACE("[POOL] createConnection UNKNOWN EXCEPTION");
    }
    return nullptr;
}

#undef POOL_TRACE

} // namespace database
} // namespace astock