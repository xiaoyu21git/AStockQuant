#pragma once

#include <cstdint>

namespace domain::risk {

struct AccountSnapshot {
    double totalAsset = 0.0;
    double marketValue = 0.0;
    double availableCash = 0.0;
    double realizedPnl = 0.0;
    double unrealizedPnl = 0.0;
    std::string accountId;
};

struct RiskConfig {
    double maxTotalExposureRatio = 0.67;
    double maxSinglePositionRatio = 0.20;
    double stopLossRatio = 0.10;
};

struct LiveRiskMetrics {
    double currentDrawdownPercent = 0.0;
    double varUsagePercent = 0.0;
    double currentTotalExposurePercent = 0.0;
};

class LiveRiskEvaluator {
public:
    LiveRiskMetrics computeMetrics(const AccountSnapshot& account,
                                    const RiskConfig& config,
                                    double& peakObservedTotalAsset);

    static double clampMaxTotalExposure(const RiskConfig& config, double fallback);
};

} // namespace domain::risk