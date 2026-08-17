#include "SignalEvaluationPipeline.h"

#include "IStrategyService.h"   // IRuntimeFactorService 完整定义 (probeFactorCrossSection)
#include "OrderGenerator.h"
#include "PositionBook.h"
#include "SubmissionFinalizer.h"
#include "TradeJournal.h"
#include "TimedCircuitBreaker.h"
#include "RuleGate.h"
#include "RuleVariableProvider.h"
#include "EventRiskSubscriber.h"
#include "IPriceProvider.h"
#include "../../factor/include/factor_compute/IMarketDataView.h"
#include "../../trading/TradingTypes.h"
#include "../../trading/include/OrderBuilder.h"
#include "MarketDataService.h"
#include "../../../infrastructure/include/database/OrderRecorder.h"
#include "foundation/market/AStockSymbol.h"
#include "foundation/Utils/DateUtils.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace domain::strategy {

// ══════════════════════════════════════════════════════════════════════════
// run — 评估主入口 (§6.1 时序; §11.2 日志断言: [Eval] 开始 → preflight 通过 → [Price] 源=...)
// ══════════════════════════════════════════════════════════════════════════

EvalResult SignalEvaluationPipeline::run(const EvalRequest& req, PipelineDeps& deps) {
    INTERNAL_INFO_STREAM << "[Eval] 开始 tradingDay=" << req.tradingDay
                         << " period=" << BarPeriodNaming::suffix(req.period)
                         << " 补单=" << (req.isCompensation ? "是" : "否");

    EvalSession s;
    EvalStage currentStage = EvalStage::CheckRebalance;
    try {
        // 阶段1: 调仓日判定 (非调仓日 → Skipped)
        if (auto r = checkRebalance(req, deps)) return *r;

        // 阶段2: 前置自检 P1/P2/P3/P5 (C9, 首败即截断)
        currentStage = EvalStage::Preflight;
        if (auto r = preflight(req, deps, s)) return *r;
        INTERNAL_INFO_STREAM << "[Eval] preflight 通过";

        // 阶段3: 评估上下文
        currentStage = EvalStage::PrepareContext;
        prepareContext(req, s);

        // 阶段4: 当日价格 (P4 全空 → Error(PriceDataEmpty))
        currentStage = EvalStage::FetchPrices;
        if (auto r = fetchPrices(req, deps, s)) return *r;

        // 阶段5: 市场宽度
        currentStage = EvalStage::ComputeBreadth;
        computeBreadth(s);

        // 阶段6: 闸门评估
        currentStage = EvalStage::EvaluateGates;
        evaluateGates(deps, s);

        // 择时强平 (原 evaluateEndOfDay L1848-1851: 不经规则闸门, 强制清仓)
        if (s.gates.timing.forceLiquidate
            && deps.circuitBreaker && !deps.circuitBreaker->isHalted()
            && deps.onForceLiquidate) {
            deps.onForceLiquidate();
            INTERNAL_INFO_STREAM << "[Eval] 完成 status=Submitted (择时强制清仓)";
            EvalResult r;
            r.status = EvalStatus::Submitted;
            return r;
        }

        // 阶段7: 信号收集
        currentStage = EvalStage::CollectSignals;
        collectSignals(req, deps, s);

        // 阶段8: 审核 + 提交
        currentStage = EvalStage::FinalizeSubmit;
        return finalizeAndSubmit(req, deps, s);
    } catch (const std::exception& e) {
        return fail(deps, req, currentStage, EvalFailureKind::Exception, e.what());
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 阶段1: checkRebalance (原 checkRebalanceDay L1299-1317; C8: PrevTradingDayFn 注入)
// ══════════════════════════════════════════════════════════════════════════

std::optional<EvalResult> SignalEvaluationPipeline::checkRebalance(
    const EvalRequest& req, PipelineDeps& deps)
{
    if (!deps.rebalanceInterval || !deps.lastRebalanceDate) return std::nullopt;
    if (*deps.rebalanceInterval <= 1 || deps.lastRebalanceDate->empty()) return std::nullopt;

    std::string date = req.tradingDay;
    int tradingDaysSince = 0;
    for (int i = 0; i < *deps.rebalanceInterval && !date.empty(); ++i) {
        std::string prev = deps.prevTradingDayFn(date);
        if (prev.empty()) break;
        date = prev;
        ++tradingDaysSince;
        if (date == *deps.lastRebalanceDate) break;
    }
    if (tradingDaysSince < *deps.rebalanceInterval) {
        INTERNAL_INFO_STREAM << "[Eval] 非调仓日: 距上次调仓 "
            << tradingDaysSince << "/" << *deps.rebalanceInterval << " 交易日, 跳过";
        EvalResult r;
        r.status = EvalStatus::Skipped;
        r.failedStage = EvalStage::CheckRebalance;
        return r;
    }
    return std::nullopt;
}

// ══════════════════════════════════════════════════════════════════════════
// 阶段2: preflight — P1/P2/P3 (C9; P4 在 fetchPrices 阶段)
// ══════════════════════════════════════════════════════════════════════════

std::optional<EvalResult> SignalEvaluationPipeline::preflight(
    const EvalRequest& req, PipelineDeps& deps, EvalSession& s)
{
    // ── P1: 视图空 (原 prepareEodContext L1320-1324 升级为 Error(ViewEmpty)) ──
    // P3: shared_ptr 持有至 run() 结束 — RFS 并发重建视图时本评估的视图依然有效
    auto view = deps.liveViewFn ? deps.liveViewFn() : nullptr;
    if (!view || view->symbolStrings().empty())
        return fail(deps, req, EvalStage::Preflight, EvalFailureKind::ViewEmpty,
                    "liveMarketView 无数据");
    s.view = std::move(view);
    s.symbols = &s.view->symbolStrings();  // 仅在 run() 同步栈帧内使用, s.view 保活

    std::int32_t tradingDayInt{0};
    try {
        tradingDayInt = static_cast<std::int32_t>(std::stoll(req.tradingDay));
    } catch (...) {
    }
    s.tradingDayInt = tradingDayInt;

    // ── P2: 锚点缺失 (视图末行日期 < 评估日) ──
    const auto& dates = s.view->dates();
    if (dates.empty() || dates.back().value < s.tradingDayInt)
        return fail(deps, req, EvalStage::Preflight, EvalFailureKind::AnchorMissing,
                    "视图末行日期早于评估日");

    // ── P2: 因子全截面探测 (C2: 单股异常 WARN+继续, 全截面无一有限值 → Error) ──
    if (deps.factorService
        && !deps.factorService->probeFactorCrossSection(*s.symbols, s.tradingDayInt))
        return fail(deps, req, EvalStage::Preflight, EvalFailureKind::FactorSnapshotEmpty,
                    "因子全截面无有限值");

    // ── P3: 账户快照 (原 L1836-1839 Skipped 升级为 Error(AccountEmpty)) ──
    s.account = deps.accountSnapshotFn ? deps.accountSnapshotFn() : AccountState{};
    if (s.account.totalAsset <= 0)
        return fail(deps, req, EvalStage::Preflight, EvalFailureKind::AccountEmpty,
                    "账户 totalAsset<=0");

    // P5 簿记对账已迁移至 PositionBook::reconcileToBroker 实时路径 (券商快照推送驱动):
    // 账本跟随券商、对账只修正不拦截, 下单流程与账实一致性零耦合 (ADR-005 修订)
    return std::nullopt;
}

// ══════════════════════════════════════════════════════════════════════════
// 阶段3: prepareContext (原 prepareEodContext L1350-1371 + 持仓图 L1871-1873
//        + 风控解禁 L1874-1875, 迁移表 #9/#10 原样保留位置)
// ══════════════════════════════════════════════════════════════════════════

void SignalEvaluationPipeline::prepareContext(const EvalRequest&, EvalSession& s) {
    s.dates = &s.view->dates();
    s.numCols = static_cast<int>(s.symbols->size());
    s.rowStride = s.view->close().rowStride;

    char buf[32];
    foundation::utils::formatTradingDayTo(static_cast<int>(s.tradingDayInt), buf, sizeof(buf));
    s.endDateStr = buf;

    for (int c = 0; c < s.numCols; ++c)
        s.symToCol[foundation::market::AStockSymbol::codeOnly((*s.symbols)[c])] = c;

    // 持仓图 (纯代码键; ADR-001 按 strategyId 过滤 — 单策略实盘无变化)
    for (const auto& p : s.account.positions)
        s.posQtyMap[foundation::market::AStockSymbol::codeOnly(p.symbol)] = p.quantity;

    // 风控事件 T+1 解禁 (每交易日清空临时风控限制)
    if (EventRiskSubscriber::instance().isStarted())
        EventRiskSubscriber::instance().clearBlockedSymbols();
}

// ══════════════════════════════════════════════════════════════════════════
// 阶段4: fetchPrices — Provider 链取价 (P4 全空 → Error(PriceDataEmpty), C9/C10)
// ══════════════════════════════════════════════════════════════════════════

std::optional<EvalResult> SignalEvaluationPipeline::fetchPrices(
    const EvalRequest& req, PipelineDeps& deps, EvalSession& s)
{
    if (!req.priceProvider)
        return fail(deps, req, EvalStage::FetchPrices, EvalFailureKind::PriceDataEmpty,
                    "无价格数据源 (周期=" + std::string(BarPeriodNaming::suffix(req.period)) + ")");

    try {
        s.prices.bars = req.priceProvider->fetchPrices(*s.symbols, s.endDateStr, req.period);
    } catch (const std::exception& e) {
        return fail(deps, req, EvalStage::FetchPrices, EvalFailureKind::Exception, e.what());
    }

    // P4: 价格全空 = 链断 (盘中=tick 介入失败, C10 无 DB 遮掩; 补单=DB 空)
    if (s.prices.bars.empty())
        return fail(deps, req, EvalStage::FetchPrices, EvalFailureKind::PriceDataEmpty,
                    "源链=" + std::string(req.priceProvider->sourceName()));
    return std::nullopt;
}

// ══════════════════════════════════════════════════════════════════════════
// 阶段5: computeBreadth (原 computeMarketBreadth L1385-1424, 原样迁移)
// ══════════════════════════════════════════════════════════════════════════

void SignalEvaluationPipeline::computeBreadth(EvalSession& s) {
    const auto& closeMat = s.view->close();
    const int lastRow = static_cast<int>(s.dates->size()) - 1;
    int above60 = 0, above20 = 0, counted60 = 0, counted20 = 0;
    for (int c = 0; c < s.numCols; ++c) {
        const auto& sym = (*s.symbols)[c];
        auto pvIt = s.prices.bars.find(sym);
        if (pvIt == s.prices.bars.end()) continue;
        const double todayClose = pvIt->second.close;
        if (!(todayClose > 0.0)) continue;

        // MA60 宽度
        double maSum60 = 0.0; int maCnt60 = 0;
        for (int i = 0; i < 60 && (lastRow - i) >= 0; ++i) {
            const double v = static_cast<double>(
                closeMat.data[(lastRow - i) * s.rowStride + c]);
            if (!(v > 0.0)) break;
            maSum60 += v; ++maCnt60;
        }
        if (maCnt60 >= 60) { ++counted60; if (todayClose > maSum60 / 60.0) ++above60; }

        // MA20 宽度
        double maSum20 = 0.0; int maCnt20 = 0;
        for (int i = 0; i < 20 && (lastRow - i) >= 0; ++i) {
            const double v = static_cast<double>(
                closeMat.data[(lastRow - i) * s.rowStride + c]);
            if (!(v > 0.0)) break;
            maSum20 += v; ++maCnt20;
        }
        if (maCnt20 >= 20) { ++counted20; if (todayClose > maSum20 / 20.0) ++above20; }
    }
    if (counted60 > 0)
        s.prices.breadthAboveMa60 = static_cast<double>(above60) / counted60;
    if (counted20 > 0)
        s.prices.breadthAboveMa20 = static_cast<double>(above20) / counted20;
    INTERNAL_INFO_STREAM << "[Eval] 当日市场宽度: MA60=" << s.prices.breadthAboveMa60
                         << " (above=" << above60 << " counted=" << counted60 << ")"
                         << " MA20=" << s.prices.breadthAboveMa20
                         << " (above=" << above20 << " counted=" << counted20 << ")";
}

// ══════════════════════════════════════════════════════════════════════════
// 阶段6: evaluateGates (原 evaluateEodGates L1426-1496, 原样迁移)
// ══════════════════════════════════════════════════════════════════════════

void SignalEvaluationPipeline::evaluateGates(PipelineDeps& deps, EvalSession& s) {
    constexpr double kBearBreadth = 0.35;

    // ── 当日宽度冻结: MA60 和 MA20 都低于阈值才冻结 ──
    const bool ma60Bear = (s.prices.breadthAboveMa60 <= kBearBreadth);
    const bool ma20Bear = (s.prices.breadthAboveMa20 <= kBearBreadth);
    s.gates.allowNewEntries = !(ma60Bear && ma20Bear);
    if (!s.gates.allowNewEntries)
        INTERNAL_INFO_STREAM << "[Eval] 当日市场宽度冻结: MA60="
                             << s.prices.breadthAboveMa60 << " MA20="
                             << s.prices.breadthAboveMa20 << " 均 <= " << kBearBreadth;

    // ── 规则闸门叠加 ──
    if (s.gates.allowNewEntries && deps.ruleGate && deps.ruleGate->enabled()) {
        rules::BacktestRuleVariableProvider eodProvider;
        eodProvider.setDay(s.view.get(), s.tradingDayInt, nullptr);
        s.gates.allowNewEntries = deps.ruleGate->allowNewEntriesToday(eodProvider);
        if (!s.gates.allowNewEntries)
            INTERNAL_INFO_STREAM << "[Eval] 规则闸门: 市场冻结";
    }

    // ── 择时闸门 ──
    if (!deps.timingGate) return;
    auto closeMat = s.view->close();
    int eodBmCol = -1;
    for (int c = 0; c < s.numCols; ++c) {
        if ((*s.symbols)[c] == "000300.SH") { eodBmCol = c; break; }
    }
    if (eodBmCol >= 0 && static_cast<int>(s.dates->size()) > 60) {
        int lastRow = static_cast<int>(s.dates->size()) - 1;
        MarketTimingSnapshot ts;
        const double idxClose = static_cast<double>(closeMat.data[
            static_cast<size_t>(lastRow) * static_cast<size_t>(s.rowStride) + static_cast<size_t>(eodBmCol)]);
        if (idxClose > 0.0) {
            ts.indexClose = idxClose;
            double sum20 = 0, sum60 = 0; int cnt20 = 0, cnt60 = 0;
            for (int back = 0; back < 60 && (lastRow - back) >= 0; ++back) {
                double c = static_cast<double>(closeMat.data[
                    static_cast<size_t>(lastRow - back) * static_cast<size_t>(s.rowStride) + static_cast<size_t>(eodBmCol)]);
                if (c > 0) { if (back < 20) { sum20 += c; ++cnt20; } sum60 += c; ++cnt60; }
            }
            ts.ma20 = cnt20 > 0 ? sum20 / cnt20 : idxClose;
            ts.ma60 = cnt60 > 0 ? sum60 / cnt60 : idxClose;
            ts.ma20AboveMa60 = ts.ma20 > ts.ma60;
            double sum20_5 = 0; int cnt20_5 = 0;
            for (int back = 5; back < 25 && (lastRow - back) >= 0; ++back) {
                double c = static_cast<double>(closeMat.data[
                    static_cast<size_t>(lastRow - back) * static_cast<size_t>(s.rowStride) + static_cast<size_t>(eodBmCol)]);
                if (c > 0) { sum20_5 += c; ++cnt20_5; }
            }
            ts.ma20Rising = cnt20_5 > 0 ? ts.ma20 > sum20_5 / cnt20_5 : false;
            if (s.numCols > 0) {
                int up = 0, tot = 0;
                for (int c = 0; c < s.numCols; ++c) {
                    double t = static_cast<double>(closeMat.data[lastRow * static_cast<size_t>(s.rowStride) + c]);
                    double p = static_cast<double>(closeMat.data[(lastRow - 1) * static_cast<size_t>(s.rowStride) + c]);
                    if (t > 1e-9 && p > 1e-9) { if (t > p) ++up; ++tot; }
                }
                ts.advanceRatio = tot > 0 ? static_cast<double>(up) / tot : 0.5;
            }
            constexpr double kPlaceholderAtrPercent = 0.02;  // §12.4: 真实 ATR 计算本次不做
            ts.atrPercent = kPlaceholderAtrPercent;
            s.gates.timing = deps.timingGate->evaluate(ts);
            INTERNAL_INFO_STREAM << "[Eval] 择时: exposure=" << s.gates.timing.targetExposure
                << " allowNew=" << s.gates.timing.allowNewEntries
                << " liquidate=" << s.gates.timing.forceLiquidate
                << " reason=" << s.gates.timing.reason;
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 阶段7: collectSignals (原 collectEodSignals L1498-1572;
//        涨跌停过滤计数 N2 §8; 单股无价 WARN+continue §8)
// ══════════════════════════════════════════════════════════════════════════

void SignalEvaluationPipeline::collectSignals(const EvalRequest& req,
                                              PipelineDeps& deps, EvalSession& s) {
    const bool blockNewBuys = (!s.gates.allowNewEntries || !s.gates.timing.allowNewEntries);

    if (blockNewBuys) {
        INTERNAL_INFO_STREAM << "[Eval] 冻结:"
            << " ruleGate=" << (s.gates.allowNewEntries ? "允许" : "冻结")
            << " timingGate=" << (s.gates.timing.allowNewEntries ? "允许" : "冻结")
            << " timingReason=" << s.gates.timing.reason;
        if (deps.tradeJournal) {
            deps.tradeJournal->log(req.tradingDay + " 冻结 原因:"
                + std::string(s.gates.allowNewEntries ? "" : "规则闸门")
                + std::string(!s.gates.timing.allowNewEntries ? "择时空仓" : ""));
        }
    }

    for (const auto& sym : *s.symbols) {
        if (blockNewBuys && s.posQtyMap.count(foundation::market::AStockSymbol::codeOnly(sym)) == 0)
            continue;

        auto pvIt = s.prices.bars.find(sym);
        if (pvIt == s.prices.bars.end()) {
            INTERNAL_WARN_STREAM << "[Eval] 单股无价跳过 sym=" << sym;  // §8: 冗余接纳, 不截断
            continue;
        }
        double price = pvIt->second.close;
        double vol = pvIt->second.volume;
        if (price <= 0) {
            INTERNAL_WARN_STREAM << "[Eval] 单股无价跳过 sym=" << sym;
            continue;
        }

        auto aSym = foundation::market::AStockSymbol::fromString(sym);
        if (!aSym.isValid()) continue;

        // B股禁新买: 未持仓B股整标的跳过 (省掉规则/因子求值); 已持仓B股放行卖单路径
        if (aSym.isBShare() &&
            s.posQtyMap.count(foundation::market::AStockSymbol::codeOnly(sym)) == 0) {
            ++s.bShareSymbolsSkipped;
            INTERNAL_DEBUG_STREAM << "[Eval] B股标的跳过: " << sym;
            continue;
        }

        MarketDataPoint mdp(
            domain::strategy::InstrumentId{aSym.instrumentId()}, price, vol, s.tradingDayInt);

        try {
            if (EventRiskSubscriber::instance().isStarted() &&
                EventRiskSubscriber::instance().blockedSymbols().count(
                    foundation::market::AStockSymbol::codeOnly(sym))) {
                INTERNAL_INFO_STREAM << "[Eval] 跳过封堵: " << sym;
                continue;
            }

            auto orders = deps.stepFn(mdp);
            if (!orders.has_value()) continue;

            for (auto& order : *orders) {
                if (!order.isValid()) continue;
                // §8: 原始信号计数 (涨跌停过滤前 + 规则闸门拒绝前)
                ++s.totalGenerated;
                // B股禁买: 已持仓B股的加仓买单同样拒绝; 卖单不受影响。
                // 置于涨跌停过滤之前: 一字涨停B股买单的拒绝原因归为「B股禁买」而非「涨停」
                if (order.side() == OrderSide::Buy && aSym.isBShare()) {
                    ++s.bShareFiltered;
                    INTERNAL_DEBUG_STREAM << "[Eval] B股买单拒绝: " << order.symbol();
                    continue;
                }
                // 涨跌停过滤: 涨停不买, 跌停不卖
                if (order.side() == OrderSide::Buy && isAtLimitUp(price, pvIt->second.preClose)) {
                    ++s.limitFiltered;
                    continue;
                }
                if (order.side() == OrderSide::Sell && isAtLimitDown(price, pvIt->second.preClose)) {
                    ++s.limitFiltered;
                    continue;
                }
                double signalScore = order.extensionAs<double>(
                    domain::trading::ExtKey::kSignalScore, 0.5);
                double targetWeight = order.extensionAs<double>(
                    domain::trading::ExtKey::kTargetWeight, 0.0);
                order = deps.orderBuilder->buildSignalOrder(
                    order.symbol(), order.side(), 0,
                    static_cast<std::int64_t>(order.quantity()), signalScore,
                    deps.strategyId, deps.accountId);
                if (targetWeight > 0.0)
                    order.setExtension(domain::trading::ExtKey::kTargetWeight, targetWeight);
                double tickPrice = mdp.lastPrice();
                if (!std::isfinite(tickPrice) || tickPrice <= 0) continue;
                if (order.orderType() == OrderType::Market)
                    order.setPrice(0.0);  // 市价单不设限价, 避免掘金拒绝
                s.pendingOrders.push_back({std::move(order), tickPrice, targetWeight, signalScore});
            }
        } catch (const std::exception& e) {
            INTERNAL_WARN_STREAM << "[Eval] collect 异常: " << sym  // §8: 单股异常, 不截断
                                 << " " << e.what();
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 阶段8: finalizeAndSubmit (原 finalizeAndSubmit L1574-1798;
//        三计数 N/N1/N2/N3 §8; 提交段收敛至 SubmissionFinalizer)
// ══════════════════════════════════════════════════════════════════════════

EvalResult SignalEvaluationPipeline::finalizeAndSubmit(const EvalRequest& req,
                                                       PipelineDeps& deps, EvalSession& s) {
    // ── 规则闸门: 信号审核 (N1) ──
    if (deps.ruleGate && deps.ruleGate->enabled()) {
        rules::BacktestRuleVariableProvider eodProvider;
        eodProvider.setDay(s.view.get(), s.tradingDayInt, nullptr);
        std::vector<PendingOrder> filtered;
        filtered.reserve(s.pendingOrders.size());
        for (auto& po : s.pendingOrders) {
            if (po.order.side() == OrderSide::Buy) {
                const std::string sym6 = foundation::market::AStockSymbol::codeOnly(po.order.symbol());
                rules::RuleCandidateContext ruleCtx;
                auto cite = s.symToCol.find(sym6);
                ruleCtx.colIndex = cite != s.symToCol.end() ? cite->second : -1;
                ruleCtx.symbol = po.order.symbol();
                ruleCtx.code = sym6;
                eodProvider.setCandidate(ruleCtx);
                if (!deps.ruleGate->allowSignal(eodProvider)) { ++s.ruleGateRejected; continue; }
            }
            filtered.push_back(std::move(po));
        }
        s.pendingOrders = std::move(filtered);
    }

    // ── 规则闸门: 持仓出场审核 (minHoldDays 最少持有期) ──
    if (deps.ruleGate && deps.ruleGate->enabled() && !s.account.positions.empty()) {
        rules::BacktestRuleVariableProvider exitProvider;
        exitProvider.setDay(s.view.get(), s.tradingDayInt, nullptr);
        for (const auto& pos : s.account.positions) {
            if (pos.quantity <= 0 || pos.costPrice <= 0.0) continue;
            const std::string sym6 = foundation::market::AStockSymbol::codeOnly(pos.symbol);
            rules::RuleCandidateContext posCtx;
            posCtx.symbol = pos.symbol;
            posCtx.code = sym6;
            auto cite = s.symToCol.find(sym6);
            posCtx.colIndex = cite != s.symToCol.end() ? cite->second : -1;
            posCtx.isHolding = true;
            posCtx.entryPrice = pos.costPrice;
            const double currentPrice = pos.lastPrice;
            if (currentPrice > 0.0 && posCtx.entryPrice > 0.0)
                posCtx.pnlPercent = (currentPrice - posCtx.entryPrice) / posCtx.entryPrice * 100.0;
            exitProvider.setCandidate(posCtx);
            const rules::RuleAction action = deps.ruleGate->positionAction(exitProvider);
            if (action == rules::RuleAction::Exit || action == rules::RuleAction::Reduce) {
                // 最少持有期检查: 买入后持有不足 minHoldDays 个交易日不卖出
                if (deps.minHoldDays > 0 && deps.positionEntryDates) {
                    auto eit = deps.positionEntryDates->find(pos.symbol);
                    if (eit != deps.positionEntryDates->end()) {
                        int heldDays = countTradingDaysBetween(
                            eit->second, s.tradingDayInt, deps.prevTradingDayFn);
                        if (heldDays < deps.minHoldDays) continue;
                    }
                }
                // 注意: 规则出场(止损/风控)不检查跌停 — 风控指令必须尝试执行
                OrderRequest exitReq = deps.orderBuilder->buildRuleExit(
                    pos.symbol, pos.quantity,
                    action == rules::RuleAction::Exit,
                    deps.strategyId, deps.accountId, currentPrice);
                PendingOrder exitPo;
                exitPo.order = std::move(exitReq);
                exitPo.tickPrice = currentPrice;
                exitPo.signalScore = 1.0;
                exitPo.targetWeight = 0.0;
                s.pendingOrders.push_back(std::move(exitPo));
                ++s.positionExits;
            }
        }
        if (s.positionExits > 0)
            INTERNAL_INFO_STREAM << "[Eval] 规则闸门: 持仓出场=" << s.positionExits;
    }

    // 无待处理订单 → NoSignal (原 L1656-1659, 提交段不走)
    if (s.pendingOrders.empty()) {
        INTERNAL_INFO_STREAM << "[Eval] 日终评估: 无待处理订单";
        INTERNAL_INFO_STREAM << "[Eval] 完成 status=NoSignal";
        EvalResult r;
        r.status = EvalStatus::NoSignal;
        return r;
    }

    s.rawOrders.reserve(s.pendingOrders.size());
    for (auto& po : s.pendingOrders)
        s.rawOrders.push_back(std::move(po.order));

    // 诊断: 打印 generate() 入参
    INTERNAL_INFO_STREAM << "[EOD QtyDiag] maxOrderQuantity=" << deps.maxOrderQuantity
                         << " rawOrders=" << s.rawOrders.size();

    // ── 原始信号: Top-3 买入/卖出 按权重排名 (原保留) ──
    {
        std::vector<OrderRequest> rawBuys, rawSells;
        for (const auto& ro : s.rawOrders) {
            if (ro.side() == OrderSide::Buy) rawBuys.push_back(ro);
            else rawSells.push_back(ro);
        }
        auto byWeight = [](const OrderRequest& a, const OrderRequest& b) {
            return a.extensionAs<double>(domain::trading::ExtKey::kTargetWeight, 0.0)
                 > b.extensionAs<double>(domain::trading::ExtKey::kTargetWeight, 0.0);
        };
        std::sort(rawBuys.begin(), rawBuys.end(), byWeight);
        std::sort(rawSells.begin(), rawSells.end(), byWeight);
        for (size_t i = 0; i < rawBuys.size() && i < 3; ++i) {
            double tw = rawBuys[i].extensionAs<double>(domain::trading::ExtKey::kTargetWeight, -1.0);
            INTERNAL_INFO_STREAM << "[EOD Signal] 买入TOP" << (i + 1) << " " << rawBuys[i].symbol()
                                 << " tw=" << tw << " qty=" << rawBuys[i].quantity();
        }
        for (size_t i = 0; i < rawSells.size() && i < 3; ++i) {
            double tw = rawSells[i].extensionAs<double>(domain::trading::ExtKey::kTargetWeight, -1.0);
            INTERNAL_INFO_STREAM << "[EOD Signal] 卖出TOP" << (i + 1) << " " << rawSells[i].symbol()
                                 << " tw=" << tw << " qty=" << rawSells[i].quantity();
        }
    }

    if (deps.ruleGate && deps.ruleGate->enabled()) {
        // 分母 = 进入审核的信号数 (原始生成 N − 涨跌停过滤 N2 − B股拦截, B股买单不再进入规则闸门)
        const std::int64_t audited = s.totalGenerated - s.limitFiltered - s.bShareFiltered;
        INTERNAL_INFO_STREAM << "[Eval] 规则闸门:"
            << " 信号审核拒绝=" << s.ruleGateRejected
            << "/" << audited
            << " 持仓出场=" << s.positionExits
            << " (绑定模板=" << deps.ruleGate->boundTemplateCount() << ")";
    }

    // ── 提交: 幂等去重 → 生成 → 篮子 → journal → 投递 → 幂等键 (全部在 Finalizer) ──
    if (!deps.finalizer)
        return fail(deps, req, EvalStage::FinalizeSubmit, EvalFailureKind::Exception,
                    "提交核心未初始化");

    SubmissionRequest subReq;
    subReq.rawOrders = s.rawOrders;
    subReq.positionQtyMap = s.posQtyMap;
    subReq.strategyId = deps.strategyId;
    subReq.accountId = deps.accountId;
    subReq.tradingDay = req.tradingDay;
    subReq.journalPrefix = "提交";
    subReq.mandatory = false;

    SubmissionResult sub;
    try {
        sub = deps.finalizer->submit(subReq);
    } catch (const std::exception& e) {
        return fail(deps, req, EvalStage::FinalizeSubmit, EvalFailureKind::Exception, e.what());
    }

    EvalResult result;
    result.totalGenerated = s.totalGenerated;
    result.ruleGateRejected = s.ruleGateRejected;
    result.limitFiltered = s.limitFiltered;
    result.generatorFiltered = s.generatorFiltered;
    result.bShareFiltered = s.bShareFiltered;

    if (sub.skipped) {
        // ADR-006 幂等: 当日已提交 → 跳过 (调度器按 Skipped 正常持久化, 幂等键保持)
        result.status = EvalStatus::Skipped;
        result.reason = "当日已提交 basketId=" + std::to_string(sub.basketId);
        INTERNAL_INFO_STREAM << "[Eval] 完成 status=Skipped (当日已提交)";
        return result;
    }

    s.generatorFiltered = static_cast<std::int64_t>(s.pendingOrders.size()) - sub.totalSubmitted;
    result.generatorFiltered = s.generatorFiltered;

    if (sub.totalSubmitted > 0) {
        s.finalOrders = sub.submittedOrders;
        // 记录买入标的的建仓日期 (用于最少持有期校验)
        if (deps.minHoldDays > 0 && deps.positionEntryDates) {
            for (const auto& o : s.finalOrders)
                if (o.side() == OrderSide::Buy)
                    (*deps.positionEntryDates)[o.symbol()] = s.tradingDayInt;
        }
        if (deps.lastRebalanceDate) *deps.lastRebalanceDate = req.tradingDay;
    }

    // ── 日终持仓/净值 journal (原 L1796-1802 保持) ──
    if (deps.tradeJournal) {
        deps.tradeJournal->log(req.tradingDay + " 日终 持仓:"
            + std::to_string(s.account.positions.size())
            + " 净值:" + std::to_string(static_cast<int>(s.account.totalAsset))
            + " 现金:" + std::to_string(static_cast<int>(s.account.availableCash)));
    }

    if (deps.lastProcessedAt)
        deps.lastProcessedAt->store(
            std::chrono::steady_clock::now().time_since_epoch().count(),
            std::memory_order_release);

    // ── 账户快照入库 (原 L1816-1824 保持) ──
    {
        int td = static_cast<int>(domain::market::MarketDataService::instance().activeTradingDay());
        if (td > 0 && s.account.totalAsset > 0) {
            astock::infrastructure::database::OrderRecorder::instance().insertAccountSnapshot(
                td, s.account.totalAsset, s.account.availableCash, s.account.marketValue,
                s.account.frozenCash, s.account.realizedPnl, s.account.unrealizedPnl);
        }
    }

    // ── 状态判定 (§8: 提交>0 → Submitted; 生成=0 提交=0 → NoSignal; 生成>0 提交=0 → AllRejected) ──
    if (sub.totalSubmitted > 0) {
        result.status = EvalStatus::Submitted;
    } else if (s.totalGenerated == 0) {
        result.status = EvalStatus::NoSignal;
    } else {
        result.status = EvalStatus::AllRejected;
        // 三计数明细 journal (§8)
        if (deps.tradeJournal) {
            deps.tradeJournal->log(req.tradingDay + " 全部拒绝 生成="
                + std::to_string(s.totalGenerated)
                + " 规则闸门拒绝=" + std::to_string(s.ruleGateRejected)
                + " 涨跌停过滤=" + std::to_string(s.limitFiltered)
                + " 生成器过滤=" + std::to_string(s.generatorFiltered)
                + " B股拒绝=" + std::to_string(s.bShareFiltered));
        }
    }

    INTERNAL_INFO_STREAM << "[Eval] 完成 status=" << EvalNaming::statusText(result.status)
                         << " 信号=" << s.totalGenerated
                         << " 提交=" << sub.totalSubmitted
                         << " B股跳过=" << s.bShareSymbolsSkipped
                         << " B股拒绝=" << s.bShareFiltered;
    return result;
}

// ══════════════════════════════════════════════════════════════════════════
// 私有辅助
// ══════════════════════════════════════════════════════════════════════════

bool SignalEvaluationPipeline::isAtLimitUp(double close, double preClose) noexcept {
    if (preClose <= 0.0 || close <= 0.0) return false;
    return close >= preClose * 1.098;
}

bool SignalEvaluationPipeline::isAtLimitDown(double close, double preClose) noexcept {
    if (preClose <= 0.0 || close <= 0.0) return false;
    return close <= preClose * 0.902;
}

int SignalEvaluationPipeline::countTradingDaysBetween(
    std::int64_t fromDate, std::int64_t toDate,
    const std::function<std::string(const std::string&)>& prevTradingDayFn)
{
    if (fromDate >= toDate) return 0;
    std::string date = std::to_string(toDate);
    int count = 0;
    constexpr int kMaxWalkback = 365;  // 安全上限 (原实现保持)
    while (count < kMaxWalkback) {
        std::string prev = prevTradingDayFn(date);
        if (prev.empty()) break;
        ++count;
        std::int64_t prevInt = std::stoll(prev);
        if (prevInt <= fromDate) break;
        date = prev;
    }
    return count;
}

EvalResult SignalEvaluationPipeline::fail(PipelineDeps& deps, const EvalRequest& req,
                                          EvalStage stage, EvalFailureKind kind,
                                          const std::string& reason) {
    EvalResult r;
    r.status = EvalStatus::Error;
    r.failureKind = kind;
    r.failedStage = stage;
    r.reason = reason;

    if (kind == EvalFailureKind::Exception) {
        // §8: 未捕获异常模板
        INTERNAL_ERROR_STREAM << "[Eval] " << req.tradingDay << " 未捕获异常 " << reason;
        if (deps.tradeJournal)
            deps.tradeJournal->log(req.tradingDay + " 自检失败 kind=Exception " + reason);
    } else {
        // §8: 自检失败模板
        INTERNAL_ERROR_STREAM << "[Eval] " << req.tradingDay << " 自检失败 kind="
                              << EvalNaming::kindText(kind) << " stage=" << EvalNaming::stageText(stage)
                              << (reason.empty() ? "" : " " + reason);
        if (deps.tradeJournal)
            deps.tradeJournal->log(req.tradingDay + " 自检失败 kind=" + EvalNaming::kindText(kind)
                                   + (reason.empty() ? "" : " " + reason));
    }
    return r;
}

} // namespace domain::strategy
