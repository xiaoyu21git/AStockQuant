#pragma once
// OrderGenerator — 持仓感知建单器
// 将策略原始订单（含 targetWeight）与当前持仓对比，生成最终可提交的 OrderRequest。
//
// 职责: 去重 + 调用 PositionSizer 计算买卖量 + 调用 OrderBuilder 标准化字段。
// 手数/权重计算已收敛到 PositionSizer，此处不再重复实现。

#include "../../trading/TradingTypes.h"
#include "../../trading/include/OrderBuilder.h"
#include "PositionSizer.h"
#include "StrategyServiceTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace domain::strategy {

/// @brief 持仓查询接口（解耦 AccountEngine 全局单例）
class IPositionProvider {
public:
    virtual ~IPositionProvider() = default;
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
    /// @param orderBuilder 订单标准化器
    /// @param sizer 仓位计算器（持有 maxOrderQuantity 等配置）
    OrderGenerator(domain::trading::OrderBuilder& orderBuilder,
                   const PositionSizer& sizer)
        : m_orderBuilder(&orderBuilder), m_sizer(&sizer) {}

    /// @brief 从策略原始信号生成持仓感知订单
    /// @param rawOrders 策略原始信号（含 symbol/side/targetWeight/signalScore）
    /// @param posProvider 当前持仓查询接口
    /// @param strategyId 策略ID
    /// @param accountId 账户ID
    /// @return 最终可提交的订单列表（已去重、已校验最小手数）
    [[nodiscard]] std::vector<domain::trading::OrderRequest> generate(
        const std::vector<domain::trading::OrderRequest>& rawOrders,
        const IPositionProvider& posProvider,
        const std::string& strategyId,
        const std::string& accountId) const;

private:
    domain::trading::OrderBuilder* m_orderBuilder;
    const PositionSizer* m_sizer;
};

} // namespace domain::strategy
