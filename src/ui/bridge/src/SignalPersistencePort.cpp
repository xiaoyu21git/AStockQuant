// SignalPersistencePort.cpp — 信号持久化端口 PG 实现
// 异步批量写入 live.signal_history, 不阻塞策略主循环
#include "SignalPersistencePort.h"

#include "../../../domain/signal/include/SignalTypes.h"
#include "../../../infrastructure/include/database/ISqlDatabase.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"

#include <foundation/log/logging.hpp>
#include <foundation/utils/Uuid.h>

#include <ctime>
#include <future>
#include <iomanip>
#include <sstream>
#include <vector>

using domain::sigout::SignalOutput;
using astock::database::SqlParam;

namespace {

/// @brief ISO8601 timestamp → 北京时间日期 (YYYY-MM-DD)
std::string toBeijingDate(const std::string& iso8601) {
    if (iso8601.size() < 10) return iso8601;

    int year = std::stoi(iso8601.substr(0, 4));
    int month = std::stoi(iso8601.substr(5, 2));
    int day = std::stoi(iso8601.substr(8, 2));

    // UTC → 北京时间 (UTC+8)
    int hour = 0;
    if (iso8601.size() >= 13) {
        hour = std::stoi(iso8601.substr(11, 2));
    }
    hour += 8;
    if (hour >= 24) {
        hour -= 24;
        day += 1;
    }

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << year << "-"
        << std::setw(2) << month << "-"
        << std::setw(2) << day;
    return oss.str();
}

/// @brief 每行信号的参数个数 (不含 NOW())
constexpr int kParamsPerRow = 13;

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════

SignalPersistencePort::SignalPersistencePort() = default;

SignalPersistencePort::~SignalPersistencePort() {
    flush();
}

void SignalPersistencePort::saveAsync(const std::vector<SignalOutput>& signalList,
                                       const std::string& pushError) {
    if (signalList.empty()) return;

    // 异步提交写入任务, 不阻塞调用线程
    auto future = std::async(std::launch::async,
        [this, signalList, pushError]() {
            saveBatchSync(signalList, pushError);
        });

    // 清理已完成的 futures, 防止累积
    {
        std::lock_guard<std::mutex> lock(m_futuresMutex);
        m_pendingFutures.erase(
            std::remove_if(m_pendingFutures.begin(), m_pendingFutures.end(),
                [](const std::future<void>& f) {
                    return !f.valid() ||
                           f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                }),
            m_pendingFutures.end());
        m_pendingFutures.push_back(std::move(future));
    }
}

void SignalPersistencePort::flush() {
    std::vector<std::future<void>> pending;
    {
        std::lock_guard<std::mutex> lock(m_futuresMutex);
        pending = std::move(m_pendingFutures);
        m_pendingFutures.clear();
    }

    for (auto& f : pending) {
        if (f.valid()) {
            try {
                f.wait();
            } catch (const std::exception& e) {
                INTERNAL_ERROR_STREAM << "[SignalPersistencePort] flush future exception: "
                                      << e.what();
            } catch (...) {
                INTERNAL_ERROR_STREAM << "[SignalPersistencePort] flush future unknown exception";
            }
        }
    }
}

void SignalPersistencePort::saveBatchSync(const std::vector<SignalOutput>& signalList,
                                           const std::string& pushError) {
    auto& pool = astock::database::NativePgConnectionPool::instance();
    if (!pool.isInitialized()) {
        INTERNAL_ERROR_STREAM << "[SignalPersistencePort] DB pool not initialized, skip "
                              << signalList.size() << " signalList";
        return;
    }

    auto db = pool.getConnection();
    if (!db || !db->isOpen()) {
        INTERNAL_ERROR_STREAM << "[SignalPersistencePort] No DB connection, skip "
                              << signalList.size() << " signalList";
        return;
    }

    // 收集 traceIds 用于错误日志
    std::string traceIdsStr;
    {
        std::ostringstream oss;
        for (size_t i = 0; i < signalList.size() && i < 5; ++i) {
            if (i > 0) oss << ", ";
            oss << signalList[i].traceId;
        }
        if (signalList.size() > 5) oss << ", ... (" << signalList.size() << " total)";
        traceIdsStr = oss.str();
    }

    try {
        // 构建参数化批量 INSERT
        // SQL: INSERT INTO live.signal_history (col1, col2, ..., col14) VALUES (?,?,...,NOW(),...), (...), ...
        // 每行 13 个 ? 参数 + 1 个 NOW() (signal_time)
        std::ostringstream sql;
        sql << "INSERT INTO live.signal_history "
               "(signal_id, strategy_id, trading_day, symbol, full_symbol, "
               "intent, target_weight, score, signal_format, push_target, "
               "signal_time, trace_id, pushed, push_error) VALUES ";

        std::vector<SqlParam> params;
        params.reserve(signalList.size() * kParamsPerRow);

        size_t n = signalList.size();
        for (size_t i = 0; i < n; ++i) {
            if (i > 0) sql << ", ";

            const auto& s = signalList[i];
            std::string signalId = foundation::utils::Uuid::generate_v4().to_string();
            std::string tradingDay = toBeijingDate(s.timestamp);

            sql << "(?,?,?,?,?,?,?,?,?,?,NOW(),?,?,?)";

            // 按列顺序绑定参数
            params.emplace_back(signalId);                         // signal_id
            params.emplace_back(std::string{});                    // strategy_id (Phase 2)
            params.emplace_back(tradingDay);                       // trading_day
            params.emplace_back(s.symbol);                         // symbol
            params.emplace_back(s.symbol);                         // full_symbol
            params.emplace_back(static_cast<std::int32_t>(s.signalIntent)); // intent
            params.emplace_back(s.targetWeight);                   // target_weight
            params.emplace_back(s.score);                          // score
            params.emplace_back(std::string{"internal"});          // signal_format (Phase 2)
            params.emplace_back(std::string{"file"});              // push_target
            params.emplace_back(s.traceId);                        // trace_id
            params.emplace_back(static_cast<std::int32_t>(pushError.empty() ? 1 : 0)); // pushed
            params.emplace_back(pushError);                        // push_error
        }

        int affected = db->executeUpdate(sql.str(), params);
        if (affected <= 0) {
            INTERNAL_ERROR_STREAM << "[SignalPersistencePort] Batch insert failed for "
                                  << signalList.size() << " signalList, traceIds: " << traceIdsStr;
        } else {
            INTERNAL_DEBUG_STREAM << "[SignalPersistencePort] Batch insert OK: "
                                  << signalList.size() << " signalList persisted";
        }
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[SignalPersistencePort] Batch insert exception: "
                              << e.what() << " traceIds: " << traceIdsStr;
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[SignalPersistencePort] Batch insert unknown exception, "
                              << "traceIds: " << traceIdsStr;
    }
}
