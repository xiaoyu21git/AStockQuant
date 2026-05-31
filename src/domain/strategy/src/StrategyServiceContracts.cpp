#include "IStrategyService.h"

namespace domain::strategy {
namespace rules {

RuleEvaluationContext::RuleEvaluationContext(
    RuleEvaluationPhase phase,
    StrategyInstanceId strategyInstanceId,
    StrategyCount candidateSignalCount)
    : phase_(phase)
    , strategyInstanceId_(strategyInstanceId)
    , candidateSignalCount_(candidateSignalCount)
{
}

RuleEvaluationPhase RuleEvaluationContext::phase() const noexcept
{
    return phase_;
}

StrategyInstanceId RuleEvaluationContext::strategyInstanceId() const noexcept
{
    return strategyInstanceId_;
}

StrategyCount RuleEvaluationContext::candidateSignalCount() const noexcept
{
    return candidateSignalCount_;
}

RuleSet::RuleSet(RuleSetId id, const std::vector<RuleId>& rules)
    : id_(id)
    , rules_(rules)
{
}

RuleSetId RuleSet::id() const noexcept
{
    return id_;
}

const std::vector<RuleId>& RuleSet::rules() const noexcept
{
    return rules_;
}

bool RuleSet::isValid() const noexcept
{
    return id_ >= kRuleSetAllPass;
}

} // namespace rules

RuleEvaluationResult::RuleEvaluationResult(bool passed,
                                           const StrategySignal& signal,
                                           RuleRejectReason rejectReason,
                                           std::chrono::microseconds latency)
    : passed_(passed)
    , signal_(signal)
    , rejectReason_(rejectReason)
    , latency_(latency)
{
}

bool RuleEvaluationResult::passed() const noexcept
{
    return passed_;
}

const StrategySignal& RuleEvaluationResult::signal() const noexcept
{
    return signal_;
}

RuleRejectReason RuleEvaluationResult::rejectReason() const noexcept
{
    return rejectReason_;
}

std::chrono::microseconds RuleEvaluationResult::latency() const noexcept
{
    return latency_;
}

PythonRuleDescriptor::PythonRuleDescriptor(PythonRuleKind kind,
                                           bool enabled,
                                           double thresholdA,
                                           double thresholdB,
                                           StrategyInstanceId strategyScopeId)
    : kind_(kind)
    , enabled_(enabled)
    , thresholdA_(thresholdA)
    , thresholdB_(thresholdB)
    , strategyScopeId_(strategyScopeId)
{
}

PythonRuleKind PythonRuleDescriptor::kind() const noexcept
{
    return kind_;
}

bool PythonRuleDescriptor::enabled() const noexcept
{
    return enabled_;
}

double PythonRuleDescriptor::thresholdA() const noexcept
{
    return thresholdA_;
}

double PythonRuleDescriptor::thresholdB() const noexcept
{
    return thresholdB_;
}

StrategyInstanceId PythonRuleDescriptor::strategyScopeId() const noexcept
{
    return strategyScopeId_;
}

bool PythonRuleDescriptor::isValid() const noexcept
{
    return kind_ != PythonRuleKind::Invalid;
}

PythonRuleResult::PythonRuleResult(bool passed, RuleRejectReason rejectReason)
    : passed_(passed)
    , rejectReason_(rejectReason)
{
}

bool PythonRuleResult::passed() const noexcept
{
    return passed_;
}

RuleRejectReason PythonRuleResult::rejectReason() const noexcept
{
    return rejectReason_;
}

} // namespace domain::strategy
