#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace factor::bridge {

struct BacktestPreflightFailure {
    QString requestedFactorId;
    QString resolvedInstanceId;
    QString reason;
};

inline QVariantMap toVariantMap(const BacktestPreflightFailure& failure)
{
    QVariantMap map;
    map["factorId"] = failure.requestedFactorId.trimmed();
    map["instanceId"] = failure.resolvedInstanceId.trimmed();
    map["reason"] = failure.reason.trimmed();
    return map;
}

inline QString summarizeBacktestPreflightFailure(const BacktestPreflightFailure& failure)
{
    const QString factorId = failure.requestedFactorId.trimmed().isEmpty()
        ? QStringLiteral("<empty-factor-id>")
        : failure.requestedFactorId.trimmed();
    const QString reason = failure.reason.trimmed().isEmpty()
        ? QStringLiteral("未知预检失败")
        : failure.reason.trimmed();
    const QString instanceId = failure.resolvedInstanceId.trimmed();
    if (instanceId.isEmpty()) {
        return QString("%1 (%2)").arg(factorId, reason);
    }
    return QString("%1 (instanceId=%2, %3)").arg(factorId, instanceId, reason);
}

inline QVariantList toVariantList(const QList<BacktestPreflightFailure>& failures)
{
    QVariantList result;
    result.reserve(failures.size());
    for (const BacktestPreflightFailure& failure : failures) {
        result.append(toVariantMap(failure));
    }
    return result;
}

inline QString summarizeBacktestPreflightFailures(const QList<BacktestPreflightFailure>& failures)
{
    QStringList summaries;
    summaries.reserve(failures.size());
    for (const BacktestPreflightFailure& failure : failures) {
        summaries.append(summarizeBacktestPreflightFailure(failure));
    }
    return summaries.join(QStringLiteral("; "));
}

}  // namespace factor::bridge