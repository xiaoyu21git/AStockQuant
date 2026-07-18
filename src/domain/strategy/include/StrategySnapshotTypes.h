#pragma once

#include "../../types/DomainDate.h"
#include "../../types/ResolvedStrategyBehavior.h"
#include "../../factor/include/factor_enums.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace domain::strategy {

/// 域层强类型字符串包装（纯 C++，无 Qt）
template <typename Tag>
class StrongText final {
public:
    StrongText() = default;
    explicit StrongText(std::string value)
        : value_(std::move(value))
    {
    }

    explicit StrongText(const char* value)
        : value_(value ? value : "")
    {
    }

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return value_.empty();
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return !value_.empty();
    }

    [[nodiscard]] bool equalsText(std::string_view value) const noexcept
    {
        return value_ == value;
    }

    [[nodiscard]] const std::string& text() const noexcept
    {
        return value_;
    }

    [[nodiscard]] friend bool operator==(const StrongText& left, const StrongText& right) noexcept
    {
        return left.value_ == right.value_;
    }

    [[nodiscard]] friend bool operator!=(const StrongText& left, const StrongText& right) noexcept
    {
        return left.value_ != right.value_;
    }

private:
    std::string value_;
};

struct StrategyIdTag;
struct StrategyCodeTag;
struct StrategyNameTag;
struct DescriptionTextTag;
struct VersionTag;
struct AuthorNameTag;
struct StrategyTagTag;
struct SymbolCodeTag;
struct MarketEventTypeTag;
struct UniverseSourceIdTag;
struct FactorIdTag;
struct RuleTemplateIdTag;
struct FilePathTag;
struct NamespaceIdTag;
struct GroupIdTag;
struct GroupTitleTag;
struct ReasonCodeTag;
struct OrderIdTag;
struct RuntimeStrategyIdTag;
struct BatchIdTag;
struct ExecutionScopeIdTag;

using StrategyId = StrongText<StrategyIdTag>;
using StrategyCode = StrongText<StrategyCodeTag>;
using StrategyName = StrongText<StrategyNameTag>;
using DescriptionText = StrongText<DescriptionTextTag>;
using VersionText = StrongText<VersionTag>;
using AuthorName = StrongText<AuthorNameTag>;
using StrategyTag = StrongText<StrategyTagTag>;
using SymbolCode = StrongText<SymbolCodeTag>;
using MarketEventTypeId = StrongText<MarketEventTypeTag>;
using UniverseSourceId = StrongText<UniverseSourceIdTag>;
using FactorId = StrongText<FactorIdTag>;
using RuleTemplateId = StrongText<RuleTemplateIdTag>;
using FilePathToken = StrongText<FilePathTag>;
using NamespaceId = StrongText<NamespaceIdTag>;
using GroupId = StrongText<GroupIdTag>;
using GroupTitle = StrongText<GroupTitleTag>;
using ReasonCode = StrongText<ReasonCodeTag>;
using OrderId = StrongText<OrderIdTag>;
using RuntimeStrategyId = StrongText<RuntimeStrategyIdTag>;
using BatchId = StrongText<BatchIdTag>;
using ExecutionScopeId = StrongText<ExecutionScopeIdTag>;

struct DatasetId final {
    int value{-1};

    [[nodiscard]] bool isValid() const;
};

struct Quantity final {
    int64_t value{0};

    [[nodiscard]] bool isPositive() const;
};

struct Money final {
    double value{0.0};

    [[nodiscard]] bool isFinite() const;
    [[nodiscard]] bool isPositive() const;
};

struct Ratio final {
    double value{0.0};

    [[nodiscard]] bool isValid() const;
};

struct RebalanceFrequencyDays final {
    int value{1};

    [[nodiscard]] bool isPositive() const;
};

enum class StrategyLanguage : int {
    Python = 0,
    Cpp = 1,
    Julia = 2,
    R = 3,
};

enum class StrategyExecutionKind : int {
    Standard = 0,
    FactorWeightedPortfolio = 1,
};

enum class UniverseMode : int {
    ExplicitSymbols = 0,
    SavedUniverse = 1,
    LinkedWatchlist = 2,
    IndexConstituents = 3,
};

enum class UniverseType : int {
    Equity = 0,
    Index = 1,
    Basket = 2,
};

enum class DataSourceMode : int {
    Raw = 0,
    Cleaned = 1,
    CacheDataset = 2,
};

enum class PositionSizingMethod : int {
    FixedFraction = 0,
    EqualWeight = 1,
    SpreadNeutral = 2,
    Discretionary = 3,
};

enum class DefaultOrderType : int {
    Market = 0,
    MarketOnClose = 1,
};

enum class ShortSellingMode : int {
    Disabled = 0,
    Enabled = 1,
};

enum class RuleBindingPhase : int {
    Market = 0,
    Signal = 1,
    Entry = 2,
    Rebalance = 3,
    Exit = 4,
    Risk = 5,
    Watch = 6,
};

enum class RuleTemplateStage : int {
    Unspecified = -1,
    Market = 0,
    Signal = 1,
    Entry = 2,
    Rebalance = 3,
    Exit = 4,
    Risk = 5,
    Watch = 6,
    Eligibility = 7,
    Portfolio = 8,
    Execution = 9,
    AccountRisk = 10,
};

enum class RuleTemplateResultType : int {
    Unspecified = -1,
    Pass = 0,
    StateSwitch = 1,
    Halt = 2,
    Block = 3,
    CandidateEntry = 4,
    Open = 5,
    Reduce = 6,
    Exit = 7,
};

enum class RuleGroupOperator : int {
    All = 0,
    Any = 1,
    MinimumMatch = 2,
    FirstMatch = 3,
    ScoreSum = 4,
};

enum class RuleGroupRole : int {
    Unspecified = 0,
    MustPass = 1,
    AnyPass = 2,
    Trigger = 3,
    ScoreBoost = 4,
    EntryGuard = 5,
    ExitGuard = 6,
    PositionManagement = 7,
};

enum class RuntimeDecision : int {
    CandidateReady = 0,
    Blocked = 1,
    Suppressed = 2,
    Disabled = 3,
};

enum class RuntimeGate : int {
    None = 0,
    MarketEnvironment = 1,
    RuleTemplate = 2,
    Execution = 3,
    Risk = 4,
};

enum class CandidateAction : int {
    None = 0,
    Buy = 1,
    Sell = 2,
};

enum class AutoExecutionStatus : int {
    Disabled = 0,
    Blocked = 1,
    Submitted = 2,
    RiskPending = 3,
    BrokerPending = 4,
    Filled = 5,
    PartialFilled = 6,
    Cancelled = 7,
    Rejected = 8,
};

enum class AutoExecutionStage : int {
    Submit = 0,
    Queue = 1,
    Runtime = 2,
    RuleTemplate = 3,
    Risk = 4,
    Broker = 5,
    Fill = 6,
};

enum class RuntimeOrderMode : int {
    Limit = 0,
    Market = 1,
};

enum class RuntimePositionEffect : int {
    Open = 0,
    Close = 1,
};

enum class DiagnosticCode : int {
    None = 0,
    ValidationFailed = 1,
    InvalidUniverse = 2,
    InvalidExecutionKind = 3,
    InvalidTemplateBinding = 4,
    RuntimeRuleBlocked = 5,
    AutoOrderRejected = 6,
};

/// 策略生命周期状态枚举（从桥接层提取到域层，去 Qt）
enum class StrategyLifecycleStatus : int {
    Unknown = -1,
    Draft = 0,
    Active = 1,
    Inactive = 2,
    Testing = 3,
    Archived = 4,
    Running = 5,
    Paused = 6,
    Stopped = 7,
};

inline StrategyLifecycleStatus strategyLifecycleStatusFromIndex(int statusIndex)
{
    switch (static_cast<StrategyLifecycleStatus>(statusIndex)) {
    case StrategyLifecycleStatus::Draft:
    case StrategyLifecycleStatus::Active:
    case StrategyLifecycleStatus::Inactive:
    case StrategyLifecycleStatus::Testing:
    case StrategyLifecycleStatus::Archived:
    case StrategyLifecycleStatus::Running:
    case StrategyLifecycleStatus::Paused:
    case StrategyLifecycleStatus::Stopped:
        return static_cast<StrategyLifecycleStatus>(statusIndex);
    case StrategyLifecycleStatus::Unknown:
    default:
        return StrategyLifecycleStatus::Unknown;
    }
}

inline int strategyLifecycleStatusIndex(StrategyLifecycleStatus status)
{
    return static_cast<int>(status);
}

inline bool isKnownStrategyLifecycleStatus(StrategyLifecycleStatus status)
{
    return status != StrategyLifecycleStatus::Unknown;
}

struct StrategyIdentity final {
    StrategyId strategyId;
    StrategyCode strategyCode;
    StrategyName strategyName;
    domain::backtest::StrategyStoredType storedType{domain::backtest::StrategyStoredType::Unknown};
    domain::backtest::StrategyBehaviorKind behaviorKind{domain::backtest::StrategyBehaviorKind::Custom};
    StrategyExecutionKind executionKind{StrategyExecutionKind::Standard};

    [[nodiscard]] bool isValid() const;
};

struct StrategyMetadata final {
    DescriptionText description;
    VersionText version;
    AuthorName author;
    StrategyLanguage language{StrategyLanguage::Python};
    std::vector<StrategyTag> tags;
    DomainDateTime createdAt;
    DomainDateTime updatedAt;
};

struct StrategyLifecycle final {
    StrategyLifecycleStatus status{StrategyLifecycleStatus::Unknown};

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool allowsSignalEmission() const;
};

struct StrategyRuntimeProfile final {
    int assetTypeIndex{0};
    int timeFrameIndex{0};
    int riskLevelIndex{0};

    [[nodiscard]] bool hasAny() const;
};

struct RuleTemplateBinding final {
    RuleBindingPhase phase{RuleBindingPhase::Signal};
    RuleTemplateId templateId;
    FilePathToken filePath;
    NamespaceId namespaceId;
    GroupId groupId;
    GroupTitle groupTitle;
    RuleGroupRole groupRole{RuleGroupRole::Unspecified};
    RuleGroupOperator groupOperator{RuleGroupOperator::All};
    int groupMinMatchCount{0};

    [[nodiscard]] bool isValid() const;
};

struct RuleComposerRule final {
    RuleTemplateBinding binding;

    [[nodiscard]] bool isValid() const;
};

struct RuleComposerGroup final {
    GroupId groupId;
    GroupTitle title;
    RuleGroupRole groupRole{RuleGroupRole::Unspecified};
    RuleGroupOperator groupOperator{RuleGroupOperator::All};
    int minimumMatchCount{0};
    std::vector<RuleComposerRule> rules;

    [[nodiscard]] bool isValid() const;
};

struct RuleComposerStage final {
    RuleBindingPhase phase{RuleBindingPhase::Signal};
    std::vector<RuleComposerGroup> groups;

    [[nodiscard]] bool isValid() const;
};

struct RuleComposerState final {
    std::vector<RuleComposerStage> stages;

    [[nodiscard]] bool isEmpty() const;
};

struct FactorOverlayAllocation final {
    FactorId factorId;
    double weightPercent{0.0};

    [[nodiscard]] bool isValid() const;
};

struct RuleProfileSnapshot final {
    Ratio maxPositionRatio;
    Ratio maxTotalExposureRatio;
    Ratio stopLossRatio;
    Ratio takeProfitRatio;
    int rebalanceDays{0};

    [[nodiscard]] bool isValid() const;
};

struct BatchExecutionPolicy final {
    int maxBatchOrders{0};
    Money maxBatchNotional;
    bool waitPreviousBatchFilled{true};
    bool pauseOnConflict{false};
    bool pauseOnAbnormalReject{false};
    bool requireManualCheckpoint{false};
    int manualCheckpointBatchIndex{0};
};

struct ExecutionPolicySnapshot final {
    PositionSizingMethod positionSizingMethod{PositionSizingMethod::FixedFraction};
    RebalanceFrequencyDays rebalanceFrequencyDays;
    DefaultOrderType defaultOrderType{DefaultOrderType::MarketOnClose};
    ShortSellingMode shortSellingMode{ShortSellingMode::Disabled};
    BatchExecutionPolicy batchExecution;

    [[nodiscard]] bool isValid() const;
};

struct UniverseSpec final {
    UniverseMode universeMode{UniverseMode::ExplicitSymbols};
    UniverseType universeType{UniverseType::Equity};
    UniverseSourceId sourceId;
    std::vector<SymbolCode> explicitSymbols;
    std::vector<SymbolCode> resolvedSymbols;

    [[nodiscard]] bool isValid() const;
};

struct FactorOverlaySpec final {
    bool enabled{false};
    int targetPositionCount{10};
    double minimumCompositeScore{0.0};
    std::vector<FactorOverlayAllocation> allocations;
    std::vector<FactorId> selectedFactors;

    [[nodiscard]] bool isValid() const;
};

struct StrategyScopeContextSnapshot final {
    factor::MarketEnvironmentProfile marketEnvironmentProfile{factor::MarketEnvironmentProfile::GENERIC_EQUITY};
    UniverseSpec universe;
    StrategyName selectedStrategyName;
    int executionTimeFrameIndex{0};

    [[nodiscard]] bool isValid() const;
};

struct StrategySpec final {
    RuleProfileSnapshot ruleProfile;
    ExecutionPolicySnapshot executionPolicy;
    StrategyScopeContextSnapshot strategyScopeContext;
    FactorOverlaySpec factorOverlay;
    std::vector<RuleTemplateBinding> ruleTemplateBindings;
    RuleComposerState ruleComposerState;

    [[nodiscard]] bool isValid() const;
};

struct PerformanceSummaryMetrics final {
    double totalReturn{0.0};
    double annualizedReturn{0.0};
    double volatility{0.0};
    double sharpeRatio{0.0};
    double sortinoRatio{0.0};
    double calmarRatio{0.0};
    double maxDrawdown{0.0};
    double winRate{0.0};
    double profitFactor{0.0};
    double averageWin{0.0};
    double averageLoss{0.0};
    double alpha{0.0};
    double beta{0.0};
    double informationRatio{0.0};
    double trackingError{0.0};
};

struct TradeStatistics final {
    int totalTrades{0};
    int winningTrades{0};
    int losingTrades{0};
    Money totalProfit;
    Money totalLoss;
    Money largestWin;
    Money largestLoss;
    double averageHoldingPeriodDays{0.0};
};

struct RiskMetrics final {
    double var95{0.0};
    double cvar95{0.0};
    double downsideDeviation{0.0};
    double upsideDeviation{0.0};
    double skewness{0.0};
    double kurtosis{0.0};
};

struct TimeSeriesSnapshot final {
    std::vector<DomainDate> dates;
    std::vector<double> portfolioValues;
    std::vector<double> returns;
    std::vector<double> drawdowns;
    std::vector<double> positions;
    std::vector<double> cash;
    std::vector<double> benchmarkValues;    // 基准指数净值曲线
    std::vector<double> benchmarkDrawdowns; // 基准指数回撤曲线

    [[nodiscard]] bool isValid() const;
};

struct UniverseResolutionSummary final {
    UniverseMode universeMode{UniverseMode::ExplicitSymbols};
    int requestedSymbolCount{0};
    int resolvedSymbolCount{0};
};

struct RuleTemplateSummary final {
    int boundTemplateCount{0};
    int matchedTemplateCount{0};
    int blockedTemplateCount{0};
};

struct DiagnosticMessage final {
    DiagnosticCode code{DiagnosticCode::None};
    ReasonCode reasonCode;
};

struct StrategyPerformanceSummary final {
    DomainDateTime lastRecordedAt;
    PerformanceSummaryMetrics latestMetrics;
};

/// @brief 回测逐笔成交记录 (独立重放验证的审计明细)
struct BacktestTradeRecord final {
    std::int32_t tradeDate{0};       // YYYYMMDD
    std::string symbol;              // fullSymbol (如 "300097.SZ")
    bool isBuy{true};
    std::int64_t quantity{0};
    double price{0.0};               // 成交基准价(当日收盘)
    double realizedPnl{0.0};         // 卖出时的已实现盈亏(净额), 买入为 0
};

/// @brief 混合模式因子参与统计 (该因子在回测中成功喂入快照的交易日数)
struct HybridFactorCoverage final {
    std::string factorId;
    int coveredDays{0};
};

/// @brief 策略回测结果（纯域层，无 Qt 依赖）
struct StrategyBacktestResult final {
    PerformanceSummaryMetrics metrics;
    TimeSeriesSnapshot timeSeries;
    TradeStatistics tradeStats;
    std::vector<BacktestTradeRecord> tradeLog;  // 逐笔成交明细
    std::vector<HybridFactorCoverage> hybridFactorCoverage;  // 混合模式各因子参与天数
    bool success{false};
    std::string errorMessage;
    int riskRejectedCount{0};
    std::unordered_map<int, int> riskRejectionStats;  // RiskRejectCode → count
};

} // namespace domain::strategy
