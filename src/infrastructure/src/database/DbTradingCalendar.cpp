#include "database/DbTradingCalendar.h"
#include "database/MarketDataRepository.h"
#include "database/NativePgConnectionPool.h"
#include "foundation/log/logging.hpp"

#include <chrono>
#include <ctime>
#include <string>

namespace astock::infrastructure::database {

std::int64_t DbTradingCalendar::currentTradingDay()
{
    // 完全迁移自 Facade startLiveLoop 交易日查询 lambda:
    // 从 DB trade_calendar 表查，DB不可用直接报错不兜底
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) {
        INTERNAL_ERROR_STREAM << "[DailyEod] DB连接不可用，无法查询当前交易日";
        return 0;
    }
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    struct tm local;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &tt);
#else
    localtime_r(&tt, &local);
#endif
    std::int64_t today = (local.tm_year + 1900) * 10000LL
                       + (local.tm_mon + 1) * 100LL
                       + local.tm_mday;
    MarketDataRepository repo(db);
    if (repo.isTradingDay(std::to_string(today)))
        return today;
    auto prev = repo.queryPrevTradingDay(std::to_string(today));
    if (!prev.empty())
        return std::stoll(prev);
    INTERNAL_ERROR_STREAM << "[DailyEod] trade_calendar 查不到" << today << "的交易日";
    return 0;
}

std::string DbTradingCalendar::previousTradingDay(const std::string& date)
{
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) {
        INTERNAL_ERROR_STREAM << "[DailyEod] DB连接不可用，无法查询上一交易日";
        return {};
    }
    MarketDataRepository repo(db);
    return repo.queryPrevTradingDay(date);
}

} // namespace astock::infrastructure::database
