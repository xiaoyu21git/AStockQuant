// ConnectionGuard.h — DB 连接 RAII 守卫
// 消除重复的 getConnection() + isOpen() 检查模式
#pragma once

#include "ISqlDatabase.h"
#include "NativePgConnectionPool.h"
#include <memory>
#include <utility>

namespace astock {
namespace database {

/// @brief RAII 数据库连接守卫
/// 自动从连接池获取连接并检查有效性，析构时归还连接
///
/// 用法:
///   ConnectionGuard conn;
///   if (!conn.isValid()) return false;
///   conn->executeUpdate(sql);
class ConnectionGuard {
public:
    ConnectionGuard()
        : m_db(NativePgConnectionPool::instance().getConnection())
    {}

    explicit ConnectionGuard(std::shared_ptr<ISqlDatabase> db)
        : m_db(std::move(db))
    {}

    [[nodiscard]] bool isValid() const {
        return m_db && m_db->isOpen();
    }

    [[nodiscard]] ISqlDatabase* operator->() const noexcept {
        return m_db.get();
    }

    [[nodiscard]] ISqlDatabase& operator*() const {
        return *m_db;
    }

    [[nodiscard]] const std::shared_ptr<ISqlDatabase>& get() const {
        return m_db;
    }

    [[nodiscard]] explicit operator bool() const {
        return isValid();
    }

private:
    std::shared_ptr<ISqlDatabase> m_db;
};

} // namespace database
} // namespace astock
