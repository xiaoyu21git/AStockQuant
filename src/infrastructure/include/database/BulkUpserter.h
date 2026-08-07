// BulkUpserter.h — 批量 UPSERT 工具类
// 消除 PostMarketSyncService 中各 sync 方法重复的 batch + flush lambda 模式
#pragma once

#include "ISqlDatabase.h"
#include <memory>
#include <string>
#include <vector>

namespace astock {
namespace database {

/// @brief 批量 SQL 执行器，析构时自动 flush 剩余批次
///
/// 用法:
///   BulkUpserter up(db.get(), "INSERT INTO t VALUES(?,?)", 500);
///   up.addRow({SqlParam{1}, SqlParam{"a"}});
///   // 批次满 500 时自动 flush，析构时 flush 剩余
class BulkUpserter {
public:
    /// @param db      数据库连接（非拥有）
    /// @param sql     参数化 SQL 模板
    /// @param batchSize 批次大小，默认 500
    BulkUpserter(ISqlDatabase* db, std::string sql, std::size_t batchSize = 500)
        : m_db(db), m_sql(std::move(sql)), m_batchSize(batchSize)
    {
        m_batch.reserve(batchSize);
    }

    ~BulkUpserter() {
        flush();
    }

    BulkUpserter(const BulkUpserter&) = delete;
    BulkUpserter& operator=(const BulkUpserter&) = delete;

    /// @brief 添加一行参数
    void addRow(std::vector<SqlParam> row) {
        m_batch.push_back(std::move(row));
        if (m_batch.size() >= m_batchSize) {
            flush();
        }
    }

    /// @brief 手动 flush 当前批次
    void flush() {
        if (m_batch.empty() || !m_db) return;
        for (auto& params : m_batch) {
            m_db->executeUpdate(m_sql, params);
        }
        m_batch.clear();
    }

    [[nodiscard]] std::size_t pending() const {
        return m_batch.size();
    }

private:
    ISqlDatabase* m_db;
    std::string m_sql;
    std::size_t m_batchSize;
    std::vector<std::vector<SqlParam>> m_batch;
};

} // namespace database
} // namespace astock
