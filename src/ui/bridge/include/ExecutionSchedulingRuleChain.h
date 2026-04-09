#pragma once

#include <QVariantMap>

#include <functional>
#include <optional>
#include <vector>

namespace trading::execution {

struct SchedulingRuleBlock {
    QString ruleId;
    QString reasonCode;
    QString message;
    QVariantMap attributes;

    bool isValid() const;
};

using SchedulingRuleEvaluator = std::function<std::optional<SchedulingRuleBlock>(const QVariantMap& orderRequest)>;

struct SchedulingRuleDefinition {
    QString ruleId;
    SchedulingRuleEvaluator evaluator;
};

std::optional<SchedulingRuleBlock> evaluateSchedulingRules(
    const QVariantMap& orderRequest,
    const std::vector<SchedulingRuleDefinition>& rules);

QVariantMap buildSchedulingRejectStatus(
    const QVariantMap& orderRequest,
    const SchedulingRuleBlock& block,
    const QString& updatedAt,
    const QString& statusOrigin = QStringLiteral("execution_rule_reject"));

} // namespace trading::execution