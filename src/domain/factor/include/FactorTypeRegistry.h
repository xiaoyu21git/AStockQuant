#pragma once

#include <cstdint>
#include <string>

namespace domain::factor {

enum class FactorType : uint8_t {
    Value = 0, Momentum, Size, Quality, LowVolatility, Growth,
    Dividend, Technical, Liquidity, Macro, Industry, Sentiment, Custom,
    Reversal, HighFreq, DL
};

class FactorTypeRegistry {
public:
    static std::string displayName(FactorType type);
    static std::string typeId(FactorType type);
    static FactorType fromIndex(int index);
};

} // namespace domain::factor