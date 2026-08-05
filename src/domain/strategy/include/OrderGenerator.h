#pragma once
// OrderGenerator — 持仓感知建单器
// 将策略原始订单（含 targetWeight）与当前持仓对比，计算实际买卖数量及意图，
// 生成最终可提交的 OrderRequest 列表。
//
// 职责: 纯计算 + 去重 + 最小手数校验，不涉及 I/O、不持有可变状态。
// 依赖: 注入 OrderBuilder（用于标准化订单字段），IPositionProvider（查询持仓）。

#include "../../trading/TradingTypes.h"
#include "../../trading/include/OrderBuilder.h"

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
    domain::trading::OrderBuilder* m_orderBuilder;
};

} // namespace domain::strategy
