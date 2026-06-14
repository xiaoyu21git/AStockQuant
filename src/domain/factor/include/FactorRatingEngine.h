#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace domain::factor {

struct FactorRatingCheck {
    std::string label;
    bool passed = false;
    std::string actualText;
    std::string thresholdText;
};

class FactorRatingEngine {
public:
    static int32_t computeRating(double rankIcir, double icWinRate,
                                  double costAdjustedSharpe, double annualTurnover,
                                  double monotonicityScore);

    static std::string computeSummary(double rankIcir, double monotonicityScore,
                                       double costAdjustedSharpe);

    static std::vector<FactorRatingCheck> buildRatingChecks(double rankIcir,
                                                              double icWinRate,
                                                              double costAdjustedSharpe,
                                                              double annualTurnover,
                                                              double monotonicityScore);

    static std::string ratingLabel(int32_t rating);
};

} // namespace domain::factor