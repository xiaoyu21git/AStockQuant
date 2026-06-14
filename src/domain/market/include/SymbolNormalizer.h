#pragma once

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace domain::market {

enum class Exchange : uint8_t {
    SHSE,
    SZSE,
    BSE,
    CFFEX,
    SHFE,
    DCE,
    CZCE,
    INE,
    GFEX,
    Unknown
};

class Symbol {
public:
    explicit Symbol(std::string raw) : m_raw(std::move(raw)) {}

    const std::string& raw() const { return m_raw; }
    bool empty() const { return m_raw.empty(); }
    bool operator==(const Symbol& other) const { return m_raw == other.m_raw; }

private:
    std::string m_raw;
};

struct ExchangeInfo {
    Exchange exchange;
    std::string_view suffix;     // "SH", "SZ", "BJ", "CFFEX"...
    std::string_view prefix;     // "SHSE", "SZSE", "BSE", "CFFEX"...
    int prefixLen;
};

class SymbolNormalizer {
public:
    // "000001.SH" -> Symbol
    static Symbol normalize(std::string_view raw);

    // Symbol -> "000001" (strip exchange suffix)
    static std::string canonicalCode(const Symbol& symbol);

    // "000001.SH" -> Exchange::SHSE
    static Exchange exchangeFromSymbol(const Symbol& symbol);

    // Exchange -> ".SH" style suffix string
    static std::string_view exchangeSuffix(Exchange exchange);

    // Exchange -> "SH" style short suffix (for DB/code)
    static std::string_view exchangeShortSuffix(Exchange exchange);

    // "SHSE.000001" / "SZSE.000002" style prefix -> Exchange
    static Exchange exchangeFromPrefix(std::string_view prefix);

private:
    static constexpr std::array<ExchangeInfo, 9> s_exchanges = {
        ExchangeInfo{Exchange::SHSE,  ".SH",    "SHSE",  4},
        ExchangeInfo{Exchange::SZSE,  ".SZ",    "SZSE",  4},
        ExchangeInfo{Exchange::BSE,   ".BJ",    "BSE",   3},
        ExchangeInfo{Exchange::CFFEX, ".CFFEX", "CFFEX", 5},
        ExchangeInfo{Exchange::SHFE,  ".SHFE",  "SHFE",  4},
        ExchangeInfo{Exchange::DCE,   ".DCE",   "DCE",   3},
        ExchangeInfo{Exchange::CZCE,  ".CZCE",  "CZCE",  4},
        ExchangeInfo{Exchange::INE,   ".INE",   "INE",   3},
        ExchangeInfo{Exchange::GFEX,  ".GFEX",  "GFEX",  4},
    };
};

} // namespace domain::market