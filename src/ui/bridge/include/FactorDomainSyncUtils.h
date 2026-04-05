#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QSet>

#include <limits>

namespace factor::bridge {

struct FactorDomainExistingRecord {
    QString instanceId;
    QString factorId;
};

struct FactorDomainSyncWritePlan {
    QString persistedInstanceId;
    QStringList duplicateInstanceIds;
    bool updateExisting{false};
};

inline int factorDomainRecordPriority(const QString& requestedInstanceId,
                                      const QString& factorId,
                                      const FactorDomainExistingRecord& record)
{
    const QString existingInstanceId = record.instanceId.trimmed();
    if (existingInstanceId.isEmpty()) {
        return (std::numeric_limits<int>::max)();
    }

    if (existingInstanceId == requestedInstanceId) {
        return 0;
    }

    if (record.factorId.trimmed() == factorId) {
        return 1;
    }

    return (std::numeric_limits<int>::max)();
}

inline FactorDomainSyncWritePlan planFactorDomainSyncWrite(const QString& requestedInstanceId,
                                                           const QString& factorId,
                                                           const QVector<FactorDomainExistingRecord>& existingRecords,
                                                           bool forceRequestedInstanceId = false)
{
    FactorDomainSyncWritePlan plan;
    const QString trimmedRequestedInstanceId = requestedInstanceId.trimmed();
    const QString trimmedFactorId = factorId.trimmed();
    if (trimmedRequestedInstanceId.isEmpty() || trimmedFactorId.isEmpty()) {
        return plan;
    }

    plan.persistedInstanceId = trimmedRequestedInstanceId;

    if (forceRequestedInstanceId) {
        for (const FactorDomainExistingRecord& record : existingRecords) {
            if (record.instanceId.trimmed() == trimmedRequestedInstanceId) {
                plan.updateExisting = true;
                break;
            }
        }
    } else {
        int bestPriority = (std::numeric_limits<int>::max)();
        for (const FactorDomainExistingRecord& record : existingRecords) {
            const int priority = factorDomainRecordPriority(trimmedRequestedInstanceId, trimmedFactorId, record);
            if (priority >= bestPriority) {
                continue;
            }

            const QString existingInstanceId = record.instanceId.trimmed();
            if (existingInstanceId.isEmpty()) {
                continue;
            }

            bestPriority = priority;
            plan.persistedInstanceId = existingInstanceId;
            plan.updateExisting = true;
            if (bestPriority == 0) {
                break;
            }
        }
    }

    QSet<QString> seenDuplicateInstanceIds;
    for (const FactorDomainExistingRecord& record : existingRecords) {
        const QString existingFactorId = record.factorId.trimmed();
        const QString existingInstanceId = record.instanceId.trimmed();
        if (existingFactorId != trimmedFactorId
            || existingInstanceId.isEmpty()
            || existingInstanceId == plan.persistedInstanceId
            || seenDuplicateInstanceIds.contains(existingInstanceId)) {
            continue;
        }

        seenDuplicateInstanceIds.insert(existingInstanceId);
        plan.duplicateInstanceIds.append(existingInstanceId);
    }

    return plan;
}

}  // namespace factor::bridge