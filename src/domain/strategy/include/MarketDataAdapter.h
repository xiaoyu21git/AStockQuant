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
    /// "000001.SZ" → 1, "600000.SH" → 600000, "000001" → 1
    static InstrumentId resolveInstrumentId(const std::string& symbol) {
        if (symbol.empty()) return InstrumentId{};
        // 去除交易所后缀
        std::string code = symbol;
        auto dotPos = code.find('.');
        if (dotPos != std::string::npos) code = code.substr(0, dotPos);
        // 去除前导零
        while (code.size() > 1 && code[0] == '0') code.erase(0, 1);
        // 解析为 uint32_t
        try {
            std::uint32_t id = static_cast<std::uint32_t>(std::stoul(code));
            return InstrumentId{id};
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
