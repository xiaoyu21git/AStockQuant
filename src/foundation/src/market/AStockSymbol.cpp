#include "foundation/market/AStockSymbol.h"
#include "foundation/market/ExchangeMapper.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace foundation {
namespace market {

// ============ 交易所推断规则 ============
//
// 上海 (SSE):
//   600xxx, 601xxx, 603xxx, 605xxx — 主板
//   688xxx — 科创板
//
// 深圳 (SZSE):
//   000xxx~003xxx — 主板
//   300xxx, 301xxx — 创业板
//
// 北京 (BSE):
//   8xxxxx (830xxx~839xxx, 870xxx~879xxx 等)

Exchange AStockSymbol::inferExchange(const std::string& code) {
    if (code.size() < 3) return Exchange::Unknown;
    int prefix3 = (code[0] - '0') * 100 + (code[1] - '0') * 10 + (code[2] - '0');

    if (prefix3 >= 600 && prefix3 <= 609) return Exchange::SSE;  // 600~609
    if (prefix3 >= 680 && prefix3 <= 689) return Exchange::SSE;  // 688 (科创板)
    if (prefix3 >= 0   && prefix3 <= 3)   return Exchange::SZSE; // 000~003, 300~301
    if (prefix3 >= 300 && prefix3 <= 301) return Exchange::SZSE; // 创业板
    if (prefix3 >= 800 && prefix3 <= 899) return Exchange::BSE;  // 北交所
    if (prefix3 == 399) return Exchange::SZSE;                   // 深证指数

    return Exchange::Unknown;
}

Board AStockSymbol::inferBoard(const std::string& code) {
    if (code.size() < 3) return Board::Unknown;
    int prefix3 = (code[0] - '0') * 100 + (code[1] - '0') * 10 + (code[2] - '0');

    if (prefix3 == 688)                    return Board::STAR;
    if (prefix3 == 300 || prefix3 == 301)  return Board::ChiNext;
    if (prefix3 >= 0   && prefix3 <= 3)    return Board::Main;
    if (prefix3 >= 600 && prefix3 <= 609)  return Board::Main;
    if (prefix3 >= 800 && prefix3 <= 899)  return Board::Main;  // 北交所暂按主板

    return Board::Unknown;
}

// ============ 构造 ============

AStockSymbol::AStockSymbol(std::string code, Exchange exchange, Board board)
    : m_code(std::move(code)), m_exchange(exchange), m_board(board) {}

AStockSymbol AStockSymbol::fromString(const std::string& symbol) {
    if (symbol.empty()) return {};

    auto dot = symbol.find('.');
    if (dot == std::string::npos) {
        // 无后缀 → 根据代码推断
        return fromCode(symbol);
    }

    std::string code = symbol.substr(0, dot);
    std::string suffix = symbol.substr(dot + 1);

    // 转大写
    std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    Exchange ex = ExchangeMapper::fromSuffix(suffix);
    if (ex == Exchange::Unknown)
        ex = ExchangeMapper::fromPrefix(suffix);  // 掘金格式 SZSE / SHSE / BSE
    if (ex == Exchange::Unknown)
        ex = inferExchange(code);  // fallback: 从代码推断

    return {code, ex, inferBoard(code)};
}

AStockSymbol AStockSymbol::fromCode(const std::string& code) {
    if (code.empty()) return {};
    Exchange ex = inferExchange(code);
    return {code, ex, inferBoard(code)};
}

// ============ 格式转换 ============

std::string AStockSymbol::suffix() const {
    switch (m_exchange) {
    case Exchange::SSE:  return ".SH";
    case Exchange::SZSE: return ".SZ";
    case Exchange::BSE:  return ".BJ";
    default:             return "";
    }
}

std::string AStockSymbol::fullSymbol() const {
    return m_code + suffix();
}

std::string AStockSymbol::gmSymbol() const {
    switch (m_exchange) {
    case Exchange::SSE:  return "SHSE." + m_code;
    case Exchange::SZSE: return "SZSE." + m_code;
    case Exchange::BSE:  return "BSE." + m_code;
    default:             return m_code;
    }
}

uint32_t AStockSymbol::instrumentId() const {
    if (m_code.empty()) return 0;
    try {
        return static_cast<uint32_t>(std::stoul(m_code));
    } catch (...) {
        return 0;
    }
}

std::string AStockSymbol::codeOnly(const std::string& symbol) {
    auto dot = symbol.find('.');
    return (dot != std::string::npos) ? symbol.substr(0, dot) : symbol;
}

std::string AStockSymbol::fromInstrumentId(uint32_t id) {
    if (id > 999999) return "";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%06u", id);
    return fromCode(std::string(buf)).fullSymbol();
}

} // namespace market
} // namespace foundation
