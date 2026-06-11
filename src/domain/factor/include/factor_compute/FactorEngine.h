#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// FactorEngine — 合并 Layer 3a+3b+4 的单头文件
//   DataSvc (Layer 3a): 列存读取 + mmap + 解压 → float32 行情矩阵
//   FactorEngine (Layer 3b): 缓存+并行+SIMD → float32 因子值
//   Reporter (Layer 4): AnalysisKernel → IC/IR/分层/多空
// ══════════════════════════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
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

    /// @brief 存储原始 JSON（不构建任何视图，零内存开销）
    void storeRawJson(const std::string& jsonContent);

    /// @brief 按因子要求的字段构建 MarketView（只加载 OHLCV + 指定字段）
    void buildViewForFields(const std::vector<std::string>& extraFields);

    void setMarketView(IMarketDataView* view);
    MarketMatrixBatch loadBatch(std::size_t batchIndex);

private:
    IMarketDataView* m_marketView = nullptr;
    std::unique_ptr<class CachedMarketDataView> m_ownedView;
    std::string m_rawJson;
    bool m_jsonStored = false;
};

// ═══ FactorEngine (Layer 3b) ═══

class FactorEngine {
public:
    explicit FactorEngine(uint64_t maxMemoryBytes = 0U);
    ~FactorEngine();
    FactorEngine(const FactorEngine&) = delete;
    FactorEngine& operator=(const FactorEngine&) = delete;

    void setInstanceManager(factor::FactorInstanceManager* mgr);

    /// @brief 设置 DataSvc 引用 (compute() 中按需构建 MarketView 用)
    void setDataService(class BacktestDataService* dataSvc);

    FactorMatrix compute(const MarketMatrixBatch& marketData, const FactorCacheKey& cacheKey);

private:
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