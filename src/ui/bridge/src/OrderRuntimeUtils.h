#pragma once

#include <QString>
#include <QVariantMap>

namespace order_runtime {

enum class EmptyStatusPolicy {
    KeepEmpty,
    TreatAsPending
};

QString normalizeOrderStatus(QString status,
                             EmptyStatusPolicy emptyPolicy = EmptyStatusPolicy::TreatAsPending);

QString resolveOrderStatusFromProgress(QString status,
                                       qint64 quantity,
                                       qint64 filledQuantity,
                                       EmptyStatusPolicy emptyPolicy = EmptyStatusPolicy::TreatAsPending);

int orderStatusPhase(QString status,
                     EmptyStatusPolicy emptyPolicy = EmptyStatusPolicy::TreatAsPending);

bool shouldIgnoreOrderStatusRegression(const QVariantMap& existingStatus,
                                       const QVariantMap& nextStatus,
                                       EmptyStatusPolicy emptyPolicy = EmptyStatusPolicy::TreatAsPending);

bool isClosedOrderStatus(QString status,
                         EmptyStatusPolicy emptyPolicy = EmptyStatusPolicy::TreatAsPending);

} // namespace order_runtime