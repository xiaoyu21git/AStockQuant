#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace order_runtime {

inline QStringList orderRecordCandidateIds(const QVariantMap& orderRecord)
{
    return {
        orderRecord.value(QStringLiteral("orderId")).toString().trimmed(),
        orderRecord.value(QStringLiteral("clientOrderId")).toString().trimmed(),
        orderRecord.value(QStringLiteral("brokerOrderId")).toString().trimmed()
    };
}

inline bool orderRecordMatchesId(const QVariantMap& orderRecord, const QString& orderId)
{
    const QString normalizedOrderId = orderId.trimmed();
    if (normalizedOrderId.isEmpty()) {
        return false;
    }

    const QStringList candidateIds = orderRecordCandidateIds(orderRecord);
    for (const QString& candidateId : candidateIds) {
        if (candidateId == normalizedOrderId) {
            return true;
        }
    }

    return false;
}

inline QVariantMap findOrderRecord(const QVariantList& orderRecords, const QStringList& candidateIds)
{
    for (const QVariant& entry : orderRecords) {
        const QVariantMap orderRecord = entry.toMap();
        for (const QString& candidateId : candidateIds) {
            if (orderRecordMatchesId(orderRecord, candidateId)) {
                return orderRecord;
            }
        }
    }

    return {};
}

inline QVariantMap findOrderRecord(const QVariantList& orderRecords, const QString& orderId)
{
    return findOrderRecord(orderRecords, QStringList{ orderId });
}

inline bool orderRecordsReferToSameOrder(const QVariantMap& lhs, const QVariantMap& rhs)
{
    QStringList candidateIds = orderRecordCandidateIds(lhs);
    candidateIds.append(orderRecordCandidateIds(rhs));

    for (const QString& candidateId : candidateIds) {
        if (candidateId.isEmpty()) {
            continue;
        }
        if (orderRecordMatchesId(lhs, candidateId) && orderRecordMatchesId(rhs, candidateId)) {
            return true;
        }
    }

    return false;
}

inline bool orderRecordHasIdentity(const QVariantMap& orderRecord)
{
    const QStringList candidateIds = orderRecordCandidateIds(orderRecord);
    for (const QString& candidateId : candidateIds) {
        if (!candidateId.isEmpty()) {
            return true;
        }
    }
    return false;
}

inline QVariantMap replaceOrderRecord(const QVariantMap&, const QVariantMap& incomingRecord)
{
    return incomingRecord;
}

inline QVariantMap overlayOrderRecord(const QVariantMap& existingRecord, const QVariantMap& incomingRecord)
{
    QVariantMap mergedRecord = existingRecord;
    for (auto it = incomingRecord.constBegin(); it != incomingRecord.constEnd(); ++it) {
        mergedRecord.insert(it.key(), it.value());
    }
    return mergedRecord;
}

template <typename MergeFn, typename IgnoreFn, typename EquivalentFn>
inline bool upsertOrderRecord(QVariantList* orderRecords,
                              const QVariantMap& incomingRecord,
                              MergeFn&& mergeRecord,
                              IgnoreFn&& shouldIgnoreRegression,
                              EquivalentFn&& areEquivalent,
                              int maxRecords = 50)
{
    if (orderRecords == nullptr) {
        return false;
    }

    QVariantMap mergedRecord = incomingRecord;
    if (orderRecordHasIdentity(incomingRecord)) {
        for (int index = 0; index < orderRecords->size(); ++index) {
            const QVariantMap existingRecord = orderRecords->at(index).toMap();
            if (!orderRecordsReferToSameOrder(existingRecord, incomingRecord)) {
                continue;
            }

            if (shouldIgnoreRegression(existingRecord, incomingRecord)) {
                return false;
            }

            mergedRecord = mergeRecord(existingRecord, incomingRecord);
            if (areEquivalent(existingRecord, mergedRecord)) {
                return false;
            }

            orderRecords->removeAt(index);
            break;
        }
    }

    orderRecords->push_front(mergedRecord);
    while (orderRecords->size() > maxRecords) {
        orderRecords->removeLast();
    }
    return true;
}

} // namespace order_runtime