#pragma once

#include "BaseFactor.h"
#include "CompositeFactorConfig.h"
#include "FactorInstanceManager.h"

#include <memory>
#include <vector>

namespace factor {

class IFactorResolver;

class CompositeFactor final : public BaseFactor {
public:
    CompositeFactor();
    ~CompositeFactor() override = default;

    static std::shared_ptr<CompositeFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker,
        std::shared_ptr<IFactorResolver> resolver);

    CalculationResult calculate(const CalculationContext& context) override;
    std::vector<CalculationResult> calculateBatch(const std::vector<CalculationContext>& contexts) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

private:
    struct ChildRuntime {
        CompositeChildSpec spec;
        FactorInstanceInfo info;
        std::shared_ptr<BaseFactor> factor;
    };

    CompositeFactorParams params_;
    std::shared_ptr<IFactorResolver> resolver_;
    mutable std::vector<ChildRuntime> children_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
    const std::vector<ChildRuntime>& resolveChildrenOrThrow() const;
    static DataRequirements mergeDataRequirements(const std::vector<ChildRuntime>& children);
    static BoundaryRules mergeBoundaryRules(const std::vector<ChildRuntime>& children);
};

} // namespace factor