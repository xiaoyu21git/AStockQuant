#pragma once
// PostMarketSyncService — 盘后全市场数据同步
// 调度线程 sleep_until(15:01) 触发, 启动检查兜底, 人工 forceSyncToday 应急

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

namespace astock::database { class ISqlDatabase; }
namespace astock::infrastructure::database {

class PostMarketSyncService {
public:
    static PostMarketSyncService& instance();

    PostMarketSyncService();
    ~PostMarketSyncService();

    /// @brief 启动调度线程 (非交易日跳过)
    void start();

    /// @brief 人工触发当日同步, 返回 false 表示已在运行
    bool forceSyncToday();

    /// @brief 人工触发指定交易日同步（日线+分钟线+周月线聚合）
    /// 用于补历史缺口，不检查 m_lastSyncDay
    void forceSyncDate(int tradingDay);

    /// @brief 自动检测并补齐最近 N 天的历史缺口
    /// 扫描交易日历与 daily_bar 的差异，对缺失/不完整的日期逐一调用 syncDailyMinute
    /// @param lookbackDays 回溯天数，默认 30
    void forceSyncMissingDays(int lookbackDays = 30);

    /// @brief 查询某交易日同步状态: "idle"/"running"/"success"/"fail"
    std::string getSyncStatus(int tradingDay) const;

    /// @brief 探测掘金数据覆盖：对一个标的测多个日期，日志输出结果
    void probeGmCoverage(const std::string& symbol, const std::vector<std::string>& dates);

    /// @brief 补全 GMSDK 数据的复权因子（pre_adjust_factor / post_adjust_factor）
    void fillAdjFactors();

private:
    void schedulerLoop();

    // 频率分层
    void syncAll(int tradingDay);
    void syncDailyMinute(int tradingDay);
    void syncWeeklyMonthly(int tradingDay);
    void syncFinancialData(int tradingDay);

    bool isWeeklyMaintenanceDay();
    bool isMonthlyMaintenanceDay();

    bool syncDaily(std::shared_ptr<astock::database::ISqlDatabase> db,
                   const std::unordered_map<std::string,int>& symToId,
                   const std::vector<std::string>& symbols, int tradingDay);
    bool syncDailyRange(std::shared_ptr<astock::database::ISqlDatabase> db,
                   const std::unordered_map<std::string,int>& symToId,
                   const std::vector<std::string>& symbols,
                   int startDay, int endDay,
                   const std::vector<std::string>& targetDates);
    bool syncMinute(std::shared_ptr<astock::database::ISqlDatabase> db,
                    const std::unordered_map<std::string,int>& symToId,
                    const std::vector<std::string>& symbols, int tradingDay);
    bool syncWeekly(std::shared_ptr<astock::database::ISqlDatabase> db, int tradingDay);
    bool syncMonthly(std::shared_ptr<astock::database::ISqlDatabase> db, int tradingDay);
    bool syncFinancial(std::shared_ptr<astock::database::ISqlDatabase> db,
                       const std::unordered_map<std::string,int>& symToId,
                       const std::vector<std::string>& symbols, int tradingDay);

    void syncValuation(std::shared_ptr<astock::database::ISqlDatabase> db,
                       const std::unordered_map<std::string,int>& symToId,
                       const std::vector<std::string>& symbols, int tradingDay);

    void logTaskStart(const std::string& taskType, int tradingDay);
    void logTaskEnd(const std::string& taskType, int tradingDay, bool success,
                    int rows, const std::string& error = "");

    int getCurrentLocalMinutes();
    int getCurrentTradingDay();
    bool isTradingDay(int date);
    std::string toGmSymbol(const std::string& sym);

    std::unique_ptr<std::thread> m_scheduler;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_syncRunning{false};  // 手动同步正在执行，防止并发
    std::atomic<int> m_lastSyncDay{0};
    std::mutex m_mutex;
    bool m_started = false;

    bool todayDataExists(int tradingDay);
    void fillHistoryGaps();
    void syncTradeCalendar();
    void syncIndexConstituents();
    void loadLastSyncDay();
    void saveLastSyncDay(int tradingDay);
    std::string m_persistPath;  // 持久化文件路径
};

} // namespace astock::infrastructure::database
