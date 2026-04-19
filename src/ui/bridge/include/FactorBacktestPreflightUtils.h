#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace factor::bridge {

struct BacktestPreflightFailure {
    QString requestedFactorId;
    QString resolvedInstanceId;
    QString reason;
    QString category;
};

inline QString classifyBacktestPreflightCategory(const QString& category)
{
    const QString normalized = category.trimmed().toLower();
    if (normalized == QStringLiteral("missing-field")) {
        return QStringLiteral("字段缺失");
    }
    if (normalized == QStringLiteral("missing-field-value")) {
        return QStringLiteral("字段值缺失");
    }
    if (normalized == QStringLiteral("invalid-field-value")) {
        return QStringLiteral("字段值无效");
    }
    if (normalized == QStringLiteral("insufficient-history")) {
        return QStringLiteral("历史长度不足");
    }
    if (normalized == QStringLiteral("data-unavailable")) {
        return QStringLiteral("数据不可用");
    }
    if (normalized == QStringLiteral("instance-missing")) {
        return QStringLiteral("实例不可用");
    }
    if (normalized == QStringLiteral("instance-create-failed")) {
        return QStringLiteral("实例初始化失败");
    }
    if (normalized == QStringLiteral("unsupported-type")) {
        return QStringLiteral("因子类型不支持");
    }
    if (normalized == QStringLiteral("unsupported-metric")) {
        return QStringLiteral("指标配置不支持");
    }
    if (normalized == QStringLiteral("runtime-init-failed")) {
        return QStringLiteral("回测运行时初始化失败");
    }
    if (normalized == QStringLiteral("invalid-data-source-mode")) {
        return QStringLiteral("数据源模式非法");
    }
    if (normalized == QStringLiteral("dataset-invalid")) {
        return QStringLiteral("缓存集不可用");
    }
    if (normalized == QStringLiteral("dataset-empty")) {
        return QStringLiteral("缓存集为空");
    }
    if (normalized == QStringLiteral("dataset-missing")) {
        return QStringLiteral("未选择缓存集");
    }
    if (normalized == QStringLiteral("stock-pool-mismatch")) {
        return QStringLiteral("股票池与缓存集不匹配");
    }
    return {};
}

inline QString makeBacktestPreflightDisplayReason(const BacktestPreflightFailure& failure)
{
    const QString categoryLabel = classifyBacktestPreflightCategory(failure.category);
    const QString detail = failure.reason.trimmed();

    if (categoryLabel.isEmpty()) {
        return detail.isEmpty() ? QStringLiteral("未知预检失败") : detail;
    }
    if (detail.isEmpty()) {
        return categoryLabel;
    }
    if (detail.contains(categoryLabel)) {
        return detail;
    }
    return QStringLiteral("%1: %2").arg(categoryLabel, detail);
}

inline QVariantMap toVariantMap(const BacktestPreflightFailure& failure)
{
    QVariantMap map;
    map["factorId"] = failure.requestedFactorId.trimmed();
    map["instanceId"] = failure.resolvedInstanceId.trimmed();
    map["reason"] = makeBacktestPreflightDisplayReason(failure);
    map["category"] = failure.category.trimmed();
    return map;
}

inline QString summarizeBacktestPreflightFailure(const BacktestPreflightFailure& failure)
{
    const QString factorId = failure.requestedFactorId.trimmed().isEmpty()
        ? QStringLiteral("<empty-factor-id>")
        : failure.requestedFactorId.trimmed();
    const QString reason = makeBacktestPreflightDisplayReason(failure);
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