// GmMarketSource.h — 掘金行情数据源（纯 C++，零 Qt）
#pragma once
#include <string>
#include <optional>
#include <vector>
#include <unordered_map>

namespace bridge {

struct GmQuote {
    std::string symbol;
    double price = 0.0, open = 0.0, high = 0.0, low = 0.0;
    double preClose = 0.0;
    double volume = 0.0;
    struct DepthLevel { double price = 0.0; double volume = 0.0; };
    std::vector<DepthLevel> bids;
    std::vector<DepthLevel> asks;
    bool valid = false;

    /// 通用涨跌幅限制（A股主板±10%，科创板/创业板±20%，北交所±30%）
    double limitPct() const {
        if (symbol.size() < 3) return 10.0;
        char e2 = symbol[symbol.size()-2]; // 交易所 S/B
        std::string code = symbol.substr(0, symbol.size()-3);
        if (code.size() == 6 && (code[0] == '3' || (code[0] == '6' && code[1] == '8'))) return 20.0;
        if (e2 == 'B') return 30.0; // BJ
        return 10.0;
    }
    double changePct() const { return preClose > 0 ? (price - preClose) / preClose * 100.0 : 0.0; }
    bool isLimitUp() const { return preClose > 0 && changePct() >= limitPct() - 0.05; }
    bool isLimitDown() const { return preClose > 0 && changePct() <= -limitPct() + 0.05; }
};

/// @brief 掘金行情数据源（纯 C++，零 Qt 依赖）
class GmMarketSource {
public:
    /// @brief 拉取实时/盘后快照
    static std::optional<GmQuote> fetchQuote(const std::string& aStockSymbol);

    /// @brief 拉取昨收价（含日级缓存）
    static double fetchPreClose(const std::string& aStockSymbol);

private:
    static std::string toGmSymbol(const std::string& aStockSymbol);
    static std::unordered_map<std::string, double> s_preCloseCache;
    static std::string s_cacheDate;
};

} // namespace bridge
