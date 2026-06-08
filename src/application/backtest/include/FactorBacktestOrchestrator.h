#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// FactorBacktestOrchestrator — 因子回测编排层 (Bridge 之下的逻辑层)
// 职责: 控制分批循环、编排 Scheduler/DataSvc/FactorEngine/Reporter
// 上家: FactorBacktestBridge (只调 start/进度回调/结果回调)
// 当前状态: 纯类壳 — 只定义接口和持有关系，不包含任何具体逻辑
// ══════════════════════════════════════════════════════════════════════════════

#include "BacktestRunConfig.h"
#include "factor_compute/SimulatedTradingExecutor.h"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>


namespace factor::compute {
    class BacktestDataService;
    class BacktestFactorEngine;
    class BacktestReporter;
    struct MarketMatrixBatch;
    struct FactorMatrix;
    struct FactorCacheKey;
}

namespace domain::scheduler {
    class BacktestScheduler;
    struct BatchPlan;
}

namespace application::backtest {

/// @brief 进度回调: (进度百分比 0~100, 状态文本)
using FactorOrchestratorProgressCallback = std::function<void(double progress, std::string status)>;

/// @brief 结果回调: 回测完成时调用, 传递序列化后的 QVariantMap 结果
using FactorOrchestratorResultCallback = std::function<void(std::string serializedResult)>;

/// @brief 因子回测编排器 (纯类壳)
class FactorBacktestOrchestrator {
public:
    FactorBacktestOrchestrator();
    ~FactorBacktestOrchestrator();

    FactorBacktestOrchestrator(const FactorBacktestOrchestrator&) = delete;
    FactorBacktestOrchestrator& operator=(const FactorBacktestOrchestrator&) = delete;

    // ── 依赖注入 (由 Bridge 在 start 前设置) ──
    void setScheduler(domain::scheduler::BacktestScheduler* scheduler);
    void setDataService(factor::compute::BacktestDataService* dataService);
    void setFactorEngine(factor::compute::BacktestFactorEngine* engine);
    void setReporter(factor::compute::BacktestReporter* reporter);

    // ── 启动回测 ──
    /// @brief 启动因子回测, 内部控制分批循环
    /// @param config        回测运行时参数 (缓存配置+因子配置+回测参数), 由 Bridge 从 QML 转换
    /// @param onProgress    进度回调 → 传给 Bridge → emit progressChanged
    /// @param onComplete    完成回调 → 传给 Bridge → emit backtestCompleted
    void run(const BacktestRunConfig& config,
             FactorOrchestratorProgressCallback onProgress,
             FactorOrchestratorResultCallback onComplete);

private:
    // 从 reporterInput.factorValuesByDate 构建排序日期列表
    static std::vector<std::string> sortedDatesFrom(
        const std::map<std::string, std::map<std::string, double>>& fvByDate);

    // ── 持有的下层组件引用 ──
    domain::scheduler::BacktestScheduler* m_scheduler = nullptr;
    factor::compute::BacktestDataService* m_dataService = nullptr;
    factor::compute::BacktestFactorEngine* m_engine = nullptr;
    factor::compute::BacktestReporter* m_reporter = nullptr;
    std::unique_ptr<factor::compute::SimulatedTradingExecutor> m_executor;
};

} // namespace application::backtest