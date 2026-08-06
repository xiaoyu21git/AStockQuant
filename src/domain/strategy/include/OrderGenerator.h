#pragma once
// OrderGenerator — 持仓感知建单器
// 将策略原始订单（含 targetWeight）与当前持仓对比，计算实际买卖数量及意图，
// 生成最终可提交的 OrderRequest 列表。
//
// 职责: 纯计算 + 去重 + 最小手数校验，不涉及 I/O、不持有可变状态。
// 依赖: 注入 OrderBuilder（用于标准化订单字段），IPositionProvider（查询持仓）。

#include "../../trading/TradingTypes.h"
#include "../../trading/include/OrderBuilder.h"
#include "StrategyServiceTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine { struct AccountInfo; }

namespace domain::strategy {

/// @brief 持仓查询接口（解耦 AccountEngine 全局单例）
class IPositionProvider {
public:
    virtual ~IPositionProvider() = default;
    /// @brief 查询标的当前持仓量；无后缀6位码 → 持仓股数
    [[nodiscard]] virtual std::int64_t quantityOf(const std::string& code) const = 0;
};

/// @brief 默认持仓提供者: 从 std::unordered_map 查询
class MapPositionProvider final : public IPositionProvider {
public:
    explicit MapPositionProvider(const std::unordered_map<std::string, std::int64_t>& map)
        : m_map(&map) {}
    [[nodiscard]] std::int64_t quantityOf(const std::string& code) const override {
        auto it = m_map->find(code);
        return it != m_map->end() ? it->second : 0;
    }
private:
    const std::unordered_map<std::string, std::int64_t>* m_map;
};

class OrderGenerator {
public:
    /// @param orderBuilder 订单标准化器（设置 accountId/strategyId 等公共字段）
    explicit OrderGenerator(domain::trading::OrderBuilder& orderBuilder)
        : m_orderBuilder(&orderBuilder) {}

    /// @brief 从策略原始信号生成持仓感知订单
    /// @param rawOrders 策略原始信号订单（含 symbol / side / targetWeight / signalScore）
    /// @param posProvider 当前持仓查询接口
    /// @param account 账户信息（totalAsset 用于计算目标股数）
    /// @param priceForWeight 参考价格（用于权重→股数换算）
    /// @return 最终可提交的订单列表（已去重、已校验最小手数）
    [[nodiscard]] std::vector<domain::trading::OrderRequest> generate(
        const std::vector<domain::trading::OrderRequest>& rawOrders,
        const IPositionProvider& posProvider,
        const engine::AccountInfo& account,
        double priceForWeight) const;

private:
    struct OrderDelta {
        SignalIntent intent = SignalIntent::KEEP;
        std::int64_t deltaQty = 0;
    };

    /// @brief 计算买入增量: 新开仓 → OPEN, 加仓 → ADD, 矛盾 → 返回 0
    [[nodiscard]] OrderDelta computeBuyDelta(
        std::int64_t currentQty, double currentWeight,
        double targetWeight, double priceForWeight, double totalAsset) const;

    /// @brief 计算卖出减量: strategy signal(targetWeight>0) → REDUCE/CLOSE,
    ///        rule exit(requestedQty>0) → 尊重显式数量, 否则 CLOSE
    [[nodiscard]] OrderDelta computeSellDelta(
        std::int64_t currentQty, double currentWeight,
        double targetWeight, double priceForWeight, double totalAsset,
        std::int64_t requestedQty) const;

    /// @brief 买单总敞口压缩: 超出 100% 时按等比缩放所有买单
    void compressBuyTotalWeight(std::vector<domain::trading::OrderRequest>& orders) const;

    domain::trading::OrderBuilder* m_orderBuilder;
};

} // namespace domain::strategy
