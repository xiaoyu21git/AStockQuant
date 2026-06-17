#pragma once
// ═════════════════════════════════════════════════════════════════════════
// MarketDataAdapter — 行情数据适配器 (纯 C++，零 Qt)
// 将原始行情数据转换为 MarketDataPoint 并推送到策略引擎
// 供 Jujin/MarketDataBridge/模拟行情源调用
// ═════════════════════════════════════════════════════════════════════════

#include "StrategyServiceTypes.h"
#include "StrategyManager.h"

#include <cstdint>
#include <string>

namespace domain::strategy {

class MarketDataAdapter final {
public:
    MarketDataAdapter() = default;

    /// @brief 推送单条行情到所有运行中的策略引擎
    /// @param symbol       标的代码 (如 "000001.SZ", "600000.SH")
    /// @param lastPrice    最新价
    /// @param volume       成交量
    /// @param tradingDay   交易日 (YYYYMMDD int32_t)
    void pushTick(const std::string& symbol, double lastPrice,
                  double volume, std::int32_t tradingDay) {
        InstrumentId instId(resolveInstrumentId(symbol));
        if (!instId.isValid()) return;
        MarketDataPoint mdp(instId, lastPrice, volume, tradingDay);
        StrategyManager::instance().pushMarketData(mdp);
    }

    /// @brief 从 symbol 字符串解析 InstrumentId
    /// ⚠️ 已知问题: 去前导零后 SZ/SH 相同代码会碰撞 ("000001.SZ" 和 "000001.SH" 都是 1)
    /// 实盘应通过 RuntimeFactorSvc 的 symbol resolver 做反向查找，或使用 symbol → ID 映射表
    static InstrumentId resolveInstrumentId(const std::string& symbol) {
        if (symbol.empty()) return InstrumentId{};
        std::string code = symbol;
        auto dotPos = code.find('.');
        if (dotPos != std::string::npos) code = code.substr(0, dotPos);
        while (code.size() > 1 && code[0] == '0') code.erase(0, 1);
        try {
            return InstrumentId{static_cast<std::uint32_t>(std::stoul(code))};
        } catch (...) {
            return InstrumentId{};
        }
    }

    /// @brief 检查是否有运行中的引擎
    [[nodiscard]] bool hasActiveEngines() const {
        return !StrategyManager::instance().empty();
    }
};

} // namespace domain::strategy
