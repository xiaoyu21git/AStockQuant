#include "SymbolNormalizer.h"

#include <algorithm>
#include <cctype>

namespace domain::market {

Symbol SymbolNormalizer::normalize(std::string_view raw) {
    if (raw.empty()) {
        return Symbol{};
    }

    std::string upper;
    upper.reserve(raw.size());
    for (char ch : raw) {
        if (ch == ' ') continue;
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    if (upper.empty()) {
        return Symbol{};
    }

    // "SHSE.000001" style -> "000001.SH" style
    const auto dot = upper.find('.');
    if (dot == std::string::npos) {
        return Symbol(std::move(upper));
    }

    const std::string_view prefix{&upper[0], dot};
    const std::string_view code = std::string_view{upper}.substr(dot + 1);

    const Exchange prefixExchange = exchangeFromPrefix(prefix);
    if (prefixExchange != Exchange::Unknown) {
        // Convert "SHSE.000001" -> "000001.SH"
        std::string result;
        result.reserve(code.size() + 4);
        result.append(code);
        result.append(exchangeSuffix(prefixExchange));
        return Symbol(std::move(result));
    }

    // "000001.SHSE" style -> "000001.SH" style
    const Exchange suffixExchange = exchangeFromPrefix(code);
    if (suffixExchange != Exchange::Unknown) {
        std::string result;
        result.reserve(prefix.size() + 4);
        result.append(prefix);
        result.append(exchangeSuffix(suffixExchange));
        return Symbol(std::move(result));
    }

    return Symbol(std::move(upper));
}

std::string SymbolNormalizer::canonicalCode(const Symbol& symbol) {
    const std::string_view raw = symbol.raw();
    const auto dot = raw.find('.');
    if (dot == std::string::npos) {
        return std::string{raw};
    }

    const std::string_view first = raw.substr(0, dot);
    const std::string_view second = raw.substr(dot + 1);

    // If first part looks like exchange prefix, return second part as code
    if (exchangeFromPrefix(first) != Exchange::Unknown) {
        return std::string{second};
    }
    return std::string{first};
}

Exchange SymbolNormalizer::exchangeFromSymbol(const Symbol& symbol) {
    const std::string_view raw = symbol.raw();
    const auto dot = raw.find('.');
    if (dot == std::string::npos) {
        return Exchange::Unknown;
    }

    const std::string_view suffix = raw.substr(dot + 1);
    for (const auto& info : s_exchanges) {
        if (info.suffix == suffix) {
            return info.exchange;
        }
    }
    return Exchange::Unknown;
}

std::string_view SymbolNormalizer::exchangeSuffix(Exchange exchange) {
    for (const auto& info : s_exchanges) {
        if (info.exchange == exchange) {
            return info.suffix;
        }
    }
    return {};
}

std::string_view SymbolNormalizer::exchangeShortSuffix(Exchange exchange) {
    for (const auto& info : s_exchanges) {
        if (info.exchange == exchange) {
            // ".SH" -> "SH", ".SZ" -> "SZ", etc.
            return info.suffix.substr(1);
        }
    }
    return {};
}

Exchange SymbolNormalizer::exchangeFromPrefix(std::string_view prefix) {
    for (const auto& info : s_exchanges) {
        if (prefix.size() == static_cast<size_t>(info.prefixLen) && info.prefix == prefix) {
            return info.exchange;
        }
    }
    return Exchange::Unknown;
}

} // namespace domain::market