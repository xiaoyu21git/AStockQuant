#pragma once
// OrderFieldKeys.h — 订单/持仓/状态 QVariantMap key 常量 (bridge 层)
//
// 集中管理 TradingBridges 中所有 QVariantMap/QJsonObject 的字段名,
// 消除散落裸字符串, 保证 QML 端接收的字段名一致。

#include <QString>

namespace bridge::keys {

// ── 订单通用 ──
inline const QString kBrokerOrderId = QStringLiteral("brokerOrderId");
inline const QString kSymbol        = QStringLiteral("symbol");
inline const QString kSide          = QStringLiteral("side");
inline const QString kPrice         = QStringLiteral("price");
inline const QString kQuantity      = QStringLiteral("quantity");
inline const QString kStatus        = QStringLiteral("status");
inline const QString kStrategyId    = QStringLiteral("strategyId");
inline const QString kAccountId     = QStringLiteral("accountId");

// ── 成交明细 ──
inline const QString kFilledPrice    = QStringLiteral("filledPrice");
inline const QString kFilledQuantity = QStringLiteral("filledQuantity");

// ── 状态扩展 ──
inline const QString kRawStatus = QStringLiteral("rawStatus");
inline const QString kMessage   = QStringLiteral("message");

// ── 快速平仓 ──
inline const QString kLastPrice = QStringLiteral("lastPrice");

} // namespace bridge::keys
