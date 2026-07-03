#pragma once
// PostMarketSyncService — 盘后全市场数据同步
// 调度线程 sleep_until(15:01) 触发, 启动检查兜底, 人工 forceSyncToday 应急

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace astock::database { class ISqlDatabase; }
namespace astock::infrastructure::database {

class PostMarketSyncService {
public:
    PostMarketSyncService();
    ~PostMarketSyncService();

    /// @brief 启动调度线程 (非交易日跳过)
    void start();

    /// @brief 人工触发当日同步, 返回 false 表示已在运行
    bool forceSyncToday();

    /// @brief 查询某交易日同步状态: "idle"/"running"/"success"/"fail"
    std::string getSyncStatus(int tradingDay) const;

private:
    void schedulerLoop();
    void syncAll(int tradingDay);

    bool syncDaily(std::shared_ptr<astock::database::ISqlDatabase> db,
                   const std::unordered_map<std::string,int>& symToId,
                   const std::vector<std::string>& symbols, int tradingDay);
    bool syncMinute(std::shared_ptr<astock::database::ISqlDatabase> db,
                    const std::unordered_map<std::string,int>& symToId,
                    const std::vector<std::string>& symbols, int tradingDay);
    bool syncWeekly(std::shared_ptr<astock::database::ISqlDatabase> db, int tradingDay);
    bool syncMonthly(std::shared_ptr<astock::database::ISqlDatabase> db, int tradingDay);

    void logTaskStart(const std::string& taskType, int tradingDay);
    void logTaskEnd(const std::string& taskType, int tradingDay, bool success,
                    int rows, const std::string& error = "");

    int getCurrentLocalMinutes();
    int getCurrentTradingDay();
    bool isTradingDay(int date);
    std::string toGmSymbol(const std::string& sym);

    std::unique_ptr<std::thread> m_scheduler;
    std::atomic<bool> m_running{false};
    std::mutex m_mutex;
    bool m_started = false;
};

} // namespace astock::infrastructure::database
