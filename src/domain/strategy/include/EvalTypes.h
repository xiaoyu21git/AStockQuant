#pragma once
// EvalTypes — EOD 评估域纯值类型 (P0 基础件, ADR-007 命名)
// 职责: 评估链路的周期/模式/阶段/失败/结果类型, 零 Qt、零外部依赖
// 周期经 EvalRequest 显式携带, 管道内零周期分支 (ADR-002)

#include <cstdint>
#include <map>
#include <string>

namespace domain::strategy {

/// @brief 评估周期 (数据粒度, 一等配置项)
enum class BarPeriod : std::uint8_t { Daily, Minute1, Minute5 };

/// @brief 评估模式 (由调度器触发时机决定, 见设计澄清 C1-C5)
enum class EvalMode : std::uint8_t {
    Intraday,     // 盘中/正常窗口评估
    Compensation  // 次日补单窗口评估
};

/// @brief 评估阶段 (EvalResult.failedStage 定位用)
enum class EvalStage : std::uint8_t {
    CheckRebalance,
    Preflight,
    PrepareContext,
    FetchPrices,
    ComputeBreadth,
    EvaluateGates,
    CollectSignals,
    FinalizeSubmit
};

/// @brief 链断级失败类别 (硬性失败 → Error 截断, §8 异常矩阵)
enum class EvalFailureKind : std::uint8_t {
    None,
    ViewEmpty,            // P1 视图空
    AnchorMissing,        // P2 锚点缺失
    FactorSnapshotEmpty,  // P2 因子全截面无有限值
    AccountEmpty,         // P3 账户快照空
    PriceDataEmpty,       // P4 价格链全空
    BookKeepingMismatch,  // P5 账本与券商快照偏差
    Exception             // 未捕获异常
};

/// @brief 评估结果状态 (枚举名与值不变; 判定规则: AllRejected=生成>0且提交=0)
enum class EvalStatus : std::uint8_t {
    Submitted,    // 篮子已提交, 至少一笔订单成功发出
    NoSignal,     // 策略评估后无交易信号
    AllRejected,  // 有信号但全部被风控/资金拒绝
    Skipped,      // 评估被跳过 (非调仓日/回测/无标的)
    Error         // 链断截断 (硬性失败, 不伪装 NoSignal)
};

/// @brief 周期维度命名: 持久化键后缀 (调度器/账本共用, 避免散落)
/// 例: lastEvalDay.<strategyId>.daily / positionBook.<strategyId>.1min
struct BarPeriodNaming {
    static constexpr const char* suffix(BarPeriod period) noexcept {
        switch (period) {
            case BarPeriod::Minute1: return "1min";
            case BarPeriod::Minute5: return "5min";
            case BarPeriod::Daily:
            default:                 return "daily";
        }
    }
};

/// @brief 评估枚举文本命名 (引擎日志/journal 统一, 避免散落自由函数)
struct EvalNaming {
    /// @brief 评估阶段文本 (EvalResult.failedStage 日志用)
    static constexpr const char* stageText(EvalStage stage) noexcept {
        switch (stage) {
        case EvalStage::CheckRebalance:  return "CheckRebalance";
        case EvalStage::Preflight:       return "Preflight";
        case EvalStage::PrepareContext:  return "PrepareContext";
        case EvalStage::FetchPrices:     return "FetchPrices";
        case EvalStage::ComputeBreadth:  return "ComputeBreadth";
        case EvalStage::EvaluateGates:   return "EvaluateGates";
        case EvalStage::CollectSignals:  return "CollectSignals";
        case EvalStage::FinalizeSubmit:  return "FinalizeSubmit";
        }
        return "Unknown";
    }

    /// @brief 失败类别文本 (§8 异常矩阵日志模板)
    static constexpr const char* kindText(EvalFailureKind kind) noexcept {
        switch (kind) {
        case EvalFailureKind::None:                return "None";
        case EvalFailureKind::ViewEmpty:           return "ViewEmpty";
        case EvalFailureKind::AnchorMissing:       return "AnchorMissing";
        case EvalFailureKind::FactorSnapshotEmpty: return "FactorSnapshotEmpty";
        case EvalFailureKind::AccountEmpty:        return "AccountEmpty";
        case EvalFailureKind::PriceDataEmpty:      return "PriceDataEmpty";
        case EvalFailureKind::BookKeepingMismatch: return "BookKeepingMismatch";
        case EvalFailureKind::Exception:           return "Exception";
        }
        return "Unknown";
    }

    /// @brief 评估结果状态文本 (完成日志用)
    static constexpr const char* statusText(EvalStatus status) noexcept {
        switch (status) {
        case EvalStatus::Submitted:    return "Submitted";
        case EvalStatus::NoSignal:     return "NoSignal";
        case EvalStatus::AllRejected:  return "AllRejected";
        case EvalStatus::Skipped:      return "Skipped";
        case EvalStatus::Error:        return "Error";
        }
        return "Unknown";
    }
};

/// @brief 单标的当日价量 (价格源输出单元)
struct EodDayBar {
    double close = 0.0;
    double volume = 0.0;
    double preClose = 0.0;  // 前一交易日收盘价, 用于涨跌停计算
};

/// @brief 价格数据聚合 (Provider 链结果 + 市场宽度)
struct PriceData {
    std::map<std::string, EodDayBar> bars;  // sym → 当日价量
    double breadthAboveMa60 = 0.5;
    double breadthAboveMa20 = 0.5;
};

class IPriceProvider;

/// @brief 一次评估的请求 (调度器 → Facade → 管道, 显式携带周期)
struct EvalRequest {
    std::string tradingDay;        // 评估日 (YYYYMMDD)
    bool isCompensation = false;   // 是否补单窗口评估
    BarPeriod period = BarPeriod::Daily;
    const IPriceProvider* priceProvider = nullptr;  // 观察者, 仅 run() 同步栈帧内有效 (澄清 C1)
};

/// @brief 一次评估的结果 (调度器持久化判定用)
struct EvalResult {
    EvalStatus status = EvalStatus::Error;
    EvalFailureKind failureKind = EvalFailureKind::None;
    EvalStage failedStage = EvalStage::Preflight;
    std::string reason;  // 失败原因 (引擎日志/journal 用)

    // 信号三计数 (§8 判定规则: N=原始生成, N1=规则闸门拒绝, N2=涨跌停过滤, N3=生成器过滤)
    std::int64_t totalGenerated = 0;
    std::int64_t ruleGateRejected = 0;
    std::int64_t limitFiltered = 0;
    std::int64_t generatorFiltered = 0;
};

} // namespace domain::strategy
