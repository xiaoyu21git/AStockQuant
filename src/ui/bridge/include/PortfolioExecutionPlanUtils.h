#pragma once

#include <QCryptographicHash>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>

namespace portfolio_execution_plan {

inline int executionPriorityForSide(const QString& side)
{
    return side.trimmed().compare(QStringLiteral("SELL"), Qt::CaseInsensitive) == 0 ? 0 : 1;
}

inline QString normalizedExecutionSide(const QString& side)
{
    return side.trimmed().toUpper();
}

inline QString batchRoleForSide(const QString& side)
{
    return executionPriorityForSide(side) == 0 ? QStringLiteral("sell") : QStringLiteral("buy");
}

inline QString batchPhaseForSide(const QString& side)
{
    return executionPriorityForSide(side) == 0 ? QStringLiteral("de_risk") : QStringLiteral("re_risk");
}

inline QString batchLabelForSide(const QString& side)
{
    return executionPriorityForSide(side) == 0 ? QStringLiteral("先卖批次") : QStringLiteral("后买批次");
}

inline QString batchDescriptionForSide(const QString& side)
{
    return executionPriorityForSide(side) == 0
        ? QStringLiteral("先释放风险和现金，再推进后续买入批次")
        : QStringLiteral("等待前序卖出批次确认后，再扩张目标仓位");
}

inline QString batchIdForIndex(int batchIndex)
{
    return QStringLiteral("batch_%1").arg(batchIndex + 1);
}

inline int normalizedPositiveBatchCount(const QVariant& value)
{
    bool ok = false;
    const int numericValue = value.toInt(&ok);
    return ok && numericValue > 0 ? numericValue : 0;
}

inline double normalizedPositiveBatchNotional(const QVariant& value)
{
    bool ok = false;
    const double numericValue = value.toDouble(&ok);
    return ok && numericValue > 0.0 ? numericValue : 0.0;
}

inline QString executionScopeIdForOrders(const QVariantList& orders)
{
    QStringList signatureParts;
    signatureParts.reserve(orders.size());
    for (const QVariant& orderVariant : orders) {
        const QVariantMap order = orderVariant.toMap();
        signatureParts.append(QStringLiteral("%1|%2|%3|%4")
            .arg(order.value(QStringLiteral("side")).toString().trimmed().toUpper(),
                 order.value(QStringLiteral("symbol")).toString().trimmed().toUpper(),
                 QString::number(order.value(QStringLiteral("quantity")).toLongLong()),
                 QString::number(order.value(QStringLiteral("price")).toDouble(), 'f', 6)));
    }

    const QByteArray digest = QCryptographicHash::hash(signatureParts.join(QLatin1Char(';')).toUtf8(),
                                                       QCryptographicHash::Sha1).toHex();
    return QStringLiteral("plan_%1").arg(QString::fromLatin1(digest.left(12)));
}

inline int executionPriorityForOrder(const QVariantMap& order)
{
    return executionPriorityForSide(order.value(QStringLiteral("side")).toString());
}

inline void applySellFirstOrdering(QVariantList* orders)
{
    if (!orders || orders->size() < 2) {
        return;
    }

    std::stable_sort(orders->begin(), orders->end(), [](const QVariant& left, const QVariant& right) {
        return executionPriorityForOrder(left.toMap()) < executionPriorityForOrder(right.toMap());
    });
}

inline QVariantMap buildSellFirstExecutionBatches(QVariantList* orders,
                                                  const QVariantMap& batchOptions = QVariantMap{})
{
    const int maxBatchOrders = normalizedPositiveBatchCount(batchOptions.value(QStringLiteral("maxBatchOrders")));
    const double maxBatchNotional = normalizedPositiveBatchNotional(batchOptions.value(QStringLiteral("maxBatchNotional")));
    const double maxBatchNotionalWan = normalizedPositiveBatchNotional(batchOptions.value(QStringLiteral("maxBatchNotionalWan")));
    const int manualCheckpointBatchIndex = normalizedPositiveBatchCount(batchOptions.value(QStringLiteral("manualCheckpointBatchIndex")));
    const bool waitPreviousBatchFilled = !batchOptions.contains(QStringLiteral("waitPreviousBatchFilled"))
        || batchOptions.value(QStringLiteral("waitPreviousBatchFilled")).toBool();
    const bool pauseOnConflict = batchOptions.value(QStringLiteral("pauseOnConflict")).toBool();
    const bool pauseOnAbnormalReject = batchOptions.value(QStringLiteral("pauseOnAbnormalReject")).toBool();
    const bool requireManualCheckpoint = batchOptions.value(QStringLiteral("requireManualCheckpoint")).toBool()
        || manualCheckpointBatchIndex > 0;
    const int effectiveManualCheckpointBatchIndex = requireManualCheckpoint
        ? (manualCheckpointBatchIndex > 0 ? manualCheckpointBatchIndex : 1)
        : 0;
    const bool constrainedBatching = maxBatchOrders > 0 || maxBatchNotional > 0.0;

    QVariantList batches;
    QVariantMap batchSummary{{QStringLiteral("batchCount"), 0},
                             {QStringLiteral("sellBatchCount"), 0},
                             {QStringLiteral("buyBatchCount"), 0},
                             {QStringLiteral("requiresSequentialExecution"), false},
                             {QStringLiteral("batchMode"), constrainedBatching
                                 ? QStringLiteral("sell_first_limited")
                                 : QStringLiteral("sell_first")},
                             {QStringLiteral("constrainedBatching"), constrainedBatching},
                             {QStringLiteral("waitPreviousBatchFilled"), waitPreviousBatchFilled},
                             {QStringLiteral("pauseOnConflict"), pauseOnConflict},
                             {QStringLiteral("pauseOnAbnormalReject"), pauseOnAbnormalReject},
                             {QStringLiteral("requireManualCheckpoint"), requireManualCheckpoint},
                             {QStringLiteral("manualCheckpointBatchIndex"), effectiveManualCheckpointBatchIndex},
                             {QStringLiteral("maxBatchOrders"), maxBatchOrders},
                             {QStringLiteral("maxBatchNotional"), maxBatchNotional},
                             {QStringLiteral("maxBatchNotionalWan"), maxBatchNotionalWan}};

    if (!orders) {
        return QVariantMap{{QStringLiteral("batches"), batches},
                           {QStringLiteral("summary"), batchSummary}};
    }

    applySellFirstOrdering(orders);

    QVariantList annotatedOrders;
    QVariantList activeBatchOrders;
    QString activeSide;
    int activeBatchIndex = -1;
    int executionSequence = 0;
    double activeBatchNotional = 0.0;

    auto flushActiveBatch = [&]() {
        if (activeBatchOrders.isEmpty() || activeBatchIndex < 0) {
            return;
        }

        QVariantMap batch;
        batch.insert(QStringLiteral("batchIndex"), activeBatchIndex);
        batch.insert(QStringLiteral("batchId"), batchIdForIndex(activeBatchIndex));
        batch.insert(QStringLiteral("side"), activeSide);
        batch.insert(QStringLiteral("role"), batchRoleForSide(activeSide));
        batch.insert(QStringLiteral("phase"), batchPhaseForSide(activeSide));
        batch.insert(QStringLiteral("label"), batchLabelForSide(activeSide));
        batch.insert(QStringLiteral("description"), batchDescriptionForSide(activeSide));
        batch.insert(QStringLiteral("orderCount"), activeBatchOrders.size());
        batch.insert(QStringLiteral("estimatedNotional"), activeBatchNotional);
        batch.insert(QStringLiteral("orders"), activeBatchOrders);
        batches.append(batch);
        activeBatchOrders.clear();
        activeBatchNotional = 0.0;
    };

    auto startNewBatch = [&](const QString& side) {
        activeSide = side;
        activeBatchIndex = batches.size();
        activeBatchOrders.clear();
        activeBatchNotional = 0.0;
    };

    for (const QVariant& orderVariant : *orders) {
        QVariantMap order = orderVariant.toMap();
        const QString side = normalizedExecutionSide(order.value(QStringLiteral("side")).toString());
        if (side.isEmpty()) {
            continue;
        }

        if (activeSide != side) {
            flushActiveBatch();
            startNewBatch(side);
        }

        const double orderNotional = (std::max)(0.0, order.value(QStringLiteral("requestedNotional")).toDouble());
        const bool exceedsBatchOrderLimit = maxBatchOrders > 0 && activeBatchOrders.size() >= maxBatchOrders;
        const bool exceedsBatchNotionalLimit = maxBatchNotional > 0.0
            && !activeBatchOrders.isEmpty()
            && (activeBatchNotional + orderNotional) > maxBatchNotional;
        if (!activeBatchOrders.isEmpty() && (exceedsBatchOrderLimit || exceedsBatchNotionalLimit)) {
            flushActiveBatch();
            startNewBatch(side);
        }

        order.insert(QStringLiteral("side"), side);
        order.insert(QStringLiteral("executionSequence"), executionSequence);
        order.insert(QStringLiteral("batchIndex"), activeBatchIndex);
        order.insert(QStringLiteral("batchId"), batchIdForIndex(activeBatchIndex));
        order.insert(QStringLiteral("batchRole"), batchRoleForSide(side));
        order.insert(QStringLiteral("batchPhase"), batchPhaseForSide(side));
        order.insert(QStringLiteral("requiresPreviousBatchFilled"), activeBatchIndex > 0);

        annotatedOrders.append(order);
        activeBatchOrders.append(order);
        activeBatchNotional += orderNotional;
        ++executionSequence;
    }

    flushActiveBatch();
    *orders = annotatedOrders;
    const QString executionScopeId = executionScopeIdForOrders(annotatedOrders);

    int sellBatchCount = 0;
    int buyBatchCount = 0;
    QVariantList finalizedBatches;
    finalizedBatches.reserve(batches.size());
    for (int index = 0; index < batches.size(); ++index) {
        QVariantMap batch = batches.at(index).toMap();
        const QString side = batch.value(QStringLiteral("side")).toString();
        if (executionPriorityForSide(side) == 0) {
            ++sellBatchCount;
        } else {
            ++buyBatchCount;
        }

        const bool batchRequiresManualCheckpoint = effectiveManualCheckpointBatchIndex > 0
            && index >= effectiveManualCheckpointBatchIndex;
        batch.insert(QStringLiteral("requiresPreviousBatchFilled"), waitPreviousBatchFilled && index > 0);
        batch.insert(QStringLiteral("blocksFollowingBatches"), index + 1 < batches.size());
        batch.insert(QStringLiteral("nextBatchId"), index + 1 < batches.size() ? batchIdForIndex(index + 1) : QString());
        batch.insert(QStringLiteral("executionScopeId"), executionScopeId);
        batch.insert(QStringLiteral("pauseOnConflict"), pauseOnConflict);
        batch.insert(QStringLiteral("pauseOnAbnormalReject"), pauseOnAbnormalReject);
        batch.insert(QStringLiteral("requiresManualCheckpoint"), batchRequiresManualCheckpoint);
        batch.insert(QStringLiteral("manualCheckpointBatchIndex"), effectiveManualCheckpointBatchIndex);
        finalizedBatches.append(batch);
    }

    QVariantList ordersWithBatchContext;
    ordersWithBatchContext.reserve(orders->size());
    for (const QVariant& orderVariant : *orders) {
        QVariantMap order = orderVariant.toMap();
        const int batchIndex = order.value(QStringLiteral("batchIndex")).toInt();
        if (batchIndex >= 0 && batchIndex < finalizedBatches.size()) {
            const QVariantMap batch = finalizedBatches.at(batchIndex).toMap();
            order.insert(QStringLiteral("batchOrderCount"), batch.value(QStringLiteral("orderCount")).toInt());
            order.insert(QStringLiteral("blocksFollowingBatches"), batch.value(QStringLiteral("blocksFollowingBatches")).toBool());
            order.insert(QStringLiteral("nextBatchId"), batch.value(QStringLiteral("nextBatchId")).toString());
            order.insert(QStringLiteral("executionScopeId"), executionScopeId);
            order.insert(QStringLiteral("pauseOnConflict"), batch.value(QStringLiteral("pauseOnConflict")).toBool());
            order.insert(QStringLiteral("pauseOnAbnormalReject"), batch.value(QStringLiteral("pauseOnAbnormalReject")).toBool());
            order.insert(QStringLiteral("requiresManualCheckpoint"), batch.value(QStringLiteral("requiresManualCheckpoint")).toBool());
            order.insert(QStringLiteral("manualCheckpointBatchIndex"), batch.value(QStringLiteral("manualCheckpointBatchIndex")).toInt());
            if (batchIndex > 0 && batchIndex - 1 < finalizedBatches.size()) {
                const QVariantMap previousBatch = finalizedBatches.at(batchIndex - 1).toMap();
                order.insert(QStringLiteral("previousBatchId"), previousBatch.value(QStringLiteral("batchId")).toString());
                order.insert(QStringLiteral("previousBatchOrderCount"), previousBatch.value(QStringLiteral("orderCount")).toInt());
            }
        }
        ordersWithBatchContext.append(order);
    }
    *orders = ordersWithBatchContext;

    batchSummary.insert(QStringLiteral("batchCount"), finalizedBatches.size());
    batchSummary.insert(QStringLiteral("sellBatchCount"), sellBatchCount);
    batchSummary.insert(QStringLiteral("buyBatchCount"), buyBatchCount);
    batchSummary.insert(QStringLiteral("requiresSequentialExecution"), finalizedBatches.size() > 1);
    batchSummary.insert(QStringLiteral("executionScopeId"), executionScopeId);
    batchSummary.insert(QStringLiteral("firstManualCheckpointBatchId"),
        effectiveManualCheckpointBatchIndex > 0 && effectiveManualCheckpointBatchIndex < finalizedBatches.size()
            ? finalizedBatches.at(effectiveManualCheckpointBatchIndex).toMap().value(QStringLiteral("batchId")).toString()
            : QString());
    batchSummary.insert(QStringLiteral("firstBatchId"), finalizedBatches.isEmpty()
        ? QString()
        : finalizedBatches.first().toMap().value(QStringLiteral("batchId")).toString());

    return QVariantMap{{QStringLiteral("batches"), finalizedBatches},
                       {QStringLiteral("summary"), batchSummary}};
}

}  // namespace portfolio_execution_plan