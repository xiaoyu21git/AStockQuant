#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// RiskEvaluator — 域层风控评估器 (纯 C++，零 Qt 依赖)
// 供：
//   回测场景：RiskPipeline + IRiskRule 矩阵评估
//   实盘场景：evaluateOrder() 单笔订单审批
// 规则：
//   - 不使用字符串比较
//   - 不使用 QVariantMap 透传
//   - 所有成员私有，通过 getter 访问
// ══════════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <string>

namespace domain::strategy {

// ── 实时风控指标（纯 C++，零 Qt） ──
struct RiskLiveMetrics final {
    double currentDrawdownPercent{0.0};
    double varUsagePercent{0.0};
    double currentTotalExposurePercent{0.0};
    double varBudgetAmount{0.0};
    double estimatedVarAmount{0.0};
};

// ── 拒绝原因枚举 (替代字符串) ──
enum class RiskRejectCode : int {
    None = 0,
    MissingRequiredFields,
    StrategyNotBound,
    StrategyNotActive,
    PriceInvalid,
    SignalStrengthTooWeak,
    PositionSnapshotNotReady,
    TradingSessionClosed,
    NoSellablePosition,
    SellQuantityExceedsHolding,
    OrderSizeExceeded,
    SlippageExceeded,
    DailyTurnoverExceeded,
    StopLossTriggered,
    TakeProfitTriggered,
    BreakerLevel1,
    BreakerLevel2,
    BreakerLevel3,
    MaxDrawdownExceeded,
    PositionConcentrationExceeded,
    TotalExposureExceeded,
    IndexDrawdownExceeded,       // 大盘指数回撤超限，暂停开仓
    Level3TradingHaltActive,
    AutoStrategyWithoutQuantity,
};

// ── 订单方向枚举 ──
enum class OrderDirection : int { Buy = 0, Sell = 1 };

// ── 仓位效应枚举 ──
enum class PositionEffect : int { Open = 0, Close = 1, Unspecified = 2 };

// ── 特殊操作枚举 (替代 isCashRepayAction / isShareReturnAction 字符串函数) ──
enum class SpecialAction : int { None = 0, CashRepay, ShareReturn };

// ── 风控输入 (纯 getter 封装) ──
class RiskInput final {
public:
    RiskInput() = default;

    // -- 订单 --
    [[nodiscard]] const std::string& strategyId() const noexcept { return m_strategyId; }
    void setStrategyId(std::string v) { m_strategyId = std::move(v); }

    [[nodiscard]] const std::string& symbol() const noexcept { return m_symbol; }
    void setSymbol(std::string v) { m_symbol = std::move(v); }

    [[nodiscard]] bool isBuyOrder() const noexcept { return m_isBuyOrder; }
    void setBuyOrder(bool v) noexcept { m_isBuyOrder = v; }

    [[nodiscard]] double price() const noexcept { return m_price; }
    void setPrice(double v) noexcept { m_price = v; }

    [[nodiscard]] std::int64_t quantity() const noexcept { return m_quantity; }
    void setQuantity(std::int64_t v) noexcept { m_quantity = v; }

    [[nodiscard]] double requestedNotional() const noexcept { return m_requestedNotional; }
    void setRequestedNotional(double v) noexcept { m_requestedNotional = v; }

    [[nodiscard]] double signalStrength() const noexcept { return m_signalStrength; }
    void setSignalStrength(double v) noexcept { m_signalStrength = v; }

    [[nodiscard]] double cashAmount() const noexcept { return m_cashAmount; }
    void setCashAmount(double v) noexcept { m_cashAmount = v; }

    [[nodiscard]] bool isAutoStrategySignal() const noexcept { return m_isAutoStrategySignal; }
    void setAutoStrategySignal(bool v) noexcept { m_isAutoStrategySignal = v; }

    // -- 策略上下文 --
    [[nodiscard]] bool strategyBound() const noexcept { return m_strategyBound; }
    void setStrategyBound(bool v) noexcept { m_strategyBound = v; }

    [[nodiscard]] bool strategyActive() const noexcept { return m_strategyActive; }
    void setStrategyActive(bool v) noexcept { m_strategyActive = v; }

    // -- 账户 --
    [[nodiscard]] double currentTotalAsset() const noexcept { return m_currentTotalAsset; }
    void setCurrentTotalAsset(double v) noexcept { m_currentTotalAsset = v; }

    [[nodiscard]] double currentMarketValue() const noexcept { return m_currentMarketValue; }
    void setCurrentMarketValue(double v) noexcept { m_currentMarketValue = v; }

    [[nodiscard]] double currentDailyTurnoverNotional() const noexcept { return m_currentDailyTurnoverNotional; }
    void setCurrentDailyTurnoverNotional(double v) noexcept { m_currentDailyTurnoverNotional = v; }

    [[nodiscard]] double symbolMarketValue() const noexcept { return m_symbolMarketValue; }
    void setSymbolMarketValue(double v) noexcept { m_symbolMarketValue = v; }

    [[nodiscard]] double symbolPositionReturnPercent() const noexcept { return m_symbolPositionReturnPercent; }
    void setSymbolPositionReturnPercent(double v) noexcept { m_symbolPositionReturnPercent = v; }

    [[nodiscard]] std::int64_t closeableQuantity() const noexcept { return m_closeableQuantity; }
    void setCloseableQuantity(std::int64_t v) noexcept { m_closeableQuantity = v; }

    [[nodiscard]] bool positionSnapshotReady() const noexcept { return m_positionSnapshotReady; }
    void setPositionSnapshotReady(bool v) noexcept { m_positionSnapshotReady = v; }

    // -- 市场 --
    [[nodiscard]] double referencePrice() const noexcept { return m_referencePrice; }
    void setReferencePrice(double v) noexcept { m_referencePrice = v; }

    [[nodiscard]] bool tradingSessionOpen() const noexcept { return m_tradingSessionOpen; }
    void setTradingSessionOpen(bool v) noexcept { m_tradingSessionOpen = v; }

    // -- 配置 --
    [[nodiscard]] double orderSizeLimitWan() const noexcept { return m_orderSizeLimitWan; }
    void setOrderSizeLimitWan(double v) noexcept { m_orderSizeLimitWan = v; }

    [[nodiscard]] double slippageLimitPercent() const noexcept { return m_slippageLimitPercent; }
    void setSlippageLimitPercent(double v) noexcept { m_slippageLimitPercent = v; }

    [[nodiscard]] double turnoverLimitWan() const noexcept { return m_turnoverLimitWan; }
    void setTurnoverLimitWan(double v) noexcept { m_turnoverLimitWan = v; }

    [[nodiscard]] double stopLossPercent() const noexcept { return m_stopLossPercent; }
    void setStopLossPercent(double v) noexcept { m_stopLossPercent = v; }

    [[nodiscard]] double takeProfitPercent() const noexcept { return m_takeProfitPercent; }
    void setTakeProfitPercent(double v) noexcept { m_takeProfitPercent = v; }

    [[nodiscard]] double maxDrawdownLimitPercent() const noexcept { return m_maxDrawdownLimitPercent; }
    void setMaxDrawdownLimitPercent(double v) noexcept { m_maxDrawdownLimitPercent = v; }

    [[nodiscard]] double breakerLevel1Percent() const noexcept { return m_breakerLevel1Percent; }
    void setBreakerLevel1Percent(double v) noexcept { m_breakerLevel1Percent = v; }

    [[nodiscard]] double breakerLevel2Percent() const noexcept { return m_breakerLevel2Percent; }
    void setBreakerLevel2Percent(double v) noexcept { m_breakerLevel2Percent = v; }

    [[nodiscard]] double breakerLevel3Percent() const noexcept { return m_breakerLevel3Percent; }
    void setBreakerLevel3Percent(double v) noexcept { m_breakerLevel3Percent = v; }

    [[nodiscard]] double maxPositionPercent() const noexcept { return m_maxPositionPercent; }
    void setMaxPositionPercent(double v) noexcept { m_maxPositionPercent = v; }

    [[nodiscard]] double maxTotalExposurePercent() const noexcept { return m_maxTotalExposurePercent; }
    void setMaxTotalExposurePercent(double v) noexcept { m_maxTotalExposurePercent = v; }

    [[nodiscard]] bool level3TradingHaltActive() const noexcept { return m_level3TradingHaltActive; }
    void setLevel3TradingHaltActive(bool v) noexcept { m_level3TradingHaltActive = v; }

    [[nodiscard]] double currentDrawdownPercent() const noexcept { return m_currentDrawdownPercent; }
    void setCurrentDrawdownPercent(double v) noexcept { m_currentDrawdownPercent = v; }

    // 大盘指数保护: 调用方在 evaluateOrder 前填入; 0 表示不启用。
    [[nodiscard]] double indexDrawdownPct() const noexcept { return m_indexDrawdownPct; }
    void setIndexDrawdownPct(double v) noexcept { m_indexDrawdownPct = v; }

    [[nodiscard]] double indexDrawdownLimitPercent() const noexcept { return m_indexDrawdownLimitPercent; }
    void setIndexDrawdownLimitPercent(double v) noexcept { m_indexDrawdownLimitPercent = v; }

private:
    std::string m_strategyId;
    std::string m_symbol;
    bool m_isBuyOrder{false};
    double m_price{0.0};
    std::int64_t m_quantity{0};
    double m_requestedNotional{0.0};
    double m_signalStrength{0.0};
    double m_cashAmount{0.0};
    bool m_isAutoStrategySignal{false};

    bool m_strategyBound{false};
    bool m_strategyActive{false};

    double m_currentTotalAsset{0.0};
    double m_currentMarketValue{0.0};
    double m_currentDailyTurnoverNotional{0.0};
    double m_symbolMarketValue{0.0};
    double m_symbolPositionReturnPercent{0.0};
    std::int64_t m_closeableQuantity{0};
    bool m_positionSnapshotReady{false};

    double m_referencePrice{0.0};
    bool m_tradingSessionOpen{true};

    double m_orderSizeLimitWan{0.0};
    double m_slippageLimitPercent{0.0};
    double m_turnoverLimitWan{0.0};
    double m_stopLossPercent{0.0};
    double m_takeProfitPercent{0.0};
    double m_maxDrawdownLimitPercent{0.0};
    double m_breakerLevel1Percent{0.0};
    double m_breakerLevel2Percent{0.0};
    double m_breakerLevel3Percent{0.0};
    double m_maxPositionPercent{0.0};
    double m_maxTotalExposurePercent{0.0};

    bool m_level3TradingHaltActive{false};
    double m_currentDrawdownPercent{0.0};
    double m_indexDrawdownPct{0.0};
    double m_indexDrawdownLimitPercent{0.0};
};

// ── 风控输出 (纯 getter 封装) ──
class RiskResult final {
public:
    RiskResult() = default;

    static RiskResult accept(RiskRejectCode code = RiskRejectCode::None,
                               double score = 0.15,
                               std::string description = "基础风控校验通过");

    static RiskResult rejected(RiskRejectCode code, double score, std::string description);

    [[nodiscard]] bool approved() const noexcept { return m_approved; }
    [[nodiscard]] RiskRejectCode code() const noexcept { return m_code; }
    [[nodiscard]] double riskScore() const noexcept { return m_riskScore; }
    [[nodiscard]] const std::string& description() const noexcept { return m_description; }

private:
    RiskResult(bool approved, RiskRejectCode code, double score, std::string description);

    bool m_approved{true};
    RiskRejectCode m_code{RiskRejectCode::None};
    double m_riskScore{0.15};
    std::string m_description;
};

// ── 风控评估器 ──
class RiskEvaluator final {
public:
    RiskEvaluator() = delete;

    /// @brief 单笔订单风险审批（实盘路径）
    /// @return RiskResult
    [[nodiscard]] static RiskResult evaluateOrder(const RiskInput& input);

    /// @brief 根据 RiskRejectCode 返回标准中文消息
    [[nodiscard]] static std::string descriptionForCode(RiskRejectCode code);

    // ── 域层枚举转换 (替代桥接层全部字符串比较) ──

    /// @brief 从原始字符串解析订单方向 (BUY/SELL/LONG/SHORT/1/2)
    [[nodiscard]] static OrderDirection directionFromString(const std::string& raw);

    /// @brief 从原始字符串解析仓位效应 (OPEN/CLOSE/1/2)
    [[nodiscard]] static PositionEffect positionEffectFromString(const std::string& raw);

    /// @brief 从原始字符串解析特殊操作类型 (repay/cashrepay/returnstock/...)
    [[nodiscard]] static SpecialAction specialActionFromString(const std::string& raw);

    /// @brief 判断订单方向是否增加风险敞口
    [[nodiscard]] static bool increasesExposure(OrderDirection dir, PositionEffect effect);

    /// @brief 计算滑点偏差百分比
    [[nodiscard]] static double adverseSlippagePct(OrderDirection dir, double orderPrice, double referencePrice);

    /// @brief 持仓盈亏计算方向
    [[nodiscard]] static bool isShortSide(PositionEffect effect, const std::string& rawPositionSide);

    /// @brief 计算实时风控指标（供桥接层 Q_PROPERTY 使用）
    [[nodiscard]] static RiskLiveMetrics computeLiveMetrics(
        double totalAsset,
        double marketValue,
        double maxTotalExposurePct,
        double peakTotalAsset);

    /// @brief 浮动熔断阈值计算（第三层级）
    [[nodiscard]] static double clampMaxTotalExposure(double rawPct);

    /// @brief 将 RiskConfig 填充到 RiskInput 的配置字段
    static void applyConfig(RiskInput& input, const struct RiskConfig& config);
};

// ── 风控配置 (纯 C++，零 Qt) ──
struct RiskConfig final {
    // ── 风控阈值 ──
    double orderSizeLimitWan{0.0};       // 单笔委托上限（万元）
    double slippageLimitPercent{0.0};    // 滑点容忍（%）
    double turnoverLimitWan{0.0};        // 日成交额上限（万元）
    double stopLossPercent{0.0};         // 止损线（%）
    double takeProfitPercent{0.0};       // 止盈线（%）
    double maxDrawdownLimitPercent{0.0}; // 最大回撤限制（%）
    double maxDailyLossPercent{0.0};     // 日内最大亏损（%）
    double breakerLevel1Percent{0.0};    // 一级熔断（%）
    double breakerLevel2Percent{0.0};    // 二级熔断（%）
    double breakerLevel3Percent{0.0};    // 三级熔断（%）
    double maxPositionPercent{0.0};      // 单标的持仓集中度（%）
    double maxTotalExposurePercent{0.0}; // 总敞口上限（%）

    // ── 大盘指数保护 ──
    double indexDrawdownLimitPercent{3.0};   // 大盘回撤阈值（%，默认 3%）
    int    indexDrawdownLookbackDays{5};     // 回看交易日数（默认 5 天）
    std::string indexSymbol{"000001.SH"};    // 跟踪的指数代码

    // ── 费率 ──
    double commissionRate{0.0};          // 手续费率（如 0.0003 = 万三）
    double minCommission{0.0};           // 最低手续费（元，如 5.0）
    double stampTaxRate{0.0};            // 印花税率（如 0.001 = 千一，仅卖出）

    /// @brief 构建默认风控配置（开发/模拟环境）
    static RiskConfig defaults() noexcept;
};

} // namespace domain::strategy