#ifndef ASTOCK_INFRASTRUCTURE_DATABASE_MARKETDATAREPOSITORY_H
#define ASTOCK_INFRASTRUCTURE_DATABASE_MARKETDATAREPOSITORY_H

#include "IRepository.h"
#include "MarketDataModels.h"
#include "QtMySQLDatabase.h"
#include <vector>
#include <optional>
#include <memory>

namespace astock {
namespace database {

class MarketDataRepositoryTestAccess;

/**
 * @brief 市场数据仓储
 * 
 * 提供统一的市场数据CRUD接口
 */
class MarketDataRepository {
public:
    explicit MarketDataRepository(std::shared_ptr<QtMySQLDatabase> database);
    ~MarketDataRepository() = default;
    
    // ============ Symbol Info 操作 ============
    
    /**
     * @brief 保存或更新标的信息
     */
    bool saveSymbol(const SymbolInfo& symbol);
    
    /**
     * @brief 获取标的信息
     */
    std::optional<SymbolInfo> getSymbol(const std::string& symbol);
    
    /**
     * @brief 获取所有标的
     * @param symbol_type 标的类型过滤（空表示全部）
     * @param status 状态过滤
     */
    std::vector<SymbolInfo> getAllSymbols(
        std::optional<SymbolType> symbol_type = std::nullopt,
        const std::string& status = "active");
    
    // ============ Daily Bar 操作 ============
    
    /**
     * @brief 批量保存日线数据
     * @return 保存的记录数
     */
    size_t saveDailyBars(const std::vector<DailyBar>& bars);
    
    /**
     * @brief 获取日线数据
     * @param symbol 标的代码
     * @param start_date 开始日期
     * @param end_date 结束日期
     */
    std::vector<DailyBar> getDailyBars(
        const std::string& symbol,
        std::time_t start_date,
        std::time_t end_date);
    
    /**
     * @brief 获取最新日线数据
     */
    std::optional<DailyBar> getLatestBar(const std::string& symbol);
    
    // ============ Minute Bar 操作 ============
    
    /**
     * @brief 批量保存分钟线数据
     * @param bars 分钟线数据
     * @param frequency 频率（1/5/15/30/60）
     */
    size_t saveMinuteBars(const std::vector<MinuteBar>& bars);
    
    /**
     * @brief 获取分钟线数据
     */
    std::vector<MinuteBar> getMinuteBars(
        const std::string& symbol,
        std::time_t start_datetime,
        std::time_t end_datetime,
        int frequency = 1);
    
    // ============ Tick Data 操作 ============
    
    /**
     * @brief 批量保存Tick数据
     */
    size_t saveTickData(const std::vector<TickData>& ticks);
    
    /**
     * @brief 获取Tick数据
     */
    std::vector<TickData> getTickData(
        const std::string& symbol,
        std::time_t start_datetime,
        std::time_t end_datetime);

    // ============ 衍生数据：资金流向 & 龙虎榜 ============

    /**
     * @brief 获取日度资金流向数据
     */
    std::vector<MoneyFlowDaily> getMoneyFlowDaily(
        const std::string& symbol,
        std::time_t start_date,
        std::time_t end_date);

    /**
     * @brief 获取日度龙虎榜上榜记录
     */
    std::vector<DragonTigerRecord> getDragonTigerRecords(
        const std::string& symbol,
        std::time_t start_date,
        std::time_t end_date);
    
    // ============ 工具方法 ============
    
    /**
     * @brief 执行原始SQL查询
     */
    bool executeQuery(const std::string& sql);
    
    /**
     * @brief 开始事务
     */
    bool beginTransaction();
    
    /**
     * @brief 提交事务
     */
    bool commit();
    
    /**
     * @brief 回滚事务
     */
    bool rollback();

    friend class MarketDataRepositoryTestAccess;
    
private:
    std::shared_ptr<QtMySQLDatabase> database_;
    
    /**
     * @brief 从查询结果构建SymbolInfo
     */
    SymbolInfo buildSymbolInfo(const QueryResultRow& row);
    
    /**
     * @brief 从查询结果构建DailyBar
     */
    DailyBar buildDailyBar(const QueryResultRow& row);
    
    /**
     * @brief 从查询结果构建MinuteBar
     */
    MinuteBar buildMinuteBar(const QueryResultRow& row);
    
    /**
     * @brief 从查询结果构建TickData
     */
    TickData buildTickData(const QueryResultRow& row);

    /**
     * @brief 从查询结果构建 MoneyFlowDaily
     */
    MoneyFlowDaily buildMoneyFlowDaily(const QueryResultRow& row);

    /**
     * @brief 从查询结果构建 DragonTigerRecord
     */
    DragonTigerRecord buildDragonTigerRecord(const QueryResultRow& row);
    
    /**
     * @brief 将time_t转换为QString日期格式
     */
    QString timeToDateString(std::time_t time);
    
    /**
     * @brief 将time_t转换为QString日期时间格式
     */
    QString timeToDateTimeString(std::time_t time);
};

class MarketDataRepositoryTestAccess {
public:
    static DailyBar buildDailyBar(MarketDataRepository& repository, const QueryResultRow& row) {
        return repository.buildDailyBar(row);
    }
};

} // namespace database
} // namespace astock

#endif // ASTOCK_INFRASTRUCTURE_DATABASE_MARKETDATAREPOSITORY_H
