#include "../include/IStrategyService.h"
#include "foundation/thread/thread_pool.hpp"

#include <cstdlib>
#include <utility>

namespace domain::strategy {

StrategyEngine::Builder StrategyEngine::builder()
{
    return Builder();
}

StrategyEngine::StrategyEngine(std::unique_ptr<IRuntimeFactorService> factorService,
                               std::unique_ptr<IRuleEvaluationService> ruleEvaluationService,
                               std::unique_ptr<IStrategyService> strategyService)
    : factorService_(std::move(factorService))
    , ruleEvaluationService_(std::move(ruleEvaluationService))
    , strategyService_(std::move(strategyService))
    , asyncExecutor_(foundation::thread::ThreadPoolFactory::create_cpu_aware())
{
}

StrategyServiceFlowResult StrategyEngine::registerStrategy(
    std::shared_ptr<IRuntimeStrategy> strategy,
    const RuntimeStrategyContext& context)
{
    return strategyService_->registerStrategy(std::move(strategy), context);
}

StrategyServiceFlowResult StrategyEngine::registerStrategies(
    const std::vector<std::shared_ptr<IRuntimeStrategy>>& strategies,
    const std::vector<RuntimeStrategyContext>& contexts)
{
    if (strategies.size() != contexts.size()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    }
    for (std::size_t i = 0; i < strategies.size(); ++i) {
        const StrategyServiceFlowResult result =
            strategyService_->registerStrategy(strategies[i], contexts[i]);
        if (!result.isOk()) {
            return result;
        }
    }
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

StrategyServiceFlowResult StrategyEngine::start()
{
    return strategyService_->start();
}

StrategyServiceFlowResult StrategyEngine::pause()
{
    return strategyService_->pause();
}

StrategyServiceFlowResult StrategyEngine::resume()
{
    return strategyService_->resume();
}

StrategyServiceFlowResult StrategyEngine::stop()
{
    return strategyService_->stop();
}

std::optional<std::vector<OrderRequest>> StrategyEngine::step(const MarketDataPoint& marketDataPoint)
{
    return collectOrders(strategyService_->onMarketDataPoint(marketDataPoint));
}

std::optional<std::vector<OrderRequest>> StrategyEngine::stepBatch(
    const std::vector<MarketDataPoint>& batch)
{
    return collectOrders(strategyService_->onMarketDataBatch(batch));
}

std::future<std::optional<std::vector<OrderRequest>>> StrategyEngine::stepAsync(
    const MarketDataPoint& marketDataPoint)
{
    return foundation::thread::async(asyncExecutor_, [this, marketDataPoint]() {
        return step(marketDataPoint);
    });
}

std::future<std::optional<std::vector<OrderRequest>>> StrategyEngine::stepBatchAsync(
    std::vector<MarketDataPoint> batch)
{
    return foundation::thread::async(asyncExecutor_, [this, batch = std::move(batch)]() {
        return stepBatch(batch);
    });
}

IStrategyService& StrategyEngine::service() noexcept
{
    return *strategyService_;
}

const IStrategyService& StrategyEngine::service() const noexcept
{
    return *strategyService_;
}

void StrategyEngine::setAsyncExecutor(std::shared_ptr<foundation::thread::IExecutor> executor)
{
    if (executor) {
        asyncExecutor_ = std::move(executor);
    }
}

std::optional<std::vector<OrderRequest>> StrategyEngine::collectOrders(
    const StrategyServiceFlowResult& flowResult)
{
    if (!flowResult.isOk()) {
        return std::nullopt;
    }

    if (strategyService_->pendingOrderCount() == 0) {
        return std::nullopt;
    }

    std::vector<OrderRequest> output;
    strategyService_->copyPendingOrders(output);
    return output;
}

StrategyEngine::Builder::Builder() = default;

StrategyEngine::Builder& StrategyEngine::Builder::withFactorService(
    std::unique_ptr<IRuntimeFactorService> factorService)
{
    factorService_ = std::move(factorService);
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withFactorCallbacks(
    CallbackRuntimeFactorServiceAdapter::Callbacks callbacks)
{
    factorService_ = std::make_unique<CallbackRuntimeFactorServiceAdapter>(std::move(callbacks));
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withOrderSink(IRuntimeOrderSink& orderSink)
{
    orderSink_ = &orderSink;
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withDiagnosticsSink(IDiagnosticsSink& diagnosticsSink)
{
    diagnosticsSink_ = &diagnosticsSink;
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withOrderBuilder(const IOrderBuilder& orderBuilder)
{
    orderBuilder_ = &orderBuilder;
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::withAsyncExecutor(
    std::shared_ptr<foundation::thread::IExecutor> executor)
{
    asyncExecutor_ = std::move(executor);
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::maxStrategies(StrategyCount value)
{
    plan_ = StrategyServiceExecutionPlan(
        value,
        plan_.maxMarketDataPerBatch(),
        plan_.maxSignalPerBatch(),
        plan_.maxRuleResultPerBatch());
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::maxSignalsPerBatch(StrategyCount value)
{
    plan_ = StrategyServiceExecutionPlan(
        plan_.maxStrategyCount(),
        plan_.maxMarketDataPerBatch(),
        value,
        plan_.maxRuleResultPerBatch());
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::maxRuleResultsPerBatch(StrategyCount value)
{
    plan_ = StrategyServiceExecutionPlan(
        plan_.maxStrategyCount(),
        plan_.maxMarketDataPerBatch(),
        plan_.maxSignalPerBatch(),
        value);
    return *this;
}

StrategyEngine::Builder& StrategyEngine::Builder::maxMarketDataPerBatch(StrategyCount value)
{
    plan_ = StrategyServiceExecutionPlan(
        plan_.maxStrategyCount(),
        value,
        plan_.maxSignalPerBatch(),
        plan_.maxRuleResultPerBatch());
    return *this;
}

StrategyEngine StrategyEngine::Builder::build()
{
    if (!factorService_) {
        // 运行时主链必须显式注入真实因子服务，不允许默认伪服务回退。
        std::abort();
    }
    std::unique_ptr<IRuntimeFactorService> factorService = std::move(factorService_);

    std::unique_ptr<IRuleEvaluationService> ruleEvaluationService =
        std::make_unique<LocalRuleEvaluationService>();

    std::unique_ptr<IStrategyService> strategyService;
    if (orderSink_) {
        strategyService = std::make_unique<StrategyService>(
            *factorService,
            *ruleEvaluationService,
            *orderSink_);
    } else {
        strategyService = std::make_unique<StrategyService>(*factorService, *ruleEvaluationService);
    }

    const StrategyServiceFlowResult configureResult = strategyService->configureExecutionPlan(plan_);
    if (!configureResult.isOk()) {
        std::abort();
    }
    strategyService->setDiagnosticsSink(diagnosticsSink_);
    if (orderBuilder_) {
        strategyService->setOrderBuilder(orderBuilder_);
    }
    const StrategyServiceFlowResult startResult = strategyService->start();
    if (!startResult.isOk()) {
        std::abort();
    }

    StrategyEngine engine(
        std::move(factorService),
        std::move(ruleEvaluationService),
        std::move(strategyService));
    if (asyncExecutor_) {
        engine.setAsyncExecutor(std::move(asyncExecutor_));
    }
    return engine;
}

} // namespace domain::strategy
