#include "TradeExecutionService.h"

#include "ExecutionSchedulingRuleChain.h"
#include "OrderRecordUtils.h"
#include "OrderRuntimeUtils.h"
#include "RiskConfigService.h"
#include "RiskMonitorService.h"
#include "TradingConnectionConfigService.h"

#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"

#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>
#include <QPointer>
#include <QSet>
#include <QTimeZone>

#include <cmath>
#include <cstdlib>
#include <thread>

#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
#include "JujinApi.h"
#endif

namespace {

constexpr order_runtime::EmptyStatusPolicy kRecentOrderStatusPolicy = order_runtime::EmptyStatusPolicy::KeepEmpty;

QString eventStringValue(const engine::EventFormat& event, const std::string& key)
{
    const auto metadataIt = event.metadata.find(key);
    if (metadataIt != event.metadata.end()) {
        return QString::fromStdString(metadataIt->second).trimmed();
    }

    auto stringValue = event.get<std::string>(key);
    if (stringValue.has_value()) {
        return QString::fromStdString(*stringValue).trimmed();
    }

    auto doubleValue = event.get<double>(key);
    if (doubleValue.has_value()) {
        return QString::number(*doubleValue, 'f', 6);
    }

    auto intValue = event.get<int64_t>(key);
    if (intValue.has_value()) {
        return QString::number(*intValue);
    }

    return {};
}

QString normalizedStatusOriginValue(const QVariantMap& orderRecord)
{
    return orderRecord.value(QStringLiteral("statusOrigin")).toString().trimmed().toLower();
}

QString ruleHitStageCode(const QString& statusOrigin)
{
    if (statusOrigin == QStringLiteral("execution_rule_reject")) {
        return QStringLiteral("ExecutionScheduling");
    }
    if (statusOrigin == QStringLiteral("risk_reject") || statusOrigin == QStringLiteral("risk_pending")) {
        return QStringLiteral("PreTradeRisk");
    }
    if (statusOrigin == QStringLiteral("broker_reject") || statusOrigin == QStringLiteral("broker_submit")) {
        return QStringLiteral("BrokerSubmission");
    }
    if (statusOrigin == QStringLiteral("runtime")) {
        return QStringLiteral("RuntimeRecovery");
    }
    return QStringLiteral("Unknown");
}

QString ruleHitStageLabel(const QString& stageCode)
{
    if (stageCode == QStringLiteral("ExecutionScheduling")) {
        return QStringLiteral("执行编排");
    }
    if (stageCode == QStringLiteral("PreTradeRisk")) {
        return QStringLiteral("预交易风控");
    }
    if (stageCode == QStringLiteral("BrokerSubmission")) {
        return QStringLiteral("券商提交");
    }
    if (stageCode == QStringLiteral("RuntimeRecovery")) {
        return QStringLiteral("运行时恢复");
    }
    return QStringLiteral("未知阶段");
}

QString ruleHitDecisionType(const QVariantMap& orderRecord)
{
    const QString normalizedStatus = order_runtime::normalizeOrderStatus(
        orderRecord.value(QStringLiteral("status")).toString(),
        kRecentOrderStatusPolicy);
    if (normalizedStatus == QStringLiteral("REJECTED")) {
        return QStringLiteral("Block");
    }
    if (normalizedStatus == QStringLiteral("PENDING_RISK")) {
        return QStringLiteral("Warn");
    }
    return QStringLiteral("Pass");
}

QString ruleHitOrderIdentity(const QVariantMap& orderRecord)
{
    for (const QString& key : {QStringLiteral("orderId"),
                               QStringLiteral("clientOrderId"),
                               QStringLiteral("brokerOrderId")}) {
        const QString value = orderRecord.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString ruleHitKey(const QVariantMap& orderRecord)
{
    const QString ruleId = orderRecord.value(QStringLiteral("ruleId")).toString().trimmed();
    const QString reasonCode = orderRecord.value(QStringLiteral("reasonCode")).toString().trimmed();
    if (ruleId.isEmpty() && reasonCode.isEmpty()) {
        return {};
    }

    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(ruleHitOrderIdentity(orderRecord),
             ruleId,
             reasonCode,
             orderRecord.value(QStringLiteral("requiredBatchId")).toString().trimmed(),
             orderRecord.value(QStringLiteral("blockingBatchId")).toString().trimmed(),
             normalizedStatusOriginValue(orderRecord),
             order_runtime::normalizeOrderStatus(orderRecord.value(QStringLiteral("status")).toString(),
                                                 kRecentOrderStatusPolicy),
             orderRecord.value(QStringLiteral("message")).toString().trimmed());
}

QVariantMap buildRuleHitRecord(const QVariantMap& orderRecord)
{
    const QString hitId = ruleHitKey(orderRecord);
    if (hitId.isEmpty()) {
        return {};
    }

    const QString statusOrigin = normalizedStatusOriginValue(orderRecord);
    const QString stageCode = ruleHitStageCode(statusOrigin);
    QVariantMap ruleHit;
    ruleHit.insert(QStringLiteral("hitId"), hitId);
    ruleHit.insert(QStringLiteral("orderId"), ruleHitOrderIdentity(orderRecord));
    ruleHit.insert(QStringLiteral("clientOrderId"), orderRecord.value(QStringLiteral("clientOrderId")).toString());
    ruleHit.insert(QStringLiteral("brokerOrderId"), orderRecord.value(QStringLiteral("brokerOrderId")).toString());
    ruleHit.insert(QStringLiteral("strategyId"), orderRecord.value(QStringLiteral("strategyId")).toString());
    ruleHit.insert(QStringLiteral("strategyName"), orderRecord.value(QStringLiteral("strategyName")).toString());
    ruleHit.insert(QStringLiteral("runtimeStrategyId"), orderRecord.value(QStringLiteral("runtimeStrategyId")).toString());
    ruleHit.insert(QStringLiteral("symbol"), orderRecord.value(QStringLiteral("symbol")).toString());
    ruleHit.insert(QStringLiteral("side"), orderRecord.value(QStringLiteral("side")).toString());
    ruleHit.insert(QStringLiteral("action"), orderRecord.value(QStringLiteral("action")).toString());
    ruleHit.insert(QStringLiteral("price"), orderRecord.value(QStringLiteral("price")));
    ruleHit.insert(QStringLiteral("quantity"), orderRecord.value(QStringLiteral("quantity")));
    ruleHit.insert(QStringLiteral("status"), orderRecord.value(QStringLiteral("status")).toString());
    ruleHit.insert(QStringLiteral("statusOrigin"), statusOrigin);
    ruleHit.insert(QStringLiteral("ruleId"), orderRecord.value(QStringLiteral("ruleId")).toString());
    ruleHit.insert(QStringLiteral("reasonCode"), orderRecord.value(QStringLiteral("reasonCode")).toString());
    ruleHit.insert(QStringLiteral("message"), orderRecord.value(QStringLiteral("message")).toString());
    ruleHit.insert(QStringLiteral("stageCode"), stageCode);
    ruleHit.insert(QStringLiteral("stageLabel"), ruleHitStageLabel(stageCode));
    ruleHit.insert(QStringLiteral("decisionType"), ruleHitDecisionType(orderRecord));
    ruleHit.insert(QStringLiteral("templateRuleGroupId"),
                   orderRecord.value(QStringLiteral("templateRuleGroupId")).toString());
    ruleHit.insert(QStringLiteral("templateRuleGroupTitle"),
                   orderRecord.value(QStringLiteral("templateRuleGroupTitle")).toString());
    ruleHit.insert(QStringLiteral("templateRuleGroupRole"),
                   orderRecord.value(QStringLiteral("templateRuleGroupRole")).toString());
    ruleHit.insert(QStringLiteral("templateRuleGroupOperator"),
                   orderRecord.value(QStringLiteral("templateRuleGroupOperator")).toString());
    ruleHit.insert(QStringLiteral("executionScopeId"), orderRecord.value(QStringLiteral("executionScopeId")).toString());
    ruleHit.insert(QStringLiteral("batchId"), orderRecord.value(QStringLiteral("batchId")).toString());
    ruleHit.insert(QStringLiteral("requiredBatchId"), orderRecord.value(QStringLiteral("requiredBatchId")).toString());
    ruleHit.insert(QStringLiteral("blockingBatchId"), orderRecord.value(QStringLiteral("blockingBatchId")).toString());
    ruleHit.insert(QStringLiteral("blockingOrderId"), orderRecord.value(QStringLiteral("blockingOrderId")).toString());
    ruleHit.insert(QStringLiteral("blockingStatus"), orderRecord.value(QStringLiteral("blockingStatus")).toString());
    ruleHit.insert(QStringLiteral("observedAt"),
                   orderRecord.value(QStringLiteral("updatedAt")).toString().trimmed().isEmpty()
                       ? (orderRecord.value(QStringLiteral("createdAt")).toString().trimmed().isEmpty()
                              ? QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                              : orderRecord.value(QStringLiteral("createdAt")).toString())
                       : orderRecord.value(QStringLiteral("updatedAt")).toString());
    return ruleHit;
}

std::optional<trading::execution::SchedulingRuleBlock> schedulingRuleBlockFromMap(
    const QString& ruleId,
    const QVariantMap& blockMap)
{
    if (blockMap.isEmpty()) {
        return std::nullopt;
    }

    trading::execution::SchedulingRuleBlock block;
    block.ruleId = ruleId;
    block.reasonCode = blockMap.value(QStringLiteral("reasonCode")).toString();
    block.message = blockMap.value(QStringLiteral("message")).toString();
    block.attributes = blockMap;
    block.attributes.remove(QStringLiteral("reasonCode"));
    block.attributes.remove(QStringLiteral("message"));
    return block;
}

double eventDoubleValue(const engine::EventFormat& event, const std::string& key, double fallback = 0.0)
{
    auto doubleValue = event.get<double>(key);
    if (doubleValue.has_value()) {
        return *doubleValue;
    }

    auto intValue = event.get<int64_t>(key);
    if (intValue.has_value()) {
        return static_cast<double>(*intValue);
    }

    const QString textValue = eventStringValue(event, key);
    if (textValue.isEmpty()) {
        return fallback;
    }

    bool ok = false;
    const double value = textValue.toDouble(&ok);
    return ok ? value : fallback;
}

double configDoubleValue(const QVariantMap& configuration, const QStringList& keys, double fallback)
{
    for (const QString& key : keys) {
        if (!configuration.contains(key)) {
            continue;
        }

        bool ok = false;
        const double value = configuration.value(key).toDouble(&ok);
        if (ok) {
            return value;
        }
    }

    return fallback;
}

QVariantMap loadTradingConnectionConfiguration()
{
    TradingConnectionConfigService* configService = TradingConnectionConfigService::instance();
    if (!configService) {
        return {};
    }

    return configService->loadConfiguration();
}

QString configuredStrategyIdFromBindingEntry(const QVariantMap& entry)
{
    return entry.value(QStringLiteral("strategyId")).toString().trimmed();
}

QSet<QString> configuredBoundStrategyIds(const QVariantMap& configuration)
{
    QSet<QString> strategyIds;

    const QVariantList boundStrategies = configuration.value(QStringLiteral("boundStrategies")).toList();
    for (const QVariant& rawEntry : boundStrategies) {
        const QString strategyId = configuredStrategyIdFromBindingEntry(rawEntry.toMap());
        if (!strategyId.isEmpty()) {
            strategyIds.insert(strategyId);
        }
    }

    const QString primaryStrategyId = configuration.value(QStringLiteral("boundStrategyId")).toString().trimmed();
    if (!primaryStrategyId.isEmpty()) {
        strategyIds.insert(primaryStrategyId);
    }

    return strategyIds;
}

QString resolvedBusinessStrategyId(const QVariantMap& request, const QVariantMap& configuration)
{
    const QString requested = request.value(QStringLiteral("strategyId")).toString().trimmed();
    if (!requested.isEmpty()) {
        return requested;
    }

    const QString configured = configuration.value(QStringLiteral("boundStrategyId")).toString().trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    return QStringLiteral("manual_test");
}

QString resolvedBusinessStrategyName(const QVariantMap& request,
                                    const QVariantMap& configuration,
                                    const QString& strategyId)
{
    const QString requested = request.value(QStringLiteral("strategyName")).toString().trimmed();
    if (!requested.isEmpty()) {
        return requested;
    }

    const QString configured = configuration.value(QStringLiteral("boundStrategyName")).toString().trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    if (strategyId == QStringLiteral("manual_test")) {
        return QStringLiteral("Manual Test");
    }

    return strategyId;
}

QString resolvedGmStrategyId(const QVariantMap& request, const QVariantMap& configuration)
{
    const QString requestedGmStrategyId = request.value(QStringLiteral("gmStrategyId")).toString().trimmed();
    if (!requestedGmStrategyId.isEmpty()) {
        return requestedGmStrategyId;
    }
    const QString requested = request.value(QStringLiteral("runtimeStrategyId")).toString().trimmed();
    if (!requested.isEmpty()) {
        return requested;
    }

    const QString configuredGmStrategyId = configuration.value(QStringLiteral("gmStrategyId")).toString().trimmed();
    if (!configuredGmStrategyId.isEmpty()) {
        return configuredGmStrategyId;
    }

    const QString configured = configuration.value(QStringLiteral("runtimeStrategyId")).toString().trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    return {};
}

QString eventStrategyId(const engine::EventFormat& event)
{
    return eventStringValue(event, "business_strategy_id");
}

qint64 deriveOrderQuantity(double orderSizeLimitWan, double price)
{
    if (price <= 0.0) {
        return 0;
    }

    const double normalizedOrderSizeLimitWan = orderSizeLimitWan > 1.0 ? orderSizeLimitWan : 1.0;
    const double notional = normalizedOrderSizeLimitWan * 10000.0;
    const double boardLots = std::floor(notional / price / 100.0);
    const qint64 quantity = static_cast<qint64>(boardLots) * 100;
    return quantity > 0 ? quantity : 100;
}

bool isPlaceholderAccountId(const QString& accountId)
{
    const QString normalized = accountId.trimmed().toUpper();
    return normalized.isEmpty() || normalized == QStringLiteral("SIM_ACCOUNT");
}

QString readEnvironmentText(const char* name)
{
    if (const char* value = std::getenv(name)) {
        return QString::fromLocal8Bit(value).trimmed();
    }
    return {};
}

bool isDeferredBrokerSubmissionError(const QString& errorText)
{
    const QString normalized = errorText.trimmed().toLower();
    return normalized.contains(QStringLiteral("timed out waiting for sdk callback thread to place order"));
}

bool isPendingConfirmationOrderId(const std::string& orderId)
{
    return orderId.rfind("pending-confirmation-", 0) == 0;
}

QString normalizeManualOrderType(QString orderType)
{
    orderType = orderType.trimmed().toUpper();
    if (orderType == QStringLiteral("MARKET")) {
        return QStringLiteral("MARKET");
    }
    return QStringLiteral("LIMIT");
}

QString normalizeOrderMode(QString mode)
{
    mode = mode.trimmed().toLower();
    if (mode == QStringLiteral("futures") ||
        mode == QStringLiteral("options") ||
        mode == QStringLiteral("margin_buy") ||
        mode == QStringLiteral("margin_sell")) {
        return mode;
    }
    return QStringLiteral("stock");
}

QString normalizeOrderSideText(QString side)
{
    side = side.trimmed().toUpper();
    if (side == QStringLiteral("BUY") || side == QStringLiteral("LONG")) {
        return QStringLiteral("BUY");
    }
    if (side == QStringLiteral("SELL") || side == QStringLiteral("SHORT")) {
        return QStringLiteral("SELL");
    }
    return {};
}

QString normalizePositionEffectText(QString positionEffect)
{
    positionEffect = positionEffect.trimmed().toUpper();
    if (positionEffect == QStringLiteral("1") || positionEffect == QStringLiteral("OPEN")) {
        return QStringLiteral("OPEN");
    }
    if (positionEffect == QStringLiteral("2") || positionEffect == QStringLiteral("CLOSE")) {
        return QStringLiteral("CLOSE");
    }
    return {};
}

QString normalizedOrderSideFromRecord(const QVariantMap& orderRecord)
{
    return normalizeOrderSideText(
        orderRecord.value(QStringLiteral("side")).toString().trimmed().isEmpty()
            ? orderRecord.value(QStringLiteral("action")).toString()
            : orderRecord.value(QStringLiteral("side")).toString());
}

QString normalizedConflictStatus(const QVariantMap& orderRecord)
{
    return order_runtime::normalizeOrderStatus(orderRecord.value(QStringLiteral("status")).toString(),
                                               kRecentOrderStatusPolicy);
}

QString preferredOrderIdentity(const QVariantMap& orderRecord)
{
    const QString clientOrderId = orderRecord.value(QStringLiteral("clientOrderId")).toString().trimmed();
    if (!clientOrderId.isEmpty()) {
        return clientOrderId;
    }

    const QString orderId = orderRecord.value(QStringLiteral("orderId")).toString().trimmed();
    if (!orderId.isEmpty()) {
        return orderId;
    }

    return orderRecord.value(QStringLiteral("brokerOrderId")).toString().trimmed();
}

QString pendingOrderConflictMessage(const QString& symbol,
                                   const QString& requestedSide,
                                   const QVariantMap& conflictingOrder)
{
    const QString conflictingSide = normalizedOrderSideFromRecord(conflictingOrder);
    const QString conflictingStatus = normalizedConflictStatus(conflictingOrder);
    const QString conflictingOrderId = preferredOrderIdentity(conflictingOrder);

    QString message = QStringLiteral("同标的 %1 存在未完成的 %2 委托")
        .arg(symbol, conflictingSide.isEmpty() ? QStringLiteral("未知方向") : conflictingSide);

    if (!conflictingStatus.isEmpty()) {
        message += QStringLiteral("（状态 %1")
            .arg(conflictingStatus);
        if (!conflictingOrderId.isEmpty()) {
            message += QStringLiteral(" / 编号 %1").arg(conflictingOrderId);
        }
        message += QLatin1Char(')');
    } else if (!conflictingOrderId.isEmpty()) {
        message += QStringLiteral("（编号 %1）").arg(conflictingOrderId);
    }

    message += QStringLiteral("，禁止继续提交反向 %1 委托").arg(requestedSide);
    return message;
}

bool isCashRepayAction(const QString& action)
{
    const QString normalized = action.trimmed().toLower();
    return normalized == QStringLiteral("repay")
        || normalized == QStringLiteral("cashrepay")
        || normalized == QStringLiteral("creditrepaycash");
}

bool isShareReturnAction(const QString& action)
{
    const QString normalized = action.trimmed().toLower();
    return normalized == QStringLiteral("returnstock")
        || normalized == QStringLiteral("repayshare")
        || normalized == QStringLiteral("creditrepayshare");
}

bool isPriceOptionalAction(const QString& action)
{
    return isCashRepayAction(action) || isShareReturnAction(action);
}

bool isQuantityOptionalAction(const QString& action)
{
    return isCashRepayAction(action);
}

double requestedNotionalValue(double price, qint64 quantity, double cashAmount)
{
    if (cashAmount > 0.0) {
        return cashAmount;
    }
    if (price > 0.0 && quantity > 0) {
        return price * static_cast<double>(quantity);
    }
    return 0.0;
}

QString positionEffectMetadataText(const QString& positionEffect)
{
    if (positionEffect == QStringLiteral("OPEN")) {
        return QStringLiteral("1");
    }
    if (positionEffect == QStringLiteral("CLOSE")) {
        return QStringLiteral("2");
    }
    return {};
}

bool isBoardLotOrderMode(const QString& mode)
{
    return mode == QStringLiteral("stock") ||
        mode == QStringLiteral("margin_buy") ||
        mode == QStringLiteral("margin_sell");
}

bool boolishText(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    return normalized == QStringLiteral("true")
        || normalized == QStringLiteral("1")
        || normalized == QStringLiteral("yes")
        || normalized == QStringLiteral("y");
}

bool boolishValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return false;
    }

    if (value.metaType().id() == QMetaType::Bool) {
        return value.toBool();
    }

    return boolishText(value.toString());
}

int positiveIntegerValue(const QVariant& value)
{
    bool ok = false;
    const int numericValue = value.toInt(&ok);
    return ok && numericValue > 0 ? numericValue : 0;
}

QString executionBatchIdForIndex(int batchIndex)
{
    return batchIndex >= 0 ? QStringLiteral("batch_%1").arg(batchIndex + 1) : QString();
}

QString matchingOrderIdentity(const QVariantMap& orderRecord)
{
    return preferredOrderIdentity(orderRecord);
}

QString partialFillAdvanceMissingBatchMessage(const QString& requestedBatchId,
                                             const QString& requiredBatchId,
                                             int observedOrderCount,
                                             int expectedOrderCount)
{
    QString message = QStringLiteral("执行批次 %1 依赖前序批次 %2 全部成交")
        .arg(requestedBatchId.isEmpty() ? QStringLiteral("未知批次") : requestedBatchId,
             requiredBatchId.isEmpty() ? QStringLiteral("未知批次") : requiredBatchId);

    if (expectedOrderCount > 0) {
        message += QStringLiteral("，当前仅检测到 %1/%2 笔前序委托")
            .arg(observedOrderCount)
            .arg(expectedOrderCount);
    } else if (observedOrderCount <= 0) {
        message += QStringLiteral("，当前尚未检测到前序批次委托");
    }

    message += QStringLiteral("，禁止提前推进后续批次");
    return message;
}

QString partialFillAdvanceBlockingOrderMessage(const QString& requestedBatchId,
                                              const QString& requiredBatchId,
                                              const QVariantMap& blockingOrder)
{
    const QString blockingOrderId = matchingOrderIdentity(blockingOrder);
    const QString blockingStatus = order_runtime::normalizeOrderStatus(
        blockingOrder.value(QStringLiteral("status")).toString(),
        kRecentOrderStatusPolicy);
    const qint64 quantity = blockingOrder.value(QStringLiteral("quantity")).toLongLong();
    const qint64 filledQuantity = blockingOrder.value(QStringLiteral("filledQuantity")).toLongLong();

    QString message = QStringLiteral("执行批次 %1 依赖前序批次 %2 全部成交")
        .arg(requestedBatchId.isEmpty() ? QStringLiteral("未知批次") : requestedBatchId,
             requiredBatchId.isEmpty() ? QStringLiteral("未知批次") : requiredBatchId);

    if (!blockingOrderId.isEmpty()) {
        message += QStringLiteral("，当前委托 %1").arg(blockingOrderId);
    } else {
        message += QStringLiteral("，当前前序委托");
    }

    if (blockingStatus == QStringLiteral("PARTIAL_FILLED") && quantity > 0) {
        message += QStringLiteral("仅部分成交（%1/%2）").arg(filledQuantity).arg(quantity);
    } else if (!blockingStatus.isEmpty()) {
        message += QStringLiteral("状态为 %1").arg(blockingStatus);
    } else {
        message += QStringLiteral("尚未满足全部成交推进条件");
    }

    message += QStringLiteral("，禁止继续推进后续批次");
    return message;
}

bool matchesExecutionBatchIdentity(const QVariantMap& orderRecord,
                                   const QString& strategyId,
                                   const QString& runtimeStrategyId)
{
    const QString recordRuntimeStrategyId = orderRecord.value(QStringLiteral("runtimeStrategyId")).toString().trimmed();
    if (!runtimeStrategyId.trimmed().isEmpty()
        && !recordRuntimeStrategyId.isEmpty()
        && recordRuntimeStrategyId != runtimeStrategyId.trimmed()) {
        return false;
    }

    const QString recordStrategyId = orderRecord.value(QStringLiteral("strategyId")).toString().trimmed();
    if (!strategyId.trimmed().isEmpty()
        && !recordStrategyId.isEmpty()
        && recordStrategyId != strategyId.trimmed()) {
        return false;
    }

    return true;
}

QString executionScopeIdFromRecord(const QVariantMap& orderRecord)
{
    return orderRecord.value(QStringLiteral("executionScopeId")).toString().trimmed();
}

QString executionPauseScopeKey(const QVariantMap& orderRecord);

QString executionCheckpointKey(const QVariantMap& orderRecord)
{
    const QString scopeKey = executionPauseScopeKey(orderRecord);
    const QString batchId = orderRecord.value(QStringLiteral("batchId")).toString().trimmed();
    if (scopeKey.isEmpty() || batchId.isEmpty()) {
        return {};
    }

    return QStringLiteral("%1|checkpoint:%2").arg(scopeKey, batchId);
}

QString executionPauseScopeKey(const QVariantMap& orderRecord)
{
    const QString executionScopeId = executionScopeIdFromRecord(orderRecord);
    if (!executionScopeId.isEmpty()) {
        return QStringLiteral("scope:%1").arg(executionScopeId);
    }
    return {};
}

bool isAbnormalRejectStatusOrigin(const QString& statusOrigin)
{
    const QString normalized = statusOrigin.trimmed().toLower();
    return normalized == QStringLiteral("runtime") || normalized == QStringLiteral("broker_reject");
}

bool shouldClearPausedExecutionFromOrder(const QVariantMap& orderRecord)
{
    const QString statusOrigin = orderRecord.value(QStringLiteral("statusOrigin")).toString().trimmed().toLower();
    if (statusOrigin != QStringLiteral("runtime")
        && statusOrigin != QStringLiteral("broker_submit")
        && statusOrigin != QStringLiteral("local_pending")) {
        return false;
    }

    const QString status = order_runtime::normalizeOrderStatus(orderRecord.value(QStringLiteral("status")).toString(),
                                                               kRecentOrderStatusPolicy);
    return status == QStringLiteral("PENDING")
        || status == QStringLiteral("SUBMITTED")
        || status == QStringLiteral("PARTIAL_FILLED")
        || status == QStringLiteral("FILLED");
}

QString pausedExecutionMessage(const QString& requestedBatchId,
                               const QVariantMap& pausedScope)
{
    const QString pausedBatchId = pausedScope.value(QStringLiteral("pausedBatchId")).toString().trimmed();
    const QString blockingOrderId = pausedScope.value(QStringLiteral("blockingOrderId")).toString().trimmed();
    const QString blockingStatus = pausedScope.value(QStringLiteral("blockingStatus")).toString().trimmed();

    QString message = QStringLiteral("执行轮次已因异常拒单暂停");
    if (!requestedBatchId.isEmpty()) {
        message += QStringLiteral("，当前批次 %1").arg(requestedBatchId);
    }
    if (!pausedBatchId.isEmpty()) {
        message += QStringLiteral(" 受前序批次 %1 影响").arg(pausedBatchId);
    }
    if (!blockingOrderId.isEmpty()) {
        message += QStringLiteral("，阻断委托 %1").arg(blockingOrderId);
    }
    if (!blockingStatus.isEmpty()) {
        message += QStringLiteral(" 状态为 %1").arg(blockingStatus);
    }
    message += QStringLiteral("，请重试当前批次或等待人工恢复后再推进后续批次");
    return message;
}

QString manualCheckpointMessage(const QString& requestedBatchId,
                               const QVariantMap& orderRequest)
{
    QString message = QStringLiteral("执行轮次需要人工检查点确认");
    if (!requestedBatchId.isEmpty()) {
        message += QStringLiteral("，当前批次 %1").arg(requestedBatchId);
    }

    const QString previousBatchId = orderRequest.value(QStringLiteral("previousBatchId")).toString().trimmed();
    if (!previousBatchId.isEmpty()) {
        message += QStringLiteral(" 位于前序批次 %1 之后").arg(previousBatchId);
    }

    message += QStringLiteral("，请先完成人工确认后再继续提交该批次");
    return message;
}

void applyOrderContext(QVariantMap* target, const QVariantMap& orderContext)
{
    if (!target) {
        return;
    }

    for (auto it = orderContext.constBegin(); it != orderContext.constEnd(); ++it) {
        const QString textValue = it.value().toString().trimmed();
        if (textValue.isEmpty()) {
            continue;
        }
        target->insert(it.key(), textValue);
    }
}

void applyEventString(engine::EventFormat* event, const char* eventKey, const char* metadataKey, const QString& value)
{
    if (!event) {
        return;
    }

    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    event->set(eventKey, trimmed.toStdString());
    event->metadata[metadataKey] = trimmed.toStdString();
}

void applyEventNumber(engine::EventFormat* event, const char* eventKey, const char* metadataKey, double value)
{
    if (!event || !std::isfinite(value) || value <= 0.0) {
        return;
    }

    event->set(eventKey, value);
    event->metadata[metadataKey] = QString::number(value, 'f', 6).toStdString();
}

void applyOrderContextToEvent(engine::EventFormat* event, const QVariantMap& orderRecord)
{
    applyEventString(event, "client_order_id", "client_order_id", orderRecord.value(QStringLiteral("clientOrderId")).toString());
    applyEventString(event, "broker_order_id", "broker_order_id", orderRecord.value(QStringLiteral("brokerOrderId")).toString());
    applyEventString(event, "business_strategy_id", "business_strategy_id", orderRecord.value(QStringLiteral("strategyId")).toString());
    applyEventString(event, "runtime_strategy_id", "runtime_strategy_id", orderRecord.value(QStringLiteral("runtimeStrategyId")).toString());
    applyEventString(event, "type", "type", orderRecord.value(QStringLiteral("type")).toString());
    applyEventString(event, "action", "action", orderRecord.value(QStringLiteral("action")).toString());
    applyEventString(event, "position_effect", "position_effect", orderRecord.value(QStringLiteral("position_effect")).toString());
    applyEventString(event, "position_effect_text", "position_effect_text", orderRecord.value(QStringLiteral("positionEffect")).toString());
    applyEventString(event, "underlying", "underlying", orderRecord.value(QStringLiteral("underlying")).toString());
    applyEventString(event, "option_type", "option_type", orderRecord.value(QStringLiteral("optionType")).toString());
    applyEventString(event, "expiry", "expiry", orderRecord.value(QStringLiteral("expiry")).toString());
    applyEventString(event, "batch_id", "batch_id", orderRecord.value(QStringLiteral("batchId")).toString());
    applyEventString(event, "batch_index", "batch_index", orderRecord.value(QStringLiteral("batchIndex")).toString());
    applyEventString(event, "execution_sequence", "execution_sequence", orderRecord.value(QStringLiteral("executionSequence")).toString());
    applyEventString(event, "batch_role", "batch_role", orderRecord.value(QStringLiteral("batchRole")).toString());
    applyEventString(event, "batch_phase", "batch_phase", orderRecord.value(QStringLiteral("batchPhase")).toString());
    applyEventString(event, "batch_order_count", "batch_order_count", orderRecord.value(QStringLiteral("batchOrderCount")).toString());
    applyEventString(event, "previous_batch_id", "previous_batch_id", orderRecord.value(QStringLiteral("previousBatchId")).toString());
    applyEventString(event, "previous_batch_order_count", "previous_batch_order_count", orderRecord.value(QStringLiteral("previousBatchOrderCount")).toString());
    applyEventString(event, "next_batch_id", "next_batch_id", orderRecord.value(QStringLiteral("nextBatchId")).toString());
    applyEventString(event, "execution_scope_id", "execution_scope_id", orderRecord.value(QStringLiteral("executionScopeId")).toString());
    applyEventString(event, "requires_previous_batch_filled", "requires_previous_batch_filled", orderRecord.value(QStringLiteral("requiresPreviousBatchFilled")).toString());
    applyEventString(event, "pause_on_conflict", "pause_on_conflict", orderRecord.value(QStringLiteral("pauseOnConflict")).toString());
    applyEventString(event, "pause_on_abnormal_reject", "pause_on_abnormal_reject", orderRecord.value(QStringLiteral("pauseOnAbnormalReject")).toString());
    applyEventString(event, "requires_manual_checkpoint", "requires_manual_checkpoint", orderRecord.value(QStringLiteral("requiresManualCheckpoint")).toString());
    applyEventString(event, "manual_checkpoint_batch_index", "manual_checkpoint_batch_index", orderRecord.value(QStringLiteral("manualCheckpointBatchIndex")).toString());
    applyEventString(event, "blocks_following_batches", "blocks_following_batches", orderRecord.value(QStringLiteral("blocksFollowingBatches")).toString());
    applyEventString(event, "risk_action_source", "risk_action_source", orderRecord.value(QStringLiteral("riskActionSource")).toString());
    applyEventString(event, "runtime_rule_decision", "runtime_rule_decision", orderRecord.value(QStringLiteral("runtimeRuleDecision")).toString());
    applyEventString(event, "runtime_rule_gate", "runtime_rule_gate", orderRecord.value(QStringLiteral("runtimeRuleGate")).toString());
    applyEventString(event, "runtime_rule_reason", "runtime_rule_reason", orderRecord.value(QStringLiteral("runtimeRuleReason")).toString());
    applyEventNumber(event, "cash_amount", "cash_amount", orderRecord.value(QStringLiteral("cashAmount")).toDouble());
}

QVariantMap buildRecentOrderRecordFromEvent(const engine::EventFormat& event)
{
    QVariantMap orderRecord;
    const QString statusOrigin = eventStringValue(event, "status_origin").trimmed().toLower();

    const QString clientOrderId = eventStringValue(event, "client_order_id");
    const QString brokerOrderId = eventStringValue(event, "broker_order_id");
    QString orderId = eventStringValue(event, "order_id");
    if (orderId.isEmpty()) {
        orderId = !clientOrderId.isEmpty() ? clientOrderId : brokerOrderId;
    }

    if (orderId.isEmpty() && clientOrderId.isEmpty() && brokerOrderId.isEmpty()) {
        return {};
    }

    orderRecord.insert(QStringLiteral("orderId"), orderId);
    if (!clientOrderId.isEmpty()) {
        orderRecord.insert(QStringLiteral("clientOrderId"), clientOrderId);
    }
    if (!brokerOrderId.isEmpty()) {
        orderRecord.insert(QStringLiteral("brokerOrderId"), brokerOrderId);
    }
    if (!statusOrigin.isEmpty()) {
        orderRecord.insert(QStringLiteral("statusOrigin"), statusOrigin);
    }

    const QString strategyId = eventStrategyId(event);
    if (!strategyId.isEmpty()) {
        orderRecord.insert(QStringLiteral("strategyId"), strategyId);
    }

    const QString gmStrategyId = eventStringValue(event, "runtime_strategy_id");
    if (!gmStrategyId.isEmpty()) {
        orderRecord.insert(QStringLiteral("runtimeStrategyId"), gmStrategyId);
    }

    const QString symbol = eventStringValue(event, "symbol").trimmed().toUpper();
    if (!symbol.isEmpty()) {
        orderRecord.insert(QStringLiteral("symbol"), symbol);
    }

    const QString exchange = eventStringValue(event, "exchange").trimmed().toUpper();
    if (!exchange.isEmpty()) {
        orderRecord.insert(QStringLiteral("exchange"), exchange);
    }

    const QString side = eventStringValue(event, "side").trimmed().toUpper();
    if (!side.isEmpty()) {
        orderRecord.insert(QStringLiteral("side"), side);
    }

    const QString orderType = eventStringValue(event, "order_type").trimmed().toUpper();
    if (!orderType.isEmpty()) {
        orderRecord.insert(QStringLiteral("orderType"), orderType);
    }

    const double price = eventDoubleValue(event, "price", 0.0);
    if (price > 0.0) {
        orderRecord.insert(QStringLiteral("price"), price);
    }

    const qint64 quantity = static_cast<qint64>(eventDoubleValue(event, "quantity", eventDoubleValue(event, "total_quantity", 0.0)));
    if (quantity > 0) {
        orderRecord.insert(QStringLiteral("quantity"), quantity);
    }

    const qint64 filledQuantity = static_cast<qint64>(eventDoubleValue(
        event,
        "filled_quantity",
        eventDoubleValue(event, "cumulative_filled_quantity", eventDoubleValue(event, "fill_quantity", 0.0))));
    if (filledQuantity > 0) {
        orderRecord.insert(QStringLiteral("filledQuantity"), filledQuantity);
    }

    const double filledNotional = eventDoubleValue(event, "filled_notional", 0.0);
    if (filledNotional > 0.0) {
        orderRecord.insert(QStringLiteral("filledNotional"), filledNotional);
    }

    const double cashAmount = eventDoubleValue(event, "cash_amount", 0.0);
    if (cashAmount > 0.0) {
        orderRecord.insert(QStringLiteral("cashAmount"), cashAmount);
    }

    const double requestedNotional = eventDoubleValue(event, "requested_notional", cashAmount);
    if (requestedNotional > 0.0) {
        orderRecord.insert(QStringLiteral("requestedNotional"), requestedNotional);
    }

    const QString status = order_runtime::resolveOrderStatusFromProgress(eventStringValue(event, "status"),
                                                                        quantity,
                                                                        filledQuantity,
                                                                        kRecentOrderStatusPolicy);
    if (!status.isEmpty()) {
        orderRecord.insert(QStringLiteral("status"), status);
    }

    const QString message = eventStringValue(event, "message");
    if (!message.isEmpty()) {
        orderRecord.insert(QStringLiteral("message"), message);
    }

    const QString createdAt = eventStringValue(event, "created_at");
    if (!createdAt.isEmpty()) {
        orderRecord.insert(QStringLiteral("createdAt"), createdAt);
    }

    const QString updatedAt = eventStringValue(event, "updated_at");
    if (!updatedAt.isEmpty()) {
        orderRecord.insert(QStringLiteral("updatedAt"), updatedAt);
    } else if (!createdAt.isEmpty()) {
        orderRecord.insert(QStringLiteral("updatedAt"), createdAt);
    }

    const QString type = eventStringValue(event, "type").trimmed().toLower();
    if (!type.isEmpty()) {
        orderRecord.insert(QStringLiteral("type"), type);
    }

    const QString action = eventStringValue(event, "action").trimmed();
    if (!action.isEmpty()) {
        orderRecord.insert(QStringLiteral("action"), action);
    }

    const QString riskActionSource = eventStringValue(event, "risk_action_source").trimmed();
    if (!riskActionSource.isEmpty()) {
        orderRecord.insert(QStringLiteral("riskActionSource"), riskActionSource);
    }

    const QString runtimeRuleDecision = eventStringValue(event, "runtime_rule_decision").trimmed();
    if (!runtimeRuleDecision.isEmpty()) {
        orderRecord.insert(QStringLiteral("runtimeRuleDecision"), runtimeRuleDecision);
    }

    const QString runtimeRuleGate = eventStringValue(event, "runtime_rule_gate").trimmed();
    if (!runtimeRuleGate.isEmpty()) {
        orderRecord.insert(QStringLiteral("runtimeRuleGate"), runtimeRuleGate);
    }

    const QString runtimeRuleReason = eventStringValue(event, "runtime_rule_reason").trimmed();
    if (!runtimeRuleReason.isEmpty()) {
        orderRecord.insert(QStringLiteral("runtimeRuleReason"), runtimeRuleReason);
    }

    const QString positionEffect = eventStringValue(event, "position_effect_text").trimmed().toUpper();
    if (!positionEffect.isEmpty()) {
        orderRecord.insert(QStringLiteral("positionEffect"), positionEffect);
    }

    const QString underlying = eventStringValue(event, "underlying").trimmed().toUpper();
    if (!underlying.isEmpty()) {
        orderRecord.insert(QStringLiteral("underlying"), underlying);
    }

    const QString optionType = eventStringValue(event, "option_type").trimmed().toLower();
    if (!optionType.isEmpty()) {
        orderRecord.insert(QStringLiteral("optionType"), optionType);
    }

    const QString expiry = eventStringValue(event, "expiry").trimmed();
    if (!expiry.isEmpty()) {
        orderRecord.insert(QStringLiteral("expiry"), expiry);
    }

    const QString batchId = eventStringValue(event, "batch_id").trimmed();
    if (!batchId.isEmpty()) {
        orderRecord.insert(QStringLiteral("batchId"), batchId);
    }

    const QString batchRole = eventStringValue(event, "batch_role").trimmed();
    if (!batchRole.isEmpty()) {
        orderRecord.insert(QStringLiteral("batchRole"), batchRole);
    }

    const QString batchPhase = eventStringValue(event, "batch_phase").trimmed();
    if (!batchPhase.isEmpty()) {
        orderRecord.insert(QStringLiteral("batchPhase"), batchPhase);
    }

    const QString previousBatchId = eventStringValue(event, "previous_batch_id").trimmed();
    if (!previousBatchId.isEmpty()) {
        orderRecord.insert(QStringLiteral("previousBatchId"), previousBatchId);
    }

    const QString nextBatchId = eventStringValue(event, "next_batch_id").trimmed();
    if (!nextBatchId.isEmpty()) {
        orderRecord.insert(QStringLiteral("nextBatchId"), nextBatchId);
    }

    const QString executionScopeId = eventStringValue(event, "execution_scope_id").trimmed();
    if (!executionScopeId.isEmpty()) {
        orderRecord.insert(QStringLiteral("executionScopeId"), executionScopeId);
    }

    const int batchIndex = eventStringValue(event, "batch_index").toInt();
    if (!eventStringValue(event, "batch_index").trimmed().isEmpty()) {
        orderRecord.insert(QStringLiteral("batchIndex"), batchIndex);
    }

    const int executionSequence = eventStringValue(event, "execution_sequence").toInt();
    if (!eventStringValue(event, "execution_sequence").trimmed().isEmpty()) {
        orderRecord.insert(QStringLiteral("executionSequence"), executionSequence);
    }

    const int batchOrderCount = eventStringValue(event, "batch_order_count").toInt();
    if (!eventStringValue(event, "batch_order_count").trimmed().isEmpty()) {
        orderRecord.insert(QStringLiteral("batchOrderCount"), batchOrderCount);
    }

    const int previousBatchOrderCount = eventStringValue(event, "previous_batch_order_count").toInt();
    if (!eventStringValue(event, "previous_batch_order_count").trimmed().isEmpty()) {
        orderRecord.insert(QStringLiteral("previousBatchOrderCount"), previousBatchOrderCount);
    }

    const QString requiresPreviousBatchFilled = eventStringValue(event, "requires_previous_batch_filled").trimmed();
    if (!requiresPreviousBatchFilled.isEmpty()) {
        orderRecord.insert(QStringLiteral("requiresPreviousBatchFilled"), boolishText(requiresPreviousBatchFilled));
    }

    const QString blocksFollowingBatches = eventStringValue(event, "blocks_following_batches").trimmed();
    if (!blocksFollowingBatches.isEmpty()) {
        orderRecord.insert(QStringLiteral("blocksFollowingBatches"), boolishText(blocksFollowingBatches));
    }

    const QString pauseOnConflict = eventStringValue(event, "pause_on_conflict").trimmed();
    if (!pauseOnConflict.isEmpty()) {
        orderRecord.insert(QStringLiteral("pauseOnConflict"), boolishText(pauseOnConflict));
    }

    const QString pauseOnAbnormalReject = eventStringValue(event, "pause_on_abnormal_reject").trimmed();
    if (!pauseOnAbnormalReject.isEmpty()) {
        orderRecord.insert(QStringLiteral("pauseOnAbnormalReject"), boolishText(pauseOnAbnormalReject));
    }

    const QString requiresManualCheckpoint = eventStringValue(event, "requires_manual_checkpoint").trimmed();
    if (!requiresManualCheckpoint.isEmpty()) {
        orderRecord.insert(QStringLiteral("requiresManualCheckpoint"), boolishText(requiresManualCheckpoint));
    }

    const QString manualCheckpointBatchIndex = eventStringValue(event, "manual_checkpoint_batch_index").trimmed();
    if (!manualCheckpointBatchIndex.isEmpty()) {
        orderRecord.insert(QStringLiteral("manualCheckpointBatchIndex"), manualCheckpointBatchIndex.toInt());
    }

    return orderRecord;
}

QString exchangeFromSymbol(const QString& symbol)
{
    const QString normalized = symbol.trimmed().toUpper();
    const int dot = normalized.lastIndexOf('.');
    if (dot >= 0 && dot + 1 < normalized.length()) {
        const QString suffix = normalized.mid(dot + 1);
        if (suffix == QStringLiteral("SH")) {
            return QStringLiteral("SHSE");
        }
        if (suffix == QStringLiteral("SZ")) {
            return QStringLiteral("SZSE");
        }
        if (suffix == QStringLiteral("BJ")) {
            return QStringLiteral("BSE");
        }
        return suffix;
    }
    if (normalized.startsWith(QStringLiteral("SHSE."))) {
        return QStringLiteral("SHSE");
    }
    if (normalized.startsWith(QStringLiteral("SZSE."))) {
        return QStringLiteral("SZSE");
    }
    if (normalized.startsWith(QStringLiteral("BSE."))) {
        return QStringLiteral("BSE");
    }
    if (normalized.startsWith(QStringLiteral("CFFEX."))) {
        return QStringLiteral("CFFEX");
    }
    if (normalized.startsWith(QStringLiteral("SHFE."))) {
        return QStringLiteral("SHFE");
    }
    if (normalized.startsWith(QStringLiteral("DCE."))) {
        return QStringLiteral("DCE");
    }
    if (normalized.startsWith(QStringLiteral("CZCE."))) {
        return QStringLiteral("CZCE");
    }
    if (normalized.startsWith(QStringLiteral("INE."))) {
        return QStringLiteral("INE");
    }
    if (normalized.startsWith(QStringLiteral("GFEX."))) {
        return QStringLiteral("GFEX");
    }
    return {};
}

} // namespace

TradeExecutionService* TradeExecutionService::m_instance = nullptr;
QMutex TradeExecutionService::m_instanceMutex;

TradeExecutionService* TradeExecutionService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new TradeExecutionService();
    }
    return m_instance;
}

TradeExecutionService::TradeExecutionService(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
    , m_eventBusIntegrated(false)
{
}

void TradeExecutionService::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        return;
    }

    initializeEventBusIntegration();
    m_initialized = true;
    emit initializedChanged();
}

void TradeExecutionService::initializeAsync()
{
    QPointer<TradeExecutionService> safeService(this);
    std::thread([safeService]() {
        if (safeService) {
            safeService->initialize();
        }
    }).detach();
}

bool TradeExecutionService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

QString TradeExecutionService::lastErrorMessage() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastErrorMessage;
}

void TradeExecutionService::updateLastErrorMessage(const QString& message)
{
    const QString normalized = message.trimmed();
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_lastErrorMessage == normalized) {
            return;
        }
        m_lastErrorMessage = normalized;
        changed = true;
    }

    if (changed) {
        emit lastErrorMessageChanged();
    }
}

bool TradeExecutionService::submitBridgeOrder(const QVariantMap& request)
{
    const QString normalizedSymbol = request.value(QStringLiteral("symbol")).toString().trimmed().toUpper();
    const QString normalizedSide = normalizeOrderSideText(request.value(QStringLiteral("side")).toString());
    const QString normalizedOrderType = normalizeManualOrderType(request.value(QStringLiteral("orderType")).toString());
    const QString normalizedMode = normalizeOrderMode(
        request.value(QStringLiteral("mode")).toString().trimmed().isEmpty()
            ? request.value(QStringLiteral("type")).toString()
            : request.value(QStringLiteral("mode")).toString());
    const QString normalizedAction = request.value(QStringLiteral("action")).toString().trimmed();
    const QString normalizedPositionEffect = normalizePositionEffectText(request.value(QStringLiteral("positionEffect")).toString());
    const double normalizedPrice = request.value(QStringLiteral("price")).toDouble();
    const bool riskBypassTradingHalt = request.value(QStringLiteral("riskBypassTradingHalt")).toBool();
    const bool isOptionExerciseAction = normalizedAction.compare(QStringLiteral("optionExercise"), Qt::CaseInsensitive) == 0
        || normalizedAction.compare(QStringLiteral("exercise"), Qt::CaseInsensitive) == 0;
    const bool isCashRepayBridgeAction = isCashRepayAction(normalizedAction);
    const double cashAmount = request.value(QStringLiteral("cashAmount")).toDouble();
    qint64 normalizedQuantity = request.value(QStringLiteral("quantity")).toLongLong();
    if (normalizedQuantity <= 0 && !isQuantityOptionalAction(normalizedAction)) {
        normalizedQuantity = isBoardLotOrderMode(normalizedMode) ? 100 : 1;
    }

    const bool invalidBoardLotQuantity = !isCashRepayBridgeAction
        && normalizedPositionEffect != QStringLiteral("CLOSE")
        && isBoardLotOrderMode(normalizedMode)
        && (normalizedQuantity < 100 || normalizedQuantity % 100 != 0);
    if (normalizedSymbol.isEmpty() ||
        normalizedSide.isEmpty() ||
        (!isOptionExerciseAction && !isPriceOptionalAction(normalizedAction) && normalizedPrice <= 0.0) ||
        (!isQuantityOptionalAction(normalizedAction) && normalizedQuantity <= 0) ||
        (isCashRepayBridgeAction && cashAmount <= 0.0) ||
        invalidBoardLotQuantity) {
        updateLastErrorMessage(QStringLiteral("无效的委托参数"));
        qWarning() << "TradeExecutionService: invalid bridge order"
                   << normalizedSymbol
                   << normalizedMode
                   << normalizedSide
                   << normalizedPrice
                   << normalizedQuantity;
        return false;
    }

    initialize();

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        updateLastErrorMessage(QStringLiteral("交易事件总线未就绪"));
        qWarning() << "TradeExecutionService: EventBus not ready for bridge order";
        return false;
    }

    QVariantMap orderContext;
    orderContext.insert(QStringLiteral("type"), normalizedMode);
    if (!normalizedAction.isEmpty()) {
        orderContext.insert(QStringLiteral("action"), normalizedAction);
    }
    if (!normalizedPositionEffect.isEmpty()) {
        orderContext.insert(QStringLiteral("positionEffect"), normalizedPositionEffect);
        orderContext.insert(QStringLiteral("position_effect"), positionEffectMetadataText(normalizedPositionEffect));
    }
    if (cashAmount > 0.0) {
        orderContext.insert(QStringLiteral("cashAmount"), cashAmount);
    }
    if (riskBypassTradingHalt) {
        orderContext.insert(QStringLiteral("riskBypassTradingHalt"), QStringLiteral("true"));
    }
    const QString riskActionSource = request.value(QStringLiteral("riskActionSource")).toString().trimmed();
    if (!riskActionSource.isEmpty()) {
        orderContext.insert(QStringLiteral("riskActionSource"), riskActionSource);
    }
    const QString runtimeRuleDecision = request.value(QStringLiteral("runtimeRuleDecision")).toString().trimmed();
    if (!runtimeRuleDecision.isEmpty()) {
        orderContext.insert(QStringLiteral("runtimeRuleDecision"), runtimeRuleDecision);
    }
    const QString runtimeRuleGate = request.value(QStringLiteral("runtimeRuleGate")).toString().trimmed();
    if (!runtimeRuleGate.isEmpty()) {
        orderContext.insert(QStringLiteral("runtimeRuleGate"), runtimeRuleGate);
    }
    const QString runtimeRuleReason = request.value(QStringLiteral("runtimeRuleReason")).toString().trimmed();
    if (!runtimeRuleReason.isEmpty()) {
        orderContext.insert(QStringLiteral("runtimeRuleReason"), runtimeRuleReason);
    }
    if (request.contains(QStringLiteral("riskBreakerStage"))) {
        orderContext.insert(QStringLiteral("riskBreakerStage"), request.value(QStringLiteral("riskBreakerStage")).toString());
    }

    const QString underlying = request.value(QStringLiteral("underlying")).toString().trimmed().toUpper();
    if (!underlying.isEmpty()) {
        orderContext.insert(QStringLiteral("underlying"), underlying);
    }
    const QString optionType = request.value(QStringLiteral("optionType")).toString().trimmed().toLower();
    if (!optionType.isEmpty()) {
        orderContext.insert(QStringLiteral("optionType"), optionType);
    }
    const QString expiry = request.value(QStringLiteral("expiry")).toString().trimmed();
    if (!expiry.isEmpty()) {
        orderContext.insert(QStringLiteral("expiry"), expiry);
    }
    const QString tradingDate = request.value(QStringLiteral("tradingDate")).toString().trimmed();
    if (!tradingDate.isEmpty()) {
        orderContext.insert(QStringLiteral("tradingDate"), tradingDate);
    }
    for (const QString& key : {QStringLiteral("batchId"),
                               QStringLiteral("batchIndex"),
                               QStringLiteral("executionSequence"),
                               QStringLiteral("batchRole"),
                               QStringLiteral("batchPhase"),
                               QStringLiteral("batchOrderCount"),
                               QStringLiteral("previousBatchId"),
                               QStringLiteral("previousBatchOrderCount"),
                               QStringLiteral("nextBatchId"),
                               QStringLiteral("executionScopeId"),
                               QStringLiteral("requiresPreviousBatchFilled"),
                               QStringLiteral("pauseOnConflict"),
                               QStringLiteral("pauseOnAbnormalReject"),
                               QStringLiteral("requiresManualCheckpoint"),
                               QStringLiteral("manualCheckpointBatchIndex"),
                               QStringLiteral("blocksFollowingBatches")}) {
        if (!request.contains(key)) {
            continue;
        }
        orderContext.insert(key, request.value(key).toString());
    }

    const QString requestedOrderId = request.value(QStringLiteral("orderId")).toString().trimmed();
    const QString requestedClientOrderId = request.value(QStringLiteral("clientOrderId")).toString().trimmed();
    const QString correlationId = !requestedClientOrderId.isEmpty()
        ? requestedClientOrderId
        : (!requestedOrderId.isEmpty()
            ? requestedOrderId
            : QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string()));
    const QVariantMap tradingConfiguration = loadTradingConnectionConfiguration();
    const QString strategyId = resolvedBusinessStrategyId(request, tradingConfiguration);
    const QString strategyName = resolvedBusinessStrategyName(request, tradingConfiguration, strategyId);
    const QString gmStrategyId = resolvedGmStrategyId(request, tradingConfiguration);

    const double signalStrength = request.value(QStringLiteral("strength")).toDouble() > 0.0
        ? request.value(QStringLiteral("strength")).toDouble()
        : 1.0;
    const QString requestOrderId = !requestedOrderId.isEmpty() ? requestedOrderId : correlationId;
    const QString requestClientOrderId = !requestedClientOrderId.isEmpty() ? requestedClientOrderId : correlationId;
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));

    QVariantMap orderRequest;
    orderRequest.insert(QStringLiteral("orderId"), requestOrderId);
    orderRequest.insert(QStringLiteral("clientOrderId"), requestClientOrderId);
    orderRequest.insert(QStringLiteral("strategyId"), strategyId);
    orderRequest.insert(QStringLiteral("strategyName"), strategyName);
    if (!gmStrategyId.isEmpty()) {
        orderRequest.insert(QStringLiteral("runtimeStrategyId"), gmStrategyId);
    }
    orderRequest.insert(QStringLiteral("symbol"), normalizedSymbol);
    orderRequest.insert(QStringLiteral("exchange"), exchangeFromSymbol(normalizedSymbol));
    orderRequest.insert(QStringLiteral("side"), normalizedSide);
    orderRequest.insert(QStringLiteral("price"), normalizedPrice);
    orderRequest.insert(QStringLiteral("quantity"), normalizedQuantity);
    orderRequest.insert(QStringLiteral("orderType"), normalizedOrderType);
    orderRequest.insert(QStringLiteral("requestedNotional"), requestedNotionalValue(normalizedPrice, normalizedQuantity, cashAmount));
    if (cashAmount > 0.0) {
        orderRequest.insert(QStringLiteral("cashAmount"), cashAmount);
    }
    orderRequest.insert(QStringLiteral("signalStrength"), signalStrength);
    orderRequest.insert(QStringLiteral("status"), QStringLiteral("REQUESTED"));
    orderRequest.insert(QStringLiteral("createdAt"), now);
    applyOrderContext(&orderRequest, orderContext);

    const std::optional<trading::execution::SchedulingRuleBlock> schedulingBlock =
        trading::execution::evaluateSchedulingRules(orderRequest, {
            {QStringLiteral("RetryOrPauseRule"), [this](const QVariantMap& candidate) {
                return schedulingRuleBlockFromMap(QStringLiteral("RetryOrPauseRule"),
                                                  findExecutionPauseBlock(candidate));
            }},
            {QStringLiteral("ManualCheckpointRule"), [this](const QVariantMap& candidate) {
                return schedulingRuleBlockFromMap(QStringLiteral("ManualCheckpointRule"),
                                                  findManualCheckpointBlock(candidate));
            }},
            {QStringLiteral("PartialFillAdvanceRule"), [this](const QVariantMap& candidate) {
                return schedulingRuleBlockFromMap(QStringLiteral("PartialFillAdvanceRule"),
                                                  findPartialFillAdvanceBlock(candidate));
            }},
            {QStringLiteral("PendingOrderConflictRule"), [this](const QVariantMap& candidate) {
                const QString symbol = candidate.value(QStringLiteral("symbol")).toString();
                const QString side = candidate.value(QStringLiteral("side")).toString();
                const QVariantMap conflictingOrder = findPendingOrderConflict(symbol, side);
                if (conflictingOrder.isEmpty()) {
                    return std::optional<trading::execution::SchedulingRuleBlock>{};
                }

                QVariantMap attributes;
                attributes.insert(QStringLiteral("conflictingSymbol"), symbol.trimmed().toUpper());

                const QString conflictingSide = normalizedOrderSideFromRecord(conflictingOrder);
                if (!conflictingSide.isEmpty()) {
                    attributes.insert(QStringLiteral("conflictingSide"), conflictingSide);
                }

                const QString conflictingStatus = normalizedConflictStatus(conflictingOrder);
                if (!conflictingStatus.isEmpty()) {
                    attributes.insert(QStringLiteral("conflictingStatus"), conflictingStatus);
                }

                const QString conflictingOrderId = preferredOrderIdentity(conflictingOrder);
                if (!conflictingOrderId.isEmpty()) {
                    attributes.insert(QStringLiteral("conflictingOrderId"), conflictingOrderId);
                }

                trading::execution::SchedulingRuleBlock block;
                block.ruleId = QStringLiteral("PendingOrderConflictRule");
                block.reasonCode = QStringLiteral("pending_conflicting_order");
                block.message = pendingOrderConflictMessage(symbol, side, conflictingOrder);
                block.attributes = attributes;
                return std::optional<trading::execution::SchedulingRuleBlock>{block};
            }}
        });

    if (schedulingBlock.has_value()) {
        publishOrderRequest(orderRequest, correlationId);

        const QVariantMap rejectStatus = trading::execution::buildSchedulingRejectStatus(
            orderRequest,
            *schedulingBlock,
            now);

        publishOrderStatus(rejectStatus, correlationId);
        updateLastErrorMessage(schedulingBlock->message);
        return false;
    }

    publishOrderRequest(orderRequest, correlationId);

    QVariantMap pendingRiskStatus = orderRequest;
    pendingRiskStatus.insert(QStringLiteral("status"), QStringLiteral("PENDING_RISK"));
    pendingRiskStatus.insert(QStringLiteral("message"), QStringLiteral("委托已进入风控审批"));
    pendingRiskStatus.insert(QStringLiteral("updatedAt"), now);
    pendingRiskStatus.insert(QStringLiteral("statusOrigin"), QStringLiteral("risk_pending"));
    publishOrderStatus(pendingRiskStatus, correlationId);

    QVariantMap riskSignalPayload;
    riskSignalPayload.insert(QStringLiteral("correlationId"), correlationId);
    riskSignalPayload.insert(QStringLiteral("orderId"), requestOrderId);
    riskSignalPayload.insert(QStringLiteral("clientOrderId"), requestOrderId);
    riskSignalPayload.insert(QStringLiteral("strategyId"), strategyId);
    riskSignalPayload.insert(QStringLiteral("businessStrategyId"), strategyId);
    riskSignalPayload.insert(QStringLiteral("strategyName"), strategyName);
    riskSignalPayload.insert(QStringLiteral("runtimeStrategyId"), gmStrategyId);
    riskSignalPayload.insert(QStringLiteral("symbol"), normalizedSymbol);
    riskSignalPayload.insert(QStringLiteral("side"), normalizedSide);
    riskSignalPayload.insert(QStringLiteral("action"), normalizedAction.isEmpty() ? normalizedSide : normalizedAction);
    riskSignalPayload.insert(QStringLiteral("price"), normalizedPrice);
    riskSignalPayload.insert(QStringLiteral("quantity"), normalizedQuantity);
    riskSignalPayload.insert(QStringLiteral("orderType"), normalizedOrderType);
    riskSignalPayload.insert(QStringLiteral("strength"), signalStrength);
    riskSignalPayload.insert(QStringLiteral("riskBypassTradingHalt"), riskBypassTradingHalt);
    if (!riskActionSource.isEmpty()) {
        riskSignalPayload.insert(QStringLiteral("riskActionSource"), riskActionSource);
    }
    if (request.contains(QStringLiteral("riskBreakerStage"))) {
        riskSignalPayload.insert(QStringLiteral("riskBreakerStage"), request.value(QStringLiteral("riskBreakerStage")));
    }
    if (!tradingDate.isEmpty()) {
        riskSignalPayload.insert(QStringLiteral("tradingDate"), tradingDate);
    }
    if (cashAmount > 0.0) {
        riskSignalPayload.insert(QStringLiteral("cashAmount"), cashAmount);
        riskSignalPayload.insert(QStringLiteral("requestedNotional"), cashAmount);
    }
    applyOrderContext(&riskSignalPayload, orderContext);

    RiskMonitorService* riskMonitorService = RiskMonitorService::instance();
    if (!riskMonitorService) {
        const QString errorMessage = QStringLiteral("风控服务未就绪");
        qWarning() << "TradeExecutionService: risk monitor service unavailable for manual order";
        updateLastErrorMessage(errorMessage);

        QVariantMap rejectStatus = orderRequest;
        rejectStatus.insert(QStringLiteral("status"), QStringLiteral("REJECTED"));
        rejectStatus.insert(QStringLiteral("message"), errorMessage);
        rejectStatus.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
        rejectStatus.insert(QStringLiteral("statusOrigin"), QStringLiteral("risk_reject"));
        publishOrderStatus(rejectStatus, correlationId);
        return false;
    }

    const QVariantMap riskDecision = riskMonitorService->reviewTradeSignal(riskSignalPayload, true);
    if (!riskDecision.value(QStringLiteral("approved")).toBool()) {
        const QString reason = riskDecision.value(QStringLiteral("reason")).toString().trimmed();
        updateLastErrorMessage(reason.isEmpty() ? QStringLiteral("风控拒绝委托") : reason);
        return false;
    }

    updateLastErrorMessage(QString());
    return true;
}

bool TradeExecutionService::submitManualTestOrder(const QString& symbol,
                                                  const QString& side,
                                                  double price,
                                                  qint64 quantity,
                                                  const QString& orderType,
                                                  const QString& strategyId,
                                                  const QString& strategyName)
{
    QVariantMap request;
    request.insert(QStringLiteral("symbol"), symbol);
    request.insert(QStringLiteral("side"), side);
    request.insert(QStringLiteral("price"), price);
    request.insert(QStringLiteral("quantity"), quantity);
    request.insert(QStringLiteral("orderType"), orderType);
    request.insert(QStringLiteral("mode"), QStringLiteral("stock"));
    request.insert(QStringLiteral("strategyId"), strategyId);
    request.insert(QStringLiteral("strategyName"), strategyName);
    return submitBridgeOrder(request);
}

bool TradeExecutionService::cancelManualTestOrder(const QString& orderId)
{
    const QString normalizedOrderId = orderId.trimmed();
    if (normalizedOrderId.isEmpty()) {
        updateLastErrorMessage(QStringLiteral("撤单请求缺少订单编号"));
        qWarning() << "TradeExecutionService: invalid cancel request with empty orderId";
        return false;
    }

    initialize();

    QVariantMap baseOrder;
    {
        QMutexLocker locker(&m_mutex);
        baseOrder = order_runtime::findOrderRecord(m_recentOrders, normalizedOrderId);
    }

    if (order_runtime::isClosedOrderStatus(baseOrder.value(QStringLiteral("status")).toString(), kRecentOrderStatusPolicy)) {
        updateLastErrorMessage(QStringLiteral("当前订单已结束，不能重复撤单"));
        qWarning() << "TradeExecutionService: order is already closed" << normalizedOrderId;
        return false;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        updateLastErrorMessage(QStringLiteral("交易事件总线未就绪"));
        qWarning() << "TradeExecutionService: EventBus not ready for cancel request";
        return false;
    }

    if (baseOrder.isEmpty()) {
        baseOrder.insert(QStringLiteral("orderId"), normalizedOrderId);
        baseOrder.insert(QStringLiteral("clientOrderId"), normalizedOrderId);
        baseOrder.insert(QStringLiteral("strategyId"), QStringLiteral("manual_test"));
        baseOrder.insert(QStringLiteral("strategyName"), QStringLiteral("Manual Test"));
    }

    if (baseOrder.value(QStringLiteral("createdAt")).toString().isEmpty()) {
        baseOrder.insert(QStringLiteral("createdAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    }

#if !defined(ASTOCK_ENABLE_JUJIN_MARKET)
    baseOrder.insert(QStringLiteral("status"), QStringLiteral("CANCELLED"));
    baseOrder.insert(QStringLiteral("message"), QStringLiteral("Local test order cancelled"));
    baseOrder.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    publishOrderStatus(baseOrder, normalizedOrderId);
    return true;
#else
    QString errorMessage;
    if (!ensureBrokerApiReady(&errorMessage)) {
        if (isPendingConfirmationOrderId(normalizedOrderId.toStdString())) {
            baseOrder.insert(QStringLiteral("status"), QStringLiteral("CANCELLED"));
            baseOrder.insert(QStringLiteral("message"), QStringLiteral("Queued test order cancelled locally"));
            baseOrder.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
            publishOrderStatus(baseOrder, normalizedOrderId);
            return true;
        }

        qWarning() << "TradeExecutionService:" << errorMessage;
        updateLastErrorMessage(errorMessage);
        return false;
    }

    const QString cancelOrderId = baseOrder.value(QStringLiteral("clientOrderId")).toString().trimmed().isEmpty()
        ? normalizedOrderId
        : baseOrder.value(QStringLiteral("clientOrderId")).toString().trimmed();

    if (!m_brokerApi->cancel_order(cancelOrderId.toStdString())) {
        updateLastErrorMessage(QString::fromStdString(m_brokerApi->last_error_message()));
        qWarning() << "TradeExecutionService: broker cancel request failed" << cancelOrderId
                   << QString::fromStdString(m_brokerApi->last_error_message());
        return false;
    }

    updateLastErrorMessage(QString());
    baseOrder.insert(QStringLiteral("status"), QStringLiteral("PENDING_CANCEL"));
    baseOrder.insert(QStringLiteral("message"), QStringLiteral("Cancel request submitted to broker runtime"));
    baseOrder.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    publishOrderStatus(baseOrder, normalizedOrderId);
    return true;
#endif
}

QVariantList TradeExecutionService::recentOrders() const
{
    QMutexLocker locker(&m_mutex);
    return m_recentOrders;
}

QVariantList TradeExecutionService::recentRuleHits() const
{
    QMutexLocker locker(&m_mutex);
    return m_recentRuleHits;
}

bool TradeExecutionService::isLiveBridgeReady()
{
#if !defined(ASTOCK_ENABLE_JUJIN_MARKET)
    return false;
#else
    return evaluateBrokerReadiness(nullptr, false);
#endif
}

QString TradeExecutionService::liveBridgeStatusMessage()
{
#if !defined(ASTOCK_ENABLE_JUJIN_MARKET)
    return QStringLiteral("当前构建未启用掘金交易桥接");
#else
    QString message;
    evaluateBrokerReadiness(&message, false);
    return message;
#endif
}

void TradeExecutionService::clearRecentOrders()
{
    bool recentOrdersStateChanged = false;
    bool recentRuleHitsStateChanged = false;
    {
        QMutexLocker locker(&m_mutex);
        recentOrdersStateChanged = !m_recentOrders.isEmpty();
        recentRuleHitsStateChanged = !m_recentRuleHits.isEmpty();
        m_recentOrders.clear();
        m_recentRuleHits.clear();
        m_pausedExecutionScopes.clear();
        m_approvedExecutionCheckpoints.clear();
    }

    if (recentOrdersStateChanged) {
        emit recentOrdersChanged();
    }
    if (recentRuleHitsStateChanged) {
        emit recentRuleHitsChanged();
    }
}

bool TradeExecutionService::approveExecutionCheckpoint(const QString& executionScopeId,
                                                      const QString& batchId)
{
    QVariantMap checkpointRecord;
    checkpointRecord.insert(QStringLiteral("executionScopeId"), executionScopeId);
    checkpointRecord.insert(QStringLiteral("batchId"), batchId);

    const QString checkpointKey = executionCheckpointKey(checkpointRecord);
    if (checkpointKey.isEmpty()) {
        updateLastErrorMessage(QStringLiteral("人工检查点确认缺少 executionScopeId 或 batchId"));
        return false;
    }

    {
        QMutexLocker locker(&m_mutex);
        m_approvedExecutionCheckpoints.insert(checkpointKey);
    }

    return true;
}

bool TradeExecutionService::resumeExecutionPause(const QString& executionScopeId,
                                                 const QString& pausedBatchId)
{
    QVariantMap pausedScopeRecord;
    pausedScopeRecord.insert(QStringLiteral("executionScopeId"), executionScopeId);

    const QString scopeKey = executionPauseScopeKey(pausedScopeRecord);
    if (scopeKey.isEmpty()) {
        updateLastErrorMessage(QStringLiteral("执行暂停恢复缺少 executionScopeId"));
        return false;
    }

    const QString expectedPausedBatchId = pausedBatchId.trimmed();
    {
        QMutexLocker locker(&m_mutex);
        auto pausedIt = m_pausedExecutionScopes.find(scopeKey);
        if (pausedIt == m_pausedExecutionScopes.end()) {
            updateLastErrorMessage(QStringLiteral("当前执行域没有待恢复的暂停批次"));
            return false;
        }

        const QString currentPausedBatchId = pausedIt->value(QStringLiteral("pausedBatchId")).toString().trimmed();
        if (!expectedPausedBatchId.isEmpty()
            && !currentPausedBatchId.isEmpty()
            && currentPausedBatchId != expectedPausedBatchId) {
            updateLastErrorMessage(QStringLiteral("当前执行域的暂停批次与恢复请求不一致"));
            return false;
        }

        m_pausedExecutionScopes.erase(pausedIt);
    }

    updateLastErrorMessage(QString());
    return true;
}

void TradeExecutionService::resetStateForTesting()
{
    bool initializationStateChanged = false;
    bool errorMessageChanged = false;
    bool recentOrdersStateChanged = false;
    bool recentRuleHitsStateChanged = false;
    engine::EventBus* bus = engine::get_engine_event_bus();

    {
        QMutexLocker locker(&m_mutex);
        initializationStateChanged = m_initialized;
        errorMessageChanged = !m_lastErrorMessage.isEmpty();
        recentOrdersStateChanged = !m_recentOrders.isEmpty();
        recentRuleHitsStateChanged = !m_recentRuleHits.isEmpty();

        if (bus && m_eventBusIntegrated) {
            if (m_orderUpdateSubscription) {
                bus->unsubscribe(m_orderUpdateSubscription);
            }
            if (m_tradeFillSubscription) {
                bus->unsubscribe(m_tradeFillSubscription);
            }
            if (m_riskApprovalSubscription) {
                bus->unsubscribe(m_riskApprovalSubscription);
            }
            if (m_riskRejectSubscription) {
                bus->unsubscribe(m_riskRejectSubscription);
            }
        }

        m_initialized = false;
        m_eventBusIntegrated = false;
        m_lastErrorMessage.clear();
        m_orderUpdateSubscription = foundation::utils::Uuid();
        m_tradeFillSubscription = foundation::utils::Uuid();
        m_riskApprovalSubscription = foundation::utils::Uuid();
        m_riskRejectSubscription = foundation::utils::Uuid();
        m_recentOrders.clear();
        m_recentRuleHits.clear();
        m_pausedExecutionScopes.clear();
        m_approvedExecutionCheckpoints.clear();
    }

    if (initializationStateChanged) {
        emit initializedChanged();
    }
    if (errorMessageChanged) {
        emit lastErrorMessageChanged();
    }
    if (recentOrdersStateChanged) {
        emit recentOrdersChanged();
    }
    if (recentRuleHitsStateChanged) {
        emit recentRuleHitsChanged();
    }
}

void TradeExecutionService::initializeEventBusIntegration()
{
    if (m_eventBusIntegrated) {
        return;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        qWarning() << "TradeExecutionService: EventBus not ready, skip event integration";
        return;
    }

    m_orderUpdateSubscription = bus->subscribe(engine::EventTypes::TRADING_ORDER_UPDATED,
        [this](const engine::EventFormat& event) {
            handleRuntimeOrderUpdate(event);
        });

    m_tradeFillSubscription = bus->subscribe(engine::EventTypes::ORDER_FILL,
        [this](const engine::EventFormat& event) {
            handleRuntimeTradeFill(event);
        });

    m_riskApprovalSubscription = bus->subscribe(engine::EventTypes::RISK_APPROVAL,
        [this](const engine::EventFormat& event) {
            handleRiskApproval(event);
        });

    m_riskRejectSubscription = bus->subscribe(engine::EventTypes::RISK_REJECT,
        [this](const engine::EventFormat& event) {
            handleRiskReject(event);
        });

    m_eventBusIntegrated = true;
    qDebug() << "TradeExecutionService: EventBus integration initialized";
}

void TradeExecutionService::handleRuntimeOrderUpdate(const engine::EventFormat& event)
{
    if (eventStringValue(event, "status_origin").trimmed().compare(QStringLiteral("local_request"), Qt::CaseInsensitive) == 0) {
        return;
    }

    const QVariantMap orderRecord = buildRecentOrderRecordFromEvent(event);
    if (orderRecord.isEmpty()) {
        return;
    }

    emit orderStatusPublished(orderRecord);
    appendRecentOrder(orderRecord);
}

void TradeExecutionService::handleRuntimeTradeFill(const engine::EventFormat& event)
{
    if (eventStringValue(event, "status_origin").trimmed().compare(QStringLiteral("local_request"), Qt::CaseInsensitive) == 0) {
        return;
    }

    const QVariantMap orderRecord = buildRecentOrderRecordFromEvent(event);
    if (orderRecord.isEmpty()) {
        return;
    }

    emit tradeFillPublished(orderRecord);
    appendRecentOrder(orderRecord);
}

void TradeExecutionService::handleRiskApproval(const engine::EventFormat& event)
{
    const QString strategyId = eventStrategyId(event);
    const QString strategyName = eventStringValue(event, "strategy_name");
    const QString symbol = eventStringValue(event, "symbol").trimmed().toUpper();
    const QString side = normalizeOrderSideText(
        eventStringValue(event, "side").trimmed().isEmpty()
            ? eventStringValue(event, "action")
            : eventStringValue(event, "side"));
    const QString action = eventStringValue(event, "action").trimmed();
    const double price = eventDoubleValue(event, "price", 0.0);
    const double cashAmount = eventDoubleValue(event, "cash_amount", 0.0);
    const double strength = eventDoubleValue(event, "strength", 0.0);
    qint64 quantity = static_cast<qint64>(eventDoubleValue(event, "quantity", eventDoubleValue(event, "total_quantity", 0.0)));
    const QString orderType = normalizeManualOrderType(eventStringValue(event, "order_type"));
    const QString marketEventType = eventStringValue(event, "market_event_type");
    const double targetWeightPercent = eventDoubleValue(event, "target_weight_percent", 0.0);
    const bool isOptionExerciseAction = action.compare(QStringLiteral("optionExercise"), Qt::CaseInsensitive) == 0
        || action.compare(QStringLiteral("exercise"), Qt::CaseInsensitive) == 0;
    const bool isCashRepayBridgeAction = isCashRepayAction(action);
    const bool isShareReturnBridgeAction = isShareReturnAction(action);
    const bool autoStrategyApproval = !marketEventType.isEmpty()
        && eventStringValue(event, "order_id").trimmed().isEmpty()
        && eventStringValue(event, "client_order_id").trimmed().isEmpty();

    RiskConfigService* riskConfigService = RiskConfigService::instance();
    QVariantMap riskConfiguration = riskConfigService->appliedConfiguration();
    if (riskConfiguration.isEmpty()) {
        riskConfiguration = riskConfigService->currentConfiguration();
    }

    const double orderSizeLimitWan = risk::config::orderSizeLimit(riskConfiguration, 100.0);
    if (!autoStrategyApproval
        && quantity <= 0
        && !isQuantityOptionalAction(action)
        && !isCashRepayBridgeAction
        && !isShareReturnBridgeAction) {
        quantity = deriveOrderQuantity(orderSizeLimitWan, price);
    }

    if (strategyId.isEmpty()
        || symbol.isEmpty()
        || side.isEmpty()
        || (!isOptionExerciseAction && !isPriceOptionalAction(action) && price <= 0.0)
        || (!isQuantityOptionalAction(action) && quantity <= 0)
        || (isCashRepayBridgeAction && cashAmount <= 0.0)) {
        qWarning() << "TradeExecutionService: skip invalid risk approval event";
        return;
    }

    if (!autoStrategyApproval && quantity <= 0 && !isCashRepayBridgeAction && !isShareReturnBridgeAction) {
        quantity = deriveOrderQuantity(orderSizeLimitWan, price);
    }
    const QString correlationId = !event.correlation_id.empty()
        ? QString::fromStdString(event.correlation_id)
        : QString::fromStdString(event.id);

    if (autoStrategyApproval && quantity <= 0 && !isCashRepayBridgeAction && !isShareReturnBridgeAction) {
        qWarning() << "TradeExecutionService: skip auto risk approval without executable quantity"
                   << "strategy=" << strategyId
                   << "symbol=" << symbol
                   << "side=" << side
                   << "price=" << price
                   << "marketEventType=" << marketEventType
                   << "targetWeightPercent=" << targetWeightPercent
                   << "correlationId=" << correlationId;
        return;
    }

    const QVariantMap tradingConfiguration = loadTradingConnectionConfiguration();
    const QSet<QString> allowedStrategyIds = configuredBoundStrategyIds(tradingConfiguration);
    if (!allowedStrategyIds.isEmpty() && !allowedStrategyIds.contains(strategyId)) {
        qWarning() << "TradeExecutionService: skip risk approval for unbound strategy"
                   << strategyId << "allowed=" << QStringList(allowedStrategyIds.begin(), allowedStrategyIds.end());
        return;
    }

    QString gmStrategyId = eventStringValue(event, "runtime_strategy_id");
    if (gmStrategyId.isEmpty()) {
        gmStrategyId = resolvedGmStrategyId(QVariantMap{}, tradingConfiguration);
    }

    QVariantMap orderContext;
    const QString mode = eventStringValue(event, "type").trimmed().toLower();
    if (!mode.isEmpty()) {
        orderContext.insert(QStringLiteral("type"), mode);
    }
    if (!action.isEmpty()) {
        orderContext.insert(QStringLiteral("action"), action);
    }
    const QString positionEffect = normalizePositionEffectText(
        eventStringValue(event, "position_effect_text").trimmed().isEmpty()
            ? eventStringValue(event, "position_effect")
            : eventStringValue(event, "position_effect_text"));
    if (!positionEffect.isEmpty()) {
        orderContext.insert(QStringLiteral("positionEffect"), positionEffect);
        orderContext.insert(QStringLiteral("position_effect"), positionEffectMetadataText(positionEffect));
    }
    const QString underlying = eventStringValue(event, "underlying").trimmed().toUpper();
    if (!underlying.isEmpty()) {
        orderContext.insert(QStringLiteral("underlying"), underlying);
    }
    const QString optionType = eventStringValue(event, "option_type").trimmed().toLower();
    if (!optionType.isEmpty()) {
        orderContext.insert(QStringLiteral("optionType"), optionType);
    }
    const QString expiry = eventStringValue(event, "expiry").trimmed();
    if (!expiry.isEmpty()) {
        orderContext.insert(QStringLiteral("expiry"), expiry);
    }
    if (cashAmount > 0.0) {
        orderContext.insert(QStringLiteral("cashAmount"), cashAmount);
    }

    qInfo() << "TradeExecutionService: handling risk approval"
            << "strategy=" << strategyId
            << "symbol=" << symbol
            << "side=" << side
            << "price=" << price
            << "quantity=" << quantity
            << "cashAmount=" << cashAmount
            << "orderType=" << orderType
            << "marketEventType=" << marketEventType
            << "targetWeightPercent=" << targetWeightPercent
            << "correlationId=" << correlationId;

    submitBrokerOrder(strategyId,
                      strategyName,
                      gmStrategyId,
                      symbol,
                      side,
                      orderType,
                      price,
                      quantity,
                      correlationId,
                      strength,
                      orderContext);
}

void TradeExecutionService::handleRiskReject(const engine::EventFormat& event)
{
    const QString correlationId = !event.correlation_id.empty()
        ? QString::fromStdString(event.correlation_id)
        : QString::fromStdString(event.id);
    const QString reason = eventStringValue(event, "reason").trimmed().isEmpty()
        ? QStringLiteral("风控拒绝委托")
        : eventStringValue(event, "reason").trimmed();

    QVariantMap orderStatus = buildRecentOrderRecordFromEvent(event);
    if (orderStatus.isEmpty()) {
        orderStatus.insert(QStringLiteral("orderId"), correlationId);
        orderStatus.insert(QStringLiteral("clientOrderId"), correlationId);
    }

    if (orderStatus.value(QStringLiteral("strategyId")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("strategyId"), eventStrategyId(event));
    }
    if (orderStatus.value(QStringLiteral("strategyName")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("strategyName"), eventStringValue(event, "strategy_name"));
    }
    if (orderStatus.value(QStringLiteral("runtimeStrategyId")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("runtimeStrategyId"), eventStringValue(event, "runtime_strategy_id"));
    }
    if (orderStatus.value(QStringLiteral("symbol")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("symbol"), eventStringValue(event, "symbol").trimmed().toUpper());
    }
    if (orderStatus.value(QStringLiteral("side")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("side"), normalizeOrderSideText(
            eventStringValue(event, "side").trimmed().isEmpty()
                ? eventStringValue(event, "action")
                : eventStringValue(event, "side")));
    }
    if (!orderStatus.contains(QStringLiteral("price"))) {
        orderStatus.insert(QStringLiteral("price"), eventDoubleValue(event, "price", 0.0));
    }
    if (!orderStatus.contains(QStringLiteral("quantity"))) {
        orderStatus.insert(QStringLiteral("quantity"), static_cast<qint64>(eventDoubleValue(event, "quantity", 0.0)));
    }
    const double cashAmount = eventDoubleValue(event, "cash_amount", 0.0);
    if (cashAmount > 0.0) {
        orderStatus.insert(QStringLiteral("cashAmount"), cashAmount);
        orderStatus.insert(QStringLiteral("requestedNotional"), cashAmount);
    }
    if (orderStatus.value(QStringLiteral("orderType")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("orderType"), normalizeManualOrderType(eventStringValue(event, "order_type")));
    }
    if (orderStatus.value(QStringLiteral("createdAt")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("createdAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    }

    orderStatus.insert(QStringLiteral("status"), QStringLiteral("REJECTED"));
    orderStatus.insert(QStringLiteral("message"), reason);
    orderStatus.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    orderStatus.insert(QStringLiteral("statusOrigin"), QStringLiteral("risk_reject"));

    const QString mode = eventStringValue(event, "type").trimmed().toLower();
    if (!mode.isEmpty()) {
        orderStatus.insert(QStringLiteral("type"), mode);
    }
    const QString action = eventStringValue(event, "action").trimmed();
    if (!action.isEmpty()) {
        orderStatus.insert(QStringLiteral("action"), action);
    }
    const QString positionEffect = normalizePositionEffectText(
        eventStringValue(event, "position_effect_text").trimmed().isEmpty()
            ? eventStringValue(event, "position_effect")
            : eventStringValue(event, "position_effect_text"));
    if (!positionEffect.isEmpty()) {
        orderStatus.insert(QStringLiteral("positionEffect"), positionEffect);
        orderStatus.insert(QStringLiteral("position_effect"), positionEffectMetadataText(positionEffect));
    }
    const QString underlying = eventStringValue(event, "underlying").trimmed().toUpper();
    if (!underlying.isEmpty()) {
        orderStatus.insert(QStringLiteral("underlying"), underlying);
    }
    const QString optionType = eventStringValue(event, "option_type").trimmed().toLower();
    if (!optionType.isEmpty()) {
        orderStatus.insert(QStringLiteral("optionType"), optionType);
    }
    const QString expiry = eventStringValue(event, "expiry").trimmed();
    if (!expiry.isEmpty()) {
        orderStatus.insert(QStringLiteral("expiry"), expiry);
    }

    publishOrderStatus(orderStatus, correlationId);
    updateLastErrorMessage(reason);
}

bool TradeExecutionService::submitBrokerOrder(const QString& strategyId,
                                              const QString& strategyName,
                                              const QString& gmStrategyId,
                                              const QString& symbol,
                                              const QString& side,
                                              const QString& orderType,
                                              double price,
                                              qint64 quantity,
                                              const QString& correlationId,
                                              double strength,
                                              const QVariantMap& orderContext,
                                              const std::map<std::string, std::string>& runtimeMetadata)
{
    Q_UNUSED(strength);

    const QString action = orderContext.value(QStringLiteral("action")).toString().trimmed();
    const bool isOptionExerciseAction = action.compare(QStringLiteral("optionExercise"), Qt::CaseInsensitive) == 0
        || action.compare(QStringLiteral("exercise"), Qt::CaseInsensitive) == 0;
    const bool isCashRepayBridgeAction = isCashRepayAction(action);
    const double cashAmount = orderContext.value(QStringLiteral("cashAmount")).toDouble();

    if (strategyId.isEmpty()
        || symbol.isEmpty()
        || side.isEmpty()
        || (!isOptionExerciseAction && !isPriceOptionalAction(action) && price <= 0.0)
        || (!isQuantityOptionalAction(action) && quantity <= 0)
        || (isCashRepayBridgeAction && cashAmount <= 0.0)) {
        qWarning() << "TradeExecutionService: skip invalid broker order";
        return false;
    }

#if !defined(ASTOCK_ENABLE_JUJIN_MARKET)
    qWarning() << "TradeExecutionService: real trading is unavailable because JUJIN support is not compiled";
    return submitLocalPendingOrder(strategyId,
                                   strategyName,
                                   gmStrategyId,
                                   symbol,
                                   side,
                                   orderType,
                                   price,
                                   quantity,
                                   correlationId,
                                   QStringLiteral("JUJIN support unavailable, queued as local pending order"),
                                   orderContext);
#else
    QString errorMessage;
    if (!ensureBrokerApiReady(&errorMessage)) {
        qWarning() << "TradeExecutionService:" << errorMessage;
        updateLastErrorMessage(errorMessage);
        return submitLocalPendingOrder(strategyId,
                                       strategyName,
                                       gmStrategyId,
                                       symbol,
                                       side,
                                       orderType,
                                       price,
                                       quantity,
                                       correlationId,
                                       errorMessage.isEmpty()
                                           ? QStringLiteral("Broker unavailable, queued as local pending order")
                                           : QStringLiteral("%1，已回退为本地待处理委托").arg(errorMessage),
                                       orderContext);
    }

    const auto brokerSide = side.trimmed().toUpper() == QStringLiteral("SELL")
        ? thirdparty::OrderSide::SELL
        : thirdparty::OrderSide::BUY;
    const QString normalizedOrderType = normalizeManualOrderType(orderType);
    const auto brokerOrderType = normalizedOrderType == QStringLiteral("MARKET")
        ? thirdparty::OrderType::MARKET
        : thirdparty::OrderType::LIMIT;

    const QString requestOrderId = correlationId.isEmpty()
        ? QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string())
        : correlationId;

    QVariantMap orderRequest;
    orderRequest.insert("orderId", requestOrderId);
    orderRequest.insert("clientOrderId", requestOrderId);
    orderRequest.insert("strategyId", strategyId);
    orderRequest.insert("strategyName", strategyName);
    if (!gmStrategyId.isEmpty()) {
        orderRequest.insert("runtimeStrategyId", gmStrategyId);
    }
    orderRequest.insert("symbol", symbol.trimmed().toUpper());
    orderRequest.insert("exchange", exchangeFromSymbol(symbol));
    orderRequest.insert("side", side.trimmed().toUpper());
    orderRequest.insert("price", price);
    orderRequest.insert("quantity", quantity);
    orderRequest.insert("orderType", normalizedOrderType);
    orderRequest.insert("requestedNotional", requestedNotionalValue(price, quantity, cashAmount));
    if (cashAmount > 0.0) {
        orderRequest.insert("cashAmount", cashAmount);
    }
    orderRequest.insert("status", QStringLiteral("REQUESTED"));
    orderRequest.insert("createdAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    applyOrderContext(&orderRequest, orderContext);

    publishOrderRequest(orderRequest, correlationId);

    std::map<std::string, std::string> brokerMetadata = runtimeMetadata;
    for (auto it = orderContext.constBegin(); it != orderContext.constEnd(); ++it) {
        const QString textValue = it.value().toString().trimmed();
        if (!textValue.isEmpty()) {
            brokerMetadata[it.key().toStdString()] = textValue.toStdString();
        }
    }
    brokerMetadata["business_strategy_id"] = strategyId.toStdString();
    if (!gmStrategyId.isEmpty()) {
        brokerMetadata["runtime_strategy_id"] = gmStrategyId.toStdString();
    }

    qInfo() << "TradeExecutionService: submit broker order"
            << "requestOrderId=" << requestOrderId
            << "strategy=" << strategyId
            << "runtimeStrategyId=" << gmStrategyId
            << "symbol=" << symbol
            << "side=" << side
            << "orderType=" << normalizedOrderType
            << "price=" << price
            << "quantity=" << quantity
            << "requestedNotional=" << orderRequest.value(QStringLiteral("requestedNotional")).toDouble()
            << "action=" << orderContext.value(QStringLiteral("action")).toString();

    const std::string orderId = m_brokerApi->place_order(
        symbol.trimmed().toStdString(),
        brokerSide,
        brokerOrderType,
        price,
        static_cast<double>(quantity),
        requestOrderId.toStdString(),
        brokerMetadata);

    QVariantMap orderStatus = orderRequest;
    orderStatus.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));

    if (orderId.empty()) {
        const QString brokerError = QString::fromStdString(m_brokerApi->last_error_message());
        if (isDeferredBrokerSubmissionError(brokerError)) {
            updateLastErrorMessage(QString());
            orderStatus.insert("status", QStringLiteral("PENDING"));
            orderStatus.insert("message", QStringLiteral("Order accepted by runtime queue"));
            publishOrderStatus(orderStatus, correlationId);
            return true;
        }

        qWarning() << "TradeExecutionService: broker place_order failed"
                   << "requestOrderId=" << requestOrderId
                   << "symbol=" << symbol
                   << "side=" << side
                   << "orderType=" << normalizedOrderType
                   << "price=" << price
                   << "quantity=" << quantity
                   << "error=" << (brokerError.isEmpty()
                       ? QStringLiteral("Broker rejected order request")
                       : brokerError);

        orderStatus.insert("status", QStringLiteral("REJECTED"));
        orderStatus.insert("message", brokerError.isEmpty()
            ? QStringLiteral("Broker rejected order request")
            : brokerError);
        orderStatus.insert("statusOrigin", QStringLiteral("broker_reject"));
        updateLastErrorMessage(orderStatus.value("message").toString());
        publishOrderStatus(orderStatus, correlationId);
        return false;
    }

    orderRequest.insert("brokerOrderId", QString::fromStdString(orderId));
    orderStatus = orderRequest;
    orderStatus.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));

    if (isPendingConfirmationOrderId(orderId)) {
        orderStatus.insert("status", QStringLiteral("PENDING"));
        orderStatus.insert("message", QStringLiteral("Order queued in runtime session"));
    } else {
        orderStatus.insert("status", QStringLiteral("SUBMITTED"));
        orderStatus.insert("message", QStringLiteral("Order submitted to broker runtime"));
    }
    orderStatus.insert("statusOrigin", QStringLiteral("broker_submit"));

    updateLastErrorMessage(QString());
    publishOrderStatus(orderStatus, correlationId);
    return true;
#endif
}

bool TradeExecutionService::submitLocalPendingOrder(const QString& strategyId,
                                                    const QString& strategyName,
                                                    const QString& gmStrategyId,
                                                    const QString& symbol,
                                                    const QString& side,
                                                    const QString& orderType,
                                                    double price,
                                                    qint64 quantity,
                                                    const QString& correlationId,
                                                    const QString& message,
                                                    const QVariantMap& orderContext)
{
    const QString action = orderContext.value(QStringLiteral("action")).toString().trimmed();
    const bool isOptionExerciseAction = action.compare(QStringLiteral("optionExercise"), Qt::CaseInsensitive) == 0
        || action.compare(QStringLiteral("exercise"), Qt::CaseInsensitive) == 0;
    const double cashAmount = orderContext.value(QStringLiteral("cashAmount")).toDouble();
    if (strategyId.isEmpty()
        || symbol.isEmpty()
        || side.isEmpty()
        || (!isOptionExerciseAction && !isPriceOptionalAction(action) && price <= 0.0)
        || (!isQuantityOptionalAction(action) && quantity <= 0)
        || (isCashRepayAction(action) && cashAmount <= 0.0)) {
        qWarning() << "TradeExecutionService: skip invalid local pending order";
        return false;
    }

    const QString resolvedCorrelationId = correlationId.isEmpty()
        ? QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string())
        : correlationId;

    QVariantMap orderRequest;
    orderRequest.insert("orderId", resolvedCorrelationId);
    orderRequest.insert("clientOrderId", resolvedCorrelationId);
    orderRequest.insert("strategyId", strategyId);
    orderRequest.insert("strategyName", strategyName);
    if (!gmStrategyId.isEmpty()) {
        orderRequest.insert("runtimeStrategyId", gmStrategyId);
    }
    orderRequest.insert("symbol", symbol.trimmed().toUpper());
    orderRequest.insert("exchange", exchangeFromSymbol(symbol));
    orderRequest.insert("side", side.trimmed().toUpper());
    orderRequest.insert("price", price);
    orderRequest.insert("quantity", quantity);
    orderRequest.insert("orderType", normalizeManualOrderType(orderType));
    orderRequest.insert("requestedNotional", requestedNotionalValue(price, quantity, cashAmount));
    if (cashAmount > 0.0) {
        orderRequest.insert("cashAmount", cashAmount);
    }
    orderRequest.insert("status", QStringLiteral("REQUESTED"));
    orderRequest.insert("createdAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    applyOrderContext(&orderRequest, orderContext);

    publishOrderRequest(orderRequest, resolvedCorrelationId);

    QVariantMap orderStatus = orderRequest;
    orderStatus.insert("status", QStringLiteral("SUBMITTED"));
    orderStatus.insert("message", message.isEmpty()
        ? QStringLiteral("Local pending order created")
        : message);
    orderStatus.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    orderStatus.insert("statusOrigin", QStringLiteral("local_pending"));
    publishOrderStatus(orderStatus, resolvedCorrelationId);
    return true;
}

bool TradeExecutionService::submitSimulatedOrder(const QString& strategyId,
                                                 const QString& strategyName,
                                                 const QString& gmStrategyId,
                                                 const QString& symbol,
                                                 const QString& side,
                                                 double price,
                                                 qint64 quantity,
                                                 const QString& correlationId,
                                                 double strength)
{
    if (strategyId.isEmpty() || symbol.isEmpty() || side.isEmpty() || price <= 0.0 || quantity <= 0) {
        qWarning() << "TradeExecutionService: skip invalid simulated order";
        return false;
    }

    RiskConfigService* riskConfigService = RiskConfigService::instance();
    QVariantMap riskConfiguration = riskConfigService->appliedConfiguration();
    if (riskConfiguration.isEmpty()) {
        riskConfiguration = riskConfigService->currentConfiguration();
    }

    const double orderSizeLimitWan = risk::config::orderSizeLimit(riskConfiguration, 100.0);
    const double maxPositionPercent = risk::config::maxPositionPercent(riskConfiguration, 15.0);
    const QString orderId = QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string());

    QVariantMap orderRequest;
    orderRequest.insert("orderId", orderId);
    orderRequest.insert("clientOrderId", orderId);
    orderRequest.insert("strategyId", strategyId);
    orderRequest.insert("strategyName", strategyName);
    if (!gmStrategyId.isEmpty()) {
        orderRequest.insert("runtimeStrategyId", gmStrategyId);
    }
    orderRequest.insert("symbol", symbol.trimmed().toUpper());
    orderRequest.insert("exchange", exchangeFromSymbol(symbol));
    orderRequest.insert("side", side.trimmed().toUpper());
    orderRequest.insert("price", price);
    orderRequest.insert("quantity", quantity);
    orderRequest.insert("orderType", QStringLiteral("LIMIT"));
    orderRequest.insert("requestedNotional", price * static_cast<double>(quantity));
    orderRequest.insert("orderSizeLimitWan", orderSizeLimitWan);
    risk::config::setMaxPositionPercent(orderRequest, maxPositionPercent);
    orderRequest.insert("signalStrength", strength);
    orderRequest.insert("status", QStringLiteral("REQUESTED"));
    orderRequest.insert("createdAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));

    publishOrderRequest(orderRequest, correlationId);

    QVariantMap orderStatus = orderRequest;
    orderStatus.insert("status", QStringLiteral("SUBMITTED"));
    orderStatus.insert("message", QStringLiteral("Simulated order request accepted"));
    orderStatus.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    publishOrderStatus(orderStatus, correlationId);

    QVariantMap tradeFill = orderStatus;
    tradeFill.insert("fillId", QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string()));
    tradeFill.insert("fillPrice", price);
    tradeFill.insert("fillQuantity", quantity);
    tradeFill.insert("filledNotional", price * static_cast<double>(quantity));
    tradeFill.insert("status", QStringLiteral("FILLED"));
    tradeFill.insert("message", QStringLiteral("Simulated trade fill completed"));
    tradeFill.insert("filledAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    publishTradeFill(tradeFill, correlationId);

    return true;
}

void TradeExecutionService::publishOrderRequest(const QVariantMap& orderRequest, const QString& correlationId)
{
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        engine::EventTypes::TRADING_ORDER_SUBMIT_REQUEST,
        "TRADE_EXECUTION_SERVICE",
        0);
    event.correlation_id = correlationId.toStdString();
    event.set("order_id", orderRequest.value("orderId").toString().toStdString());
    event.set("business_strategy_id", orderRequest.value("strategyId").toString().toStdString());
    event.set("strategy_name", orderRequest.value("strategyName").toString().toStdString());
    event.set("symbol", orderRequest.value("symbol").toString().toStdString());
    event.set("side", orderRequest.value("side").toString().toStdString());
    event.set("price", orderRequest.value("price").toDouble());
    event.set("quantity", static_cast<int64_t>(orderRequest.value("quantity").toLongLong()));
    event.set("order_type", orderRequest.value("orderType").toString().toStdString());
    event.metadata["order_id"] = orderRequest.value("orderId").toString().toStdString();
    event.metadata["business_strategy_id"] = orderRequest.value("strategyId").toString().toStdString();
    if (!orderRequest.value("runtimeStrategyId").toString().trimmed().isEmpty()) {
        event.set("runtime_strategy_id", orderRequest.value("runtimeStrategyId").toString().toStdString());
        event.metadata["runtime_strategy_id"] = orderRequest.value("runtimeStrategyId").toString().toStdString();
    }
    event.metadata["symbol"] = orderRequest.value("symbol").toString().toStdString();
    event.metadata["side"] = orderRequest.value("side").toString().toStdString();
    event.metadata["order_type"] = orderRequest.value("orderType").toString().toStdString();
    event.metadata["status"] = orderRequest.value("status").toString().toStdString();
    event.metadata["event_contract"] = "canonical";
    applyOrderContextToEvent(&event, orderRequest);

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    if (!result) {
        qWarning() << "TradeExecutionService: failed to publish order request" << QString::fromStdString(result.message);
        return;
    }

    emit orderRequestPublished(orderRequest);
    appendRecentOrder(orderRequest);
}

void TradeExecutionService::publishOrderStatus(const QVariantMap& orderStatus, const QString& correlationId)
{
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

    const QString statusOrigin = orderStatus.value(QStringLiteral("statusOrigin")).toString().trimmed().isEmpty()
        ? QStringLiteral("local_request")
        : orderStatus.value(QStringLiteral("statusOrigin")).toString().trimmed();

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        engine::EventTypes::TRADING_ORDER_UPDATED,
        "TRADE_EXECUTION_SERVICE",
        0);
    event.correlation_id = correlationId.toStdString();
    event.set("order_id", orderStatus.value("orderId").toString().toStdString());
    event.set("business_strategy_id", orderStatus.value("strategyId").toString().toStdString());
    event.set("symbol", orderStatus.value("symbol").toString().toStdString());
    event.set("side", orderStatus.value("side").toString().toStdString());
    event.set("price", orderStatus.value("price").toDouble());
    event.set("quantity", static_cast<int64_t>(orderStatus.value("quantity").toLongLong()));
    event.set("filled_quantity", static_cast<int64_t>(orderStatus.value("filledQuantity").toLongLong()));
    event.set("filled_notional", orderStatus.value("filledNotional").toDouble());
    event.set("order_type", orderStatus.value("orderType").toString().toStdString());
    event.set("status", orderStatus.value("status").toString().toStdString());
    event.set("message", orderStatus.value("message").toString().toStdString());
    event.set("created_at", orderStatus.value("createdAt").toString().toStdString());
    event.set("updated_at", orderStatus.value("updatedAt").toString().toStdString());
    event.metadata["order_id"] = orderStatus.value("orderId").toString().toStdString();
    event.metadata["business_strategy_id"] = orderStatus.value("strategyId").toString().toStdString();
    if (!orderStatus.value("runtimeStrategyId").toString().trimmed().isEmpty()) {
        event.set("runtime_strategy_id", orderStatus.value("runtimeStrategyId").toString().toStdString());
        event.metadata["runtime_strategy_id"] = orderStatus.value("runtimeStrategyId").toString().toStdString();
    }
    event.metadata["symbol"] = orderStatus.value("symbol").toString().toStdString();
    event.metadata["side"] = orderStatus.value("side").toString().toStdString();
    event.metadata["order_type"] = orderStatus.value("orderType").toString().toStdString();
    event.metadata["status"] = orderStatus.value("status").toString().toStdString();
    event.metadata["message"] = orderStatus.value("message").toString().toStdString();
    event.metadata["status_origin"] = statusOrigin.toStdString();
    event.metadata["event_contract"] = "canonical";
    if (!orderStatus.value(QStringLiteral("ruleId")).toString().trimmed().isEmpty()) {
        event.set("rule_id", orderStatus.value(QStringLiteral("ruleId")).toString().toStdString());
        event.metadata["rule_id"] = orderStatus.value(QStringLiteral("ruleId")).toString().toStdString();
    }
    if (!orderStatus.value(QStringLiteral("reasonCode")).toString().trimmed().isEmpty()) {
        event.set("reason_code", orderStatus.value(QStringLiteral("reasonCode")).toString().toStdString());
        event.metadata["reason_code"] = orderStatus.value(QStringLiteral("reasonCode")).toString().toStdString();
    }
    if (!orderStatus.value(QStringLiteral("conflictingOrderId")).toString().trimmed().isEmpty()) {
        event.set("conflicting_order_id", orderStatus.value(QStringLiteral("conflictingOrderId")).toString().toStdString());
        event.metadata["conflicting_order_id"] = orderStatus.value(QStringLiteral("conflictingOrderId")).toString().toStdString();
    }
    if (!orderStatus.value(QStringLiteral("conflictingSide")).toString().trimmed().isEmpty()) {
        event.set("conflicting_side", orderStatus.value(QStringLiteral("conflictingSide")).toString().toStdString());
        event.metadata["conflicting_side"] = orderStatus.value(QStringLiteral("conflictingSide")).toString().toStdString();
    }
    if (!orderStatus.value(QStringLiteral("conflictingStatus")).toString().trimmed().isEmpty()) {
        event.set("conflicting_status", orderStatus.value(QStringLiteral("conflictingStatus")).toString().toStdString());
        event.metadata["conflicting_status"] = orderStatus.value(QStringLiteral("conflictingStatus")).toString().toStdString();
    }
    if (!orderStatus.value(QStringLiteral("requiredBatchId")).toString().trimmed().isEmpty()) {
        event.set("required_batch_id", orderStatus.value(QStringLiteral("requiredBatchId")).toString().toStdString());
        event.metadata["required_batch_id"] = orderStatus.value(QStringLiteral("requiredBatchId")).toString().toStdString();
    }
    if (!orderStatus.value(QStringLiteral("blockingBatchId")).toString().trimmed().isEmpty()) {
        event.set("blocking_batch_id", orderStatus.value(QStringLiteral("blockingBatchId")).toString().toStdString());
        event.metadata["blocking_batch_id"] = orderStatus.value(QStringLiteral("blockingBatchId")).toString().toStdString();
    }
    if (!orderStatus.value(QStringLiteral("blockingOrderId")).toString().trimmed().isEmpty()) {
        event.set("blocking_order_id", orderStatus.value(QStringLiteral("blockingOrderId")).toString().toStdString());
        event.metadata["blocking_order_id"] = orderStatus.value(QStringLiteral("blockingOrderId")).toString().toStdString();
    }
    if (!orderStatus.value(QStringLiteral("blockingStatus")).toString().trimmed().isEmpty()) {
        event.set("blocking_status", orderStatus.value(QStringLiteral("blockingStatus")).toString().toStdString());
        event.metadata["blocking_status"] = orderStatus.value(QStringLiteral("blockingStatus")).toString().toStdString();
    }
    if (orderStatus.contains(QStringLiteral("observedBatchOrderCount"))) {
        event.set("observed_batch_order_count", static_cast<int64_t>(orderStatus.value(QStringLiteral("observedBatchOrderCount")).toInt()));
        event.metadata["observed_batch_order_count"] = QString::number(orderStatus.value(QStringLiteral("observedBatchOrderCount")).toInt()).toStdString();
    }
    if (orderStatus.contains(QStringLiteral("expectedBatchOrderCount"))) {
        event.set("expected_batch_order_count", static_cast<int64_t>(orderStatus.value(QStringLiteral("expectedBatchOrderCount")).toInt()));
        event.metadata["expected_batch_order_count"] = QString::number(orderStatus.value(QStringLiteral("expectedBatchOrderCount")).toInt()).toStdString();
    }
    applyOrderContextToEvent(&event, orderStatus);

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    if (!result) {
        qWarning() << "TradeExecutionService: failed to publish order status" << QString::fromStdString(result.message);
        return;
    }

    QVariantMap recentOrder = orderStatus;
    recentOrder.insert(QStringLiteral("statusOrigin"), statusOrigin);
    emit orderStatusPublished(recentOrder);
    appendRecentOrder(recentOrder);
}

void TradeExecutionService::publishTradeFill(const QVariantMap& tradeFill, const QString& correlationId)
{
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

    const QString statusOrigin = tradeFill.value(QStringLiteral("statusOrigin")).toString().trimmed().isEmpty()
        ? QStringLiteral("local_request")
        : tradeFill.value(QStringLiteral("statusOrigin")).toString().trimmed();

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        engine::EventTypes::ORDER_FILL,
        "TRADE_EXECUTION_SERVICE",
        0);
    event.correlation_id = correlationId.toStdString();
    event.set("fill_id", tradeFill.value("fillId").toString().toStdString());
    event.set("order_id", tradeFill.value("orderId").toString().toStdString());
    event.set("business_strategy_id", tradeFill.value("strategyId").toString().toStdString());
    event.set("symbol", tradeFill.value("symbol").toString().toStdString());
    event.set("side", tradeFill.value("side").toString().toStdString());
    event.set("fill_price", tradeFill.value("fillPrice").toDouble());
    event.set("fill_quantity", static_cast<int64_t>(tradeFill.value("fillQuantity").toLongLong()));
    event.set("filled_notional", tradeFill.value("filledNotional").toDouble());
    event.set("status", tradeFill.value("status").toString().toStdString());
    event.metadata["fill_id"] = tradeFill.value("fillId").toString().toStdString();
    event.metadata["order_id"] = tradeFill.value("orderId").toString().toStdString();
    event.metadata["business_strategy_id"] = tradeFill.value("strategyId").toString().toStdString();
    if (!tradeFill.value("runtimeStrategyId").toString().trimmed().isEmpty()) {
        event.set("runtime_strategy_id", tradeFill.value("runtimeStrategyId").toString().toStdString());
        event.metadata["runtime_strategy_id"] = tradeFill.value("runtimeStrategyId").toString().toStdString();
    }
    event.metadata["symbol"] = tradeFill.value("symbol").toString().toStdString();
    event.metadata["side"] = tradeFill.value("side").toString().toStdString();
    event.metadata["status"] = tradeFill.value("status").toString().toStdString();
    event.metadata["status_origin"] = statusOrigin.toStdString();
    applyOrderContextToEvent(&event, tradeFill);

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    if (!result) {
        qWarning() << "TradeExecutionService: failed to publish trade fill" << QString::fromStdString(result.message);
        return;
    }

    QVariantMap recentOrder = tradeFill;
    recentOrder.insert(QStringLiteral("statusOrigin"), statusOrigin);
    emit tradeFillPublished(recentOrder);
    appendRecentOrder(recentOrder);
}

QVariantMap TradeExecutionService::findPartialFillAdvanceBlock(const QVariantMap& orderRequest) const
{
    if (!boolishValue(orderRequest.value(QStringLiteral("requiresPreviousBatchFilled")))) {
        return {};
    }

    const QString requestedBatchId = orderRequest.value(QStringLiteral("batchId")).toString().trimmed();
    const int batchIndex = orderRequest.value(QStringLiteral("batchIndex")).toInt();
    QString requiredBatchId = orderRequest.value(QStringLiteral("previousBatchId")).toString().trimmed();
    if (requiredBatchId.isEmpty() && batchIndex > 0) {
        requiredBatchId = executionBatchIdForIndex(batchIndex - 1);
    }
    if (requiredBatchId.isEmpty()) {
        return {};
    }

    const int expectedBatchOrderCount = positiveIntegerValue(orderRequest.value(QStringLiteral("previousBatchOrderCount")));
    const QString strategyId = orderRequest.value(QStringLiteral("strategyId")).toString().trimmed();
    const QString runtimeStrategyId = orderRequest.value(QStringLiteral("runtimeStrategyId")).toString().trimmed();

    QVariantMap blockingOrder;
    int observedBatchOrderCount = 0;

    QMutexLocker locker(&m_mutex);
    for (const QVariant& orderVariant : m_recentOrders) {
        const QVariantMap orderRecord = orderVariant.toMap();
        if (!matchesExecutionBatchIdentity(orderRecord, strategyId, runtimeStrategyId)) {
            continue;
        }

        if (orderRecord.value(QStringLiteral("batchId")).toString().trimmed() != requiredBatchId) {
            continue;
        }

        ++observedBatchOrderCount;
        const QString status = order_runtime::normalizeOrderStatus(
            orderRecord.value(QStringLiteral("status")).toString(),
            kRecentOrderStatusPolicy);
        if (status == QStringLiteral("FILLED")) {
            continue;
        }

        if (blockingOrder.isEmpty()
            || order_runtime::normalizeOrderStatus(blockingOrder.value(QStringLiteral("status")).toString(), kRecentOrderStatusPolicy) != QStringLiteral("PARTIAL_FILLED")) {
            blockingOrder = orderRecord;
        }

        if (status == QStringLiteral("PARTIAL_FILLED")) {
            blockingOrder = orderRecord;
            break;
        }
    }

    if (expectedBatchOrderCount > 0 && observedBatchOrderCount < expectedBatchOrderCount) {
        return QVariantMap{{QStringLiteral("reasonCode"), QStringLiteral("previous_batch_missing_orders")},
                           {QStringLiteral("message"), partialFillAdvanceMissingBatchMessage(requestedBatchId,
                                                                                              requiredBatchId,
                                                                                              observedBatchOrderCount,
                                                                                              expectedBatchOrderCount)},
                           {QStringLiteral("requiredBatchId"), requiredBatchId},
                           {QStringLiteral("blockingBatchId"), requiredBatchId},
                           {QStringLiteral("observedBatchOrderCount"), observedBatchOrderCount},
                           {QStringLiteral("expectedBatchOrderCount"), expectedBatchOrderCount}};
    }

    if (observedBatchOrderCount <= 0) {
        return QVariantMap{{QStringLiteral("reasonCode"), QStringLiteral("previous_batch_not_started")},
                           {QStringLiteral("message"), partialFillAdvanceMissingBatchMessage(requestedBatchId,
                                                                                              requiredBatchId,
                                                                                              observedBatchOrderCount,
                                                                                              expectedBatchOrderCount)},
                           {QStringLiteral("requiredBatchId"), requiredBatchId},
                           {QStringLiteral("blockingBatchId"), requiredBatchId},
                           {QStringLiteral("observedBatchOrderCount"), observedBatchOrderCount},
                           {QStringLiteral("expectedBatchOrderCount"), expectedBatchOrderCount}};
    }

    if (!blockingOrder.isEmpty()) {
        const QString blockingStatus = order_runtime::normalizeOrderStatus(
            blockingOrder.value(QStringLiteral("status")).toString(),
            kRecentOrderStatusPolicy);
        return QVariantMap{{QStringLiteral("reasonCode"),
                            blockingStatus == QStringLiteral("PARTIAL_FILLED")
                                ? QStringLiteral("previous_batch_partially_filled")
                                : QStringLiteral("previous_batch_not_filled")},
                           {QStringLiteral("message"), partialFillAdvanceBlockingOrderMessage(requestedBatchId,
                                                                                               requiredBatchId,
                                                                                               blockingOrder)},
                           {QStringLiteral("requiredBatchId"), requiredBatchId},
                           {QStringLiteral("blockingBatchId"), requiredBatchId},
                           {QStringLiteral("blockingOrderId"), matchingOrderIdentity(blockingOrder)},
                           {QStringLiteral("blockingStatus"), blockingStatus},
                           {QStringLiteral("observedBatchOrderCount"), observedBatchOrderCount},
                           {QStringLiteral("expectedBatchOrderCount"), expectedBatchOrderCount}};
    }

    return {};
}

QVariantMap TradeExecutionService::findExecutionPauseBlock(const QVariantMap& orderRequest) const
{
    if (!boolishValue(orderRequest.value(QStringLiteral("pauseOnAbnormalReject")))) {
        return {};
    }

    const int requestedBatchIndex = orderRequest.value(QStringLiteral("batchIndex")).toInt();
    if (requestedBatchIndex <= 0) {
        return {};
    }

    const QString scopeKey = executionPauseScopeKey(orderRequest);
    if (scopeKey.isEmpty()) {
        return {};
    }

    QMutexLocker locker(&m_mutex);
    const QVariantMap pausedScope = m_pausedExecutionScopes.value(scopeKey);
    if (pausedScope.isEmpty()) {
        return {};
    }

    const int pausedBatchIndex = pausedScope.value(QStringLiteral("pausedBatchIndex")).toInt();
    if (requestedBatchIndex <= pausedBatchIndex) {
        return {};
    }

    const QString requestedBatchId = orderRequest.value(QStringLiteral("batchId")).toString().trimmed();
    return QVariantMap{{QStringLiteral("reasonCode"), QStringLiteral("execution_paused_after_reject")},
                       {QStringLiteral("message"), pausedExecutionMessage(requestedBatchId, pausedScope)},
                       {QStringLiteral("requiredBatchId"), pausedScope.value(QStringLiteral("pausedBatchId")).toString()},
                       {QStringLiteral("blockingBatchId"), pausedScope.value(QStringLiteral("pausedBatchId")).toString()},
                       {QStringLiteral("blockingOrderId"), pausedScope.value(QStringLiteral("blockingOrderId")).toString()},
                       {QStringLiteral("blockingStatus"), pausedScope.value(QStringLiteral("blockingStatus")).toString()},
                       {QStringLiteral("executionScopeId"), pausedScope.value(QStringLiteral("executionScopeId")).toString()}};
}

QVariantMap TradeExecutionService::findManualCheckpointBlock(const QVariantMap& orderRequest) const
{
    if (!boolishValue(orderRequest.value(QStringLiteral("requiresManualCheckpoint")))) {
        return {};
    }

    const QString checkpointKey = executionCheckpointKey(orderRequest);
    if (checkpointKey.isEmpty()) {
        return {};
    }

    QMutexLocker locker(&m_mutex);
    if (m_approvedExecutionCheckpoints.contains(checkpointKey)) {
        return {};
    }

    const QString requestedBatchId = orderRequest.value(QStringLiteral("batchId")).toString().trimmed();
    return QVariantMap{{QStringLiteral("reasonCode"), QStringLiteral("manual_checkpoint_required")},
                       {QStringLiteral("message"), manualCheckpointMessage(requestedBatchId, orderRequest)},
                       {QStringLiteral("requiredBatchId"), requestedBatchId},
                       {QStringLiteral("executionScopeId"), executionScopeIdFromRecord(orderRequest)}};
}

QVariantMap TradeExecutionService::findPendingOrderConflict(const QString& symbol, const QString& side) const
{
    const QString normalizedSymbol = symbol.trimmed().toUpper();
    const QString normalizedSide = normalizeOrderSideText(side);
    if (normalizedSymbol.isEmpty() || normalizedSide.isEmpty()) {
        return {};
    }

    QMutexLocker locker(&m_mutex);
    for (const QVariant& orderVariant : m_recentOrders) {
        const QVariantMap orderRecord = orderVariant.toMap();
        if (orderRecord.value(QStringLiteral("symbol")).toString().trimmed().toUpper() != normalizedSymbol) {
            continue;
        }

        if (order_runtime::isClosedOrderStatus(orderRecord.value(QStringLiteral("status")).toString(),
                                               kRecentOrderStatusPolicy)) {
            continue;
        }

        const QString existingSide = normalizedOrderSideFromRecord(orderRecord);
        if (existingSide.isEmpty() || existingSide == normalizedSide) {
            continue;
        }

        return orderRecord;
    }

    return {};
}

void TradeExecutionService::appendRecentOrder(const QVariantMap& orderRecord)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        changed = order_runtime::upsertOrderRecord(&m_recentOrders,
                                                   orderRecord,
                                                   order_runtime::overlayOrderRecord,
                                                   [](const QVariantMap& existingRecord, const QVariantMap& nextRecord) {
                                                       return order_runtime::shouldIgnoreOrderStatusRegression(existingRecord, nextRecord, kRecentOrderStatusPolicy);
                                                   },
                                                   [](const QVariantMap& existingRecord, const QVariantMap& nextRecord) {
                                                       const bool sameStatus = order_runtime::normalizeOrderStatus(existingRecord.value(QStringLiteral("status")).toString(), kRecentOrderStatusPolicy)
                                                           == order_runtime::normalizeOrderStatus(nextRecord.value(QStringLiteral("status")).toString(), kRecentOrderStatusPolicy);
                                                       const bool sameQuantity = existingRecord.value(QStringLiteral("quantity")) == nextRecord.value(QStringLiteral("quantity"));
                                                       const bool sameFilledQuantity = existingRecord.value(QStringLiteral("filledQuantity")) == nextRecord.value(QStringLiteral("filledQuantity"));
                                                       const bool sameMessage = existingRecord.value(QStringLiteral("message")) == nextRecord.value(QStringLiteral("message"));
                                                       const bool sameBrokerOrderId = existingRecord.value(QStringLiteral("brokerOrderId")) == nextRecord.value(QStringLiteral("brokerOrderId"));
                                                       return sameStatus && sameQuantity && sameFilledQuantity && sameMessage && sameBrokerOrderId;
                                                   });
        updateExecutionPauseLocked(orderRecord);
    }

    if (!changed) {
        return;
    }

    emit recentOrdersChanged();
    appendRecentRuleHit(orderRecord);
}

void TradeExecutionService::appendRecentRuleHit(const QVariantMap& orderRecord)
{
    const QVariantMap ruleHit = buildRuleHitRecord(orderRecord);
    if (ruleHit.isEmpty()) {
        return;
    }

    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        const QString hitId = ruleHit.value(QStringLiteral("hitId")).toString();
        for (const QVariant& entry : m_recentRuleHits) {
            if (entry.toMap().value(QStringLiteral("hitId")).toString() == hitId) {
                return;
            }
        }

        m_recentRuleHits.prepend(ruleHit);
        constexpr int kRecentRuleHitLimit = 64;
        while (m_recentRuleHits.size() > kRecentRuleHitLimit) {
            m_recentRuleHits.removeLast();
        }
        changed = true;
    }

    if (changed) {
        emit recentRuleHitsChanged();
    }
}

bool TradeExecutionService::updateExecutionPauseLocked(const QVariantMap& orderRecord)
{
    if (!boolishValue(orderRecord.value(QStringLiteral("pauseOnAbnormalReject")))) {
        return false;
    }

    const QString scopeKey = executionPauseScopeKey(orderRecord);
    if (scopeKey.isEmpty()) {
        return false;
    }

    const int batchIndex = orderRecord.value(QStringLiteral("batchIndex")).toInt();
    const QString status = order_runtime::normalizeOrderStatus(orderRecord.value(QStringLiteral("status")).toString(),
                                                               kRecentOrderStatusPolicy);
    const QString statusOrigin = orderRecord.value(QStringLiteral("statusOrigin")).toString().trimmed().toLower();

    if (status == QStringLiteral("REJECTED") && isAbnormalRejectStatusOrigin(statusOrigin)) {
        m_pausedExecutionScopes.insert(scopeKey,
            QVariantMap{{QStringLiteral("executionScopeId"), executionScopeIdFromRecord(orderRecord)},
                        {QStringLiteral("strategyId"), orderRecord.value(QStringLiteral("strategyId")).toString()},
                        {QStringLiteral("runtimeStrategyId"), orderRecord.value(QStringLiteral("runtimeStrategyId")).toString()},
                        {QStringLiteral("pausedBatchId"), orderRecord.value(QStringLiteral("batchId")).toString()},
                        {QStringLiteral("pausedBatchIndex"), batchIndex},
                        {QStringLiteral("blockingOrderId"), matchingOrderIdentity(orderRecord)},
                        {QStringLiteral("blockingStatus"), status},
                        {QStringLiteral("pausedAt"), orderRecord.value(QStringLiteral("updatedAt")).toString()},
                        {QStringLiteral("message"), orderRecord.value(QStringLiteral("message")).toString()}});
        return true;
    }

    auto pausedIt = m_pausedExecutionScopes.find(scopeKey);
    if (pausedIt == m_pausedExecutionScopes.end()) {
        return false;
    }

    if (batchIndex == pausedIt->value(QStringLiteral("pausedBatchIndex")).toInt()
        && shouldClearPausedExecutionFromOrder(orderRecord)) {
        m_pausedExecutionScopes.erase(pausedIt);
        return true;
    }

    return false;
}

#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
bool TradeExecutionService::evaluateBrokerReadiness(QString* errorMessage, bool bindBrokerApi)
{
    TradingConnectionConfigService* configService = TradingConnectionConfigService::instance();
    if (!configService) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Trading connection config service unavailable");
        }
        return false;
    }
    const QVariantMap startupGate = configService->evaluateStartupGate(true);
    if (!startupGate.value(QStringLiteral("ready")).toBool()) {
        if (errorMessage) {
            *errorMessage = startupGate.value(QStringLiteral("reason")).toString();
        }
        return false;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("EventBus is not ready");
        }
        return false;
    }

    thirdparty::JujinApi* sharedApi = engine::get_shared_jujin_api();
    if (!sharedApi) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Shared Jujin trading session is unavailable");
        }
        return false;
    }

    if (!sharedApi->is_connected()) {
        if (errorMessage) {
            const QString brokerDetail = QString::fromStdString(sharedApi->last_error_message()).trimmed();
            *errorMessage = brokerDetail.isEmpty()
                ? QStringLiteral("Shared Jujin trading session is not connected")
                : QStringLiteral("Shared Jujin trading session is not connected: %1").arg(brokerDetail);
        }
        return false;
    }

    if (bindBrokerApi) {
        m_brokerApi = sharedApi;
    }
    return true;
}

bool TradeExecutionService::ensureBrokerApiReady(QString* errorMessage)
{
    return evaluateBrokerReadiness(errorMessage, true);
}
#endif






