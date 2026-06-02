#pragma once

#include "BacktestInterfaces.hpp"

namespace application::backtest {

class IRunOrchestrator {
public:
    virtual ~IRunOrchestrator() = default;

    [[nodiscard]] virtual RunResult run(const RunSpec& spec) const = 0;
};

class RunOrchestratorSkeleton final : public IRunOrchestrator {
public:
    explicit RunOrchestratorSkeleton(RunModuleRegistry moduleRegistry);

    [[nodiscard]] RunResult run(const RunSpec& spec) const override;

private:
    [[nodiscard]] static RunResult failure(
        RunErrorCode code,
        RunStage stage,
        const RunContext& context);

    [[nodiscard]] const ISignalProducer* resolveSignalProducer(RunMode mode) const;
    [[nodiscard]] const IFillEngine* resolveFillEngine(RunMode mode) const;
    [[nodiscard]] bool hasRequiredModules(RunMode mode) const;

    RunModuleRegistry moduleRegistry_;
};

} // namespace application::backtest