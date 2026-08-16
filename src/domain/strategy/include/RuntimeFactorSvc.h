#pragma once

#include "IFactorSvc.h"
#include "IStrategyService.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace astock { namespace database { class SqlQueryResultRow; } }
namespace factor {
class FactorInstanceManager;
namespace compute {
class IMarketDataView;
class CachedMarketDataView;
class ArrowMarketDataView;
class BacktestDataService;
class FactorEngine;
}
}

namespace domain::strategy {

/// @brief 统一的因子服务 — 同时实现 IFactorSvc 和 IRuntimeFactorService
///
/// 回测: getValues()/backtestValuesBySymbol → FactorValuePipeline (与因子回测同一份计算实现);
/// 实盘: getValues() → FactorEngine::computeSingleDate。
/// 数据通过 setDataService (回测) 或 setLiveMarketView (实盘) 注入。
/// IRuntimeFactorService 的 updateIncremental/updateBatch/copySnapshots
/// 直接内聚于此，不再需要 CallbackRuntimeFactorServiceAdapter 和 buildFactorCallbacks。
class RuntimeFactorSvc final : public IFactorSvc,
                                public IRuntimeFactorService {
public:
    using SymbolResolver = std::function<std::string(std::uint32_t)>;
    using FactorNameResolver = std::function<std::string(std::uint64_t)>;

    RuntimeFactorSvc(factor::FactorInstanceManager& instanceManager,
                     SymbolResolver symbolResolver,
                     FactorNameResolver factorNameResolver);
    ~RuntimeFactorSvc() override;

    // ── 数据注入 ──
    void setMarketView(const factor::compute::IMarketDataView* view);  // 非接口方法，向后兼容
    void setDataService(factor::compute::BacktestDataService* dataSvc) override;

    /// @brief 设置实盘行情视图 (P3: 发布即持有 — shared_ptr 直接入库)
    void setLiveMarketView(
        std::shared_ptr<const factor::compute::IMarketDataView> view) override;

    /// @brief 设置活跃评估周期 (ADR-004 缓存分区维度; 默认 Daily)
    void setActivePeriod(BarPeriod period) override { m_activePeriod = period; }

    /// @brief 设置关注的因子实例 ID 列表（copySnapshots 迭代用）
    void setFactorIds(const std::vector<std::string>& factorIds) override;

    /// @brief 回测: 直取某因子某交易日的 symbol→value 全量映射 (首次访问触发全量计算并缓存)
    /// @param date YYYYMMDD 整数; 无数据返回 nullptr。key 为视图 symbolStrings (无交易所后缀)
    [[nodiscard]] const std::map<std::string, double>* backtestValuesBySymbol(
        const std::string& instanceId, std::int32_t date) const override;

    /// @brief 从因子需求收集所需的数据字段
    [[nodiscard]] std::vector<std::string> getRequiredFields() const override;

    /// @brief 从因子需求计算最大回溯窗口（交易日数，用于确定查多少天历史数据）
    [[nodiscard]] int getMaxLookbackDays() const override;
    /// @brief 清理 FactorEngine 信号缓存 (回测结束后释放内存)
    void clearSignalCache();
    /// @brief 从PG加载商品事件到 EventDrivenFactor 缓存 (回测初始化时调用)
    void loadCommodityEvents(const std::string& startDate, const std::string& endDate);

    /// @brief 用 DB 查询结果构建实盘 MarketView（零 JSON）
    void buildLiveView(
        const std::vector<astock::database::SqlQueryResultRow>& rows,
        const std::vector<std::string>& extraFields) override;

    /// @brief 获取当前实盘视图 (P3: shared_ptr 发布句柄 — 调用方持有的拷贝在视图重建后依然有效)
    [[nodiscard]] std::shared_ptr<const factor::compute::IMarketDataView> liveView() const override {
        return m_liveMarketView;
    }

    // ── IFactorSvc ──
    [[nodiscard]] std::unordered_map<std::uint32_t, double> getValues(
        const std::string& instanceId,
        std::int32_t date,
        const std::vector<std::uint32_t>& symbolIds) override;

    // ── IRuntimeFactorService (原 CallbackRuntimeFactorServiceAdapter 职责) ──
    [[nodiscard]] StrategyServiceFlowResult updateIncremental(
        const MarketDataPoint& marketDataPoint) override;
    [[nodiscard]] StrategyServiceFlowResult updateBatch(
        const std::vector<MarketDataPoint>& batch) override;
    void copySnapshots(std::vector<RuntimeFactorSnapshot>& outputSnapshots) const override;
    [[nodiscard]] bool probeFactorCrossSection(
        const std::vector<std::string>& symbols, std::int32_t tradingDayInt) const override;

    // ── 缓存预热 (终审 4.1 + C11, P3 接线) ──
    /// @brief 纯预热: 锚点日全因子全截面计算并写入 period 对应缓存分区 (copySnapshots 后续直接命中)
    /// 由调度器 start() 经 setWarmUpFn 显式调用 (不做引擎全局预热); 无视图/无因子/无标的 → 内部早退
    void warmUpCache(const std::string& strategyId, BarPeriod period,
                     const std::vector<std::string>& symbols) override;

private:
    /// @brief 纯计算: 某因子实例在某日的 symbol→value 全截面 (终审 4.1 拆分)
    /// 只调 FactorEngine::computeSingleDate, 不写缓存不更新状态 (probe 无"顺带"副作用)
    std::unordered_map<std::string, double> computeFactor(
        const std::string& instanceId, const std::string& dateBuf,
        const std::vector<std::string>& fullSymbols);

    // ── ADR-004: 因子缓存按 BarPeriod 物理分区 (跨频零共享) ──
    // 单 tier 结构 = (iid, dateBuf) → sym → value (原 m_factorCache 不变);
    // RFS 每策略一实例 → strategyId 维度实例内天然唯一, 分区维度即 period
    using FactorCacheTier = std::unordered_map<
        std::string, std::map<std::string, std::map<std::string, double>>>;
    /// @brief 活跃周期分区 (getValues/copySnapshots/probe 读写目标; 默认 Daily)
    /// 缓存为 mutable — const 接口 (backtestValuesBySymbol/probe) 也经此读写, 首次访问插入空 tier
    FactorCacheTier& activeTier() const { return m_factorCacheByPeriod[m_activePeriod]; }
    /// @brief 指定周期分区 (warmUpCache 按 period 参数落 tier)
    FactorCacheTier& tier(BarPeriod period) const { return m_factorCacheByPeriod[period]; }

    factor::FactorInstanceManager& m_instanceManager;
    SymbolResolver m_symbolResolver;
    FactorNameResolver m_factorNameResolver;

    factor::compute::BacktestDataService* m_dataSvc = nullptr;
    /// @brief 回测数据集 Arrow 视图 (setDataService 时缓存) — 统一因子值管线的分块输入
    const factor::compute::ArrowMarketDataView* m_arrowView = nullptr;
    std::shared_ptr<const factor::compute::IMarketDataView> m_liveMarketView;  // P3: 发布即持有
    std::unique_ptr<factor::compute::FactorEngine> m_engine;

    // ── 因子缓存（mutable — const 接口首次访问需写入缓存; 按 BarPeriod 物理分区）──
    mutable std::unordered_map<BarPeriod, FactorCacheTier> m_factorCacheByPeriod;
    BarPeriod m_activePeriod{BarPeriod::Daily};

    // ── 实盘状态 (原 buildFactorCallbacks 闭包状态) ──
    mutable std::mutex m_stateMutex;
    std::int32_t m_latestTradeDay{0};
    std::vector<std::uint32_t> m_latestSymbols;
    std::vector<std::string> m_factorIds;
};

} // namespace domain::strategy
