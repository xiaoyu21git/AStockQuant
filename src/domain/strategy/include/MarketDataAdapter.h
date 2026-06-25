#pragma once
// ═════════════════════════════════════════════════════════════════════════
// MarketDataAdapter — 行情数据适配器 (纯 C++，零 Qt)
// 将原始行情数据转换为 MarketDataPoint 并推送到策略引擎
// 供 Jujin/MarketDataBridge/模拟行情源调用
// ═════════════════════════════════════════════════════════════════════════

#include "StrategyServiceTypes.h"
#include "StrategyManager.h"
#include "foundation/market/AStockSymbol.h"

#include <cstdint>
#include <string>

namespace domain::strategy {

class MarketDataAdapter final {
public:
    MarketDataAdapter() = default;

    /// @brief 推送单条行情到所有运行中的策略引擎
    void pushTick(const std::string& symbol, double lastPrice,
                  double volume, std::int32_t tradingDay) {
        InstrumentId instId(resolveInstrumentId(symbol));
        if (!instId.isValid()) return;
        MarketDataPoint mdp(instId, lastPrice, volume, tradingDay);
        StrategyManager::instance().pushMarketData(mdp);
    }

    /// @brief 从 symbol 字符串解析 InstrumentId
    /// @note 使用 AStockSymbol 统一解析，保留 6 位代码完整数值（SZ/SH 通过不同代码段区分，无碰撞）
    static InstrumentId resolveInstrumentId(const std::string& symbol) {
        auto sym = foundation::market::AStockSymbol::fromString(symbol);
        if (!sym.isValid()) return InstrumentId{};
        return InstrumentId{sym.instrumentId()};
    }

    /// @brief 检查是否有运行中的引擎
    [[nodiscard]] bool hasActiveEngines() const {
        return !StrategyManager::instance().empty();
    }
};

} // namespace domain::strategy
