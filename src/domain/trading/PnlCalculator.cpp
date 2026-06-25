#include "PnlCalculator.h"

#include <algorithm>

namespace domain::trading {

// ============ FeeParams ============

FeeParams::FeeParams(double commissionRate, double minCommission, double stampTaxRate)
    : m_commissionRate(commissionRate)
    , m_minCommission(minCommission)
    , m_stampTaxRate(stampTaxRate) {}

// ============ PnlCalculator ============

PnlCalculator::PnlCalculator(const FeeParams& params)
    : m_params(params) {}

double PnlCalculator::calcCommission(double notional) const {
    if (notional <= 0.0) return 0.0;
    double byRate = notional * m_params.commissionRate();
    return std::max(byRate, m_params.minCommission());
}

double PnlCalculator::calcStampTax(double notional, bool isSell) const {
    if (!isSell || notional <= 0.0) return 0.0;
    return notional * m_params.stampTaxRate();
}

PnlResult PnlCalculator::realizedPnl(double buyPrice, double sellPrice, std::int64_t qty) const {
    PnlResult r;
    if (qty <= 0 || buyPrice <= 0.0 || sellPrice <= 0.0) return r;

    double buyNotional  = buyPrice * static_cast<double>(qty);
    double sellNotional = sellPrice * static_cast<double>(qty);

    r.m_grossPnl   = (sellPrice - buyPrice) * static_cast<double>(qty);
    r.m_commission = calcCommission(buyNotional) + calcCommission(sellNotional);
    r.m_stampTax   = calcStampTax(sellNotional, true);
    r.m_netPnl     = r.m_grossPnl - r.m_commission - r.m_stampTax;
    return r;
}

PositionPnl PnlCalculator::unrealizedPnl(double avgCost, double lastPrice, std::int64_t qty) const {
    PositionPnl p;
    if (qty <= 0 || avgCost <= 0.0) return p;

    p.m_costBasis     = avgCost;
    p.m_marketValue   = lastPrice * static_cast<double>(qty);
    p.m_unrealizedPnl = (lastPrice - avgCost) * static_cast<double>(qty);
    if (avgCost > 0.0) {
        p.m_pnlPercent = (lastPrice - avgCost) / avgCost * 100.0;
    }
    return p;
}

} // namespace domain::trading
