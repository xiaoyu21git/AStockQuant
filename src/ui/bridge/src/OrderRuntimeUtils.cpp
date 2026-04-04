#include "OrderRuntimeUtils.h"

namespace order_runtime {

QString normalizeOrderStatus(QString status, EmptyStatusPolicy emptyPolicy)
{
    status = status.trimmed();
    if (status.isEmpty()) {
        return emptyPolicy == EmptyStatusPolicy::TreatAsPending
            ? QStringLiteral("PENDING")
            : QString{};
    }

    if (status == QStringLiteral("已请求")) {
        return QStringLiteral("REQUESTED");
    }
    if (status == QStringLiteral("已报")) {
        return QStringLiteral("SUBMITTED");
    }
    if (status == QStringLiteral("待处理")) {
        return QStringLiteral("PENDING");
    }
    if (status == QStringLiteral("部分成交")) {
        return QStringLiteral("PARTIAL_FILLED");
    }
    if (status == QStringLiteral("已成")) {
        return QStringLiteral("FILLED");
    }
    if (status == QStringLiteral("撤单中")) {
        return QStringLiteral("PENDING_CANCEL");
    }
    if (status == QStringLiteral("已撤")) {
        return QStringLiteral("CANCELLED");
    }
    if (status == QStringLiteral("已拒")) {
        return QStringLiteral("REJECTED");
    }

    status = status.toUpper();
    if (status == QStringLiteral("PARTIALLY_FILLED")) {
        return QStringLiteral("PARTIAL_FILLED");
    }
    if (status == QStringLiteral("PENDING_CANCEL")) {
        return QStringLiteral("PENDING_CANCEL");
    }

    bool ok = false;
    const double numericStatus = status.toDouble(&ok);
    if (ok) {
        const int code = static_cast<int>(numericStatus);
        switch (code) {
        case 0:
            return QStringLiteral("PENDING");
        case 1:
        case 10:
        case 13:
            return QStringLiteral("SUBMITTED");
        case 2:
            return QStringLiteral("PARTIAL_FILLED");
        case 3:
            return QStringLiteral("FILLED");
        case 4:
        case 5:
        case 12:
            return QStringLiteral("CANCELLED");
        case 8:
            return QStringLiteral("REJECTED");
        default:
            return emptyPolicy == EmptyStatusPolicy::TreatAsPending
                ? QStringLiteral("PENDING")
                : QString{};
        }
    }

    if (status == QStringLiteral("NEW") || status == QStringLiteral("PENDINGNEW")) {
        return QStringLiteral("SUBMITTED");
    }

    return status;
}

QString resolveOrderStatusFromProgress(QString status,
                                       qint64 quantity,
                                       qint64 filledQuantity,
                                       EmptyStatusPolicy emptyPolicy)
{
    const QString normalized = normalizeOrderStatus(std::move(status), emptyPolicy);

    if (quantity > 0
            && filledQuantity >= quantity
            && normalized != QStringLiteral("CANCELLED")
            && normalized != QStringLiteral("REJECTED")
            && normalized != QStringLiteral("EXPIRED")) {
        return QStringLiteral("FILLED");
    }

    if (filledQuantity > 0
            && (normalized == QStringLiteral("PENDING")
                || normalized == QStringLiteral("SUBMITTED")
                || normalized == QStringLiteral("REQUESTED"))) {
        return QStringLiteral("PARTIAL_FILLED");
    }

    return normalized;
}

int orderStatusPhase(QString status, EmptyStatusPolicy emptyPolicy)
{
    const QString normalized = normalizeOrderStatus(std::move(status), emptyPolicy);
    if (normalized == QStringLiteral("REQUESTED")) {
        return 0;
    }
    if (normalized == QStringLiteral("PENDING") || normalized == QStringLiteral("SUBMITTED")) {
        return 1;
    }
    if (normalized == QStringLiteral("PENDING_CANCEL") || normalized == QStringLiteral("PARTIAL_FILLED")) {
        return 2;
    }
    if (normalized == QStringLiteral("CANCELLED") || normalized == QStringLiteral("REJECTED")) {
        return 3;
    }
    if (normalized == QStringLiteral("FILLED")) {
        return 4;
    }
    return 0;
}

bool shouldIgnoreOrderStatusRegression(const QVariantMap& existingStatus,
                                       const QVariantMap& nextStatus,
                                       EmptyStatusPolicy emptyPolicy)
{
    const QString existingNormalized = normalizeOrderStatus(existingStatus.value(QStringLiteral("status")).toString(),
                                                            emptyPolicy);
    const QString nextNormalized = normalizeOrderStatus(nextStatus.value(QStringLiteral("status")).toString(),
                                                        emptyPolicy);
    const qint64 existingFilledQuantity = existingStatus.value(QStringLiteral("filledQuantity")).toLongLong();
    const qint64 nextFilledQuantity = nextStatus.value(QStringLiteral("filledQuantity")).toLongLong();
    const int existingPhase = orderStatusPhase(existingNormalized, emptyPolicy);
    const int nextPhase = orderStatusPhase(nextNormalized, emptyPolicy);

    if (existingFilledQuantity > nextFilledQuantity) {
        return true;
    }

    if (existingPhase > nextPhase && existingFilledQuantity >= nextFilledQuantity) {
        return true;
    }

    return false;
}

bool isClosedOrderStatus(QString status, EmptyStatusPolicy emptyPolicy)
{
    const QString normalized = normalizeOrderStatus(std::move(status), emptyPolicy);
    return normalized == QStringLiteral("FILLED")
        || normalized == QStringLiteral("CANCELLED")
        || normalized == QStringLiteral("REJECTED");
}

} // namespace order_runtime