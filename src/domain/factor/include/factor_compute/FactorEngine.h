#pragma once
#include "factor_compute/MarketDataViewHistoricalAdapter.h"
// ══════════════════════════════════════════════════════════════════════════════
// FactorEngine — 合并 Layer 3a+3b+4 的单头文件
//   DataSvc (Layer 3a): 列存读取 + mmap + 解压 → float32 行情矩阵
//   FactorEngine (Layer 3b): 缓存+并行+SIMD → float32 因子值
//   Reporter (Layer 4): AnalysisKernel → IC/IR/分层/多空
// ══════════════════════════════════════════════════════════════════════════════

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// 前置声明，完整定义在 FactorEngine.cpp 中引入
namespace factor::compute { class SignalCache; }

namespace factor { class FactorInstanceManager; class BaseFactor; }

namespace factor::compute {

// ═══ 公共类型 ═══

// 前向声明避免循环依赖
class IMarketDataView;

enum class FactorComputeMode : int {
    Backtest = 0,  // 回测模式 — 需要完整回溯窗口, 分批处理, 列存读取
    Live     = 1,  // 实盘模式 — 不需要回溯窗口, 当日数据直接计算
};

struct MarketMatrixBatch {
    std::size_t batchIndex = 0;
    const IMarketDataView* marketView = nullptr;  // ParquetMarketDataView 指针
    FactorComputeMode mode = FactorComputeMode::Backtest;
};

struct FactorCacheKey {
    std::string factorName;
    std::string parameterHash;
    std::string dateRangeHash;
    std::string instrumentSetHash;
};

struct FactorMatrix {
    std::size_t batchIndex = 0;
    std::map<std::string, std::map<std::string, double>> factorValues;  // date → symbol → value
};

struct BacktestReporterInput {
    std::map<std::string, std::map<std::string, double>> factorValuesByDate;
    int numGroups = 5;
    int forwardDays = 30;
    double commissionRate = 0.001;
    double slippageRate = 0.001;
    double riskFreeRate = 0.02;
};

struct BacktestReporterOutput {
    double turnoverRatio = 0.0;
    uint32_t totalSignalCount = 0;
    uint32_t presentSignalCount = 0;
};

// ═══ DataSvc (Layer 3a) ═══

class BacktestDataService {
public:
    BacktestDataService();
    ~BacktestDataService();
    BacktestDataService(const BacktestDataService&) = delete;
    BacktestDataService& operator=(const BacktestDataService&) = delete;

    void buildViewForFields(const std::vector<std::string>& extraFields,
                            const std::function<void(double)>& onProgress);
    void buildViewForFields(const std::vector<std::string>& extraFields);

    void setMarketView(IMarketDataView* view);
    IMarketDataView* getView() const { return m_marketView; }
    MarketMatrixBatch loadBatch(std::size_t batchIndex);

    using DbFallbackFn = std::function<std::unordered_map<std::string, double>(
        const std::string& date, const std::string& field,
        const std::vector<std::string>& symbols)>;
    void setDbFallback(DbFallbackFn fn) { m_dbFallback = std::move(fn); }
    const DbFallbackFn& dbFallback() const { return m_dbFallback; }

private:
    IMarketDataView* m_marketView = nullptr;
    std::unique_ptr<class CachedMarketDataView> m_ownedView;
    DbFallbackFn m_dbFallback;
};

// ═══ FactorEngine (Layer 3b) ═══

class FactorEngine {
public:
    explicit FactorEngine(uint64_t maxMemoryBytes = 0U);
    ~FactorEngine();
    FactorEngine(const FactorEngine&) = delete;
    FactorEngine& operator=(const FactorEngine&) = delete;

    void setInstanceManager(factor::FactorInstanceManager* mgr);

    /// @brief 检查 InstanceManager 是否已注入 (compute 的前置条件)
    [[nodiscard]] bool hasInstanceManager() const noexcept { return m_instanceManager != nullptr; }

    /// @brief 获取 InstanceManager（供 Orchestrator 收集字段需求）
    [[nodiscard]] factor::FactorInstanceManager* instanceManager() const noexcept { return m_instanceManager; }

    /// @brief 设置 DataSvc 引用 (compute() 中按需构建 MarketView 用)
    void setDataService(class BacktestDataService* dataSvc);

    /// @brief 清除 SignalCache 中的所有缓存条目
    void clearSignalCache();

    /// @param dbFallback 数据库回退回调 (并行块任务经此参数传入, 空则回退读 m_dataSvc)
    /// @param cancelFlag 软取消标志 (每日期循环间检查, nullptr = 永不取消; 并行 worker 由管线透传)
    FactorMatrix compute(const MarketMatrixBatch& marketData, const FactorCacheKey& cacheKey,
                         size_t skipDates = 0,
                         const CachedMarketDataViewHistoricalAdapter::DbFallbackFn& dbFallback = {},
                         const std::atomic<bool>* cancelFlag = nullptr);

    /// @brief 用指定因子实例计算 (并行 worker 路径)
    /// 实例由 FactorWorkerContext 经 createIsolatedInstance 隔离提供 (消除共享实例
    /// 记忆化缓存竞态); 串行路径 compute() 内部 createInstance 后同样落到本入口 —
    /// 两条路径共享 computeImpl 唯一实现。
    FactorMatrix computeWithInstance(factor::BaseFactor& factor,
                                     const MarketMatrixBatch& marketData,
                                     const FactorCacheKey& cacheKey,
                                     size_t skipDates,
                                     const CachedMarketDataViewHistoricalAdapter::DbFallbackFn& dbFallback,
                                     const std::atomic<bool>* cancelFlag);

    /// @brief 单日因子计算（实盘 / 逐 tick 路径用）
    /// @param factorName  因子实例 ID
    /// @param date        交易日字符串 "YYYY-MM-DD"
    /// @param symbols     标的符号列表
    /// @param view        行情数据视图（实盘积累的历史窗口视图，不能为 null）
    /// @return symbol → factorValue 映射。view 为 null 或因子创建失败返回空 map。
    [[nodiscard]] std::unordered_map<std::string, double> computeSingleDate(
        const std::string& factorName,
        const std::string& date,
        const std::vector<std::string>& symbols,
        const IMarketDataView* view);

private:
    /// @brief 核心计算循环 (compute/computeWithInstance 共用的唯一实现, 静态纯函数)
    /// engine 以 const 指针传入 — 路径只读 m_dataSvc (回退取 dbFallback);
    /// 不触碰 SignalCache/实例管理器等可变状态 → 并行多 worker 共享同一引擎不加锁的前提。
    [[nodiscard]] static FactorMatrix computeImpl(
        factor::BaseFactor& factor,
        const MarketMatrixBatch& marketData,
        const FactorCacheKey& cacheKey,
        size_t skipDates,
        const CachedMarketDataViewHistoricalAdapter::DbFallbackFn& dbFallback,
        const std::atomic<bool>* cancelFlag,
        const FactorEngine* engine);

    /// @brief 公共的 "给定因子 + 行情 → 因子值" 计算（compute 和 computeSingleDate 共享）
    /// @param historicalView 复用构建的 adapter (compute 全日期循环复用, 避免每日期重建索引)
    [[nodiscard]] static std::unordered_map<std::string, double> computeOneDay(
        class factor::BaseFactor& factor,
        const std::string& dateStr,
        const std::vector<std::string>& symbols,
        const std::shared_ptr<CachedMarketDataViewHistoricalAdapter>& historicalView);

    std::unique_ptr<SignalCache> m_signalCache;
    factor::FactorInstanceManager* m_instanceManager = nullptr;
    class BacktestDataService* m_dataSvc = nullptr;
};

// ═══ Reporter (Layer 4) ═══

class BacktestReporter {
public:
    BacktestReporter();
    ~BacktestReporter();
    BacktestReporter(const BacktestReporter&) = delete;
    BacktestReporter& operator=(const BacktestReporter&) = delete;

    BacktestReporterOutput analyze(const BacktestReporterInput& input);
};

} // namespace factor::compute