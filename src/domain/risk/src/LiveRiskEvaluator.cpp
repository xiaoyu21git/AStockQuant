#include "LiveRiskEvaluator.h"

#include <algorithm>
#include <cmath>

namespace domain::risk {

LiveRiskMetrics LiveRiskEvaluator::computeMetrics(const AccountSnapshot& account,
                                                    const RiskConfig& config,
                                                    double& peakObservedTotalAsset) {
    LiveRiskMetrics metrics;
    if (account.totalAsset <= 0.0) {
        return metrics;
    }
    if (peakObservedTotalAsset < account.totalAsset) {
        peakObservedTotalAsset = account.totalAsset;
    }
    if (peakObservedTotalAsset > 0.0) {
        metrics.currentDrawdownPercent =
            (peakObservedTotalAsset - account.totalAsset) / peakObservedTotalAsset;
    }
    const double exposureRatio = (account.marketValue > 0.0)
        ? account.marketValue / account.totalAsset : 0.0;
    metrics.currentTotalExposurePercent = exposureRatio;
    if (config.maxTotalExposureRatio > 0.0) {
        metrics.varUsagePercent = exposureRatio / config.maxTotalExposureRatio;
    }
    return metrics;
}

double LiveRiskEvaluator::clampMaxTotalExposure(const RiskConfig& config, double fallback) {
    return config.maxTotalExposureRatio > 0.0 ? config.maxTotalExposureRatio : fallback;
}

} // namespace domain::risk