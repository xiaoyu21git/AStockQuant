#include "SubmissionFinalizer.h"

#include "OrderGenerator.h"
#include "TradeJournal.h"
#include "../../../infrastructure/include/database/AppStateStore.h"
#include "../../trading/TradingTypes.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace domain::strategy {

SubmissionFinalizer::SubmissionFinalizer(
    std::string strategyId, BarPeriod period,
    OrderGenerator& orderGenerator, TradeJournal* tradeJournal,
    std::shared_ptr<astock::infrastructure::database::AppStateStore> store,
    std::function<void(const std::vector<OrderRequest>&)> dispatchFn)
    : m_strategyId(std::move(strategyId))
    , m_period(period)
    , m_orderGenerator(&orderGenerator)
    , m_tradeJournal(tradeJournal)
    , m_store(std::move(store))
    , m_dispatchFn(std::move(dispatchFn))
{
}

std::uint64_t SubmissionFinalizer::generateBasketId() {
    static std::atomic<std::uint64_t> s_basketSeq{0};
    auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
    auto id = std::to_string(ts) + "_" + std::to_string(s_basketSeq.fetch_add(1));
    return std::hash<std::string>{}(id);
}

std::uint64_t SubmissionFinalizer::tagBasketOrders(std::vector<OrderRequest>& orders) {
    std::uint64_t basketId = generateBasketId();
    for (auto& o : orders)
        o.setExtension(domain::trading::ExtKey::kBasketId, basketId);
    return basketId;
}

bool SubmissionFinalizer::alreadySubmitted(const std::string& tradingDay,
                                           std::uint64_t& outBasketId) const {
    if (tradingDay.empty() || !m_store || m_strategyId.empty()) return false;
    const std::string base = m_strategyId + "." + BarPeriodNaming::suffix(m_period);
    std::int64_t day{0};
    if (!m_store->readInt("lastBasket", base + ".day", day)) return false;
    std::int64_t storedDay{0};
    try {
        storedDay = std::stoll(tradingDay);
    } catch (...) {
        return false;  // 交易日非法 → 不拦截 (防御, 正常链路不可达)
    }
    if (day != storedDay) return false;
    std::int64_t basket{0};
    m_store->readInt("lastBasket", base + ".basketId", basket);
    outBasketId = static_cast<std::uint64_t>(basket);
    return true;
}

void SubmissionFinalizer::persistSubmission(const std::string& tradingDay,
                                            std::uint64_t basketId) {
    if (tradingDay.empty() || !m_store || m_strategyId.empty()) return;
    std::int64_t day{0};
    try {
        day = std::stoll(tradingDay);
    } catch (...) {
        return;
    }
    const std::string base = m_strategyId + "." + BarPeriodNaming::suffix(m_period);
    m_store->writeInt("lastBasket", base + ".day", day);
    m_store->writeInt("lastBasket", base + ".basketId", static_cast<std::int64_t>(basketId));
}

SubmissionResult SubmissionFinalizer::submit(const SubmissionRequest& req) {
    SubmissionResult result;

    // ── 幂等去重 (ADR-006): 在 OrderGenerator 之前, 已提交 → skipped 直接返回 ──
    // (P6: 日志中性化 — 该跳过与入口模式无关, EOD 与补单共用同一幂等拦截)
    if (!req.mandatory) {
        std::uint64_t prevBasket{0};
        if (alreadySubmitted(req.tradingDay, prevBasket)) {
            result.skipped = true;
            result.basketId = prevBasket;
            INTERNAL_INFO_STREAM << "[Eval] 提交跳过: 当日已提交 basketId=" << prevBasket
                                 << " (策略=" << req.strategyId << " 周期="
                                 << BarPeriodNaming::suffix(m_period) << ")";
            return result;
        }
    }

    // ── 持仓感知生成 (清仓同样经过, 不经过规则闸门) ──
    MapPositionProvider posProvider(req.positionQtyMap);
    auto finalOrders = m_orderGenerator->generate(
        req.rawOrders, posProvider, req.strategyId, req.accountId);
    INTERNAL_INFO_STREAM << "[EOD QtyDiag] finalOrders=" << finalOrders.size();

    if (finalOrders.empty()) return result;

    // ── 最终订单 Top-3 日志 (原 finalizeAndSubmit 提交段保留) ──
    {
        std::vector<OrderRequest> finalBuys, finalSells;
        for (const auto& fo : finalOrders) {
            if (fo.side() == OrderSide::Buy) finalBuys.push_back(fo);
            else finalSells.push_back(fo);
        }
        auto byWeight = [](const OrderRequest& a, const OrderRequest& b) {
            return a.extensionAs<double>(domain::trading::ExtKey::kTargetWeight, 0.0)
                 > b.extensionAs<double>(domain::trading::ExtKey::kTargetWeight, 0.0);
        };
        std::sort(finalBuys.begin(), finalBuys.end(), byWeight);
        std::sort(finalSells.begin(), finalSells.end(), byWeight);
        for (size_t i = 0; i < finalBuys.size() && i < 3; ++i) {
            double tw = finalBuys[i].extensionAs<double>(domain::trading::ExtKey::kTargetWeight, -1.0);
            INTERNAL_INFO_STREAM << "[EOD Order] 买入TOP" << (i + 1) << " " << finalBuys[i].symbol()
                                 << " tw=" << tw << " qty=" << finalBuys[i].quantity();
        }
        for (size_t i = 0; i < finalSells.size() && i < 3; ++i) {
            double tw = finalSells[i].extensionAs<double>(domain::trading::ExtKey::kTargetWeight, -1.0);
            INTERNAL_INFO_STREAM << "[EOD Order] 卖出TOP" << (i + 1) << " " << finalSells[i].symbol()
                                 << " tw=" << tw << " qty=" << finalSells[i].quantity();
        }
    }

    // ── 篮子ID + 投递 + journal ──
    result.basketId = tagBasketOrders(finalOrders);
    m_dispatchFn(finalOrders);
    result.totalSubmitted = static_cast<std::int64_t>(finalOrders.size());
    result.submittedOrders = finalOrders;

    INTERNAL_INFO_STREAM << "[Eval] 篮子提交: basketId=" << result.basketId
                         << " orders=" << result.totalSubmitted;

    if (m_tradeJournal) {
        for (const auto& o : finalOrders) {
            std::string side = o.side() == OrderSide::Buy ? "买入" : "卖出";
            double score = o.extensionAs<double>(domain::trading::ExtKey::kSignalScore, 0.0);
            double weight = o.extensionAs<double>(domain::trading::ExtKey::kTargetWeight, 0.0);
            std::ostringstream js;
            js << req.tradingDay << " " << req.journalPrefix << " " << side << " "
               << o.symbol() << " " << o.quantity() << "股";
            if (score > 0.0) js << " 评分:" << std::fixed << std::setprecision(2) << score;
            if (weight > 0.0) js << " 权重:" << std::fixed << std::setprecision(1)
                                 << (weight * 100.0) << "%";
            m_tradeJournal->log(js.str());
        }
    }

    // ── 投递成功后落幂等键 (先投递后落盘, 投递异常不落) ──
    if (!req.mandatory)
        persistSubmission(req.tradingDay, result.basketId);

    return result;
}

} // namespace domain::strategy
