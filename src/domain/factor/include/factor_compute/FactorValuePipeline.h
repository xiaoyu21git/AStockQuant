#pragma once

#include "factor_compute/FactorEngine.h"
#include "factor_compute/IMarketDataView.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace factor::compute {

class ArrowMarketDataView;
class ArrowReaderHandle;

/// @brief 回看数据加载器 — PG 按字段懒加载 + 日期/静态/财报语义查询
///
/// 因子值管线与策略日循环视图的 DB 回看唯一入口 (因子回测/策略回测共用一份)。
/// 每个字段首次查询时一次性拉回 [minReportDate, cacheStartDate] 全区间并缓存,
/// 后续查询纯内存命中。必须由 shared_ptr 持有 (dbFallbackFn 回调捕获自身所有权)。
/// 块级并行时由主线程预载全部字段 + 多 worker 共享同一 loader: 缓存读写经
/// shared_mutex (读共享/写独占, PG 查询被写锁自然串行化)。
class WarmupDataLoader : public std::enable_shared_from_this<WarmupDataLoader> {
public:
    using CacheMap = std::unordered_map<std::string,                 // field
        std::unordered_map<std::string, std::map<std::string, double>>>;  // symbol → date → value

    /// @param firstDateVal 数据集首日 (YYYYMMDD int) — minReportDate/cacheStartDate 锚点
    /// @param maxLookback  最大回看交易日数
    WarmupDataLoader(std::int32_t firstDateVal, int maxLookback);

    /// @brief 惰性 dbFallback 回调
    /// 回调持有 loader 的 shared_ptr 所有权, loader 释放后回调仍可安全调用 (缓存常驻)
    [[nodiscard]] BacktestDataService::DbFallbackFn dbFallbackFn();

    /// @brief 字段级懒加载: 首次查询时从 PG 拉回全区间并写入缓存 (幂等, __loaded__ 标记)
    /// 主线程预载 (块级并行前全字段) + 运行期 dbFallback 兜底调用; 内部 mutex 双检
    void ensureFieldLoaded(const std::string& field,
                           const std::vector<std::string>& symbols);

private:
    /// @brief 字段分类 — 决定 PG 查询方式与缓存查询语义
    enum class FieldClass { SymbolInfo, Financial, Kline, Index, Minute, Unknown };
    [[nodiscard]] static FieldClass classifyField(const std::string& field);

    /// @brief 从缓存取某日截面 (静态取 "_", 财报取公告日最近, 其余精确日期)
    [[nodiscard]] std::unordered_map<std::string, double> lookupField(
        const std::string& date, const std::string& field,
        const std::vector<std::string>& symbols) const;

    std::shared_ptr<CacheMap> m_dbCache;
    mutable std::shared_mutex m_cacheMutex;  // ensureFieldLoaded 写 / lookupField 读
    std::string m_minReportDate;
    std::string m_cacheStartDate;
};

/// @brief 回看扩展视图构建器 — [warmupDates(数据集前 DB 回看)] + [arrowPrefix(数据集内回看)] + [dates] + [tailDates]
///
/// warmupDates 在数据集中不存在, makeChunkView 铺 NaN 后从 PG 逐日覆写 (仅管线首块);
/// arrowPrefix/dates/tailDates 为数据集已有交易日, 由 makeChunkView 直接从 Arrow 填充
/// (块 N>0 的回看行 — 消除跨块回看缺口)。
/// 管线首块 (回看=maxLookback) 与策略日循环视图 (回看=90) 共用此构建器。
class WarmupViewBuilder {
public:
    WarmupViewBuilder(const ArrowMarketDataView& arrowView,
                      std::shared_ptr<WarmupDataLoader> loader);

    /// @param dates            目标交易日序列
    /// @param tailDates        尾部扩展交易日 (IC 前向收益用, 因子值不产出)
    /// @param fields           需要物化的列名
    /// @param warmupDays       数据集前 DB 回看交易日数 (0 = 不查 DB)
    /// @param arrowPrefixDates 数据集内回看交易日 (块 N>0 由调用方从数据集日期切出)
    /// @param prefixRowCountOut 前置行总数 = warmupDates + arrowPrefixDates (compute 跳过用)
    /// @param readerHandle     null = 串行路径 (共享 reader); 非 null = 并行 worker 独立句柄
    /// @return 扩展视图, 构建失败返回 nullptr
    [[nodiscard]] std::unique_ptr<IMarketDataView> build(
        const std::vector<DateKey>& dates,
        const std::vector<DateKey>& tailDates,
        const std::vector<std::string>& fields,
        int warmupDays,
        const std::vector<DateKey>& arrowPrefixDates,
        std::size_t& prefixRowCountOut,
        const ArrowReaderHandle* readerHandle = nullptr) const;

private:
    /// @brief 从目标首日往前, data.trade_calendar 查 warmupDays 个交易日
    [[nodiscard]] std::vector<DateKey> queryWarmupDates(
        std::int32_t firstDateVal, int warmupDays) const;

    /// @brief 回看行 (数据集不存在) 从 DB 逐日逐列覆写
    void overwriteWarmupRows(IMarketDataView& view,
                             const std::vector<DateKey>& warmupDates,
                             const std::vector<std::string>& fields) const;

    const ArrowMarketDataView& m_arrowView;
    std::shared_ptr<WarmupDataLoader> m_loader;
};

/// @brief 因子值管线 — 回测因子计算的唯一实现 (因子回测/策略回测共用)
///
/// 分块编排: 每块 [回看(首块=DB, 后续块=Arrow 前缀)+本块+尾部扩展] 视图
/// → FactorEngine::compute(skipDates=前置行数) → 仅保留本块交易日因子值经 sink 产出。
/// 块视图在 sink 回调期间有效, 消费方 (IC/交易分析) 在回调内读取 close 等矩阵,
/// 块出作用域即释放内存。
class FactorValuePipeline {
public:
    using FactorValuesByDate = std::map<std::string, std::map<std::string, double>>;  // date → symbol → value

    struct ChunkOutput {
        std::size_t chunkIndex{0};
        const std::vector<DateKey>* chunkDates{nullptr};   // 本块交易日 (不含回看/尾部扩展)
        const std::vector<DateKey>* tailDates{nullptr};    // 尾部扩展交易日
        const IMarketDataView* chunkView{nullptr};          // 回看+本块+扩展视图, 回调期间有效
        const std::unordered_map<std::string, FactorValuesByDate>* perFactorValues{nullptr};
    };
    using ChunkSink = std::function<void(const ChunkOutput&)>;
    using ProgressFn = std::function<void(double, const std::string&)>;  // (0~1 块进度, 状态文本)

    FactorValuePipeline(FactorEngine& engine, BacktestDataService& dataSvc);

    /// @brief 分块计算全部因子实例的按日值
    /// @param arrowView   数据集视图 (makeChunkView + symbolStrings 来源)
    /// @param dates       交易日序列 (调用方已完成窗口/交易日历过滤)
    /// @param factorIds   因子实例 ID 列表
    /// @param forwardDays 块尾部扩展交易日 (0 = 无扩展)
    /// @param sink        每块回调 (同步, 回调返回后块视图即销毁; 并行模式下主线程按块序串行调用)
    /// @param onProgress  进度回调 — 按"已完成 sink 的块数"计
    /// @param workerThreads 块级并行 worker 数 (<2 或块数<2 走串行路径, 同一套代码)
    /// @param cancelFlag  软取消标志 (每日期循环间检查; 取消后未 sink 块丢弃, 提前返回)
    void run(const ArrowMarketDataView& arrowView,
             const std::vector<DateKey>& dates,
             const std::vector<std::string>& factorIds,
             int forwardDays,
             ChunkSink sink,
             ProgressFn onProgress = {},
             int workerThreads = 1,
             const std::atomic<bool>* cancelFlag = nullptr);

private:
    /// @brief 收集因子所需额外字段 (去核心 5 列) + 最大回看交易日
    void collectFieldRequirements(const std::vector<std::string>& factorIds,
                                  std::vector<std::string>& extraFieldsOut,
                                  int& maxLookbackOut) const;

    /// @brief 分块加载列名: 核心 5 列 + 因子字段 + 基类中性化字段 (去重)
    [[nodiscard]] static std::vector<std::string> buildChunkColumns(
        const std::vector<std::string>& extraFields);

    FactorEngine& m_engine;
    BacktestDataService& m_dataSvc;
};

} // namespace factor::compute
