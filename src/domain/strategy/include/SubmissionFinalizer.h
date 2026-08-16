#pragma once
// SubmissionFinalizer — 共享提交核心 (P2, 收敛 finalizeAndSubmit/liquidateAll 提交段; P4 死代码清理后)
// 职责: 幂等去重 (ADR-006, 在 OrderGenerator 之前) → 持仓感知生成 → 篮子ID → journal → 投递 → 落幂等键
// 幂等键: app_state.json lastBasket.<strategyId>.<period>.day / .basketId (AppStateStore 原子写)
// mandatory=true (强制清仓) 跳过去重; 账本入账由 EngineListenerAssembler 簿记挂钩在 onOrders 单点执行
// 篮子ID 生成收拢为本类静态方法 (替代散落三套策略; 半自动 pending basket 共用)

#include "EvalTypes.h"
#include "IOrderListener.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace astock::infrastructure::database { class AppStateStore; }

namespace domain::strategy {

class OrderGenerator;
class TradeJournal;

/// @brief 一次提交请求 (评估管道 finalize 阶段与清仓共用)
struct SubmissionRequest {
    std::vector<OrderRequest> rawOrders;      // 原始订单 (含 targetWeight/signalScore)
    std::unordered_map<std::string, std::int64_t> positionQtyMap;  // 持仓视图 (OrderGenerator 入参)
    std::string strategyId;
    std::string accountId;
    std::string tradingDay;       // 评估日 YYYYMMDD (幂等键维度 + journal 日期前缀)
    std::string journalPrefix;    // journal 行前缀 ("提交" / "清仓")
    bool mandatory{false};        // true = 强制提交 (清仓), 跳过去重
};

/// @brief 一次提交结果
struct SubmissionResult {
    std::int64_t totalSubmitted{0};            // 实际投递的订单数
    std::uint64_t basketId{0};
    bool skipped{false};                       // 幂等去重命中 (未调 OrderGenerator)
    std::vector<OrderRequest> submittedOrders; // 实际投递订单副本 (管道用于建仓日期记录)
};

/// @brief 共享提交核心
class SubmissionFinalizer {
public:
    /// @param strategyId 策略 ID (幂等键组成)
    /// @param period 评估周期 (幂等键维度)
    /// @param orderGenerator 持仓感知建单器 (引擎成员, 生命周期须长于本对象)
    /// @param tradeJournal 策略交易日志 (可为空)
    /// @param store app_state.json 统一写者
    /// @param dispatchFn 订单投递函数 (Facade::dispatchOrders 包装, 内部不抛)
    SubmissionFinalizer(std::string strategyId, BarPeriod period,
                        OrderGenerator& orderGenerator, TradeJournal* tradeJournal,
                        std::shared_ptr<astock::infrastructure::database::AppStateStore> store,
                        std::function<void(const std::vector<OrderRequest>&)> dispatchFn);

    /// @brief 提交订单: 去重 → 生成 → 篮子 → journal → 投递 → 幂等键落盘
    SubmissionResult submit(const SubmissionRequest& req);

    /// @brief 生成统一篮子ID (时间戳 + 原子计数器 → hash)
    static std::uint64_t generateBasketId();

    /// @brief 为订单列表打上篮子ID标签, 返回篮子ID
    static std::uint64_t tagBasketOrders(std::vector<OrderRequest>& orders);

private:
    /// @brief 幂等判定: 同 (策略, 周期, 交易日) 已提交 → true (outBasketId 供跳过日志)
    bool alreadySubmitted(const std::string& tradingDay, std::uint64_t& outBasketId) const;
    /// @brief 投递成功后落盘幂等键 (day + basketId)
    void persistSubmission(const std::string& tradingDay, std::uint64_t basketId);

    std::string m_strategyId;
    BarPeriod m_period;
    OrderGenerator* m_orderGenerator{nullptr};
    TradeJournal* m_tradeJournal{nullptr};
    std::shared_ptr<astock::infrastructure::database::AppStateStore> m_store;
    std::function<void(const std::vector<OrderRequest>&)> m_dispatchFn;
};

} // namespace domain::strategy
