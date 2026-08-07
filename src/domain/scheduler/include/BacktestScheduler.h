#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// BacktestScheduler — 调度器纯类壳 (Layer 2)
// 职责: 接收任务 → 设内存上限 → 规划分批 → 循环调 DataSvc 取数据
// 内部: ResourceGovernor (内存管控 + 分批算法)
// 上家: FactorUI / Bridge
// 下家: DataService (通过内部批次循环调用)
// 当前状态: 纯类壳，无任何逻辑和调用
// ══════════════════════════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace factor::compute { class BacktestDataService; struct MarketMatrixBatch; }

namespace domain::scheduler {

class ResourceGovernor;

/// @brief 批次计划
struct BatchPlan {
    std::size_t totalBatches = 0;
    std::size_t batchSize = 500;
    std::size_t totalItems = 0;
};

/// @brief 批次数据回调: Scheduler 内部调 DataSvc 后, 将 float32 行情矩阵传给 FactorUI
using BatchDataCallback = std::function<void(const factor::compute::MarketMatrixBatch& marketMatrix)>;

/// @brief 调度器
/// 时序图职责: FactorUI→Scheduler(提交任务) → Scheduler内部(分批) → Scheduler→DataSvc(请求数据)
class BacktestScheduler {
public:
    explicit BacktestScheduler(std::uint64_t memoryLimitBytes = 0);
    ~BacktestScheduler();

    BacktestScheduler(const BacktestScheduler&) = delete;
    BacktestScheduler& operator=(const BacktestScheduler&) = delete;

    /// @brief 设置 DataService 引用 (Scheduler 内部循环调用它)
    void setDataService(factor::compute::BacktestDataService* dataService);

    /// @brief 提交任务，规划分批 (时序图: FactorUI→Scheduler → Scheduler内部设内存上限+分批)
    BatchPlan submit(std::size_t totalStockCount, std::size_t batchSize = 500);

    /// @brief 遍历所有批次, 内部调 DataSvc.loadBatch(), 结果通过 callback 返给 Orchestrator
    /// 时序图: Scheduler→DataSvc (请求批次数据) → DataSvc→FactorUI (返回float32矩阵)
    /// DataSvc 内部自己知道需要哪些字段, Scheduler 不关心
    void forEachBatch(const BatchPlan& plan,
                      BatchDataCallback callback);

private:
    std::unique_ptr<ResourceGovernor> m_governor;
    factor::compute::BacktestDataService* m_dataService = nullptr; // Scheduler 内部调它
};

} // namespace domain::scheduler