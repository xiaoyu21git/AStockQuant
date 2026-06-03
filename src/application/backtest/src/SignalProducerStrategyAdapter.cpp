#include "SignalProducers.h"

#include "../../../domain/strategy/include/IStrategyService.h"
#include "../../../domain/strategy/include/IStrategySignalEngine.h"
#include "../../../domain/backtest/include/BacktestRequest.h"

#include <QByteArray>
#include <QString>

namespace application::backtest {

StrategySignalProducerAdapter::StrategySignalProducerAdapter(
    const domain::strategy::IStrategyService& strategyService)
    : strategyService_(strategyService)
{
}

StageResult StrategySignalProducerAdapter::generateSignal(RunContext& context) const
{
    // P3-T2：统一链路接入
    // 策略信号接入同一后半链路，无第二条执行主链。
    // 枚举错误码与阶段状态一致（RunErrorCode + RunStage）。
    //
    // 优先使用 IStrategySignalEngine::evaluateBatch（批接口），
    // 回退到 IStrategyService::copyPendingOrders（旧逐条路径）。

    StageResult stageResult;
    stageResult.stage = RunStage::GenerateSignal;
    stageResult.code = RunErrorCode::None;

    // 通过 dynamic_cast 检测策略服务是否实现了批接口
    // 注意：const 版本 evaluateBatch 通过 const_cast 调用（非 const 方法）
    auto* mutableService = const_cast<domain::strategy::IStrategyService*>(&strategyService_);
    auto* signalEngine = dynamic_cast<domain::strategy::IStrategySignalEngine*>(mutableService);
    if (signalEngine != nullptr && context.spec.request) {
        const domain::backtest::BacktestRequest& request = *context.spec.request;
        const factor::compute::DateRange dateRange{
            factor::compute::DateKey{request.window.startDate},
            factor::compute::DateKey{request.window.endDate}};

        // 从 BacktestRequest 提取标的池（使用 FNV-1a 哈希）
        constexpr std::uint32_t kFNV1aOffsetBasis = 2166136261U;
        constexpr std::uint32_t kFNV1aPrime = 16777619U;
        std::vector<factor::compute::InstrumentId> universe;
        const auto& symbols = request.universeSpec.resolvedSymbols;
        universe.reserve(symbols.size());
        for (const auto& symbol : symbols) {
            const QString symbolText = symbol.text().trimmed();
            if (symbolText.isEmpty()) {
                continue;
            }
            const QByteArray utf8 = symbolText.toUtf8();
            std::uint32_t hash = kFNV1aOffsetBasis;
            for (char ch : utf8) {
                hash ^= static_cast<std::uint8_t>(ch);
                hash *= kFNV1aPrime;
            }
            factor::compute::InstrumentId id;
            id.value = hash;
            universe.push_back(id);
        }

        factor::compute::FactorResult<factor::compute::SignalSet> batchResult =
            signalEngine->evaluateBatch(dateRange, universe);

        if (batchResult.hasValue() && batchResult.value().isValid()) {
            const std::uint32_t sigCount = static_cast<std::uint32_t>(batchResult.value().signalIds.size());
            context.workingSet.signalBatch.factorSignalSet =
                std::make_shared<const factor::compute::SignalSet>(std::move(batchResult.value()));
            context.workingSet.signalBatch.strategySignalCount = sigCount;
            return stageResult;
        }
    }

    // 回退：旧逐条订单路径（P3-T2 向后兼容）
    std::vector<domain::strategy::OrderRequest> pendingOrders;
    strategyService_.copyPendingOrders(pendingOrders);

    if (pendingOrders.empty()) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        context.workingSet.signalBatch.factorSignalSet.reset();
        context.workingSet.signalBatch.strategySignalCount = 0U;
        return stageResult;
    }

    context.workingSet.signalBatch.factorSignalSet.reset();
    context.workingSet.signalBatch.strategySignalCount =
        static_cast<std::uint32_t>(pendingOrders.size());
    return stageResult;
}

} // namespace application::backtest
