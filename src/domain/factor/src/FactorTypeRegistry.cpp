#include "FactorTypeRegistry.h"

namespace domain::factor {

std::string FactorTypeRegistry::displayName(factor::FactorType type) {
    switch (type) {
    case factor::FactorType::VALUE:          return "value";
    case factor::FactorType::MOMENTUM:       return "momentum";
    case factor::FactorType::SIZE:           return "size";
    case factor::FactorType::QUALITY:        return "quality";
    case factor::FactorType::GROWTH:         return "growth";
    case factor::FactorType::DIVIDEND:       return "dividend";
    case factor::FactorType::TECHNICAL:      return "technical";
    case factor::FactorType::LIQUIDITY:      return "liquidity";
    case factor::FactorType::MACRO:          return "macro";
    case factor::FactorType::INDUSTRY:       return "industry";
    case factor::FactorType::SENTIMENT:      return "sentiment";
    case factor::FactorType::CUSTOM:         return "custom";
    case factor::FactorType::LOW_VOLATILITY: return "low_volatility";
    case factor::FactorType::COMPOSITE:      return "composite";
    case factor::FactorType::EVENT_DRIVEN:   return "event_driven";
    case factor::FactorType::REVERSAL:       return "reversal";
    case factor::FactorType::HIGH_FREQ:      return "high_freq";
    case factor::FactorType::DL:             return "dl";
    case factor::FactorType::SUPPLY_CHAIN:   return "supply_chain";
    default:                                 return "unknown";
    }
}

std::string FactorTypeRegistry::typeId(factor::FactorType type) {
    return displayName(type);
}

factor::FactorType FactorTypeRegistry::fromIndex(int index) {
    switch (index) {
    case 0:  return factor::FactorType::VALUE;
    case 1:  return factor::FactorType::MOMENTUM;
    case 2:  return factor::FactorType::SIZE;
    case 3:  return factor::FactorType::QUALITY;
    case 4:  return factor::FactorType::GROWTH;
    case 5:  return factor::FactorType::DIVIDEND;
    case 6:  return factor::FactorType::TECHNICAL;
    case 7:  return factor::FactorType::LIQUIDITY;
    case 8:  return factor::FactorType::MACRO;
    case 9:  return factor::FactorType::INDUSTRY;
    case 10: return factor::FactorType::SENTIMENT;
    case 11: return factor::FactorType::CUSTOM;
    case 12: return factor::FactorType::LOW_VOLATILITY;
    case 13: return factor::FactorType::COMPOSITE;
    case 14: return factor::FactorType::EVENT_DRIVEN;
    case 15: return factor::FactorType::REVERSAL;
    case 16: return factor::FactorType::HIGH_FREQ;
    case 17: return factor::FactorType::DL;
    case 18: return factor::FactorType::SUPPLY_CHAIN;
    default: return factor::FactorType::UNKNOWN;
    }
}

} // namespace domain::factor
