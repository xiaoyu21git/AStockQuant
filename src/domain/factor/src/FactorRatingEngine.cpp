#include "FactorRatingEngine.h"

#include <cmath>

namespace domain::factor {

int32_t FactorRatingEngine::computeRating(double rankIcir, double icWinRate,
                                            double costAdjustedSharpe, double annualTurnover,
                                            double monotonicityScore) {
    int score = 0;
    if (std::isfinite(rankIcir) && rankIcir >= 0.8) score += 2;
    else if (std::isfinite(rankIcir) && rankIcir >= 0.5) score += 1;

    if (std::isfinite(icWinRate) && icWinRate >= 0.55) score += 1;

    if (std::isfinite(costAdjustedSharpe) && costAdjustedSharpe >= 0.5) score += 1;

    if (std::isfinite(monotonicityScore) && std::abs(monotonicityScore) >= 0.7) score += 1;

    if (score >= 5) return 3;
    if (score >= 3) return 2;
    if (score >= 1) return 1;
    return 0;
}

std::string FactorRatingEngine::computeSummary(double rankIcir, double monotonicityScore,
                                                 double costAdjustedSharpe) {
    if (!std::isfinite(rankIcir)) return "ir_unavailable";

    std::string summary;
    if (rankIcir >= 0.8) summary = "ir_excellent";
    else if (rankIcir >= 0.5) summary = "ir_good";
    else if (rankIcir >= 0.3) summary = "ir_fair";
    else summary = "ir_low";

    if (std::isfinite(monotonicityScore)) {
        double mono = std::abs(monotonicityScore);
        if (mono >= 0.9) summary += ",mono_excellent";
        else if (mono >= 0.7) summary += ",mono_good";
        else summary += ",mono_fair";
    }

    if (std::isfinite(costAdjustedSharpe) && costAdjustedSharpe > 0.5) {
        summary += ",cost_sharpe_good";
    }

    return summary;
}

std::vector<FactorRatingCheck> FactorRatingEngine::buildRatingChecks(double rankIcir,
                                                                        double icWinRate,
                                                                        double costAdjustedSharpe,
                                                                        double annualTurnover,
                                                                        double monotonicityScore) {
    std::vector<FactorRatingCheck> checks;

    bool irOk = std::isfinite(rankIcir);
    checks.push_back({"rank_icir", irOk && rankIcir >= 0.3,
        irOk ? std::to_string(rankIcir) : "N/A", ">= 0.30"});

    bool wrOk = std::isfinite(icWinRate);
    checks.push_back({"ic_winrate", wrOk && icWinRate >= 0.55,
        wrOk ? std::to_string(icWinRate) : "N/A", ">= 0.55"});

    bool csOk = std::isfinite(costAdjustedSharpe);
    checks.push_back({"cost_sharpe", csOk && costAdjustedSharpe >= 0.0,
        csOk ? std::to_string(costAdjustedSharpe) : "N/A", ">= 0.00"});

    bool toOk = std::isfinite(annualTurnover);
    checks.push_back({"turnover", toOk && annualTurnover <= 3.0,
        toOk ? std::to_string(annualTurnover) : "N/A", "<= 3.0"});

    bool moOk = std::isfinite(monotonicityScore);
    checks.push_back({"monotonicity", moOk && std::abs(monotonicityScore) >= 0.5,
        moOk ? std::to_string(monotonicityScore) : "N/A", "abs >= 0.50"});

    return checks;
}

std::string FactorRatingEngine::ratingLabel(int32_t rating) {
    switch (rating) {
    case 3: return "excellent_3star";
    case 2: return "good_2star";
    case 1: return "fair_1star";
    default: return "fail";
    }
}

} // namespace domain::factor