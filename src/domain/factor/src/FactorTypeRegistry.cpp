#include "FactorTypeRegistry.h"

namespace domain::factor {

std::string FactorTypeRegistry::displayName(FactorType type) {
    switch (type) {
    case FactorType::Value: return "value";
    case FactorType::Momentum: return "momentum";
    case FactorType::Size: return "size";
    case FactorType::Quality: return "quality";
    case FactorType::LowVolatility: return "low_volatility";
    case FactorType::Growth: return "growth";
    case FactorType::Dividend: return "dividend";
    case FactorType::Technical: return "technical";
    case FactorType::Liquidity: return "liquidity";
    case FactorType::Macro: return "macro";
    case FactorType::Industry: return "industry";
    case FactorType::Sentiment: return "sentiment";
    case FactorType::Custom: return "custom";
    default: return "unknown";
    }
}

std::string FactorTypeRegistry::typeId(FactorType type) {
    switch (type) {
    case FactorType::Value: return "value";
    case FactorType::Momentum: return "momentum";
    case FactorType::Size: return "size";
    case FactorType::Quality: return "quality";
    case FactorType::LowVolatility: return "low_volatility";
    case FactorType::Growth: return "growth";
    case FactorType::Dividend: return "dividend";
    case FactorType::Technical: return "technical";
    case FactorType::Liquidity: return "liquidity";
    case FactorType::Macro: return "macro";
    case FactorType::Industry: return "industry";
    case FactorType::Sentiment: return "sentiment";
    case FactorType::Custom: return "custom";
    default: return "unknown";
    }
}

FactorType FactorTypeRegistry::fromIndex(int index) {
    switch (index) {
    case 0: return FactorType::Value;
    case 1: return FactorType::Momentum;
    case 2: return FactorType::Size;
    case 3: return FactorType::Quality;
    case 4: return FactorType::LowVolatility;
    case 5: return FactorType::Growth;
    case 6: return FactorType::Dividend;
    case 7: return FactorType::Technical;
    case 8: return FactorType::Liquidity;
    case 9: return FactorType::Macro;
    case 10: return FactorType::Industry;
    case 11: return FactorType::Sentiment;
    default: return FactorType::Custom;
    }
}

} // namespace domain::factor