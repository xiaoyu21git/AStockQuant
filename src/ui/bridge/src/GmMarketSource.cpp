// GmMarketSource.cpp — 掘金行情数据源实现（纯 C++，零 Qt）
#include "GmMarketSource.h"
#include "../../../thirdparty/gmsdk/gmapi.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace bridge {

std::unordered_map<std::string, double> GmMarketSource::s_preCloseCache;
std::string GmMarketSource::s_cacheDate;

std::string GmMarketSource::toGmSymbol(const std::string& symbol) {
    auto dot = symbol.find('.');
    if (dot == std::string::npos) return "";
    std::string code = symbol.substr(0, dot);
    std::string exch = symbol.substr(dot + 1);
    if (exch == "SH") return "SHSE." + code;
    if (exch == "SZ") return "SZSE." + code;
    if (exch == "BJ") return "BSE."  + code;
    return "";
}

static constexpr int kDepthLevels = 5;

std::optional<GmQuote> GmMarketSource::fetchQuote(const std::string& symbol) {
    std::string gmSym = toGmSymbol(symbol);
    if (gmSym.empty()) return std::nullopt;

    // 先试实时快照，失败回退盘后快照
    auto* arr = ::current(gmSym.c_str(), false);
    if (!arr || arr->status() != 0 || arr->count() == 0) {
        if (arr) arr->release();
        arr = ::last_tick(gmSym.c_str(), false);
    }
    if (!arr || arr->status() != 0 || arr->count() == 0) {
        if (arr) arr->release();
        return std::nullopt;
    }

    auto& tick = arr->at(0);
    GmQuote q;
    q.symbol   = symbol;
    q.price    = static_cast<double>(tick.price);
    q.open     = static_cast<double>(tick.open);
    q.high     = static_cast<double>(tick.high);
    q.low      = static_cast<double>(tick.low);
    q.preClose = fetchPreClose(symbol);
    q.volume   = tick.cum_volume;
    q.valid    = true;

    for (int i = 0; i < kDepthLevels; ++i) {
        auto& lv = tick.quotes[i];
        if (lv.bid_price > 0) q.bids.push_back({static_cast<double>(lv.bid_price), static_cast<double>(lv.bid_volume)});
        if (lv.ask_price > 0) q.asks.push_back({static_cast<double>(lv.ask_price), static_cast<double>(lv.ask_volume)});
    }

    arr->release();
    return q;
}

double GmMarketSource::fetchPreClose(const std::string& symbol) {
    // 日期缓存：每天清一次
    time_t now = time(nullptr);
    char today[16];
    strftime(today, sizeof(today), "%Y%m%d", localtime(&now));
    if (s_cacheDate != today) {
        s_preCloseCache.clear();
        s_cacheDate = today;
    }
    auto it = s_preCloseCache.find(symbol);
    if (it != s_preCloseCache.end()) return it->second;

    std::string gmSym = toGmSymbol(symbol);
    if (gmSym.empty()) return 0.0;

    char start[32], end[32];
    time_t yesterday = now - 86400;
    strftime(start, sizeof(start), "%Y-%m-%d", localtime(&yesterday));
    strftime(end,   sizeof(end),   "%Y-%m-%d", localtime(&now));

    auto* bars = ::history_bars(gmSym.c_str(), "1d", start, end, 0, nullptr, true, nullptr);
    double pc = 0.0;
    if (bars && bars->status() == 0 && bars->count() > 0)
        pc = static_cast<double>(bars->at(0).close);
    if (bars) bars->release();

    s_preCloseCache[symbol] = pc;
    return pc;
}

} // namespace bridge
