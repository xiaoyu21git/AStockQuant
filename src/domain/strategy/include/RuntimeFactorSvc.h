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
class BacktestDataService;
class FactorEngine;
}
}

namespace domain::strategy {

/// @brief 统一的因子服务 — 同时实现 IFactorSvc 和 IRuntimeFactorService
///
/// 回测和实盘共用同一个 getValues() → FactorEngine::compute() 链路。
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
    void setMarketView(const factor::compute::IMarketDataView* view);
    void setDataService(factor::compute::BacktestDataService* dataSvc);

    /// @brief 设置实盘行情视图（包含足够回溯窗口的滑动 MarketView）
    void setLiveMarketView(const factor::compute::IMarketDataView* view);

    /// @brief 设置关注的因子实例 ID 列表（copySnapshots 迭代用）
    void setFactorIds(const std::vector<std::string>& factorIds);

    /// @brief 从因子需求收集所需的数据字段
    [[nodiscard]] std::vector<std::string> getRequiredFields() const;

    /// @brief 从因子需求计算最大回溯窗口（交易日数，用于确定查多少天历史数据）
    [[nodiscard]] int getMaxLookbackDays() const;

    /// @brief 用 DB 查询结果构建实盘 MarketView（零 JSON）
    void buildLiveView(
        const std::vector<astock::database::SqlQueryResultRow>& rows,
        const std::vector<std::string>& extraFields);

    /// @brief 获取当前实盘视图（供桥接层读取元数据）
    [[nodiscard]] const factor::compute::IMarketDataView* liveView() const {
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

private:
    factor::FactorInstanceManager& m_instanceManager;
    SymbolResolver m_symbolResolver;
    FactorNameResolver m_factorNameResolver;

    factor::compute::BacktestDataService* m_dataSvc = nullptr;
    const factor::compute::IMarketDataView* m_liveMarketView = nullptr;
    std::unique_ptr<factor::compute::CachedMarketDataView> m_ownedLiveView;
    std::unique_ptr<factor::compute::FactorEngine> m_engine;

    // ── 回测缓存 ──
    std::unordered_map<std::string, std::map<std::string, std::map<std::string, double>>> m_factorCache;

    // ── 实盘状态 (原 buildFactorCallbacks 闭包状态) ──
    mutable std::mutex m_stateMutex;
    std::int32_t m_latestTradeDay{0};
    std::vector<std::uint32_t> m_latestSymbols;
    std::vector<std::string> m_factorIds;
};

} // namespace domain::strategy
