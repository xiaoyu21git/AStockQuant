#include "ExecutionSchedulingRuleChain.h"

namespace trading::execution {

bool SchedulingRuleBlock::isValid() const
{
    return !ruleId.trimmed().isEmpty() && !message.trimmed().isEmpty();
}

std::optional<SchedulingRuleBlock> evaluateSchedulingRules(
    const QVariantMap& orderRequest,
    const std::vector<SchedulingRuleDefinition>& rules)
{
    for (const SchedulingRuleDefinition& rule : rules) {
        if (!rule.evaluator) {
            continue;
        }

        std::optional<SchedulingRuleBlock> block = rule.evaluator(orderRequest);
        if (!block.has_value()) {
            continue;
        }

        if (block->ruleId.trimmed().isEmpty()) {
            block->ruleId = rule.ruleId;
        }

        if (block->isValid()) {
            return block;
        }
    }

    return std::nullopt;
}

QVariantMap buildSchedulingRejectStatus(
    const QVariantMap& orderRequest,
    const SchedulingRuleBlock& block,
    const QString& updatedAt,
    const QString& statusOrigin)
{
    QVariantMap rejectStatus = orderRequest;
    rejectStatus.insert(QStringLiteral("status"), QStringLiteral("REJECTED"));
    rejectStatus.insert(QStringLiteral("message"), block.message);
    rejectStatus.insert(QStringLiteral("updatedAt"), updatedAt);
    rejectStatus.insert(QStringLiteral("statusOrigin"), statusOrigin.trimmed().isEmpty()
        ? QStringLiteral("execution_rule_reject")
        : statusOrigin.trimmed());
    rejectStatus.insert(QStringLiteral("ruleId"), block.ruleId);
    rejectStatus.insert(QStringLiteral("reasonCode"), block.reasonCode);

    for (auto it = block.attributes.constBegin(); it != block.attributes.constEnd(); ++it) {
        if (!it.value().isValid() || it.value().isNull()) {
            continue;
        }
        rejectStatus.insert(it.key(), it.value());
    }

    return rejectStatus;
}

} // namespace trading::execution